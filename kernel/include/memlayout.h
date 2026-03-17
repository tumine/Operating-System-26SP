#ifndef __MEMLAYOUT_H__
#define __MEMLAYOUT_H__

// QEMU virt machine memory layout (subset).

#include "riscv.h"

#define UART0 0x10000000L

#define VIRTIO0 0x10001000
#define VIRTIO0_IRQ 1

#define CLINT 0x02000000L
#define CLINT_MTIMECMP(hart) (CLINT + 0x4000 + 8 * (hart))
#define CLINT_MTIME (CLINT + 0xBFF8)

// Platform-Level Interrupt Controller (PLIC).
#define PLIC 0x0c000000L

// Device IRQs (QEMU virt).
#define UART0_IRQ 10

// QEMU puts RAM at 0x80000000.
#define KERNBASE 0x80000000L
// The kernel expects there to be RAM for use by the kernel and user pages
// from KERNBASE up to PHYSTOP.  Default is 128MB
#define PHYSTOP (KERNBASE + 128 * 1024 * 1024)

// A conservative timer interval; tune as needed.
#define TIMER_INTERVAL 1000000L

// Trampoline and trapframe mapping.
// - TRAMPOLINE: a page containing the trap entry/return code, mapped in both the
//   kernel and each user page table.
// - TRAPFRAME: a per-process page mapped in each user page table, used by the
//   trampoline code to save/restore registers on user<->kernel transitions.
#define TRAMPOLINE (MAXVA - PGSIZE)
#define TRAPFRAME  (TRAMPOLINE - PGSIZE)

#endif
