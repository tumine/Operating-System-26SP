#include "user.h"
#include "stat.h"

static void uitoa10(int value, char *out, int out_cap) {
    char digits[16];
    int nd = 0;

    if (out_cap <= 0) {
        return;
    }
    if (value <= 0) {
        out[0] = '0';
        if (out_cap > 1) {
            out[1] = '\0';
        }
        return;
    }

    while (value > 0 && nd < (int)sizeof(digits)) {
        digits[nd++] = (char)('0' + (value % 10));
        value /= 10;
    }

    int pos = 0;
    for (int i = nd - 1; i >= 0 && pos + 1 < out_cap; i--) {
        out[pos++] = digits[i];
    }
    out[pos] = '\0';
}

static int have_ai_assets(void) {
    struct stat st;
    return stat("/AI/SMOL/CFG.BIN", &st) == 0 || stat("/AI/QWEN/CFG.BIN", &st) == 0;
}

static int start_shell(void) {
    int pid = fork();
    if (pid < 0) {
        printf("[init] fork /sh failed\n");
        return -1;
    }
    if (pid == 0) {
        char *argv[] = {"sh", 0};
        exec("/sh", argv);
        printf("[init] exec /sh failed\n");
        exit(127);
    }
    return pid;
}

static int start_ai_daemon(int *ready_out) {
    int ready_pipe[2];
    if (ready_out != 0) {
        *ready_out = 0;
    }
    if (pipe(ready_pipe) < 0) {
        printf("[init] pipe /ai_daemon failed\n");
        return -1;
    }

    int pid = fork();
    if (pid < 0) {
        close(ready_pipe[0]);
        close(ready_pipe[1]);
        printf("[init] fork /ai_daemon failed\n");
        return -1;
    }

    if (pid == 0) {     // 子进程
        char fdarg[16];
        close(ready_pipe[0]);   // 关闭管道读端（子进程通过管道向父进程发送就绪信号）
        uitoa10(ready_pipe[1], fdarg, sizeof(fdarg));
        char *argv[] = {"ai_daemon", "--ready-fd", fdarg, 0};
        exec("/ai_daemon", argv);
        printf("[init] exec /ai_daemon failed\n");
        close(ready_pipe[1]);
        exit(127);
    }

    // 父进程
    close(ready_pipe[1]);       // 关闭管道写端（父进程只读）
    char ready = 0;
    int n = read(ready_pipe[0], &ready, 1);     // 父进程阻塞等待子进程发送就绪信号
    close(ready_pipe[0]);
    if (n == 1) {
        if (ready_out != 0) {
            *ready_out = 1;
        }
        printf("[init] ai_daemon ready (pid=%d)\n", pid);
    } else {
        printf("[init] ai_daemon did not signal ready\n");
    }
    return pid;
}

int main(void) {
    int shell_pid = -1;
    int daemon_pid = -1;
    int want_ai = have_ai_assets();     // 是否存在 AI 资产文件

    for (;;) {
        // 启动 AI 守护进程
        if (want_ai && daemon_pid < 0) {
            int daemon_ready = 0;
            daemon_pid = start_ai_daemon(&daemon_ready);
            if (daemon_pid < 0) {
                want_ai = 0;
                printf("[init] disabling ai_daemon after launch failure\n");
            } else if (!daemon_ready) {
                want_ai = 0;
                printf("[init] disabling ai_daemon restarts after startup failure\n");
            }
        }

        // 启动 shell
        if (shell_pid < 0) {
            shell_pid = start_shell();
            if (shell_pid < 0) {
                yield();        // 让出 CPU 资源
                continue;
            }
        }

        int status = 0;
        int pid = wait(&status);        // 阻塞等待任意子进程退出
        if (pid < 0) {
            printf("[init] wait failed\n");
            yield();
            continue;
        }

        // shell 退出
        if (pid == shell_pid) {
            shell_pid = -1;
            continue;
        }

        // AI 守护进程退出
        if (pid == daemon_pid) {
            printf("[init] ai_daemon exited status=%d\n", status);
            daemon_pid = -1;
            continue;
        }
    }
}
