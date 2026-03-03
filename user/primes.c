#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void stage(int read_fd)
{
    int prime;

    // 读第一个数
    if (read(read_fd, &prime, sizeof(int)) == 0) {
        close(read_fd);
        exit(0);
    }

    printf("prime %d\n", prime);

    int p[2];
    pipe(p);

    if (fork() == 0) {
        close(p[1]);         // 子进程只读
        stage(p[0]);
    } else {
        close(p[0]);         // 父进程只写

        int num;
        while (read(read_fd, &num, sizeof(int)) > 0) {
            if (num % prime != 0) {
                write(p[1], &num, sizeof(int));
            }
        }

        close(p[1]);
        wait(0);
        exit(0);
    }
}

int main()
{
    // 存放管道两端文件描述符
    // p[0] 用于读，p[1] 用于写
    int p[2];
    pipe(p);

    if (fork() == 0) {
        close(p[1]);      // 子进程只读
        stage(p[0]);
    } else {
        close(p[0]);      // 父进程只写

        for (int i = 2; i <= 35; i++) {
            write(p[1], &i, sizeof(int));
        }

        close(p[1]);
        wait(0);
    }

    exit(0);
}