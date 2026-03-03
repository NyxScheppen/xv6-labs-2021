struct stat;
struct rtcdate;

// system calls
int fork(void);
int exit(int) __attribute__((noreturn));
int wait(int*);
int pipe(int*);
int write(int, const void*, int);
int read(int, void*, int);
int close(int);
int kill(int);
int exec(char*, char**);
int open(const char*, int);
int mknod(const char*, short, short); // 在文件系统中创建一个新的特殊文件
int unlink(const char*); // 删除某个文件或目录项
int fstat(int fd, struct stat*); // 获取文件描述符 fd 对应文件的统计信息，并将其存储在 stat 结构体中
int link(const char*, const char*); // 创建一个新的链接（硬链接），将新路径链接到旧路径所指向的文件
int mkdir(const char*); // 在文件系统中创建一个新的目录
int chdir(const char*); // 更改当前工作目录到指定路径
int dup(int); // 复制一个文件描述符，返回新的文件描述符
int getpid(void); // 获取当前进程的进程 ID
char* sbrk(int); // 增加进程的数据段大小，返回增加前的地址
int sleep(int); // 使当前进程睡眠指定的时间（以时钟滴答数为单位）
int uptime(void); // 获取系统自启动以来的时钟滴答数

// ulib.c
int stat(const char*, struct stat*); // 获取指定路径的文件统计信息，并将其存储在 stat 结构体中
char* strcpy(char*, const char*); // 将源字符串复制到目标字符串，返回目标字符串的指针
void *memmove(void*, const void*, int); // 在内存中复制 n 个字节，从源地址复制到目标地址，处理重叠情况
char* strchr(const char*, char c); // 在字符串中查找字符 c 的第一次出现，返回指向该字符的指针，如果未找到则返回 NULL
int strcmp(const char*, const char*);
void fprintf(int, const char*, ...); // 向指定文件描述符写入格式化的字符串，类似于 printf，但输出到指定的文件描述符（如标准错误）
void printf(const char*, ...);
char* gets(char*, int max); // 从标准输入读取一行文本，存储在 buf 中，最多读取 max-1 个字符，并在末尾添加 null 字符
uint strlen(const char*); // 计算字符串的长度，不包括 null 字符，返回字符串中的字符数
void* memset(void*, int, uint); // 将内存块的前 n 个字节设置为指定的值，返回指向内存块的指针
void* malloc(uint);
void free(void*);
int atoi(const char*);
int memcmp(const void *, const void *, uint);
void *memcpy(void *, const void *, uint);
