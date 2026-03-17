#include "types.h"
#include "cpu.h"
#include "proc.h"
#include "spinlock.h"
#include "defs.h"
#include "elf.h"
#include "memlayout.h"
#include "param.h"
extern void swtch(struct context *old, struct context *new);
extern void panic(char *s);
extern char trampoline[];

static struct proc procs[NPROC];
static struct spinlock pid_lock;
static struct spinlock wait_lock;
static struct proc *initproc = 0;
static int next_pid = 1;

static void *kmemset(void *dst, int c, uint n) {
    uint8 *p = (uint8 *)dst;
    for (uint i = 0; i < n; i++) {
        p[i] = (uint8)c;
    }
    return dst;
}

static uint kstrlen(const char *s) {
    uint n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

static int allocpid(void) {
    acquire(&pid_lock);
    int pid = next_pid++;
    release(&pid_lock);
    return pid;
}

struct proc *myproc(void) {
    push_off();
    struct proc *p = mycpu()->proc;
    pop_off();
    return p;
}

struct inode *proc_cwddup(void) {
    struct proc *p = myproc();
    if (p == 0) {
        return 0;
    }
    acquire(&p->lock);
    struct inode *ip = p->cwd ? idup(p->cwd) : 0;
    release(&p->lock);
    return ip;
}

static uint64 kstack_top(int idx) {
    // Put kernel stacks in high virtual memory (below TRAMPOLINE), with a guard page.
    // Layout per-proc: [guard (unmapped)] [KSTACK_PAGES mapped pages]
    uint64 t = TRAMPOLINE;
    return t - (uint64)(idx * (KSTACK_PAGES + 1) + 1) * PGSIZE;
}

static void proc_entry(void) {
    struct proc *p = myproc();
    // Returned from scheduler with p->lock held.
    if (p) {
        release(&p->lock);
    }
    if (p && p->start) {
        p->start();
    }
    // If the task returns, mark it unused and yield forever.
    if (p) {
        acquire(&p->lock);
        p->state = UNUSED;
        // Switch to scheduler; this proc will never be runnable again.
        swtch(&p->context, &mycpu()->sched_ctx);
    }
    panic("proc_entry");
}

static void user_entry(void) {
    struct proc *p = myproc();
    // Returned from scheduler with p->lock held.
    if (p) {
        release(&p->lock);
    }
    usertrapret();
    panic("user_entry");
}

static int flags2perm(uint flags) {
    int perm = PTE_U;
    if (flags & PF_R) {
        perm |= PTE_R;
    }
    if (flags & PF_W) {
        perm |= PTE_W;
    }
    if (flags & PF_X) {
        perm |= PTE_X;
    }
    return perm;
}

static int uvmmaprange(pagetable_t pagetable, uint64 va, uint64 sz, int perm) {
    if ((va % PGSIZE) != 0) {
        panic("uvmmaprange: va not aligned");
    }
    if ((sz % PGSIZE) != 0) {
        panic("uvmmaprange: sz not aligned");
    }

    uint64 a;
    for (a = va; a < va + sz; a += PGSIZE) {
        void *mem = kalloc();
        if (mem == 0) {
            goto err;
        }
        memset(mem, 0, PGSIZE);
        if (mappages(pagetable, a, PGSIZE, (uint64)mem, perm) != 0) {
            kfree(mem);
            goto err;
        }
    }
    return 0;

err:
    // Best-effort cleanup of the pages we already mapped.
    if (a > va) {
        uvmunmap(pagetable, va, (a - va) / PGSIZE, 1);
    }
    return -1;
}

static int uvmload_from_inode(pagetable_t pagetable, uint64 dstva,
                              struct inode *ip, uint64 file_off, uint64 len) {
    uint64 i = 0;
    while (i < len) {
        uint64 va0 = PGROUNDDOWN(dstva + i);
        uint64 pa0 = walkaddr(pagetable, va0);
        if (pa0 == 0) {
            return -1;
        }
        uint64 off = (dstva + i) - va0;
        uint64 n = PGSIZE - off;
        if (n > len - i) {
            n = len - i;
        }
        int rd = readi(ip, file_off + i, (void *)(pa0 + off), (uint)n);
        if (rd < 0 || (uint64)rd != n) {
            return -1;
        }
        i += n;
    }
    return 0;
}

static int load_elf(pagetable_t pagetable, const char *path, uint64 *entry, uint64 *maxva) {
    struct inode *ip = namei(path);
    if (ip == 0) {
        return -1;
    }

    ilock(ip);
    if (ip->type != T_FILE) {
        iunlockput(ip);
        return -1;
    }
    uint64 file_size = ip->size;
    iunlock(ip);

    struct elfhdr eh;
    if (file_size < sizeof(eh)) {
        iput(ip);
        return -1;
    }

    if (readi(ip, 0, &eh, sizeof(eh)) != (int)sizeof(eh)) {
        iput(ip);
        return -1;
    }
    if (eh.magic != ELF_MAGIC) {
        iput(ip);
        return -1;
    }
    if (eh.phentsize != sizeof(struct proghdr)) {
        iput(ip);
        return -1;
    }
    if (eh.phoff + (uint64)eh.phnum * eh.phentsize > file_size) {
        iput(ip);
        return -1;
    }

    *entry = eh.entry;

    uint64 max = 0;
    for (uint16 n = 0; n < eh.phnum; n++) {
        struct proghdr ph;
        uint64 off = eh.phoff + (uint64)n * eh.phentsize;
        if (readi(ip, off, &ph, sizeof(ph)) != (int)sizeof(ph)) {
            iput(ip);
            *maxva = max;
            return -1;
        }
        if (ph.type != PT_LOAD) {
            continue;
        }
        if (ph.memsz == 0) {
            continue;
        }
        if (ph.memsz < ph.filesz) {
            iput(ip);
            *maxva = max;
            return -1;
        }
        if ((ph.vaddr % PGSIZE) != 0 || (ph.off % PGSIZE) != 0) {
            iput(ip);
            *maxva = max;
            return -1;
        }
        if (ph.off + ph.filesz > file_size) {
            iput(ip);
            *maxva = max;
            return -1;
        }
        if (ph.vaddr + ph.memsz < ph.vaddr) {
            iput(ip);
            *maxva = max;
            return -1;
        }
        uint64 seg_end = ph.vaddr + ph.memsz;
        if (seg_end >= TRAPFRAME) {
            iput(ip);
            *maxva = max;
            return -1;
        }

        uint64 alloc_sz = PGROUNDUP(ph.memsz);
        int perm = flags2perm(ph.flags);
        if (uvmmaprange(pagetable, ph.vaddr, alloc_sz, perm) < 0) {
            iput(ip);
            *maxva = max;
            return -1;
        }
        if (uvmload_from_inode(pagetable, ph.vaddr, ip, ph.off, ph.filesz) < 0) {
            uvmunmap(pagetable, ph.vaddr, alloc_sz / PGSIZE, 1);
            iput(ip);
            *maxva = max;
            return -1;
        }
        if (seg_end > max) {
            max = seg_end;
        }
    }

    *maxva = max;
    iput(ip);
    return 0;
}

// Allocate user stack: one guard page + USERSTACK pages.
// oldsz is current loaded image size (bytes). On success:
// - *newsz_out is the new process size including guard+stack.
// - *sp_out is initial user stack pointer (top of stack).
static int setup_user_stack(pagetable_t pagetable, uint64 oldsz, uint64 *newsz_out, uint64 *sp_out) {
    uint64 sz = PGROUNDUP(oldsz);
    uint64 stack_bytes = (uint64)(USERSTACK + 1) * PGSIZE;
    if (sz + stack_bytes < sz || sz + stack_bytes > TRAPFRAME) {
        return -1;
    }

    uint64 newsz = uvmalloc(pagetable, sz, sz + stack_bytes, PTE_W);
    if (newsz == 0) {
        return -1;
    }

    // Guard page sits just below the user stack.
    uvmclear(pagetable, newsz - stack_bytes);
    *newsz_out = newsz;
    *sp_out = newsz;
    return 0;
}

static pagetable_t proc_pagetable(struct proc *p) {
    pagetable_t pagetable = uvmcreate();
    if (pagetable == 0) {
        return 0;
    }

    // Map the trampoline code (no PTE_U; only executable in S-mode).
    if (mappages(pagetable, TRAMPOLINE, PGSIZE, (uint64)trampoline, PTE_R | PTE_X) != 0) {
        uvmunmap(pagetable, TRAMPOLINE, 1, 0);
        uvmfree(pagetable, 0);
        return 0;
    }

    // Map the trapframe page (no PTE_U; only accessible in S-mode).
    if (mappages(pagetable, TRAPFRAME, PGSIZE, (uint64)p->trapframe, PTE_R | PTE_W) != 0) {
        uvmunmap(pagetable, TRAPFRAME, 1, 0);
        uvmunmap(pagetable, TRAMPOLINE, 1, 0);
        uvmfree(pagetable, 0);
        return 0;
    }

    return pagetable;
}

static void proc_freepagetable(pagetable_t pagetable, uint64 sz) {
    // Unmap special pages, then free user memory and page tables.
    uvmunmap(pagetable, TRAPFRAME, 1, 0);
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, sz);
}

