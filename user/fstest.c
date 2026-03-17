#include "user.h"
#include "fcntl.h"

int main(void) {
    const char *dir = "/tmpd";
    const char *path = "/tmpd/hello";
    const char *msg = "hello-write\n";
    int msg_len = (int)strlen(msg);

    if (mkdir(dir) < 0) {
        fprintf(2, "[fstest] mkdir failed\n");
        exit(1);
    }

    int fd = open(path, O_CREATE | O_RDWR);
    if (fd < 0) {
        fprintf(2, "[fstest] open create failed\n");
        exit(2);
    }

    int wn = write(fd, msg, msg_len);
    if (wn != msg_len) {
        fprintf(2, "[fstest] first write failed\n");
        close(fd);
        exit(3);
    }
    close(fd);

    char buf[32];
    memset(buf, 0, sizeof(buf));

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(2, "[fstest] reopen failed\n");
        exit(4);
    }
    int rn = read(fd, buf, sizeof(buf));
    close(fd);
    if (rn != msg_len || memcmp(buf, msg, (uint)rn) != 0) {
        fprintf(2, "[fstest] readback mismatch\n");
        exit(5);
    }

    fd = open(path, O_WRONLY | O_TRUNC);
    if (fd < 0) {
        fprintf(2, "[fstest] open trunc failed\n");
        exit(6);
    }
    if (write(fd, "x", 1) != 1) {
        fprintf(2, "[fstest] trunc write failed\n");
        close(fd);
        exit(7);
    }
    close(fd);

    buf[0] = 0;
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(2, "[fstest] reopen2 failed\n");
        exit(8);
    }
    rn = read(fd, buf, sizeof(buf));
    close(fd);
    if (rn != 1 || buf[0] != 'x') {
        fprintf(2, "[fstest] trunc readback mismatch\n");
        exit(9);
    }

    if (unlink(path) < 0) {
        fprintf(2, "[fstest] unlink file failed\n");
        exit(10);
    }
    if (unlink(dir) < 0) {
        fprintf(2, "[fstest] unlink dir failed\n");
        exit(11);
    }

    printf("[fstest] ok\n");
    exit(0);
}
