#include "types.h"
#include "cpu.h"
#include "riscv.h"
#include "spinlock.h"

extern void panic(char *s);

static void kstrncpy(char *dst, const char *src, int n) {
    int i = 0;
    for (; i + 1 < n && src[i]; i++) {
        dst[i] = src[i];
    }
    dst[i] = 0;
}

void initlock(struct spinlock *lk, char *name) {
    lk->locked = 0;
    lk->cpu = 0;
    kstrncpy(lk->name, name, (int)sizeof(lk->name));
}

int holding(struct spinlock *lk) {
    return lk->locked && lk->cpu == mycpu();
}

void acquire(struct spinlock *lk) {
    push_off();
    if (holding(lk)) {
        panic("acquire");
    }

    while (__sync_lock_test_and_set(&lk->locked, 1) != 0) {
        ;
    }
    __sync_synchronize();

    struct cpu *c = mycpu();
    lk->cpu = c;
}

void release(struct spinlock *lk) {
    if (!holding(lk)) {
        panic("release");
    }

    lk->cpu = 0;
    __sync_synchronize();
    __sync_lock_release(&lk->locked);

    pop_off();
}