static void freeproc(struct proc *p) {
    if (p->pagetable) {
        proc_freepagetable(p->pagetable, p->sz);
        p->pagetable = 0;
    }
    p->sz = 0;

    if (p->trapframe) {
        kfree((void *)p->trapframe);
        p->trapframe = 0;
    }

    for (int fd = 0; fd < NOFILE; fd++) {
        p->ofile[fd] = 0;
    }
    if (p->cwd) {
        iput(p->cwd);
        p->cwd = 0;
    }

    p->pid = 0;
    p->state = UNUSED;
    p->chan = 0;
    p->killed = 0;
    p->xstate = 0;
    p->parent = 0;
    p->start = 0;
    p->cpu_id = -1;
    kmemset(&p->context, 0, sizeof(p->context));
}

static struct proc *allocproc(void) {
    for (int i = 0; i < NPROC; i++) {
        struct proc *p = &procs[i];
        acquire(&p->lock);
        if (p->state != UNUSED) {
            release(&p->lock);
            continue;
        }

        p->pid = allocpid();
        p->state = USED;
        p->chan = 0;
        p->killed = 0;
        p->xstate = 0;
        p->parent = 0;
        p->cwd = 0;
        for (int fd = 0; fd < NOFILE; fd++) {
            p->ofile[fd] = 0;
        }
        p->start = 0;
        p->cpu_id = -1;
        p->sz = 0;

        // Allocate trapframe.
        p->trapframe = (struct trapframe *)kalloc();
        if (p->trapframe == 0) {
            freeproc(p);
            release(&p->lock);
            return 0;
        }
        memset(p->trapframe, 0, PGSIZE);

        // Create and initialize user page table.
        p->pagetable = proc_pagetable(p);
        if (p->pagetable == 0) {
            freeproc(p);
            release(&p->lock);
            return 0;
        }

        // Set up kernel context to enter user space.
        kmemset(&p->context, 0, sizeof(p->context));
        if (p->kstack_top == 0) {
            panic("allocproc: no kstack");
        }
        uint64 ksp = p->kstack_top;
        ksp &= ~0xFULL; // 16-byte align
        p->context.sp = ksp;
        p->context.ra = (uint64)user_entry;

        return p; // still holding p->lock
    }
    return 0;
}

