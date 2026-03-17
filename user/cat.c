#include "user.h"
#include "fcntl.h"

static int catfd(int fd) {
    char buf[512];
    for (;;) {
        int n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            return -1;
        }
        if (n == 0) {
            return 0;
        }
        if (write(1, buf, n) != n) {
            return -1;
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc <= 1) {
        if (catfd(0) < 0) {
            fprintf(2, "cat: read/write error\n");
            exit(1);
        }
        exit(0);
    }

    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            fprintf(2, "cat: cannot open %s\n", argv[i]);
            exit(1);
        }
        if (catfd(fd) < 0) {
            fprintf(2, "cat: read/write error\n");
            close(fd);
            exit(1);
        }
        close(fd);
    }
    exit(0);
}
