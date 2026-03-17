#include "user.h"

int main(void) {
    int t = uptime();
    if (t < 0) {
        printf("[uptime] syscall failed\n");
        exit(1);
    }
    printf("[uptime] ticks=%d\n", t);
    exit(0);
}
