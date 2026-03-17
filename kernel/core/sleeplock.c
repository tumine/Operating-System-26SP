#include "types.h"
#include "cpu.h"
#include "proc.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "defs.h"

static void sl_namecpy(char *dst, const char *src, int n) {
    int i = 0;
    for (; i + 1 < n && src[i]; i++) {
        dst[i] = src[i];
    }
    dst[i] = 0;
}

static int sl_holding_locked(struct sleeplock *lk) {
    if (lk->locked == 0) {
        return 0;
    }
    struct proc *p = myproc();
    if (p) {
        return lk->owner_pid == p->pid;
    }
    return lk->owner_pid == -1 && lk->owner_cpu == cpuid();
}

void initsleeplock(struct sleeplock *lk, char *name) {
    lk->locked = 0;
    lk->owner_pid = 0;
    lk->owner_cpu = -1;
    initlock(&lk->lk, "sleeplock");
    sl_namecpy(lk->name, name, (int)sizeof(lk->name));
}

void acquiresleep(struct sleeplock *lk) {
    acquire(&lk->lk);
    while (lk->locked) {
        sleep(lk, &lk->lk);
    }
    lk->locked = 1;

    struct proc *p = myproc();
    if (p) {
        lk->owner_pid = p->pid;
        lk->owner_cpu = -1;
    } else {
        lk->owner_pid = -1;
        lk->owner_cpu = cpuid();
    }
    release(&lk->lk);
}

void releasesleep(struct sleeplock *lk) {
    acquire(&lk->lk);
    if (!sl_holding_locked(lk)) {
        panic("releasesleep");
    }
    lk->locked = 0;
    lk->owner_pid = 0;
    lk->owner_cpu = -1;
    wakeup(lk);
    release(&lk->lk);
}

int holdingsleep(struct sleeplock *lk) {
    acquire(&lk->lk);
    int r = sl_holding_locked(lk);
    release(&lk->lk);
    return r;
}
