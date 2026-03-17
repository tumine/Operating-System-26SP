#include "user.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(2, "usage: rm files...\n");
        exit(1);
    }

    int failed = 0;
    for (int i = 1; i < argc; i++) {
        if (unlink(argv[i]) < 0) {
            fprintf(2, "rm: failed to remove %s\n", argv[i]);
            failed = 1;
        }
    }

    exit(failed ? 1 : 0);
}
