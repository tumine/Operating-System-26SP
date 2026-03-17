#include "types.h"
#include "riscv.h"
#include "spinlock.h"
#include "memlayout.h"
#include "defs.h"
#include "log.h"

// Physical page allocator.
//
// Frees physical memory between the end of the kernel and PHYSTOP, and
// hands out 4096-byte pages.

extern char end[]; // provided by kernel.ld

struct run {
    struct run *next;
};

static struct {
    struct spinlock lock;
    struct run *freelist;
} kmem;

void kfree(void *pa) {
    uint64 a = (uint64)pa;
    if ((a % PGSIZE) != 0) {
        panic("kfree: not aligned");
    }
    if (a < (uint64)end || a >= PHYSTOP) {
        panic("kfree: bad pa");
    }

    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    struct run *r = (struct run *)pa;

    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
}

void kinit(void) {
    initlock(&kmem.lock, "kmem");
    kmem.freelist = 0;

    // Free every page after the kernel.
    uint64 p = PGROUNDUP((uint64)end);
    for (; p + PGSIZE <= PHYSTOP; p += PGSIZE) {
        kfree((void *)p);
    }
}

void *kalloc(void) {
    acquire(&kmem.lock);
    struct run *r = kmem.freelist;
    if (r) {
        kmem.freelist = r->next;
    }
    release(&kmem.lock);

    if (r) {
        // Fill with junk to help spot uninitialized use.
        memset((void *)r, 5, PGSIZE);
        LOG_DEBUG("Allocated physical page at %p", r); // [埋点]
    }
    else {
        LOG_ERROR("kalloc out of memory!");
    }
    return (void *)r;
}