void proc_init(void) {
    initlock(&pid_lock, "pid");
    initlock(&wait_lock, "wait");

    for (int i = 0; i < NPROC; i++) {
        initlock(&procs[i].lock, "proc");
        procs[i].pid = 0;
        procs[i].state = UNUSED;
        procs[i].chan = 0;
        procs[i].killed = 0;
        procs[i].xstate = 0;
        procs[i].parent = 0;
        procs[i].cwd = 0;
        for (int fd = 0; fd < NOFILE; fd++) {
            procs[i].ofile[fd] = 0;
        }
        procs[i].start = 0;
        procs[i].pagetable = 0;
        procs[i].trapframe = 0;
        procs[i].sz = 0;
        kmemset(&procs[i].context, 0, sizeof(procs[i].context));
        procs[i].kstack_base = 0;
        procs[i].kstack_top = 0;
        procs[i].cpu_id = -1;
    }

    // Allocate and map per-proc kernel stacks into the shared kernel page table.
    // Must run before other harts start (so they see the mappings at kvminithart()).
    for (int i = 0; i < NPROC; i++) {
        uint64 top = kstack_top(i);
        uint64 base = top - KSTACK_SIZE;
        procs[i].kstack_base = base;
        procs[i].kstack_top = top;

        for (int pg = 0; pg < KSTACK_PAGES; pg++) {
            void *pa = kalloc();
            if (pa == 0) {
                panic("proc_init: kalloc kstack");
            }
            memset(pa, 0, PGSIZE);
            if (mappages(kernel_pagetable, base + (uint64)pg * PGSIZE, PGSIZE, (uint64)pa,
                         PTE_R | PTE_W) != 0) {
                panic("proc_init: mappages kstack");
            }
        }
    }
    sfence_vma();
}

