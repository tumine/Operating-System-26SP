#include "syscall.h"

.section .text.entry
.globl _start
_start:
    call main
    // main's return value is in a0; pass it to exit(status).
    li a7, SYS_exit
    ecall
1:
    j 1b

