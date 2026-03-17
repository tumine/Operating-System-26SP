#include "user.h"

int main(void) {
    const int rounds = 24;
    const int workers = 6;

    printf("[stressio] start\n");

    for (int r = 0; r < rounds; r++) {
        for (int i = 0; i < workers; i++) {
            int pid = fork();
            if (pid < 0) {
                printf("[stressio] fork failed\n");
                exit(1);
            }
            if (pid == 0) {
                char *argv[] = {"quiet", 0};
                exec("/quiet", argv);
                printf("[stressio] exec failed\n");
                exit(2);
            }
        }

        for (int i = 0; i < workers; i++) {
            int status = 0;
            if (wait(&status) < 0) {
                printf("[stressio] wait failed\n");
                exit(3);
            }
        }

        if ((r & 3) == 3) {
            printf("[stressio] progress\n");
        }
        yield();
    }

    printf("[stressio] done\n");
    exit(0);
}
