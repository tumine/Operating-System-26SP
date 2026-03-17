#ifndef __SPINLOCK_H__
#define __SPINLOCK_H__

#include "types.h"

struct cpu;

// Simple spinlock.
struct spinlock {
    uint locked;          // Is the lock held?
    char name[16];        // Name (debugging)
    struct cpu *cpu;      // The cpu holding the lock.
};

void initlock(struct spinlock *lk, char *name);
void acquire(struct spinlock *lk);
void release(struct spinlock *lk);
int holding(struct spinlock *lk);

#endif

