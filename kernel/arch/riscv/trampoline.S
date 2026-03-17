// Trampoline code mapped at a high virtual address (TRAMPOLINE) in both the
// kernel and user page tables. Handles user<->kernel transitions.
//
// Trapframe layout (struct trapframe in proc.h):
//   0   kernel_satp
//   8   kernel_sp
//   16  kernel_trap
//   24  epc
//   32  kernel_hartid
//   40  ra
//   48  sp
//   56  gp
//   64  tp
//   72  t0
//   80  t1
//   88  t2
//   96  s0
//   104 s1
//   112 a0
//   120 a1
//   128 a2
//   136 a3
//   144 a4
//   152 a5
//   160 a6
//   168 a7
//   176 s2
//   184 s3
//   192 s4
//   200 s5
//   208 s6
//   216 s7
//   224 s8
//   232 s9
//   240 s10
//   248 s11
//   256 t3
//   264 t4
//   272 t5
//   280 t6

.section .text.trampoline
.globl uservec
uservec:
    // Swap a0 with sscratch:
    // - a0 becomes the trapframe VA (TRAPFRAME)
    // - sscratch holds the user a0 value
    csrrw a0, sscratch, a0

    sd ra, 40(a0)
    sd sp, 48(a0)
    sd gp, 56(a0)
    sd tp, 64(a0)
    sd t0, 72(a0)
    sd t1, 80(a0)
    sd t2, 88(a0)
    sd s0, 96(a0)
    sd s1, 104(a0)

    // Save user a0 from sscratch.
    csrr t0, sscratch
    sd t0, 112(a0)

    sd a1, 120(a0)
    sd a2, 128(a0)
    sd a3, 136(a0)
    sd a4, 144(a0)
    sd a5, 152(a0)
    sd a6, 160(a0)
    sd a7, 168(a0)

    sd s2, 176(a0)
    sd s3, 184(a0)
    sd s4, 192(a0)
    sd s5, 200(a0)
    sd s6, 208(a0)
    sd s7, 216(a0)
    sd s8, 224(a0)
    sd s9, 232(a0)
    sd s10, 240(a0)
    sd s11, 248(a0)

    sd t3, 256(a0)
    sd t4, 264(a0)
    sd t5, 272(a0)
    sd t6, 280(a0)

    // Restore kernel context from trapframe.
    ld sp, 8(a0)     // kernel_sp
    ld tp, 32(a0)    // kernel_hartid
    ld t0, 0(a0)     // kernel_satp
    ld t1, 16(a0)    // kernel_trap

    // Switch to kernel page table.
    csrw satp, t0
    sfence.vma zero, zero

    jr t1

.globl userret
userret:
    // a0 = TRAPFRAME, a1 = user satp.
    csrw satp, a1
    sfence.vma zero, zero

    // Make trapframe VA available to uservec on the next trap.
    csrw sscratch, a0

    ld ra, 40(a0)
    ld sp, 48(a0)
    ld gp, 56(a0)
    ld tp, 64(a0)
    ld t0, 72(a0)
    ld t1, 80(a0)
    ld t2, 88(a0)
    ld s0, 96(a0)
    ld s1, 104(a0)
    ld a1, 120(a0)
    ld a2, 128(a0)
    ld a3, 136(a0)
    ld a4, 144(a0)
    ld a5, 152(a0)
    ld a6, 160(a0)
    ld a7, 168(a0)
    ld s2, 176(a0)
    ld s3, 184(a0)
    ld s4, 192(a0)
    ld s5, 200(a0)
    ld s6, 208(a0)
    ld s7, 216(a0)
    ld s8, 224(a0)
    ld s9, 232(a0)
    ld s10, 240(a0)
    ld s11, 248(a0)
    ld t3, 256(a0)
    ld t4, 264(a0)
    ld t5, 272(a0)
    ld t6, 280(a0)

    // Restore user a0 last.
    ld a0, 112(a0)

    sret
