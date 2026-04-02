#include "user.h"
#include "fcntl.h"

#define MAXLINE 256
#define MAXARGS 16
#define READCHUNK 256

/* 判断是否为空白字符（空格、制表符、回车），用于行内裁剪与参数切分。 */
static int isspace1(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

/* 从字符串开头跳过连续空白，返回第一个非空白字符的指针。 */
static char *skipspace(char *s) {
    while (*s && isspace1(*s)) {
        s++;
    }
    return s;
}

/* 去掉字符串尾部空白（原地修改）。 */
static void rstrip(char *s) {
    int n = (int)strlen(s);
    while (n > 0 && isspace1(s[n - 1])) {
        s[n - 1] = '\0';
        n--;
    }
}

/* 按块 read 脚本文件，并在内部缓冲中解析出“逻辑行”（以 \\n 分隔）。 */
struct line_reader {
    int fd;
    char buf[READCHUNK];
    int nbuf;
    int off;
    int eof;
};

/* 初始化行读取器：绑定已打开的脚本 fd，清空缓冲区与 EOF 标记。 */
static void lr_init(struct line_reader *lr, int fd) {
    lr->fd = fd;
    lr->nbuf = 0;
    lr->off = 0;
    lr->eof = 0;
}

/*
 * 从脚本中读取一行（不含换行符），写入 out 并以 '\\0' 结尾。
 * 返回值：1=读到一行；0=EOF 且无未结束的半行；-1=read 失败；-2=超过 cap-1 字节。
 */
static int lr_readline(struct line_reader *lr, char *out, int cap) {
    int pos = 0;
    if (cap <= 0) {
        return -2;
    }

    for (;;) {
        if (lr->off >= lr->nbuf) {
            if (lr->eof) {
                if (pos == 0) {
                    return 0;
                }
                out[pos] = '\0';
                return 1;
            }
            int n = read(lr->fd, lr->buf, sizeof(lr->buf));
            if (n < 0) {
                return -1;
            }
            if (n == 0) {
                lr->eof = 1;
                continue;
            }
            lr->nbuf = n;
            lr->off = 0;
        }

        char c = lr->buf[lr->off++];
        if (c == '\r') {
            c = '\n';
        }
        if (c == '\n') {
            out[pos] = '\0';
            return 1;
        }
        if (pos + 1 >= cap) {
            return -2;
        }
        out[pos++] = c;
    }
}

/*
 * 将命令名转为可 exec 的路径：无前导 '/' 时补成 "/cmd"（与 sh 一致）。
 * 成功返回 0，缓冲区不足返回 -1。
 */
static int makepath(const char *cmd, char *path, int cap) {
    int i = 0;
    if (cmd[0] != '/') {
        if (cap < 2) {
            return -1;
        }
        path[i++] = '/';
    }
    for (int j = 0; cmd[j] != '\0'; j++) {
        if (i + 1 >= cap) {
            return -1;
        }
        path[i++] = cmd[j];
    }
    path[i] = '\0';
    return 0;
}

/*
 * 按空白切分一行得到 argv[]，在 line 上原地写 '\\0' 截断各参数。
 * 与 sh.c 的 parseargs 行为一致；参数过多返回 -1。
 */
static int parseargs(char *line, char *argv[], int maxargs) {
    int argc = 0;
    char *s = skipspace(line);
    rstrip(s);

    while (*s) {
        if (argc + 1 >= maxargs) {
            return -1;
        }
        argv[argc++] = s;
        while (*s && !isspace1(*s)) {
            s++;
        }
        if (*s == '\0') {
            break;
        }
        *s = '\0';
        s = skipspace(s + 1);
    }
    argv[argc] = 0;
    return argc;
}


/*
 * 预处理脚本中的一行：去掉首尾空白，跳过空行与以 '#' 开头的注释行，
 * 再调用 parseargs 得到 argc/argv。
 * 返回值：>0 为参数个数；0 表示本行无需执行；<0 表示参数过多等错误。
 */
static int prepare_argv(char *line, char *argv[], int maxargs) {
    char *work = line;

    rstrip(work);
    work = skipspace(work);
    if (work[0] == '\0') {
        return 0;
    }
    if (work[0] == '#') {
        return 0;
    }

    int argc = parseargs(work, argv, maxargs);
    if (argc < 0) {
        return -1;
    }
    return argc;
}

/*
 * 脚本解释器入口：打开脚本、逐行读取，对每行解析出命令后通过 fork/exec/wait 执行。
 * 用法：runscript <脚本路径>
 */
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(2, "[runscript] usage: runscript <script>\n");
        exit(1);
    }

    const char *script = argv[1];
    int fd = open(script, O_RDONLY);  // 文件描述符
    if (fd == -1)     // 文件打开失败
    {
        fprintf(2, "[runscript] error: opening script parameter (%s) failed\n", script);
        exit(1);
    }

    struct line_reader lr;      // 存储和解析脚本文件
    char oriLine[MAXLINE];      // 暂存原始单行指令
    char *cmd_argv[MAXARGS];    // 单行指令附带的参数
    char path[MAXLINE];
    int status;                 // 子进程（指令）执行结束状态
    int pid;
    int ret;

    lr_init(&lr, fd);

    // 逐行读取并执行脚本
    while ((ret = lr_readline(&lr, oriLine, MAXLINE)) > 0)
    {
        // 预处理，跳过空行和注释
        int argc = prepare_argv(oriLine, cmd_argv, MAXARGS);
        if (argc == 0)          // 本行指令无需执行
            continue;
        else if (argc < 0)      // 本行指令出现参数过多等错误
        {
            fprintf(2, "[runscript] error: too many arguments\n");
            exit(1);
        }

        // 创建子进程处理当前指令
        pid = fork();
        if (pid < 0)
        {
            fprintf(2, "[runscript] error: fork failed\n");
            exit(1);
        }
        else if (pid == 0)
        {
            // 子进程执行命令
            if (makepath(cmd_argv[0], path, MAXLINE) < 0)   // 缓冲区不足
            {
                fprintf(2, "[runscript] error: path too long\n");
                exit(1);
            }
            exec(path, cmd_argv);   // 构造路径完成，执行指令
            // 调用 exec 后直接替换子进程映像，因此正常执行指令的情况下不会执行下方的异常处理内容
            fprintf(2, "[runscript] error: execute command %s failed\n", path);
            exit(1);    // 子进程执行失败
        }

        // 父进程等待子进程结束后继续执行
        wait(&status);
        if (status != 0)    // 子进程退出状态非 0，代表子进程执行失败
            exit(1);        // 子进程中理论上已写明报错信息，无需重复输出报错信息
    }

    // 检查读取是否出错
    if (ret == 0)           // 未读取到完整一行
    {
        fprintf(2, "[runscript] warning: incomplete line read\n");
        exit(1);
    }
    else if (ret == -1)     // 读取失败
    {
        fprintf(2, "[runscript] error: read error\n");
        exit(1);
    }
    else if (ret == -2)     // 读取超长
    {
        fprintf(2, "[runscript] error: line too long\n");
        exit(1);
    }

    close(fd);
    exit(0);
}
