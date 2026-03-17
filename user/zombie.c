#include "user.h"

int main(void) {
    printf("[zombie] start\n");
    int pid = fork();
    if (pid < 0) {
        printf("[zombie] fork failed\n");
        exit(1);
    }
    if (pid == 0) {
        printf("[zombie] child exit now\n");
        exit(0);
    }

    for (int i = 0; i < 64; i++) {
        yield();
    }

    printf("[zombie] parent exits without wait\n");
    exit(0);
}
