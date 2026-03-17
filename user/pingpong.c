#include "user.h"

int main(void) {
    int p2c[2];
    int c2p[2];

    if (pipe(p2c) < 0 || pipe(c2p) < 0) {
        printf("[pingpong] pipe failed\n");
        exit(1);
    }

    int pid = fork();
    if (pid < 0) {
        printf("[pingpong] fork failed\n");
        exit(2);
    }

    if (pid == 0) {
        close(p2c[1]);
        close(c2p[0]);

        char ch = 0;
        if (read(p2c[0], &ch, 1) != 1) {
            printf("[pingpong] child read failed\n");
            exit(3);
        }
        printf("[pingpong] child got ping\n");

        ch = 'x';
        if (write(c2p[1], &ch, 1) != 1) {
            printf("[pingpong] child write failed\n");
            exit(4);
        }

        close(p2c[0]);
        close(c2p[1]);
        exit(0);
    }

    close(p2c[0]);
    close(c2p[1]);

    char ch = 'y';
    if (write(p2c[1], &ch, 1) != 1) {
        printf("[pingpong] parent write failed\n");
        exit(5);
    }
    if (read(c2p[0], &ch, 1) != 1) {
        printf("[pingpong] parent read failed\n");
        exit(6);
    }
    printf("[pingpong] parent got pong\n");

    close(p2c[1]);
    close(c2p[0]);

    int status = 0;
    wait(&status);
    exit(0);
}
