#include "user.h"

static char digits[] = "0123456789ABCDEF";

static void putc(int fd, char c) {
    write(fd, &c, 1);
}

static void printint(int fd, long long xx, int base, int signed_val) {
    char buf[20];
    int i = 0;
    int neg = 0;
    unsigned long long x;

    if (signed_val && xx < 0) {
        neg = 1;
        x = (unsigned long long)(-xx);
    } else {
        x = (unsigned long long)xx;
    }

    do {
        buf[i++] = digits[x % (unsigned long long)base];
        x /= (unsigned long long)base;
    } while (x != 0);

    if (neg) {
        buf[i++] = '-';
    }

    while (--i >= 0) {
        putc(fd, buf[i]);
    }
}

static void printptr(int fd, uint64 x) {
    putc(fd, '0');
    putc(fd, 'x');
    for (int i = 0; i < (int)(sizeof(uint64) * 2); i++, x <<= 4) {
        putc(fd, digits[x >> (sizeof(uint64) * 8 - 4)]);
    }
}

void vprintf(int fd, const char *fmt, va_list ap) {
    int state = 0;

    for (int i = 0; fmt[i] != '\0'; i++) {
        int c0 = fmt[i] & 0xff;

        if (state == 0) {
            if (c0 == '%') {
                state = '%';
            } else {
                putc(fd, (char)c0);
            }
            continue;
        }

        int c1 = fmt[i + 1] ? (fmt[i + 1] & 0xff) : 0;
        int c2 = fmt[i + 2] ? (fmt[i + 2] & 0xff) : 0;

        if (c0 == 'd') {
            printint(fd, va_arg(ap, int), 10, 1);
        } else if (c0 == 'l' && c1 == 'd') {
            printint(fd, va_arg(ap, uint64), 10, 1);
            i += 1;
        } else if (c0 == 'l' && c1 == 'l' && c2 == 'd') {
            printint(fd, va_arg(ap, uint64), 10, 1);
            i += 2;
        } else if (c0 == 'u') {
            printint(fd, va_arg(ap, uint32), 10, 0);
        } else if (c0 == 'l' && c1 == 'u') {
            printint(fd, va_arg(ap, uint64), 10, 0);
            i += 1;
        } else if (c0 == 'l' && c1 == 'l' && c2 == 'u') {
            printint(fd, va_arg(ap, uint64), 10, 0);
            i += 2;
        } else if (c0 == 'x') {
            printint(fd, va_arg(ap, uint32), 16, 0);
        } else if (c0 == 'l' && c1 == 'x') {
            printint(fd, va_arg(ap, uint64), 16, 0);
            i += 1;
        } else if (c0 == 'l' && c1 == 'l' && c2 == 'x') {
            printint(fd, va_arg(ap, uint64), 16, 0);
            i += 2;
        } else if (c0 == 'p') {
            printptr(fd, va_arg(ap, uint64));
        } else if (c0 == 'c') {
            putc(fd, (char)va_arg(ap, uint32));
        } else if (c0 == 's') {
            char *s = va_arg(ap, char *);
            if (s == 0) {
                s = "(null)";
            }
            while (*s) {
                putc(fd, *s++);
            }
        } else if (c0 == '%') {
            putc(fd, '%');
        } else {
            putc(fd, '%');
            putc(fd, (char)c0);
        }

        state = 0;
    }
}

void fprintf(int fd, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fd, fmt, ap);
    va_end(ap);
}

void printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(1, fmt, ap);
    va_end(ap);
}