int proc_create(void (*fn)(void)) {
    for (int i = 0; i < NPROC; i++) {
        struct proc *p = &procs[i];
        acquire(&p->lock);
        if (p->state != UNUSED) {
            release(&p->lock);
            continue;
        }

        p->pid = allocpid();
        p->state = RUNNABLE;
        p->chan = 0;
        p->killed = 0;
        p->xstate = 0;
        p->parent = 0;
        p->cwd = 0;
        for (int fd = 0; fd < NOFILE; fd++) {
            p->ofile[fd] = 0;
        }
        p->start = fn;
        p->pagetable = 0;
        p->trapframe = 0;
        p->sz = 0;
        p->cpu_id = -1;
        kmemset(&p->context, 0, sizeof(p->context));
        if (p->kstack_top == 0) {
            panic("proc_create: no kstack");
        }
        uint64 sp = p->kstack_top;
        sp &= ~0xFULL; // 16-byte align
        p->context.sp = sp;
        p->context.ra = (uint64)proc_entry;
        int pid = p->pid;
        release(&p->lock);
        return pid;
    }
    return -1;
}

int proc_create_user(const char *path) {
    struct proc *p = allocproc();
    if (p == 0) {
        return -1;
    }
    // allocproc() returns with p->lock held; drop it before disk I/O
    // to avoid lock-order issues with wakeup() paths in the block layer.
    release(&p->lock);

    uint64 entry = 0;
    uint64 maxva = 0;
    if (load_elf(p->pagetable, path, &entry, &maxva) < 0) {
        acquire(&p->lock);
        p->sz = PGROUNDUP(maxva);
        freeproc(p);
        release(&p->lock);
        return -1;
    }

    uint64 newsz = 0;
    uint64 sp = 0;
    if (setup_user_stack(p->pagetable, maxva, &newsz, &sp) < 0) {
        acquire(&p->lock);
        p->sz = PGROUNDUP(maxva);
        freeproc(p);
        release(&p->lock);
        return -1;
    }

    acquire(&p->lock);
    p->sz = newsz;

    // Set up initial user register state.
    p->trapframe->epc = entry;
    p->trapframe->sp = sp;

    p->state = RUNNABLE;

    int pid = p->pid;
    release(&p->lock);
    return pid;
}

void userinit(void) {
    int pid = proc_create_user("/init");
    if (pid < 0) {
        panic("userinit");
    }

    // The first user process becomes init.
    for (int i = 0; i < NPROC; i++) {
        if (procs[i].pid == pid) {
            initproc = &procs[i];
            break;
        }
    }
    if (initproc == 0) {
        panic("userinit: no initproc");
    }

    struct inode *root = namei("/");
    if (root == 0) {
        panic("userinit: no root");
    }
    initproc->cwd = root;

    // Set up console as stdin/stdout/stderr for init.
    struct file *f = filealloc();
    if (f == 0) {
        panic("userinit: filealloc");
    }
    f->type = FD_CONSOLE;
    f->readable = 1;
    f->writable = 1;
    initproc->ofile[0] = f;
    initproc->ofile[1] = filedup(f);
    initproc->ofile[2] = filedup(f);
}

static void sched(void) {
    struct proc *p = myproc();
    struct cpu *c = mycpu();

    if (p == 0) {
        panic("sched");
    }
    if (!holding(&p->lock)) {
        panic("sched p->lock");
    }
    if (c->noff != 1) {
        panic("sched locks");
    }
    if (p->state == RUNNING) {
        panic("sched running");
    }
    if (intr_get()) {
        panic("sched interruptible");
    }

    int intena = c->intena;
    swtch(&p->context, &c->sched_ctx);
    // The process may resume on a different CPU.
    mycpu()->intena = intena;
}

