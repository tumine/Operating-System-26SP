#include "types.h"
#include "riscv.h" // 假设你把寄存器宏定义放在这里
#include "memlayout.h"
#include "param.h"

void main(); // 声明 main.c 中的函数
extern void timervec(void);

static uint64 timer_scratch[NCPU][5];

static void timerinit(void) {
    int id = (int)r_mhartid();
    uint64 *scratch = timer_scratch[id];

    // Scratch layout consumed by timervec.S.
    scratch[3] = (uint64)CLINT_MTIMECMP(id);
    scratch[4] = (uint64)TIMER_INTERVAL;

    w_mscratch((uint64)scratch);
    w_mtvec((uint64)timervec);

    volatile uint64 *mtimecmp = (volatile uint64 *)scratch[3];
    volatile uint64 *mtime = (volatile uint64 *)CLINT_MTIME;
    *mtimecmp = *mtime + scratch[4];

    // Enable machine-mode timer interrupts.
    w_mie(r_mie() | MIE_MTIE);
    // Keep machine interrupts enabled across mret by setting MPIE too.
    w_mstatus(r_mstatus() | MSTATUS_MIE | MSTATUS_MPIE);
}

// 机器模式下的初始化，设置好 S 模式所需的配置
void start() {
    // 1. 设置 M Status 寄存器
    // MPP (Machine Previous Privilege) 设为 Supervisor (01)
    // 这样执行 mret 后，CPU 会进入 S 模式
    unsigned long x = r_mstatus();
    x &= ~MSTATUS_MPP_MASK;
    x |= MSTATUS_MPP_S;
    w_mstatus(x);

    // 2. 设置 M Exception Program Counter (mepc)
    // mret 指令会将 pc 设置为 mepc 的值
    // 我们希望它跳转到 main()
    w_mepc((uint64)main);

    // 3. 禁用分页 (Satp)
    // Lab 1 我们直接使用物理地址，Lab 4 才会开启分页
    w_satp(0);

    // 4. 委托中断和异常 (Delegation)
    // 默认所有中断都在 M 态处理。我们需要把它们交给 S 态。
    // 0xffff... 表示所有异常都委托
    w_medeleg(0xffff);
    w_mideleg(0xffff);
    
    // 开启 S 态下的外部中断、时钟中断、软件中断
    w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);
    // w_sie(r_sie() | SIE_SEIE | SIE_SSIE);

    // 5. 配置 PMP (Physical Memory Protection)
    // 默认 M 态下，S 态是不能访问任何物理内存的。
    // 我们需要授权 S 态访问所有物理地址 (0 ~ 2^64-1)
    // 配置 PMP 3:0 为 RWX 权限
    w_pmpaddr0(0x3fffffffffffffull);
    w_pmpcfg0(0xf);

    // 6. 初始化时钟中断（在 M 态处理 MTIP，再转成 S 态软件中断）
    timerinit();

    // 6. 切换到 S 模式
    // 这一行内联汇编会执行 mret 指令
    // 硬件行为：pc = mepc; mode = mstatus.MPP
    asm volatile("mret");
}
