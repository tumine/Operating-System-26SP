#include "user.h"
#include "fcntl.h"
#include "fs.h"
#include "stat.h"

static const char *basename1(const char *path) {
    const char *base = path;
    for (const char *p = path; *p != '\0'; p++) {
        if (*p == '/') {
            base = p + 1;
        }
    }
    return base;
}

static void findrec(const char *path, const char *target) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        return;
    }

    if (st.type == T_FILE) {
        if (strcmp(basename1(path), target) == 0) {
            printf("%s\n", path);
        }
        close(fd);
        return;
    }

    if (st.type != T_DIR) {
        close(fd);
        return;
    }

    char buf[256];
    int plen = (int)strlen(path);
    if (plen + 1 + DIRSIZ + 1 > (int)sizeof(buf)) {
        close(fd);
        return;
    }

    memmove(buf, path, plen);
    char *p = buf + plen;
    if (plen > 0 && buf[plen - 1] != '/') {
        *p++ = '/';
    }

    struct dirent de;
    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
        if (de.inum == 0) {
            continue;
        }

        char name[DIRSIZ + 1];
        for (int i = 0; i < DIRSIZ; i++) {
            name[i] = de.name[i];
        }
        name[DIRSIZ] = '\0';

        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }

        int i = 0;
        while (i < DIRSIZ && name[i] != '\0') {
            p[i] = name[i];
            i++;
        }
        p[i] = '\0';

        findrec(buf, target);
    }

    close(fd);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(2, "usage: find <path> <name>\n");
        exit(1);
    }
    findrec(argv[1], argv[2]);
    exit(0);
}
