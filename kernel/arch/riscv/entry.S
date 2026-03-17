.section .text.entry
    .global _entry
_entry:
    # 1. 设置栈指针 (Stack Pointer)
    # 这里的逻辑是：sp = stack0 + (mhartid + 1) * 4096
    # 这样每个 CPU (Hart) 都有自己独立的 4KB 栈

    la sp, stack0
    li a0, 4096
    csrr a1, mhartid
    mv tp, a1
    addi a1, a1, 1
    mul a0, a0, a1
    add sp, sp, a0

    # 2. 跳转到 C 语言的 start() 函数
    call start

    # 3. 如果 start 返回了（不应该发生），让 CPU 停在这里
spin:
    j spin

    # 4. 预留栈空间
    .section .bss
    .align 4
    .global stack0
stack0:
    .space 4096 * 8 # 支持最多 8 个 CPU
