#include "user.h"
#include "fcntl.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(2, "usage: touch files...\n");
        exit(1);
    }

    int failed = 0;
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_CREATE | O_WRONLY);
        if (fd < 0) {
            fprintf(2, "touch: failed to open %s\n", argv[i]);
            failed = 1;
            continue;
        }
        if (close(fd) < 0) {
            fprintf(2, "touch: failed to close %s\n", argv[i]);
            failed = 1;
        }
    }

    exit(failed ? 1 : 0);
}
