#include "user.h"

static void worker(void) {
    const int loops = 3000;

    for (int i = 0; i < loops; i++) {
        yield();

        // Stress file-table locking on the console fd path.
        if ((i & 255) == 0) {
            int fd = dup(1);
            if (fd >= 0) {
                close(fd);
            }
        }

        // Stress grow/shrink and page-table operations.
        if ((i & 511) == 0) {
            void *p = sbrk(4096);
            if (p == (void *)-1) {
                exit(2);
            }
            if (sbrk(-4096) == (void *)-1) {
                exit(3);
            }
        }
    }

    exit(0);
}

int main(void) {
    // Keep headroom under NPROC when running concurrently with stressio.
    const int workers = 4;
    int fail = 0;

    printf("[stresssched] start\n");

    for (int i = 0; i < workers; i++) {
        int pid = fork();
        if (pid < 0) {
            printf("[stresssched] fork failed\n");
            exit(1);
        }
        if (pid == 0) {
            worker();
        }
    }

    for (int i = 0; i < workers; i++) {
        int status = 0;
        if (wait(&status) < 0) {
            printf("[stresssched] wait failed\n");
            exit(4);
        }
        if (status != 0) {
            fail = 1;
        }
    }

    if (fail) {
        printf("[stresssched] worker failed\n");
        exit(5);
    }

    printf("[stresssched] done\n");
    exit(0);
}
