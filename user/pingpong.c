#include "kernel/types.h"
#include "user.h"

int main(int argc, char *argv[])
{
    if (argc != 1)
    {
        printf("Pingpong doesn't need arguments!\n"); // 不需要额外输入参数
        exit(-1);
    }

    int f2c[2];   //管道1：父进程->子进程
    int c2f[2];   //管道2：子进程->父进程
    pipe(f2c);
    pipe(c2f);
    int pid;
    pid = fork();
    if (pid == 0)
    {
        /* child */
        char buffer[32] = {0};
        close(f2c[1]);                               // 关闭f2c的写通道
        close(c2f[0]);                               // 关闭c2f的读通道
        read(f2c[0], buffer, 4);                     // 读入ping
        close(f2c[0]);                               // 已读出ping，可以关闭f2c的读通道
        printf("%d: received %s from pid %d\n", getpid(), buffer, getpid()-1); 
        write(c2f[1], "pong", 4);                    // 写入pong
        close(c2f[1]);                               // 关闭c2f的写通道
    }
    else if (pid > 0)
    {
        /* parent */
        char buffer[32] = {0};
        close(f2c[0]);                               // 关闭f2c的读通道
        close(c2f[1]);                               // 关闭c2f的写通道
        write(f2c[1], "ping", 4);                    // 写入ping
        close(f2c[1]);                               // 关闭f2c的写通道
        wait(0);                                       // 等待子进程结束
        read(c2f[0], buffer, 4);                     // 读入pong
        close(c2f[0]);                               // 关闭c2f的读通道
        printf("%d: received %s from pid %d\n", getpid(), buffer,getpid()+1); 
    }
    else
    {
        printf("fork error\n");
        exit(-1);
    }
    exit(0); 
}