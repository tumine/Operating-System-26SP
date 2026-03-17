#include "user.h"

#define MAXLINE 128
#define MAXARGS 16

static int isspace1(char c) {
    return c == ' ' || c == '\t';
}

static char *skipspace(char *s) {
    while (*s && isspace1(*s)) {
        s++;
    }
    return s;
}

static void rstrip(char *s) {
    int n = (int)strlen(s);
    while (n > 0 && isspace1(s[n - 1])) {
        s[n - 1] = '\0';
        n--;
    }
}

static int readline(char *buf, int cap) {
    int i = 0;
    while (i + 1 < cap) {
        char c = 0;
        int n = read(0, &c, 1);
        if (n <= 0) {
            return -1;
        }
        if (c == '\r') {
            c = '\n';
        }
        if (c == '\n') {
            buf[i] = '\0';
            return 0;
        }
        buf[i++] = c;
    }
    buf[i] = '\0';
    return 0;
}

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

int main(void) {
    char line[MAXLINE];
    char path[MAXLINE];
    char *argv[MAXARGS];

    for (;;) {
        printf("$ ");
        if (readline(line, sizeof(line)) < 0) {
            printf("\n[sh] input closed\n");
            exit(0);
        }

        int argc = parseargs(line, argv, MAXARGS);
        if (argc < 0) {
            printf("[sh] too many args\n");
            continue;
        }
        if (argc == 0) {
            continue;
        }

        char *cmd = argv[0];

        if (strcmp(cmd, "help") == 0) {
            printf("Run program by name with args, e.g. sleep 20, echo hi\n");
            printf("Builtins: cd [dir], exit\n");
            continue;
        }
        if (strcmp(cmd, "cd") == 0) {
            const char *target = "/";
            if (argc > 2) {
                printf("[sh] usage: cd [dir]\n");
                continue;
            }
            if (argc == 2) {
                target = argv[1];
            }
            if (chdir(target) < 0) {
                printf("[sh] cd failed\n");
            }
            continue;
        }
        if (strcmp(cmd, "exit") == 0) {
            exit(0);
        }

        if (makepath(cmd, path, sizeof(path)) < 0) {
            printf("[sh] command too long\n");
            continue;
        }
        argv[0] = path;

        int pid = fork();
        if (pid < 0) {
            printf("[sh] fork failed\n");
            continue;
        }
        if (pid == 0) {
            exec(path, argv);
            printf("[sh] exec failed\n");
            exit(127);
        }

        int status = 0;
        if (wait(&status) < 0) {
            printf("[sh] wait failed\n");
        }
    }
}