void yield(void) {
    struct proc *p = myproc();
    if (p == 0)
        return;

    acquire(&p->lock);
    p->state = RUNNABLE;
    sched();
    release(&p->lock);
}

void sleep(void *chan, struct spinlock *lk) {
    struct proc *p = myproc();
    if (p == 0) {
        panic("sleep");
    }
    if (lk == 0) {
        panic("sleep lk");
    }

    // Must acquire p->lock in order to change p->state and then call sched().
    // Once we hold p->lock, we can release lk so that the sleeper can be woken up.
    acquire(&p->lock);
    release(lk);

    p->chan = chan;
    p->state = SLEEPING;
    sched();

    // Reacquired p->lock after returning from sched().
    p->chan = 0;

    release(&p->lock);
    acquire(lk);
}

void wakeup(void *chan) {
    for (int i = 0; i < NPROC; i++) {
        struct proc *p = &procs[i];
        acquire(&p->lock);
        if (p->state == SLEEPING && p->chan == chan) {
            p->state = RUNNABLE;
        }
        release(&p->lock);
    }
}

int growproc(int n) {
    struct proc *p = myproc();
    if (p == 0 || p->pagetable == 0) {
        return -1;
    }

    uint64 sz = p->sz;
    if (n > 0) {
        uint64 newsz = sz + (uint64)n;
        if (newsz < sz || newsz > TRAPFRAME) {
            return -1;
        }
        uint64 allocsz = uvmalloc(p->pagetable, sz, newsz, PTE_W);
        if (allocsz == 0) {
            return -1;
        }
        p->sz = allocsz;
    } else if (n < 0) {
        int64 newsz = (int64)sz + (int64)n;
        if (newsz < 0) {
            return -1;
        }
        p->sz = uvmdealloc(p->pagetable, sz, (uint64)newsz);
    }
    return 0;
}

int fork(void) {
    struct proc *p = myproc();
    if (p == 0 || p->pagetable == 0) {
        return -1;
    }

    struct proc *np = allocproc();
    if (np == 0) {
        return -1;
    }

    // Copy user memory.
    if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0) {
        freeproc(np);
        release(&np->lock);
        return -1;
    }
    np->sz = p->sz;

    // Copy saved user registers.
    *np->trapframe = *p->trapframe;
    np->trapframe->a0 = 0; // fork() returns 0 in the child.

    for (int fd = 0; fd < NOFILE; fd++) {
        if (p->ofile[fd]) {
            np->ofile[fd] = filedup(p->ofile[fd]);
        }
    }
    if (p->cwd) {
        np->cwd = idup(p->cwd);
    }

    int pid = np->pid;
    release(&np->lock);

    // Follow wait_lock -> p->lock order to avoid deadlock with wait()/exit().
    acquire(&wait_lock);
    np->parent = p;
    release(&wait_lock);

    acquire(&np->lock);
    np->state = RUNNABLE;
    release(&np->lock);

    return pid;
}

int wait(uint64 addr) {
    struct proc *p = myproc();
    if (p == 0) {
        return -1;
    }

    acquire(&wait_lock);
    for (;;) {
        int havekids = 0;

        for (int i = 0; i < NPROC; i++) {
            struct proc *pp = &procs[i];
            if (pp->parent != p) {
                continue;
            }

            havekids = 1;
            acquire(&pp->lock);
            if (pp->state == ZOMBIE) {
                int pid = pp->pid;
                int status = pp->xstate;

                if (addr != 0 && copyout(p->pagetable, addr, (char *)&status, sizeof(status)) < 0) {
                    release(&pp->lock);
                    release(&wait_lock);
                    return -1;
                }

                freeproc(pp);
                release(&pp->lock);
                release(&wait_lock);
                return pid;
            }
            release(&pp->lock);
        }

        if (!havekids || p->killed) {
            release(&wait_lock);
            return -1;
        }
        sleep(p, &wait_lock);
    }
}

