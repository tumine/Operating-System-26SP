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
    int ticks = 100;
    if (argc > 2) {
        printf("usage: sleep [ticks]\n");
        exit(1);
    }
    if (argc == 2) {
        int ok = 0;
        ticks = parse_uint(argv[1], &ok);
        if (!ok) {
            printf("[sleep] invalid ticks\n");
            exit(1);
        }
    }

    printf("[sleep] sleeping\n");
    if (sleep(ticks) < 0) {
        printf("[sleep] syscall failed\n");
        exit(1);
    }
    printf("[sleep] done\n");
    exit(0);
}
