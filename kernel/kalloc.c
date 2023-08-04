// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU]; // 为每个 CPU 分配独立的 freelist，并用独立的锁保护它。;

/* 每个cpu有自己的锁 */
char *kmem_lock_names[] = {
  "kmem_cpu_0",
  "kmem_cpu_1",
  "kmem_cpu_2",
  "kmem_cpu_3",
  "kmem_cpu_4",
  "kmem_cpu_5",
  "kmem_cpu_6",
  "kmem_cpu_7",
};

void
kinit()
{
  for(int i=0;i<NCPU;i++) { // 初始化所有锁
    initlock(&kmem[i].lock, kmem_lock_names[i]);
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  // 用垃圾数据填充内存页，以便捕获悬空引用。

  memset(pa, 1, PGSIZE);

  // 将物理地址 pa 转换为 run 结构的指针 r。
  r = (struct run*)pa;

  // 停用中断（关中断）以确保操作的原子性。
  push_off();

  // 获取当前 CPU 的 ID。
  int cpu = cpuid();

  // 获取当前 CPU 对应的内存管理锁。
  acquire(&kmem[cpu].lock);

  // 将当前释放的 run 结构添加到对应 CPU 的空闲列表中。
  r->next = kmem[cpu].freelist;
  kmem[cpu].freelist = r;

  // 释放内存管理锁，允许其他线程或进程进行内存管理操作。
  release(&kmem[cpu].lock);

  // 恢复中断（开中断）状态。
  pop_off();

}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
// 分配一个物理页给调用者
void *
kalloc(void)
{
  struct run *r;

  // 停用中断（关中断）以确保操作的原子性
  push_off();

  // 获取当前 CPU 的 ID
  int cpu = cpuid();

  // 获取当前 CPU 对应的内存管理锁
  acquire(&kmem[cpu].lock);

  // 如果当前 CPU 的空闲列表为空，尝试从其他 CPU 偷取空闲页
  if (!kmem[cpu].freelist) {
    int steal_left = 64; // 从其他 CPU 窃取 64 页
    for (int i = 0; i < NCPU; i++) {
      if (i == cpu) continue; // 不从自己偷取
      acquire(&kmem[i].lock);
      struct run *rr = kmem[i].freelist;
      while (rr && steal_left) {
        kmem[i].freelist = rr->next;
        rr->next = kmem[cpu].freelist;
        kmem[cpu].freelist = rr;
        rr = kmem[i].freelist;
        steal_left--;
      }
      release(&kmem[i].lock);
      if (steal_left == 0) break; // 偷取完成
    }
  }

  // 从当前 CPU 的空闲列表中取出一个页
  r = kmem[cpu].freelist;
  if (r)
    kmem[cpu].freelist = r->next;

  // 释放当前 CPU 对应的内存管理锁
  release(&kmem[cpu].lock);

  // 恢复中断（开中断）状态
  pop_off();

  // 用垃圾数据填充页，以防止悬空引用
  if (r)
    memset((char*)r, 5, PGSIZE);

  // 返回分配的物理页指针
  return (void*)r;
}

