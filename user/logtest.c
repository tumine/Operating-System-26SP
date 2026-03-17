// user/logtest.c
#include "user.h"

int main() {
    write(1, "--- Log Trigger Test Starting ---\n", 34);

    // 触发 sys_fork 和多次内存分配 (复制页表)
    int pid = fork();

    if (pid < 0) {
        write(1, "fork failed\n", 12);
        exit(1);
    }

    if (pid == 0) {
        // 子进程：触发 sys_sbrk 和内存分配
        sbrk(4096 * 2); 
        
        // 触发一个未知的系统调用 (通过汇编直接触发 ecall)
        // 这个会触发你刚刚在 syscall() 里写的 LOG_WARN
        asm volatile("li a7, 999\n ecall"); 
        
        exit(0);
    } else {
        // 父进程：触发 sys_wait
        wait(0);
        write(1, "--- Log Trigger Test Finished ---\n", 34);
    }

    exit(0);
}