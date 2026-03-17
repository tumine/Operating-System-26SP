#ifndef __RISCV_H__
#define __RISCV_H__

#ifndef __ASSEMBLER__
#include "types.h"
#endif

// mstatus.MPP[12:11]
#define MSTATUS_MPP_MASK (3L << 11)
#define MSTATUS_MPP_S    (1L << 11)
#define MSTATUS_MIE      (1L << 3)
#define MSTATUS_MPIE     (1L << 7)

// sie bits
#define SIE_SSIE (1L << 1)  // Supervisor software interrupt enable
#define SIE_STIE (1L << 5)  // Supervisor timer interrupt enable
#define SIE_SEIE (1L << 9)  // Supervisor external interrupt enable

// sstatus bits
#define SSTATUS_SIE (1L << 1)
#define SSTATUS_SPIE (1L << 5) // Supervisor Previous Interrupt Enable
#define SSTATUS_SPP (1L << 8)  // Supervisor Previous Privilege (1 = S, 0 = U)

// mie bits
#define MIE_MTIE (1L << 7)

// scause codes (interrupt bit set)
#define SCAUSE_SSI 1
#define SCAUSE_STI 5
#define SCAUSE_SEI 9

// Exception codes (interrupt bit clear)
#define SCAUSE_ECALL_U 8 // User mode ecall
#define SCAUSE_ECALL_S 9 // Supervisor mode ecall

#ifndef __ASSEMBLER__

static inline uint64 r_mstatus(void) {
    uint64 x;
    asm volatile("csrr %0, mstatus" : "=r"(x));
    return x;
}

static inline void w_mstatus(uint64 x) {
    asm volatile("csrw mstatus, %0" : : "r"(x));
}

static inline uint64 r_mie(void) {
    uint64 x;
    asm volatile("csrr %0, mie" : "=r"(x));
    return x;
}

static inline void w_mie(uint64 x) {
    asm volatile("csrw mie, %0" : : "r"(x));
}

static inline void w_mtvec(uint64 x) {
    asm volatile("csrw mtvec, %0" : : "r"(x));
}

static inline void w_mscratch(uint64 x) {
    asm volatile("csrw mscratch, %0" : : "r"(x));
}

static inline void w_mepc(uint64 x) {
    asm volatile("csrw mepc, %0" : : "r"(x));
}

static inline void w_satp(uint64 x) {
    asm volatile("csrw satp, %0" : : "r"(x));
}

static inline uint64 r_satp(void) {
    uint64 x;
    asm volatile("csrr %0, satp" : "=r"(x));
    return x;
}

static inline void w_sscratch(uint64 x) {
    asm volatile("csrw sscratch, %0" : : "r"(x));
}

static inline void w_medeleg(uint64 x) {
    asm volatile("csrw medeleg, %0" : : "r"(x));
}

static inline void w_mideleg(uint64 x) {
    asm volatile("csrw mideleg, %0" : : "r"(x));
}

static inline uint64 r_sie(void) {
    uint64 x;
    asm volatile("csrr %0, sie" : "=r"(x));
    return x;
}

static inline void w_sie(uint64 x) {
    asm volatile("csrw sie, %0" : : "r"(x));
}

static inline uint64 r_sstatus(void) {
    uint64 x;
    asm volatile("csrr %0, sstatus" : "=r"(x));
    return x;
}

static inline void w_sstatus(uint64 x) {
    asm volatile("csrw sstatus, %0" : : "r"(x));
}

static inline void w_stvec(uint64 x) {
    asm volatile("csrw stvec, %0" : : "r"(x));
}

static inline uint64 r_scause(void) {
    uint64 x;
    asm volatile("csrr %0, scause" : "=r"(x));
    return x;
}

static inline uint64 r_sepc(void) {
    uint64 x;
    asm volatile("csrr %0, sepc" : "=r"(x));
    return x;
}

static inline uint64 r_stval(void) {
    uint64 x;
    asm volatile("csrr %0, stval" : "=r"(x));
    return x;
}

static inline void w_sepc(uint64 x) {
    asm volatile("csrw sepc, %0" : : "r"(x));
}

static inline uint64 r_sip(void) {
    uint64 x;
    asm volatile("csrr %0, sip" : "=r"(x));
    return x;
}

static inline void w_sip(uint64 x) {
    asm volatile("csrw sip, %0" : : "r"(x));
}

static inline void intr_on(void) {
    w_sstatus(r_sstatus() | SSTATUS_SIE);
}

static inline void intr_off(void) {
    w_sstatus(r_sstatus() & ~SSTATUS_SIE);
}

static inline int intr_get(void) {
    return (r_sstatus() & SSTATUS_SIE) != 0;
}

// Nestable interrupt-disable.
void push_off(void);
void pop_off(void);

// Machine CSR. Only safe to call in M-mode.
static inline uint64 r_mhartid(void) {
    uint64 x;
    asm volatile("csrr %0, mhartid" : "=r"(x));
    return x;
}

static inline void w_pmpaddr0(uint64 x) {
    asm volatile("csrw pmpaddr0, %0" : : "r"(x));
}

static inline void w_pmpcfg0(uint64 x) {
    asm volatile("csrw pmpcfg0, %0" : : "r"(x));
}

// ------------------------------------------------------------
// Paging / Sv39
// ------------------------------------------------------------

static inline void sfence_vma(void) {
    // Flush all TLB entries.
    asm volatile("sfence.vma zero, zero");
}

typedef uint64 pte_t;
typedef uint64 *pagetable_t; // 512 PTEs per page.

#endif // __ASSEMBLER__

#define PGSIZE 4096 // bytes per page
#define PGSHIFT 12  // bits of offset within a page

#define PGROUNDUP(sz)  (((sz) + PGSIZE - 1) & ~(PGSIZE - 1))
#define PGROUNDDOWN(a) (((a)) & ~(PGSIZE - 1))

// Page table entry (PTE) flags.
#define PTE_V (1L << 0) // valid
#define PTE_R (1L << 1)
#define PTE_W (1L << 2)
#define PTE_X (1L << 3)
#define PTE_U (1L << 4) // user can access

// Shift a physical address to the right place for a PTE.
#define PA2PTE(pa) ((((uint64)(pa)) >> 12) << 10)
#define PTE2PA(pte) (((pte) >> 10) << 12)
#define PTE_FLAGS(pte) ((pte) & 0x3FF)

// Extract the three 9-bit page table indices from a virtual address.
#define PXMASK 0x1FF // 9 bits
#define PXSHIFT(level) (PGSHIFT + (9 * (level)))
#define PX(level, va) ((((uint64)(va)) >> PXSHIFT(level)) & PXMASK)

// One beyond the highest possible virtual address.
// MAXVA is one bit less than the max allowed by Sv39, to avoid having
// to sign-extend virtual addresses that have the high bit set.
#define MAXVA (1L << (9 + 9 + 9 + 12 - 1))

// satp mode for Sv39.
#define SATP_SV39 (8L << 60)
#define MAKE_SATP(pagetable) (SATP_SV39 | (((uint64)(pagetable)) >> 12))

#endif