int exec(const char *name, char *const argv[]) {
    struct proc *p = myproc();
    if (p == 0 || p->pagetable == 0) {
        return -1;
    }

    pagetable_t newpt = proc_pagetable(p);
    if (newpt == 0) {
        return -1;
    }

    uint64 entry = 0;
    uint64 maxva = 0;
    uint64 freesz = 0;
    if (load_elf(newpt, name, &entry, &maxva) < 0) {
        goto bad;
    }
    freesz = PGROUNDUP(maxva);

    uint64 newsz = 0;
    uint64 sp = 0;
    if (setup_user_stack(newpt, maxva, &newsz, &sp) < 0) {
        goto bad;
    }
    freesz = newsz;

    uint64 stackbase = sp - (uint64)USERSTACK * PGSIZE;
    uint64 uargv[MAXARG + 1];
    int argc = 0;

    if (argv) {
        while (argv[argc] != 0) {
            if (argc >= MAXARG) {
                goto bad;
            }
            uint64 n = (uint64)kstrlen(argv[argc]) + 1;
            sp -= n;
            sp &= ~0xFULL;
            if (sp < stackbase) {
                goto bad;
            }
            if (copyout(newpt, sp, argv[argc], n) < 0) {
                goto bad;
            }
            uargv[argc] = sp;
            argc++;
        }
    }
    uargv[argc] = 0;

    sp -= (uint64)(argc + 1) * sizeof(uint64);
    sp &= ~0xFULL;
    if (sp < stackbase) {
        goto bad;
    }
    if (copyout(newpt, sp, (char *)uargv, (uint64)(argc + 1) * sizeof(uint64)) < 0) {
        goto bad;
    }

    // Commit to the new user image.
    pagetable_t oldpt = p->pagetable;
    uint64 oldsz = p->sz;

    p->pagetable = newpt;
    p->sz = newsz;
    p->trapframe->epc = entry;
    p->trapframe->sp = sp;
    p->trapframe->a1 = sp;

    proc_freepagetable(oldpt, oldsz);
    return argc;

bad:
    proc_freepagetable(newpt, freesz);
    return -1;
}

int kill(int pid) {
    for (int i = 0; i < NPROC; i++) {
        struct proc *p = &procs[i];
        acquire(&p->lock);
        if (p->pid == pid && p->state != UNUSED) {
            p->killed = 1;
            if (p->state == SLEEPING) {
                p->state = RUNNABLE;
            }
            release(&p->lock);
            return 0;
        }
        release(&p->lock);
    }
    return -1;
}

void proc_exit(int status) {
    struct proc *p = myproc();
    if (p == 0) {
        panic("proc_exit");
    }

    if (p == initproc) {
        panic("init exiting");
    }

    // Close all open files.
    for (int fd = 0; fd < NOFILE; fd++) {
        struct file *f = p->ofile[fd];
        if (f) {
            p->ofile[fd] = 0;
            fileclose(f);
        }
    }
    if (p->cwd) {
        iput(p->cwd);
        p->cwd = 0;
    }

    acquire(&wait_lock);

    // Re-parent any children to init.
    for (int i = 0; i < NPROC; i++) {
        if (procs[i].parent == p) {
            procs[i].parent = initproc;
            wakeup(initproc);
        }
    }

    // Wake up the parent (if sleeping in wait()).
    if (p->parent) {
        wakeup(p->parent);
    }

    acquire(&p->lock);
    p->xstate = status;
    p->state = ZOMBIE;

    release(&wait_lock);
    sched();
    panic("proc_exit: sched returned");
}

void scheduler(void) {
    struct cpu *c = mycpu();
    c->proc = 0;

    for (;;) {
        // The most recent process to run may have had interrupts
        // turned off; enable them to avoid a deadlock if all
        // processes are waiting. Then turn them back off
        // to avoid a possible race between an interrupt
        // and the first acquire().
        intr_on();
        intr_off();

        int found = 0;
        for (int i = 0; i < NPROC; i++) {
            struct proc *p = &procs[i];

            acquire(&p->lock);
            if (p->state == RUNNABLE) {
                p->state = RUNNING;
                c->proc = p;
                swtch(&c->sched_ctx, &p->context);
                // When we return here, we still hold p->lock.
                c->proc = 0;
                found = 1;
            }
            release(&p->lock);
        }
        if (found == 0) {
            // Nothing runnable on this hart right now; sleep until an interrupt.
            asm volatile("wfi");
        }
    }
}
