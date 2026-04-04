#include "user.h"

static int parse_uint(const char *s, int *ok) {
    if (s == 0 || s[0] == '\0') {
        *ok = 0;
        return 0;
    }
    for (int i = 0; s[i] != '\0'; i++) {
        char c = s[i];
        if (c < '0' || c > '9') {
            *ok = 0;
            return 0;
        }
    }
    *ok = 1;
    return atoi(s);
}

int main(int argc, char *argv[]) {
    int ticks = 1;
    if (argc > 2) {
        fprintf(2, "usage: timerdemo [ticks]\n");
        exit(1);
    }
    if (argc == 2) {
        int ok = 0;
        ticks = parse_uint(argv[1], &ok);
        if (!ok) {
            fprintf(2, "[timerdemo] invalid tick count\n");
            exit(1);
        }
    }

    uint64 start_us = timer_start();
    if (sleep(ticks) < 0) {
        fprintf(2, "[timerdemo] sleep syscall failed\n");
        exit(1);
    }
    uint64 end_us = timer_end();

    printf("[timerdemo] ticks=%d start_us=%lu end_us=%lu elapsed_us=%lu elapsed_ms=%lu\n",
           ticks,
           (unsigned long)start_us,
           (unsigned long)end_us,
           (unsigned long)timer_elapsed_us(start_us, end_us),
           (unsigned long)timer_elapsed_ms(start_us, end_us));
    exit(0);
}
