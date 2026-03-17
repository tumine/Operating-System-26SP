#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "proc.h"
#include "file.h"

#define PIPESIZE 512

struct pipe {
    struct spinlock lock;
    char data[PIPESIZE];
    uint nread;
    uint nwrite;
    int readopen;
    int writeopen;
};

int pipealloc(struct file **f0, struct file **f1) {
    struct pipe *pi = 0;

    *f0 = 0;
    *f1 = 0;
    if ((*f0 = filealloc()) == 0 || (*f1 = filealloc()) == 0) {
        goto bad;
    }
    pi = (struct pipe *)kalloc();
    if (pi == 0) {
        goto bad;
    }

    pi->readopen = 1;
    pi->writeopen = 1;
    pi->nread = 0;
    pi->nwrite = 0;
    initlock(&pi->lock, "pipe");

    (*f0)->type = FD_PIPE;
    (*f0)->readable = 1;
    (*f0)->writable = 0;
    (*f0)->pipe = pi;

    (*f1)->type = FD_PIPE;
    (*f1)->readable = 0;
    (*f1)->writable = 1;
    (*f1)->pipe = pi;
    return 0;

bad:
    if (pi) {
        kfree((void *)pi);
    }
    if (*f0) {
        fileclose(*f0);
    }
    if (*f1) {
        fileclose(*f1);
    }
    return -1;
}

void pipeclose(struct pipe *pi, int writable) {
    acquire(&pi->lock);
    if (writable) {
        pi->writeopen = 0;
        wakeup(&pi->nread);
    } else {
        pi->readopen = 0;
        wakeup(&pi->nwrite);
    }
    if (pi->readopen == 0 && pi->writeopen == 0) {
        release(&pi->lock);
        kfree((void *)pi);
    } else {
        release(&pi->lock);
    }
}

int pipewrite(struct pipe *pi, uint64 addr, int n) {
    int i = 0;
    struct proc *p = myproc();
    if (p == 0) {
        return -1;
    }

    acquire(&pi->lock);
    while (i < n) {
        if (pi->readopen == 0 || p->killed) {
            release(&pi->lock);
            return -1;
        }
        if (pi->nwrite == pi->nread + PIPESIZE) {
            wakeup(&pi->nread);
            sleep(&pi->nwrite, &pi->lock);
        } else {
            char ch;
            if (copyin(p->pagetable, &ch, addr + (uint64)i, 1) < 0) {
                break;
            }
            pi->data[pi->nwrite % PIPESIZE] = ch;
            pi->nwrite++;
            i++;
        }
    }
    wakeup(&pi->nread);
    release(&pi->lock);
    return i;
}

int piperead(struct pipe *pi, uint64 addr, int n) {
    int i = 0;
    struct proc *p = myproc();
    if (p == 0) {
        return -1;
    }

    acquire(&pi->lock);
    while (pi->nread == pi->nwrite && pi->writeopen) {
        if (p->killed) {
            release(&pi->lock);
            return -1;
        }
        sleep(&pi->nread, &pi->lock);
    }

    for (i = 0; i < n; i++) {
        if (pi->nread == pi->nwrite) {
            break;
        }
        char ch = pi->data[pi->nread % PIPESIZE];
        pi->nread++;
        if (copyout(p->pagetable, addr + (uint64)i, &ch, 1) < 0) {
            break;
        }
    }

    wakeup(&pi->nwrite);
    release(&pi->lock);
    return i;
}
