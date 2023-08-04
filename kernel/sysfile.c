//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "memlayout.h"
#include "fcntl.h"

#define max(a, b) ((a) > (b) ? (a) : (b))   // lab10
#define min(a, b) ((a) < (b) ? (a) : (b))   // lab10

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  if(argfd(0, 0, &f) < 0 || argaddr(1, &st) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    dp->nlink++;  // for ".."
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  if((n = argstr(0, path, MAXPATH)) < 0 || argint(1, &omode) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if((omode & O_TRUNC) && ip->type == T_FILE){
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  if((argstr(0, path, MAXPATH)) < 0 ||
     argint(1, &major) < 0 ||
     argint(2, &minor) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  
  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  if(argstr(0, path, MAXPATH) < 0 || argaddr(1, &uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = exec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  if(argaddr(0, &fdarray) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}



uint64 sys_mmap(void)
{
    uint64 addr, sz, offset;
    int prot, flags, fd; struct file *f;

    // 从用户空间获取参数：地址、大小、权限、标志、文件描述符、偏移量
    if(argaddr(0, &addr) < 0 || argaddr(1, &sz) < 0 || argint(2, &prot) < 0
        || argint(3, &flags) < 0 || argfd(4, &fd, &f) < 0 || argaddr(5, &offset) < 0 || sz == 0)
        return -1;

    // 检查文件权限以及映射权限是否匹配
    if((!f->readable && (prot & (PROT_READ)))
        || (!f->writable && (prot & PROT_WRITE) && !(flags & MAP_PRIVATE)))
        return -1;

    // 将映射大小向上对齐到页面大小的倍数
    sz = PGROUNDUP(sz);

    // 获取当前进程的 proc 结构体
    struct proc *p = myproc();
    struct vma *v = 0;
    uint64 vaend = MMAPEND; // 非包含（non-inclusive）的结束虚拟地址

    // 查找合适的虚拟内存区域（VMA），用于映射文件
    // 实现中将文件映射在陷阱帧（trapframe）之下，从高地址向低地址映射
    for(int i=0;i<NVMA;i++) {
        struct vma *vv = &p->vmas[i];
        if(vv->valid == 0) {
            if(v == 0) {
                v = &p->vmas[i];
                // 找到可用的 VMA
                v->valid = 1;
            }
        } else if(vv->vastart < vaend) {
            // 更新 vaend，使其成为已有 VMA 的虚拟地址下限
            vaend = PGROUNDDOWN(vv->vastart);
        }
    }

    if(v == 0){
        panic("mmap: no free vma");
    }

    // 设置 VMA 的属性
    v->vastart = vaend - sz; // 分配虚拟地址的起始位置
    v->sz = sz; // 映射大小
    v->prot = prot; // 权限
    v->flags = flags; // 标志
    v->f = f; // 文件指针，假定 f->type == FD_INODE
    v->offset = offset; // 文件偏移量

    // 增加文件引用计数，以确保文件在映射期间不会被关闭
    filedup(v->f);

    // 返回映射的起始虚拟地址
    return v->vastart;
}


// 在进程的 VMA 列表中查找包含指定虚拟地址的 VMA
// 如果找到匹配的 VMA，返回该 VMA 的指针；否则返回 0
struct vma *findvma(struct proc *p, uint64 va) {
    for (int i = 0; i < NVMA; i++) {
        struct vma *vv = &p->vmas[i]; // 获取当前索引对应的 VMA
        if (vv->valid == 1 && va >= vv->vastart && va < vv->vastart + vv->sz) {
            // 检查 VMA 是否有效，以及指定的虚拟地址是否在 VMA 的范围内
            return vv; // 返回匹配的 VMA 指针
        }
    }
    return 0; // 没有找到匹配的 VMA，返回 0
}


// 判断某个页面是否是之前进行了延迟分配（lazy-allocated），
// 需要在使用之前进行访问（touch）。
// 如果是，则访问该页面，将其映射到一个实际的物理页面，并包含映射文件的内容。
// 如果是，则返回 1，否则返回 0。
int vmatrylazytouch(uint64 va) {
    struct proc *p = myproc(); // 获取当前进程的 proc 结构体
    struct vma *v = findvma(p, va); // 查找包含虚拟地址的 VMA
    if (v == 0) {
        return 0; // 未找到匹配的 VMA，返回 0
    }

    // 分配一个物理页面
    void *pa = kalloc();
    if (pa == 0) {
        panic("vmalazytouch: kalloc"); // 分配失败，触发 panic
    }
    memset(pa, 0, PGSIZE); // 初始化页面内容为 0

    // 从磁盘读取数据到物理页面
    begin_op();
    ilock(v->f->ip);
    readi(v->f->ip, 0, (uint64)pa, v->offset + PGROUNDDOWN(va - v->vastart), PGSIZE); // 从文件读取数据
    iunlock(v->f->ip);
    end_op();

    // 设置适当的权限，然后映射物理页面到虚拟地址
    int perm = PTE_U;
    if (v->prot & PROT_READ)
        perm |= PTE_R;
    if (v->prot & PROT_WRITE)
        perm |= PTE_W;
    if (v->prot & PROT_EXEC)
        perm |= PTE_X;

    // 将物理页面映射到虚拟地址，设置相应的权限
    if (mappages(p->pagetable, va, PGSIZE, (uint64)pa, perm) < 0) {
        panic("vmalazytouch: mappages"); // 映射失败，触发 panic
    }

    return 1; // 返回 1 表示成功进行了页面访问和映射
}


// 取消映射一个范围内的内存页
uint64 sys_munmap(void)
{
    uint64 addr, sz;

    // 从用户空间获取参数：起始地址和大小
    if (argaddr(0, &addr) < 0 || argaddr(1, &sz) < 0 || sz == 0)
        return -1;

    struct proc *p = myproc(); // 获取当前进程的 proc 结构体

    struct vma *v = findvma(p, addr); // 查找包含指定虚拟地址的 VMA
    if (v == 0) {
        return -1; // 未找到匹配的 VMA，返回 -1
    }

    if (addr > v->vastart && addr + sz < v->vastart + v->sz) {
        // 尝试在内存范围内创建“洞”
        return -1;
    }

    uint64 addr_aligned = addr;
    if (addr > v->vastart) {
        addr_aligned = PGROUNDUP(addr); // 向上对齐地址
    }

    int nunmap = sz - (addr_aligned - addr); // 要取消映射的字节数
    if (nunmap < 0)
        nunmap = 0;

    vmaunmap(p->pagetable, addr_aligned, nunmap, v); // 自定义内存页面取消映射函数，用于取消映射 mmapped 页面。

    if (addr <= v->vastart && addr + sz > v->vastart) { // 在开头处取消映射
        v->offset += addr + sz - v->vastart;
        v->vastart = addr + sz;
    }
    v->sz -= sz; // 更新 VMA 的大小

    if (v->sz <= 0) {
        fileclose(v->f); // 关闭文件
        v->valid = 0; // 将 VMA 标记为无效
    }

    return 0; // 成功取消映射，返回 0
}

