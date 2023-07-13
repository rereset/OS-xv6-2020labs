#include "kernel/types.h"
#include "user/user.h"

int parameter_num =0;
int main(int argc,char *argv[])  // argc为接收参数个数，argv为指针数组
{
    parameter_num=argc-1;  // 输入参数个数
    if(parameter_num==0)  // 未输入参数
    {
        printf("请输入参数！\n");
        exit(0);
    }
    else if(parameter_num>1)  // 输入参数个数大于1
    {
        printf("输入参数过多！\n");
        exit(0);
    }
    else  // 只输入一个参数
    {
        int pos=0;
        while(argv[1][pos]!='\0')
        {
            if(argv[1][pos]>'9'||argv[1][pos]<'0')  // 输入不合法数字
            {
                printf("输入参数不合法，请输入数字！\n");
                exit(0);
            }
            pos++;
        }
        int sleep_time=atoi(argv[1]);
        sleep(sleep_time);
        exit(0);
    }
}