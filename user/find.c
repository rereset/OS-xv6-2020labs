#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "user/user.h"

/* grep.c */
char buf[1024];
int match(char*, char*);  // 判断正则表达式是否在文本中匹配
int matchhere(char*, char*);  // 在文本开头查找正则表达式的匹配
int matchstar(int, char*, char*);  // 在文本开头查找 c*re 的匹配，其中 c 是字符，re 是正则表达式

int
match(char *re, char *text) 
// re:正则表达式
// text:文本 
{
  if(re[0] == '^')  // 以^开头，调用matchhere在文本每个位置开始匹配
    return matchhere(re+1, text);
  do{  // must look at empty string
    if(matchhere(re, text))
      return 1;
  }while(*text++ != '\0');  // 遍历文本的每一个位置作为开头匹配对应字符串
  return 0;
}

// matchhere: search for re at beginning of text
int matchhere(char *re, char *text)
// re:正则表达式
// text:文本 
{
  if(re[0] == '\0')  // 正则表达式为空
    return 1;
  if(re[1] == '*')  // 正则表达式第二个字符为*，类似c*re，调用matchstar
    return matchstar(re[0], re+2, text);
  if(re[0] == '$' && re[1] == '\0')  // 判断文本是否为末尾
    return *text == '\0';
  if(*text!='\0' && (re[0]=='.' || re[0]==*text))  // 文本不为空且有.(通配符)或与当前文本相匹配即判断是否匹配，递归调用
    return matchhere(re+1, text+1);
  return 0;
}

// matchstar: search for c*re at beginning of text
int matchstar(int c, char *re, char *text)
// c:字符
// re:正则表达式
// text:文本
{
  do{  // a * matches zero or more instances
    if(matchhere(re, text))
      return 1;
  }while(*text!='\0' && (*text++==c || c=='.'));  // 匹配前一个字符的零个或多个实例
  return 0;
}

/* find.c */
char* fmtname(char *path);  // ls.c
void find(char *path,char *re);  // 改编自ls

int main(int argc,char* argv[])
{
    int parameter=argc-1;
    if(parameter!=2)
        printf("请输入两个参数！\n");
    else
        find(argv[1],argv[2]);
    exit(0);
}

/* 从给定路径中提取格式化文件名 */
char* fmtname(char *path)
{
  static char buf[DIRSIZ+1];  // DIRSIZ文件最大长度
  char *p;  // 指向路径中的字符

  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)  // 循环遍历直到遇到第一个'/'或开头
    ;
  p++;

  // Return blank-padded name.
  if(strlen(p) >= DIRSIZ)  // 当前文件名超过最大长度
    return p;
  memset(buf, 0, sizeof(buf));
  memmove(buf, p, strlen(p));
  return buf;
}

void find(char *path,char *re)
{
  char buf[512], *p;
  int fd;  // 文件描述符
  struct dirent de;  // 读取目录文件信息
//   struct dirent {
//   ushort inum;
//   char name[DIRSIZ];
// };
  struct stat st;  // 读取文件状态信息
//   struct stat {
//   int dev;     // File system's disk device
//   uint ino;    // Inode number
//   short type;  // Type of file
//   short nlink; // Number of links to file
//   uint64 size; // Size of file in bytes
// };

  if((fd = open(path, 0)) < 0)  // 文件打开失败
  {
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }
  if(fstat(fd, &st) < 0)  // 读取文件状态失败
  {
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }
  /* 成功读取文件相关信息 */
  switch(st.type)
  {
  case T_FILE:  // 普通文件实现文件名匹配
    if(match(re,fmtname(path)))
        printf("%s\n",path);
    break;

  case T_DIR:  // 目录
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf)  // 避免路径名过长溢出
    {
      printf("find: path too long\n");
      break;
    }
    strcpy(buf, path);
    p = buf+strlen(buf);
    *p++ = '/';
    while(read(fd, &de, sizeof(de)) == sizeof(de))  // 遍历当前目录下所有文件项
    {
      if(de.inum == 0)  // 目录项无效
        continue;
      memmove(p, de.name, DIRSIZ);  // 复制文件名
      p[DIRSIZ] = 0;
      if(stat(buf, &st) < 0)  // 获取当前文件详细信息
      {
        printf("find: cannot stat %s\n", buf);
        continue;
      }
      char* nowname = fmtname(buf);  // 当前指向文件
      if(strcmp(".",nowname)==0||strcmp("..",nowname)==0)
        continue;
      else
        find(buf,re);
    }
    break;
  }
  close(fd);
}

