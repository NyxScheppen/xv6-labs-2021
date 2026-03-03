#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
    char buf[512];
    int i;
    // 每次从stdin读一行
    while (gets(buf, sizeof(buf))) {

        // 去掉换行符
        for (i = 0; buf[i]; i++) {
            if (buf[i] == '\n') {
                buf[i] = 0;
                break;
            }
        }

        if (fork() == 0) {
            char *args[argc + 2];

            // 复制原命令参数
            for (i = 1; i < argc; i++) {
                args[i - 1] = argv[i];
            }

            // 把读到的一行加到最后
            args[argc - 1] = buf;
            args[argc] = 0;

            exec(argv[1], args);
            exit(1);
        }
        wait(0);
    }
    exit(0);
}