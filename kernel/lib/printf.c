#include "types.h"
#include "riscv.h"
#include "defs.h"

static volatile int panicked = 0;

void printfinit(void) {
    consoleinit();
}

// ---------------------------------------------------------
// 1. 变参处理 (Variable Arguments)
// ---------------------------------------------------------
// 在裸机环境下，我们不能 include <stdarg.h>。
// 但是 GCC 编译器提供了内置宏来处理参数压栈。
typedef __builtin_va_list va_list;

#define va_start(v,l) __builtin_va_start(v,l)
#define va_end(v)     __builtin_va_end(v)
#define va_arg(v,l)   __builtin_va_arg(v,l)

static char lower_digits[] = "0123456789abcdef";
static char upper_digits[] = "0123456789ABCDEF";

static void printuint(uint64 x, uint base, char *digits) {
    char buf[32];
    int i;

    i = 0;
    do {
        buf[i++] = digits[x % base];
        x /= base;
    } while (x != 0);

    while (--i >= 0)
        consputc(buf[i]);
}

static void printint64(long long xx, uint base) {
    uint64 x = (uint64)xx;

    if (xx < 0) {
        consputc('-');
        x = 0 - x;
    }

    printuint(x, base, lower_digits);
}

static void printstr(char *s) {
    if (s == 0)
        s = "(null)";

    for (; *s; s++)
        consputc(*s);
}

static void printptr(uint64 x) {
    int i;

    consputc('0');
    consputc('x');
    for (i = 0; i < (sizeof(uint64) * 2); i++, x <<= 4)
        consputc(lower_digits[x >> (sizeof(uint64) * 8 - 4)]);
}

static void printunknown(int length, int c) {
    consputc('%');
    while (length-- > 0)
        consputc('l');
    if (c != 0)
        consputc(c);
}

static unsigned long long getuintarg(va_list *ap, int length) {
    if (length == 2)
        return va_arg(*ap, unsigned long long);
    if (length == 1)
        return va_arg(*ap, unsigned long);
    return va_arg(*ap, unsigned int);
}

static long long getintarg(va_list *ap, int length) {
    if (length == 2)
        return va_arg(*ap, long long);
    if (length == 1)
        return va_arg(*ap, long);
    return va_arg(*ap, int);
}

// ---------------------------------------------------------
// 3. 核心函数：printf
// ---------------------------------------------------------
void printf(char *fmt, ...) {
    va_list ap;
    int i, c;

    if (panicked) {
        for(;;)
            ;
    }

    consoleacquire();

    if (fmt == 0)
        goto out; // 简单的防错保护

    va_start(ap, fmt);
    for(i = 0; (c = fmt[i] & 0xff) != 0; i++){
        if(c != '%'){
            consputc(c);
            continue;
        }

        c = fmt[++i] & 0xff;
        if(c == 0)
            break;

        int length = 0;
        if (c == 'l') {
            length = 1;
            c = fmt[++i] & 0xff;
            if (c == 0)
                break;
            if (c == 'l') {
                length = 2;
                c = fmt[++i] & 0xff;
                if (c == 0)
                    break;
            }
        }

        switch(c){
        case 'd':
        case 'i':
            printint64(getintarg(&ap, length), 10);
            break;
        case 'u':
            printuint((uint64)getuintarg(&ap, length), 10, lower_digits);
            break;
        case 'x':
            printuint((uint64)getuintarg(&ap, length), 16, lower_digits);
            break;
        case 'X':
            printuint((uint64)getuintarg(&ap, length), 16, upper_digits);
            break;
        case 'o':
            printuint((uint64)getuintarg(&ap, length), 8, lower_digits);
            break;
        case 'p':
            printptr(va_arg(ap, uint64));
            break;
        case 's':
            printstr(va_arg(ap, char*));
            break;
        case 'c':
            consputc(va_arg(ap, int));
            break;
        case '%':
            consputc('%');
            break;
        default:
            printunknown(length, c);
            break;
        }
    }
    va_end(ap);
out:
    consolerelease();
}

// ---------------------------------------------------------
// 4. Panic 函数 (内核崩溃)
// ---------------------------------------------------------
// 当遇到无法恢复的错误时调用，打印错误并死循环
void panic(char *s) {
    // Avoid taking locks during panic; they may already be held.
    consolepanic();
    intr_off();
    printf("\n[PANIC] %s\n", s);
    panicked = 1;
    for(;;)
        ;
}
