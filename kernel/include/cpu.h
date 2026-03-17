#ifndef __CPU_H__
#define __CPU_H__

#include "param.h"
#include "proc.h"

// Per-hart (per-CPU) state.
struct cpu {
    struct context sched_ctx; // swtch() here to enter scheduler
    struct proc *proc;        // currently running proc, or 0
    int noff;                 // push_off() nesting depth
    int intena;               // were interrupts enabled before push_off()?
};

extern struct cpu cpus[NCPU];

int cpuid(void);
struct cpu *mycpu(void);

#endif

