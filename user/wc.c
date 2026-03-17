#include "user.h"
#include "fcntl.h"

static int isspace1(char c) {
    return c == ' ' || c == '\r' || c == '\t' || c == '\n' || c == '\v';
}

static int do_wc(int fd, const char *name) {
    char buf[512];
    uint64 lines = 0;
    uint64 words = 0;
    uint64 bytes = 0;
    int inword = 0;

    for (;;) {
        int n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            return -1;
        }
        if (n == 0) {
            break;
        }

        for (int i = 0; i < n; i++) {
            char c = buf[i];
            bytes++;
            if (c == '\n') {
                lines++;
            }
            if (isspace1(c)) {
                inword = 0;
            } else if (!inword) {
                words++;
                inword = 1;
            }
        }
    }

    if (name && name[0]) {
        printf("%lu %lu %lu %s\n",
               (unsigned long)lines,
               (unsigned long)words,
               (unsigned long)bytes,
               name);
    } else {
        printf("%lu %lu %lu\n",
               (unsigned long)lines,
               (unsigned long)words,
               (unsigned long)bytes);
    }

    return 0;
}

int main(int argc, char *argv[]) {
    if (argc <= 1) {
        if (do_wc(0, "") < 0) {
            fprintf(2, "wc: read error\n");
            exit(1);
        }
        exit(0);
    }

    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            fprintf(2, "wc: cannot open %s\n", argv[i]);
            exit(1);
        }
        if (do_wc(fd, argv[i]) < 0) {
            fprintf(2, "wc: read error\n");
            close(fd);
            exit(1);
        }
        close(fd);
    }

    exit(0);
}
