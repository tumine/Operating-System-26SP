#include "user.h"

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(2, "usage: ln old new\n");
        exit(1);
    }

    if (link(argv[1], argv[2]) < 0) {
        fprintf(2, "ln: failed to link %s -> %s\n", argv[1], argv[2]);
        exit(1);
    }

    exit(0);
}
