#include "user.h"

/*
 * 运行 /runscript <script_path>，然后把 runscript 的退出码打印出来。
 * 输出格式：RS_RC: <code>
 *
 * 用于测试时检查返回值，而不是检查 runscript 自己写的错误信息字符串。
 */
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(2, "usage: rs_status <script_path>\n");
        exit(1);
    }

    char *script = argv[1];

    int pid = fork();
    if (pid < 0) {
        fprintf(2, "rs_status: fork failed\n");
        exit(1);
    }

    if (pid == 0) {
        char *args[3];
        args[0] = "runscript";
        args[1] = script;
        args[2] = 0;
        exec("/runscript", args);
        fprintf(2, "rs_status: exec runscript failed\n");
        exit(127);
    }

    int status = 0;
    if (wait(&status) < 0) {
        fprintf(2, "rs_status: wait failed\n");
        exit(2);
    }

    printf("RS_RC: %d\n", status);
    exit(0);
}

