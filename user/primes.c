#include "kernel/types.h"
#include "user/user.h"
#define READ 0
#define WRITE 1

const int N=35;
void FindPrimes(int fd[]);
void SendPrimes(int fd[],int input[],int cnt);  // 写入管道


int main(int argc, char* argv[])
{
    int fd[2];  // 定义管道文件描述符
    pipe(fd);  // 创建管道

    if(fork()==0)  // 子进程
    {
        FindPrimes(fd);
        exit(0);
    }
    else  // 父进程
    {
        /* 创建从2-35的初始数组 */
        int num[N];
        for(int i=0;i<N-1;i++)
        {
            num[i]=2+i;
        }
        num[N-1]=-1;  // 数组终止符
        SendPrimes(fd,num,N);
        wait(0);
        exit(0);
    }    
}

/* 写入管道 */
void SendPrimes(int fd[],int input[],int cnt)
{
    close(fd[READ]);
    write(fd[WRITE],input,cnt*sizeof(int));
    close(fd[WRITE]);
}

/* 读并处理 */
void FindPrimes(int fd[])
{
    int child_fd[2];  // 子进程文件描述符
    int input[N];  // 接收存储地址
    int* output = (int*)malloc(N * sizeof(int));  // 动态创建整数数组

    /* 读取原数组 */
    close(fd[WRITE]);
    read(fd[READ],input,sizeof(input));
    close(fd[READ]);
    printf("prime %d\n",input[0]);

    /* 筛选处理数组 */
    int pos_i=0,pos_o=0;
    int flag=1;  // 是否已经筛选成功
    while(input[pos_i]!=-1)
    {
        if(input[pos_i]%input[0]!=0)  // 非倍数
        {
            output[pos_o]=input[pos_i];
            pos_o++;
            flag=0;
        }
        pos_i++;
    }
    output[pos_o]=-1; // 添加终止符 
    if(flag)
        exit(0);

    /* 递归实现过程 */
    pipe(child_fd);  // 创建子进程
    if(fork()==0)  // 子进程
    {
        FindPrimes(child_fd);
    }
    else
    {
        SendPrimes(child_fd,output,pos_o+1);
        free(output);
        wait(0);
    }
    exit(0);
}