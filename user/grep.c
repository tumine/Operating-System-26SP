#include "user.h"
#include "fcntl.h"

// Regexp matcher from Kernighan & Pike.
static int matchhere(char *re, char *text);
static int matchstar(int c, char *re, char *text);

static int match(char *re, char *text) {
    if (re[0] == '^') {
        return matchhere(re + 1, text);
    }
    do {
        if (matchhere(re, text)) {
            return 1;
        }
    } while (*text++ != '\0');
    return 0;
}

static int matchhere(char *re, char *text) {
    if (re[0] == '\0') {
        return 1;
    }
    if (re[1] == '*') {
        return matchstar(re[0], re + 2, text);
    }
    if (re[0] == '$' && re[1] == '\0') {
        return *text == '\0';
    }
    if (*text != '\0' && (re[0] == '.' || re[0] == *text)) {
        return matchhere(re + 1, text + 1);
    }
    return 0;
}

static int matchstar(int c, char *re, char *text) {
    do {
        if (matchhere(re, text)) {
            return 1;
        }
    } while (*text != '\0' && (*text++ == c || c == '.'));
    return 0;
}

static int grepfd(char *pattern, int fd) {
    char buf[1024];
    int m = 0;

    for (;;) {
        int n = read(fd, buf + m, (int)sizeof(buf) - m - 1);
        if (n < 0) {
            return -1;
        }
        if (n == 0) {
            break;
        }
        m += n;
        buf[m] = '\0';

        char *p = buf;
        for (;;) {
            char *q = strchr(p, '\n');
            if (q == 0) {
                break;
            }
            *q = '\0';
            if (match(pattern, p)) {
                *q = '\n';
                write(1, p, (int)(q + 1 - p));
            }
            p = q + 1;
        }
        if (m > 0) {
            m -= (int)(p - buf);
            memmove(buf, p, m);
        }
    }

    if (m > 0) {
        buf[m] = '\0';
        if (match(pattern, buf)) {
            write(1, buf, m);
            if (buf[m - 1] != '\n') {
                write(1, "\n", 1);
            }
        }
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc <= 1) {
        fprintf(2, "usage: grep pattern [file ...]\n");
        exit(1);
    }
    char *pattern = argv[1];

    if (argc <= 2) {
        if (grepfd(pattern, 0) < 0) {
            fprintf(2, "grep: read error\n");
            exit(1);
        }
        exit(0);
    }

    for (int i = 2; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            fprintf(2, "grep: cannot open %s\n", argv[i]);
            exit(1);
        }
        if (grepfd(pattern, fd) < 0) {
            fprintf(2, "grep: read error\n");
            close(fd);
            exit(1);
        }
        close(fd);
    }
    exit(0);
}
