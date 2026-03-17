#include "types.h"
#include "cpu.h"
#include "defs.h"
#include "proc.h"
#include "trap.h"

extern void virtio_disk_init(void);
extern void binit(void);

static volatile int started = 0;

static void print_boot_logo(void) {
    printf("\n");
    printf(" _   _            ___   ____  \n");
    printf("| \\ | | ___ _  _ / _ \\ / ___| \n");
    printf("|  \\| |/ _ \\ \\/ / | | \\___ \\ \n");
    printf("| |\\  |  __/>  <| |_| |___) |\n");
    printf("|_| \\_|\\___/_/\\_\\\\___/|____/ \n");
    printf("\n");
}

void main() {
    if (cpuid() == 0) {
        // S-Mode 代码从此开始 (boot hart)
        uart_init();
        printfinit();
        print_boot_logo();

        kinit();
        kvminit();
        kvminithart();

        proc_init();

        // 1. 初始化中断控制器
        plic_init();
        plic_inithart();

        // 2. 初始化 Buffer Cache 和 磁盘驱动
        binit();
        virtio_disk_init();

        // 3. 挂载文件系统并读取测试文件
        printf("[NexOS] Mounting filesystem...\n");       
        fsinit(0);

        fileinit();
        userinit();
        trap_init();

        __sync_synchronize();
        started = 1;
    } else {
        while (started == 0) {
            ;
        }
        __sync_synchronize();
        kvminithart();
        plic_inithart();
        trap_init();
    }

    scheduler();
}
