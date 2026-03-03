#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "user/user.h"

void 
find(char *path, char *filename) 
{
    int fd;  // 文件描述符，用于打开目录
    struct dirent de;  // 目录项结构体，用于读取目录中的条目
    struct stat st;  // 统计信息，用于判断文件类型
    char buf[512], *p;  // buf 用于保存完整的文件路径，p 指向路径中的动态部分
    
    // 打开给定的目录
    if ((fd = open(path, 0)) < 0) {
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }
    
    // 从目录的元数据获取统计信息，确保 path 确实是一个目录
    if (fstat(fd, &st) < 0) {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }
    
    // 如果 path 不是目录，则无法搜索，直接返回
    if (st.type != T_DIR) {
        fprintf(2, "find: %s is not a directory\n", path);
        close(fd);
        return;
    }
    
    // 构造路径缓冲区：复制 path 到 buf，最后确保有一个 '/' 作为分隔符
    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/';
    
    // 逐个读取目录中的每个目录项（struct dirent）
    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
        // 跳过 inode 号为 0 的项（无效项）
        if (de.inum == 0)
            continue;
        
        // 将目录项的名称拷贝到路径缓冲区中
        strcpy(p, de.name);
        
        // 获取该目录项对应文件的统计信息
        if (stat(buf, &st) < 0) {
            fprintf(2, "find: cannot stat %s\n", buf);
            continue;
        }
        
        // 若该项是一个普通文件且文件名与搜索目标匹配，则打印该文件的路径
        if (st.type == T_FILE && strcmp(de.name, filename) == 0) {
            printf("%s\n", buf);
        }
        
        // 若该项是一个目录，需要递归搜索该目录中的所有文件
        // 但要跳过 "." 和 ".." 这两个特殊目录，避免无限递归
        if (st.type == T_DIR && strcmp(de.name, ".") != 0 && strcmp(de.name, "..") != 0) {
            // 递归调用 find() 搜索子目录
            find(buf, filename);
        }
    }
    
    // 关闭目录文件描述符
    close(fd);
}

int
main(int argc, char *argv[])
{
    // 检查命令行参数数量，应该恰好有 3 个：程序名、路径、文件名
    if (argc != 3) {
        fprintf(2, "Usage: find <path> <filename>\n");
        exit(1);
    } 
    
    // 从命令行参数中提取搜索路径和目标文件名
    char *path = argv[1];
    char *filename = argv[2];
    
    // 执行递归搜索
    find(path, filename);
    
    // 程序正常退出
    exit(0);
}