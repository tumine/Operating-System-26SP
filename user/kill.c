#include "user.h"

static int parse_pid(const char *s, int *ok) {
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
    if (argc < 2) {
        fprintf(2, "usage: kill pid...\n");
        exit(1);
    }

    int ok_all = 1;
    for (int i = 1; i < argc; i++) {
        int ok = 0;
        int pid = parse_pid(argv[i], &ok);
        if (!ok) {
            fprintf(2, "kill: bad pid\n");
            ok_all = 0;
            continue;
        }
        if (kill(pid) < 0) {
            fprintf(2, "kill: failed\n");
            ok_all = 0;
        }
    }

    exit(ok_all ? 0 : 1);
}
