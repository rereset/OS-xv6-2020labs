#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "kernel/param.h"
#include "user.h"

char* formatinput(char* buf);

int main(int argc,char *argv[])
{
    char buf[MAXPATH];  // 存储每一行的参数内容
    char *args[MAXARG];  // 定义命令行参数最大数量(最大行数)
    char *cmd;  // 存储命令

    /* 通过参数个数处理指定cmd */
    if(argc==1)  // 无额外参数只有命令
        cmd="echo";
    else
        cmd=argv[1];
    
    /* 读取多行参数 */
    int row_cnt=0;
    while(1)
    {
        memset(buf,0,sizeof(buf));
        gets(buf,MAXPATH);  // gets读取一行内容
        char *ansbuf =formatinput(buf);
        if(strlen(ansbuf)==0||row_cnt>=MAXARG)  // 终止输入
            break;
        args[row_cnt++]=ansbuf;
    }
    char* argv_exec[MAXARG];
    int pos=0;
    for(pos=0;pos<argc-1;pos++)  // 读取第一行输入内容
        argv_exec[pos]=argv[pos+1];
    for(int i=0;i<row_cnt;i++)  // 读取后续行
        argv_exec[pos+i]=args[i];
    argv_exec[pos+row_cnt]=0;  // exec规范

    if(fork()==0)
        exec(cmd,argv_exec);
    else
        wait(0);
    exit(0);
}

/* 格式化输入(删除换行符且确保指针指向有效地址) */
char* formatinput(char* buf)
{
    if(strlen(buf)>1&&buf[strlen(buf)-1]=='\n')  // 最后一个字符是换行符
    {
        char* res_buf=(char*)malloc((strlen(buf)-1) * sizeof(char));
        memcpy(res_buf,buf,(strlen(buf) - 1));
        *(res_buf+strlen(buf)-1)='\0';
        return res_buf;
    }
    else
    {
        char* res_buf=(char*)malloc(sizeof(buf));
        memcpy(res_buf,buf,strlen(buf));
        return res_buf;
    }
}

