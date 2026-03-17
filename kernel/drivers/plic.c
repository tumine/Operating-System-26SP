#include "types.h"
#include "cpu.h"
#include "memlayout.h"

// PLIC memory-mapped registers (QEMU virt).
#define PLIC_PRIORITY (PLIC + 0x0)
#define PLIC_PENDING  (PLIC + 0x1000)

// Per-hart supervisor-mode registers.
#define PLIC_SENABLE(hart)   (PLIC + 0x2080 + (hart) * 0x100)
#define PLIC_SPRIORITY(hart) (PLIC + 0x201000 + (hart) * 0x2000)
#define PLIC_SCLAIM(hart)    (PLIC + 0x201004 + (hart) * 0x2000)

static inline void plic_set_priority(int irq, int prio) {
    volatile uint32 *p = (volatile uint32 *)(PLIC_PRIORITY + irq * 4);
    *p = (uint32)prio;
}

void plic_init(void) {
    // Give UART a non-zero priority so it can be delivered.
    plic_set_priority(UART0_IRQ, 1);
    plic_set_priority(VIRTIO0_IRQ, 1);
}

void plic_inithart(void) {
    int hart = cpuid();

    // Enable UART interrupt for this hart supervisor context.
    volatile uint32 *enable = (volatile uint32 *)PLIC_SENABLE(hart);
    enable[UART0_IRQ / 32] |= (1U << (UART0_IRQ % 32));
    enable[VIRTIO0_IRQ / 32] |= (1U << (VIRTIO0_IRQ % 32));

    // Accept all interrupts with priority > 0.
    *(volatile uint32 *)PLIC_SPRIORITY(hart) = 0;
}

int plic_claim(void) {
    int hart = cpuid();
    return *(volatile uint32 *)PLIC_SCLAIM(hart);
}

void plic_complete(int irq) {
    int hart = cpuid();
    *(volatile uint32 *)PLIC_SCLAIM(hart) = (uint32)irq;
}

