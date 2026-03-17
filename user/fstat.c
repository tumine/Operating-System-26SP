#include "user.h"
#include "fcntl.h"
#include "stat.h"

static const char *typename1(short t) {
    if (t == 1) {
        return "dir";
    }
    if (t == 2) {
        return "file";
    }
    if (t == 3) {
        return "dev";
    }
    return "unknown";
}

int main(void) {
    const char *path = "/TEST.TXT";
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("[fstat] open failed: %s\n", path);
        exit(1);
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        printf("[fstat] syscall failed\n");
        close(fd);
        exit(2);
    }
    close(fd);

    printf("[fstat] path=%s dev=%d ino=%u type=%s nlink=%d size=%lu\n",
           path,
           st.dev,
           st.ino,
           typename1(st.type),
           (int)st.nlink,
           (unsigned long)st.size);
    exit(0);
}
