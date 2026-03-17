#include "user.h"
#include "fcntl.h"

char *strcpy(char *dst, const char *src) {
    char *out = dst;
    while ((*dst++ = *src++) != '\0') {
        // copy with terminator
    }
    return out;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (uchar)*a - (uchar)*b;
}

int strncmp(const char *a, const char *b, uint n) {
    while (n > 0 && *a && *a == *b) {
        a++;
        b++;
        n--;
    }
    if (n == 0) {
        return 0;
    }
    return (uchar)*a - (uchar)*b;
}

uint strlen(const char *s) {
    uint n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

void *memset(void *dst, int c, uint n) {
    uchar *p = (uchar *)dst;
    for (uint i = 0; i < n; i++) {
        p[i] = (uchar)c;
    }
    return dst;
}

char *strchr(const char *s, char c) {
    for (;; s++) {
        if (*s == c) {
            return (char *)s;
        }
        if (*s == '\0') {
            return 0;
        }
    }
}

char *gets(char *buf, int max) {
    if (max <= 0) {
        return buf;
    }

    int i = 0;
    for (; i + 1 < max;) {
        char c = 0;
        int n = read(0, &c, 1);
        if (n < 1) {
            break;
        }
        buf[i++] = c;
        if (c == '\n' || c == '\r') {
            break;
        }
    }
    buf[i] = '\0';
    return buf;
}

int stat(const char *path, struct stat *st) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }
    int r = fstat(fd, st);
    close(fd);
    return r;
}

int atoi(const char *s) {
    int n = 0;
    while ('0' <= *s && *s <= '9') {
        n = n * 10 + *s - '0';
        s++;
    }
    return n;
}

void *memmove(void *dst, const void *src, int n) {
    char *d = (char *)dst;
    const char *s = (const char *)src;

    if (s > d) {
        while (n-- > 0) {
            *d++ = *s++;
        }
    } else {
        d += n;
        s += n;
        while (n-- > 0) {
            *--d = *--s;
        }
    }

    return dst;
}

int memcmp(const void *a, const void *b, uint n) {
    const uchar *p = (const uchar *)a;
    const uchar *q = (const uchar *)b;
    while (n-- > 0) {
        if (*p != *q) {
            return (int)(*p) - (int)(*q);
        }
        p++;
        q++;
    }
    return 0;
}

void *memcpy(void *dst, const void *src, uint n) {
    return memmove(dst, src, (int)n);
}
