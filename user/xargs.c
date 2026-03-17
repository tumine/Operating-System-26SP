#include "user.h"
#include "param.h"

static int run_once(char *cmd_argv[], int fixed_argc, char *line) {
    cmd_argv[fixed_argc] = line;
    cmd_argv[fixed_argc + 1] = 0;

    int pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        exec(cmd_argv[0], cmd_argv);
        fprintf(2, "xargs: exec failed\n");
        exit(1);
    }
    int status = 0;
    if (wait(&status) < 0) {
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(2, "usage: xargs command [args ...]\n");
        exit(1);
    }

    int fixed_argc = argc - 1;
    if (fixed_argc + 2 > MAXARG) {
        fprintf(2, "xargs: too many args\n");
        exit(1);
    }

    char *cmd_argv[MAXARG + 1];
    for (int i = 0; i < fixed_argc; i++) {
        cmd_argv[i] = argv[i + 1];
    }

    char line[512];
    int n = 0;
    char c = 0;
    while (read(0, &c, 1) == 1) {
        if (c == '\n') {
            line[n] = '\0';
            if (n > 0) {
                if (run_once(cmd_argv, fixed_argc, line) < 0) {
                    fprintf(2, "xargs: run failed\n");
                    exit(1);
                }
            }
            n = 0;
            continue;
        }
        if (n + 1 < (int)sizeof(line)) {
            line[n++] = c;
        }
    }

    if (n > 0) {
        line[n] = '\0';
        if (run_once(cmd_argv, fixed_argc, line) < 0) {
            fprintf(2, "xargs: run failed\n");
            exit(1);
        }
    }

    exit(0);
}
