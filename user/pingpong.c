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
    int _pid;
    int pid;
    _pid = fork();
    char send_buffer[4] = {0};
    char rec_buffer[4] = {0};
    if (_pid == 0)
    {
        /* child */
        close(f2c[1]);                               // 关闭f2c的写通道
        close(c2f[0]);                               // 关闭c2f的读通道
        pid = getpid();
        read(f2c[0], rec_buffer, 4);                     // 读入
        close(f2c[0]);                               // 已读出，可以关闭f2c的读通道
        printf("%d: received ping from pid %s\n", pid, rec_buffer); 
        itoa(pid, send_buffer);
        write(c2f[1], send_buffer, 4);                    // 写入
        close(c2f[1]);                               // 关闭c2f的写通道
    }
    else if (_pid > 0)
    {
        /* parent */
        close(f2c[0]);                               // 关闭f2c的读通道
        close(c2f[1]);                               // 关闭c2f的写通道
        pid = getpid();
        itoa(pid, send_buffer);
        write(f2c[1], send_buffer, 4);                    // 写入
        close(f2c[1]);                               // 关闭f2c的写通道

        read(c2f[0], rec_buffer, 4);                     // 读入pong
        close(c2f[0]);                               // 关闭c2f的读通道
        printf("%d: received pong from pid %s\n", pid, rec_buffer); 
    }
    else
    {
        printf("fork error\n");
        exit(-1);
    }
    exit(0); 
}