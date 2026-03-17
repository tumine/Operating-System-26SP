#include "user.h"
#include "fcntl.h"
#include "fs.h"
#include "stat.h"

enum {
    INO_COL_WIDTH = 6,
    SIZE_COL_WIDTH = 8,
};

static void fmtname(const char *path, char *out) {
    int n = (int)strlen(path);
    int i = n - 1;
    while (i >= 0 && path[i] != '/') {
        i--;
    }
    i++;

    int j = 0;
    while (j < DIRSIZ && path[i + j] != '\0' && path[i + j] != '/') {
        out[j] = path[i + j];
        j++;
    }
    out[j] = '\0';
}

static int digits_u64(uint64 x) {
    int d = 1;
    while (x >= 10) {
        x /= 10;
        d++;
    }
    return d;
}

static void print_spaces(int n) {
    while (n-- > 0) {
        printf(" ");
    }
}

static void print_entry(const char *name, uint ino, uint64 size) {
    int name_len = (int)strlen(name);
    printf("%s", name);
    if (name_len < DIRSIZ) {
        print_spaces(DIRSIZ - name_len);
    }
    print_spaces(2);

    int ino_digits = digits_u64((uint64)ino);
    if (ino_digits < INO_COL_WIDTH) {
        print_spaces(INO_COL_WIDTH - ino_digits);
    }
    printf("%u", ino);
    print_spaces(2);

    int size_digits = digits_u64(size);
    if (size_digits < SIZE_COL_WIDTH) {
        print_spaces(SIZE_COL_WIDTH - size_digits);
    }
    printf("%lu\n", (unsigned long)size);
}

static int list_one(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(2, "ls: cannot open %s\n", path);
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        fprintf(2, "ls: cannot stat %s\n", path);
        close(fd);
        return -1;
    }

    if (st.type != T_DIR) {
        printf("%s\n", path);
        close(fd);
        return 0;
    }

    struct dirent de;
    char full[128];
    char name[DIRSIZ + 1];
    int plen = (int)strlen(path);
    int has_trailing_sep = (plen > 0 && path[plen - 1] == '/');

    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
        if (de.inum == 0) {
            continue;
        }

        for (int i = 0; i < DIRSIZ; i++) {
            name[i] = de.name[i];
        }
        name[DIRSIZ] = '\0';

        int k = 0;
        for (int i = 0; path[i] != '\0' && k + 1 < (int)sizeof(full); i++) {
            full[k++] = path[i];
        }
        if (!has_trailing_sep && k + 1 < (int)sizeof(full)) {
            full[k++] = '/';
        }
        for (int i = 0; i < DIRSIZ && name[i] != '\0' && k + 1 < (int)sizeof(full); i++) {
            full[k++] = name[i];
        }
        full[k] = '\0';

        int efd = open(full, O_RDONLY);
        if (efd < 0) {
            continue;
        }

        struct stat est;
        if (fstat(efd, &est) < 0) {
            close(efd);
            continue;
        }
        close(efd);

        char shortname[DIRSIZ + 1];
        fmtname(full, shortname);

        print_entry(shortname, est.ino, est.size);
    }

    close(fd);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc <= 1) {
        if (list_one("./") < 0) {
            exit(1);
        }
        exit(0);
    }

    int ok = 0;
    for (int i = 1; i < argc; i++) {
        if (list_one(argv[i]) == 0) {
            ok = 1;
        }
    }

    exit(ok ? 0 : 1);
}
