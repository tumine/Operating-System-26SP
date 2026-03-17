#ifndef __SLEEPLOCK_H__
#define __SLEEPLOCK_H__

#include "types.h"
#include "spinlock.h"

// Sleep-lock: can block while waiting, unlike spinlock.
struct sleeplock {
    uint locked;
    struct spinlock lk;
    char name[16];
    int owner_pid;
    int owner_cpu;
};

void initsleeplock(struct sleeplock *lk, char *name);
void acquiresleep(struct sleeplock *lk);
void releasesleep(struct sleeplock *lk);
int holdingsleep(struct sleeplock *lk);

#endif
