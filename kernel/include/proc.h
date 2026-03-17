#ifndef __PROC_H__
#define __PROC_H__

#include "types.h"
#include "riscv.h"
#include "spinlock.h"
#include "file.h"

#define NPROC 16
// Per-proc kernel stack mapping:
//   [guard page (unmapped)] [KSTACK_PAGES pages stack (mapped)]
// at a high virtual address below TRAMPOLINE.
#define KSTACK_PAGES 4
#define KSTACK_SIZE  (KSTACK_PAGES * PGSIZE)

enum procstate {
    UNUSED = 0,
    USED,
    SLEEPING,
    RUNNABLE,
    RUNNING,
    ZOMBIE,
};

struct trapframe_s {
    uint64 ra; // 0
    uint64 gp; // 8
    uint64 tp; // 16
    uint64 t0; // 24
    uint64 t1; // 32
    uint64 t2; // 40
    uint64 s0; // 48
    uint64 s1; // 56
    uint64 a0; // 64  <-- 参数/返回值
    uint64 a1; // 72
    uint64 a2; // 80
    uint64 a3; // 88
    uint64 a4; // 96
    uint64 a5; // 104
    uint64 a6; // 112
    uint64 a7; // 120 <-- 系统调用号
    uint64 s2; // 128
    uint64 s3; // 136
    uint64 s4; // 144
    uint64 s5; // 152
    uint64 s6; // 160
    uint64 s7; // 168
    uint64 s8; // 176
    uint64 s9; // 184
    uint64 s10; // 192
    uint64 s11; // 200
    uint64 t3; // 208
    uint64 t4; // 216
    uint64 t5; // 224
    uint64 t6; // 232
};

// Per-process user trapframe used by the trampoline code (mapped at TRAPFRAME
// in the user page table).
struct trapframe {
    uint64 kernel_satp;   // kernel page table
    uint64 kernel_sp;     // top of process's kernel stack
    uint64 kernel_trap;   // usertrap()
    uint64 epc;           // saved user program counter
    uint64 kernel_hartid; // saved tp (hartid)

    // Saved user registers.
    uint64 ra;
    uint64 sp;
    uint64 gp;
    uint64 tp;
    uint64 t0;
    uint64 t1;
    uint64 t2;
    uint64 s0;
    uint64 s1;
    uint64 a0;
    uint64 a1;
    uint64 a2;
    uint64 a3;
    uint64 a4;
    uint64 a5;
    uint64 a6;
    uint64 a7;
    uint64 s2;
    uint64 s3;
    uint64 s4;
    uint64 s5;
    uint64 s6;
    uint64 s7;
    uint64 s8;
    uint64 s9;
    uint64 s10;
    uint64 s11;
    uint64 t3;
    uint64 t4;
    uint64 t5;
    uint64 t6;
};

struct context {
    uint64 ra;
    uint64 sp;
    uint64 s0;
    uint64 s1;
    uint64 s2;
    uint64 s3;
    uint64 s4;
    uint64 s5;
    uint64 s6;
    uint64 s7;
    uint64 s8;
    uint64 s9;
    uint64 s10;
    uint64 s11;
};

struct proc {
    struct spinlock lock;
    int pid;
    enum procstate state;
    void *chan;            // if non-zero, sleeping on chan
    int killed;            // if non-zero, have been killed
    int xstate;            // exit status to be returned to parent's wait
    struct proc *parent;   // parent process
    struct inode *cwd;     // current working directory
    struct file *ofile[NOFILE]; // open files
    struct context context;
    void (*start)(void);
    pagetable_t pagetable;       // user page table, or 0 for kernel tasks
    struct trapframe *trapframe; // user trapframe, or 0 for kernel tasks
    uint64 sz;                   // size of process memory (bytes)
    uint64 kstack_base; // low VA of the mapped stack
    uint64 kstack_top;  // high VA (one past the last byte)
    int cpu_id;
};

void proc_init(void);
int proc_create(void (*fn)(void));
void userinit(void);
void proc_exit(int status);
int growproc(int n);
int fork(void);
int wait(uint64 addr);
int exec(const char *name, char *const argv[]);
int kill(int pid);
void sleep(void *chan, struct spinlock *lk);
void wakeup(void *chan);
void scheduler(void);
void yield(void);
struct proc *myproc(void);

#endif
