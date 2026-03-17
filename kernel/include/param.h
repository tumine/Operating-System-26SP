#ifndef __PARAM_H__
#define __PARAM_H__

// Maximum number of harts (CPUs) we support.
// Keep in sync with kernel/arch/riscv/entry.S stack allocation.
#define NCPU 8

// User stack pages.
#define USERSTACK 1

// Max number of exec arguments.
#define MAXARG 32

#endif
