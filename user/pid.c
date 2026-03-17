#include "user.h"

int main(void) {
    int pid = getpid();
    if (pid < 0) {
        printf("[pid] getpid failed\n");
        exit(1);
    }
    printf("[pid] %d\n", pid);
    exit(0);
}
