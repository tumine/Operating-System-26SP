#include "user.h"

int main(void) {
    printf("[killer] start\n");
    int pid = fork();
    if (pid < 0) {
        printf("[killer] fork failed\n");
        exit(1);
    }

    if (pid == 0) {
        for (;;) {
            yield();
        }
    }

    if (sleep(30) < 0) {
        printf("[killer] parent sleep failed\n");
        exit(2);
    }

    if (kill(pid) < 0) {
        printf("[killer] kill failed\n");
        exit(3);
    }

    int status = 0;
    if (wait(&status) < 0) {
        printf("[killer] wait failed\n");
        exit(4);
    }

    printf("[killer] child status=%d\n", status);
    printf("[killer] done\n");
    exit(0);
}
