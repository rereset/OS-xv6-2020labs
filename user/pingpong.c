#include "kernel/types.h"
#include "user/user.h"
#define READ 0
#define WRITE 1

const int N=10;
int main(int argc, char* argv[])
{
    int parent_fd[2],child_fd[2];  // 父、子进程操作端数组，0为读，1为写
    /* 父子进程创建管道 */
    pipe(parent_fd);  // 父=>子写，子=>父读
    pipe(child_fd);  // 子=>父写，父=>子读
    char buffer[N];  // 创建缓冲区存放交换字符串
    /* 子进程处理 */
    if(fork()==0)  // 子进程fork返回0
    {
        /* 接收父进程消息 */
        close(parent_fd[WRITE]);  // 关闭父进程写
        read(parent_fd[READ],buffer,4);  // 读取父进程发出内容并写入buffer
        printf("%d: received %s\n",getpid(),buffer);
        /* 给父进程写 */
        close(child_fd[READ]);  // 关闭子进程读
        write(child_fd[WRITE],"pong",4);
        exit(0);
    }
    else  //父进程fork返回子进程pid
    {
        /* 给子进程写 */
        close(parent_fd[READ]);
        write(parent_fd[WRITE],"ping",4);
        /* 接受子进程消息 */
        close(child_fd[WRITE]);
        read(child_fd[READ],buffer,4);
        printf("%d: received %s\n",getpid(),buffer);
        exit(0);
    }
}