#include "user.h"
#include "fcntl.h"

static void worker(int id) {
    static const char *files[] = {
        "/init",
        "/hello",
        "/quiet",
        "/stressio",
        "/stsched",
    };
    const int nfiles = (int)(sizeof(files) / sizeof(files[0]));
    char buf[256];

    for (int round = 0; round < 48; round++) {
        const char *name = files[(id + round) % nfiles];
        int fd = open(name, O_RDONLY);
        if (fd < 0) {
            exit(2);
        }

        for (;;) {
            int n = read(fd, buf, sizeof(buf));
            if (n < 0) {
                close(fd);
                exit(3);
            }
            if (n == 0) {
                break;
            }
        }

        if (close(fd) < 0) {
            exit(4);
        }

        if ((round & 7) == 7) {
            yield();
        }
    }

    exit(0);
}

int main(void) {
    const int workers = 6;
    int fail = 0;

    printf("[stressdisk] start\n");

    for (int i = 0; i < workers; i++) {
        int pid = fork();
        if (pid < 0) {
            printf("[stressdisk] fork failed\n");
            exit(1);
        }
        if (pid == 0) {
            worker(i);
        }
    }

    for (int i = 0; i < workers; i++) {
        int status = 0;
        if (wait(&status) < 0) {
            printf("[stressdisk] wait failed\n");
            exit(5);
        }
        if (status != 0) {
            fail = 1;
        }
    }

    if (fail) {
        printf("[stressdisk] worker failed\n");
        exit(6);
    }

    printf("[stressdisk] done\n");
    exit(0);
}
