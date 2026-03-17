#include "user.h"

int main(void) {
    for (;;) {
        int pid = fork();
        if (pid < 0) {
            printf("[init] fork /sh failed\n");
            yield();
            continue;
        }
        if (pid == 0) {
            char *argv[] = {"sh", 0};
            exec("/sh", argv);
            printf("[init] exec /sh failed\n");
            exit(127);
        }

        int status = 0;
        if (wait(&status) < 0) {
            printf("[init] wait failed\n");
            exit(1);
        }
    }
}
