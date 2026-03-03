#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
    int p1[2]; // pipe from parent -> child
    int p2[2]; // pipe from child  -> parent

    // 创建父到子的管道
    if (pipe(p1) < 0) {
        // 如果创建失败，向标准错误打印并退出
        fprintf(2, "pipe failed\n");
        exit(1);
    }

    // 创建子到父的管道
    if (pipe(p2) < 0) {
        fprintf(2, "pipe failed\n");
        exit(1);
    }

    // 创建子进程
    int pid = fork();
    if (pid < 0) {
        // fork 失败，打印错误并退出
        fprintf(2, "fork failed\n");
        exit(1);
    } else if (pid == 0) {
        // 关闭子进程不使用的写端（父->子 管道的写端）
        close(p1[1]);
        // 关闭子进程不使用的读端（子->父 管道的读端）
        close(p2[0]);

        char c; // 用于接收一个字节
        // 从父->子的管道读取一个字节
        if (read(p1[0], &c, 1) != 1) {
            // 读取失败则退出
            fprintf(2, "read failed\n");
            exit(1);
        }

        // 关闭父->子 管道的读端，已经不再需要
        close(p1[0]);

        // 打印收到 ping 的消息，前面带进程 ID
        printf("%d: received ping\n", getpid());

        // 将同一个字节写回给父进程（通过子->父 的管道）
        if (write(p2[1], &c, 1) != 1) {
            fprintf(2, "write failed\n");
            exit(1);
        }

        // 关闭子->父 管道的写端，然后子进程退出
        close(p2[1]);
        exit(0);
    } else {
        // 关闭父进程不使用的读端（父->子 管道的读端）
        close(p1[0]);
        // 关闭父进程不使用的写端（子->父 管道的写端）
        close(p2[1]);

        char c = 'x'; // 要发送的任意一个字节
        // 向子进程写入一个字节（ping）
        if (write(p1[1], &c, 1) != 1) {
            fprintf(2, "write failed\n");
            exit(1);
        }

        // 关闭父->子 管道的写端，表示写入完成
        close(p1[1]);

        // 从子->父 的管道读取一个字节（pong）
        if (read(p2[0], &c, 1) != 1) {
            fprintf(2, "read failed\n");
            exit(1);
        }

        // 关闭子->父 管道的读端
        close(p2[0]);

        // 打印收到 pong 的消息，前面带进程 ID
        printf("%d: received pong\n", getpid());
    }

    // 父进程正常退出
    exit(0);
}