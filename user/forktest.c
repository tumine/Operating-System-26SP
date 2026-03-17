#include "user.h"

int main(void) {
    const int max_children = 32;
    int n = 0;

    printf("[forktest] start\n");
    for (n = 0; n < max_children; n++) {
        int pid = fork();
        if (pid < 0) {
            break;
        }
        if (pid == 0) {
            exit(0);
        }
    }

    if (n == 0) {
        printf("[forktest] fork failed immediately\n");
        exit(1);
    }

    for (int i = 0; i < n; i++) {
        int status = 0;
        if (wait(&status) < 0) {
            printf("[forktest] wait failed\n");
            exit(2);
        }
    }

    int status = 0;
    if (wait(&status) >= 0) {
        printf("[forktest] unexpected extra child\n");
        exit(3);
    }

    printf("[forktest] reaped children=%d\n", n);
    printf("[forktest] ok\n");
    exit(0);
}
