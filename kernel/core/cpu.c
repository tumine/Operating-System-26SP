#include "types.h"
#include "cpu.h"

struct cpu cpus[NCPU];

int cpuid(void) {
    uint64 id;
    // entry.S stores mhartid in tp so S-mode can read it.
    asm volatile("mv %0, tp" : "=r"(id));
    return (int)id;
}

struct cpu *mycpu(void) {
    int id = cpuid();
    return &cpus[id];
}

