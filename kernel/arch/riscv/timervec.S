.section .text
.globl timervec
.p2align 2
// Machine-mode timer interrupt handler.
//
// Uses mscratch as a pointer to a 5x uint64 scratch area:
//   scratch[0..2] save a1,a2,a3 (a0 is saved in mscratch via csrrw)
//   scratch[3]    holds CLINT mtimecmp address
//   scratch[4]    holds interval
timervec:
    // Swap a0 with mscratch so we preserve a0 and get the scratch pointer.
    csrrw a0, mscratch, a0

    sd a1, 0(a0)
    sd a2, 8(a0)
    sd a3, 16(a0)

    ld a1, 24(a0)       // a1 = mtimecmp address
    ld a2, 32(a0)       // a2 = interval

    // *mtimecmp = *mtime + interval
    li a3, 0x0200BFF8
    ld a3, 0(a3)
    add a3, a3, a2
    sd a3, 0(a1)

    // Trigger a supervisor software interrupt.
    li a1, 2
    csrs sip, a1

    ld a1, 0(a0)
    ld a2, 8(a0)
    ld a3, 16(a0)

    // Restore original a0 from mscratch.
    csrrw a0, mscratch, a0
    mret
