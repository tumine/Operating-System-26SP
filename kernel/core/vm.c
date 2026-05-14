#include "types.h"
#include "riscv.h"
#include "memlayout.h"
#include "defs.h"
#include "proc.h"
#include "param.h"

// Kernel page table (shared by all harts).
pagetable_t kernel_pagetable;

extern char etext[]; // kernel.ld provides this (end of kernel text).
extern char trampoline[]; // kernel.ld provides this (start of trampoline page).

// Forward declarations (avoid -Werror=implicit-function-declaration).
pte_t *walk(pagetable_t pagetable, uint64 va, int alloc);
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm);
uint64 uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz);

static void kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm) {
    // va/pa/size must be page-aligned for mappages().
    if ((va % PGSIZE) || (pa % PGSIZE) || (sz % PGSIZE)) {
        panic("kvmmap: not aligned");
    }
    if (mappages(kpgtbl, va, sz, pa, perm) != 0) {
        panic("kvmmap: mappages");
    }
}

static pagetable_t kvmmake(void) {
    pagetable_t kpgtbl = (pagetable_t)kalloc();
    if (kpgtbl == 0) {
        panic("kvmmake: kalloc");
    }
    memset(kpgtbl, 0, PGSIZE);

    // Devices.
    kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);
    kvmmap(kpgtbl, PLIC, PLIC, 0x4000000, PTE_R | PTE_W);
    kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

    // Kernel text is R-X.
    kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext - KERNBASE, PTE_R | PTE_X);
    // Kernel data and the physical RAM we'll make use of is R-W.
    kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext, PTE_R | PTE_W);

    // Trampoline code (mapped at a high virtual address).
    kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

    return kpgtbl;
}

void kvminit(void) {
    kernel_pagetable = kvmmake();
}

void kvminithart(void) {
    // Wait for any previous writes to the page-table memory to finish.
    sfence_vma();

    w_satp(MAKE_SATP(kernel_pagetable));

    // Flush stale entries from the TLB.
    sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
pte_t *walk(pagetable_t pagetable, uint64 va, int alloc) {
    if (va >= MAXVA) {
        panic("walk");
    }

    for (int level = 2; level > 0; level--) {
        pte_t *pte = &pagetable[PX(level, va)];
        if (*pte & PTE_V) {
            // find the next level page table.
            pagetable = (pagetable_t)PTE2PA(*pte);
        } else {
            if (!alloc) {
                return 0;
            }
            pagetable_t newpt = (pagetable_t)kalloc();
            if (newpt == 0) {
                return 0;
            }
            memset(newpt, 0, PGSIZE);
            *pte = PA2PTE(newpt) | PTE_V;
            pagetable = newpt;
        }
    }
    return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64 walkaddr(pagetable_t pagetable, uint64 va) {
    if (va >= MAXVA) {
        return 0;
    }
    pte_t *pte = walk(pagetable, va, 0);
    if (pte == 0) {
        return 0;
    }
    if ((*pte & PTE_V) == 0) {
        return 0;
    }
    if ((*pte & PTE_U) == 0) {
        return 0;
    }
    return PTE2PA(*pte);
}

static int should_lazy_alloc(struct proc *p, pagetable_t pagetable, uint64 va) {
    // 判断 va 是否可以通过 Lazy Allocation 补页

    // 检查 va 是否处在进程的逻辑地址空间 p->sz 范围内
    if (va >= p->sz) {
        return 0;
    }

    // 检查 va 是否超过用户空间上界 TRAPFRAME
    // TRAPFRAME 以上的地址由内核使用，不允许分配
    if (va >= TRAPFRAME) {
        return 0;
    }

    // 检查 va 对应的 PTE 是否已经存在有效映射，或与栈低址方向的 guard page 重叠
    // 如果 PTE_V 置位，说明该页已映射到物理页框或属于 Guard Page，不需要重新分配
    pte_t *pte = walk(pagetable, va, 0);
    if (pte != 0 && (*pte & PTE_V)) {
        // 不论 PTE_U 是否置位，都说明不需要 Lazy Allocation
        return 0;
    }

    // 以上检查都通过，则说明此页需要 Lazy Allocation
    return 1;
}

int user_lazy_alloc(struct proc *p, pagetable_t pagetable, uint64 va) {
    // 将发生 Lazy Fault 的访存地址向下对齐到其所在页的起始地址，确保映射以页为粒度
    uint64 page = PGROUNDDOWN(va);
    if (!should_lazy_alloc(p, pagetable, page)) {   // 确认此页面是否可以通过 Lazy Allocation 修复
        return -1;
    }

    char *mem = kalloc();           // 请求一页物理内存
    if (mem == NULL) {  // 分配失败
        return -1;
    }
    memset(mem, 0, PGSIZE);         // 初始化物理页框后再使用，确保 zero-fill-on-demand

    // 将发生 Lazy Fault 的页面映射到新分配的物理页框
    // PTE 权限设置为 PTE_R|PTE_W|PTE_U|PTE_V（mappages 内部会自动添加 PTE_V 权限位）
    if (mappages(pagetable, page, PGSIZE, (uint64)mem, PTE_R | PTE_W | PTE_U) != 0) {
        // 映射失败，则释放新分配的物理页框，避免内存泄漏
        kfree(mem);
        return -1;
    }
    return 0;
}


extern int lazy_alloc_enabled;
static uint64 lazy_alloc_walkaddr(pagetable_t pagetable, uint64 va, int write) {
    uint64 pa = walkaddr(pagetable, va);    // 查询当前页面是否已经映射到用户态下可访问的物理页框
    if (pa != 0) {
        // 页面已有映射
#if COW_ALLOC
        // 检查页面是否为 COW 页，并在意图执行写操作（write 参数指示）时为当前进程复制一个私有页解除 COW 状态
        if (write) {
            pte_t *pte = walk(pagetable, va, 0);
            if (pte && (*pte & PTE_COW)) {
                // 页面是 COW 页，调用 cow_handle_fault 解除 COW 状态
                if (cow_handle_fault(pagetable, va) != 0) {
                    return 0; // COW fault 处理失败
                }
                // COW fault 处理成功后，映射的物理页框可能改变
                // 因此重新获取页面映射到的物理页框信息
                pa = walkaddr(pagetable, va);
            }
        }
#endif
        return pa;
    }

    // walkaddr 失败，说明页面尚未映射
    // 若启用 Lazy Allocation，则分配一个物理页框
    if (lazy_alloc_enabled) {
        struct proc *p = myproc();
        // 确保当前有进程上下文，且有有效页表
        if (p && p->pagetable == pagetable) {
            // 调用 user_lazy_alloc 为当前页面分配物理页框
            if (user_lazy_alloc(p, pagetable, va) == 0) {
                // 分配成功，调用 walkaddr 获取页面映射到的物理页框信息
                return walkaddr(pagetable, va);
            }
        }
    }

    // 未启用 Lazy Allocation 或 Lazy Allocation 失败
    return 0;
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa.
// va and size MUST be page-aligned.
// Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm) {
    if ((va % PGSIZE) != 0) {
        panic("mappages: va not aligned");
    }
    if ((size % PGSIZE) != 0) {
        panic("mappages: size not aligned");
    }
    if (size == 0) {
        panic("mappages: size");
    }

    uint64 a = va;
    uint64 last = va + size - PGSIZE;
    for (;;) {
        pte_t *pte = walk(pagetable, a, 1); // 通过三级页表找到页面映射到的物理页框
        if (pte == 0) {
            return -1;
        }
        // Check if the pte is valid.
        if (*pte & PTE_V) {
            panic("mappages: remap");
        }
        *pte = PA2PTE(pa) | perm | PTE_V;
        if (a == last) {
            break;
        }
        a += PGSIZE;
        pa += PGSIZE;
    }
    return 0;
}

// Create an empty user page table.
// Returns 0 if out of memory.
pagetable_t uvmcreate(void) {
    pagetable_t pagetable = (pagetable_t)kalloc();
    if (pagetable == 0) {
        return 0;
    }
    memset(pagetable, 0, PGSIZE);
    return pagetable;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. It's OK if the mappings don't exist.
// Optionally free the physical memory.
void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free) {
    if ((va % PGSIZE) != 0) {
        panic("uvmunmap: not aligned");
    }

    for (uint64 a = va; a < va + npages * PGSIZE; a += PGSIZE) {
        pte_t *pte = walk(pagetable, a, 0);
        if (pte == 0) {
            continue;
        }
        if ((*pte & PTE_V) == 0) {
            continue;
        }
        if (do_free) {
            uint64 pa = PTE2PA(*pte);
            kfree((void *)pa);
        }
        *pte = 0;
    }
}

// Allocate PTEs and physical memory to grow a process from oldsz to
// newsz, which need not be page aligned. Returns new size or 0 on error.
uint64 uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm) {
    if (newsz < oldsz) {
        return oldsz;
    }

    uint64 a = PGROUNDUP(oldsz);
    for (; a < newsz; a += PGSIZE) {
        void *mem = kalloc();
        if (mem == 0) {
            uvmdealloc(pagetable, a, oldsz);
            return 0;
        }
        memset(mem, 0, PGSIZE);
        if (mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_R | PTE_U | xperm) != 0) {
            kfree(mem);
            uvmdealloc(pagetable, a, oldsz);
            return 0;
        }
    }
    return newsz;
}

// Deallocate user pages to bring the process size from oldsz to newsz.
// Returns the new process size.
uint64 uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz) {
    if (newsz >= oldsz) {
        return oldsz;
    }
    if (PGROUNDUP(newsz) < PGROUNDUP(oldsz)) {
        uint64 npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
        uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
    }
    return newsz;
}

int uvmmapped_pages(pagetable_t pagetable, uint64 sz) {
    int n = 0;

    for (uint64 va = 0; va < sz; va += PGSIZE) {
        pte_t *pte = walk(pagetable, va, 0);
        if (pte != 0 && (*pte & PTE_V) && (*pte & PTE_U)) {
            n++;
        }
    }

    return n;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
static void freewalk(pagetable_t pagetable) {
    for (int i = 0; i < 512; i++) {
        pte_t pte = pagetable[i];
        if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
            // This PTE points to a lower-level page table.
            uint64 child = PTE2PA(pte);
            freewalk((pagetable_t)child);
            pagetable[i] = 0;
        } else if (pte & PTE_V) {
            panic("freewalk: leaf");
        }
    }
    kfree((void *)pagetable);
}

// Free user memory pages, then free page-table pages.
void uvmfree(pagetable_t pagetable, uint64 sz) {
    if (sz > 0) {
        uvmunmap(pagetable, 0, PGROUNDUP(sz) / PGSIZE, 1);
    }
    freewalk(pagetable);
}

// Given a parent process's page table, copy its memory into a child's page table.
// With COW enabled: share physical pages read-only, mark with PTE_COW.
// Without COW: copy both page table and physical memory (original behavior).
// Returns 0 on success, -1 on failure (and frees any allocated pages).
int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz) {
    for (uint64 i = 0; i < sz; i += PGSIZE) {   // 遍历父进程的每个页面
        pte_t *pte = walk(old, i, 0);
        if (pte == 0) {
            continue;
        }
        if ((*pte & PTE_V) == 0) {  // 跳过未建立映射的页面（包括 Lazy 页面）
            continue;
        }

        uint64 pa = PTE2PA(*pte);
        uint flags = (uint)PTE_FLAGS(*pte);     // PTE 原标志位

// If COW_ALLOC is enabled, fork should avoid unnecessary physical-page copies.
#if COW_ALLOC
        // 启用 COW，在执行 fork 时只将父进程的所有页面设置为 COW 状态复制给子进程，不实际复制物理页框

        // 调整 PTE 标志位：对于可写页调整为只读并添加 COW 标志位
        // 对于只读页，保持权限不变即可
        if (flags & PTE_W) {
            // 撤销父进程中所有页面的写权限，并设置为 COW 状态
            flags = (flags & ~PTE_W) | PTE_COW;
            // 更新父进程页面的 PTE
            *pte = PA2PTE(pa) | flags | PTE_V;
            // 子进程共享父进程的页面-页框映射关系，因此增加映射物理页框的引用计数
            kaddref((void *)pa);
        }
        // 将子进程的虚拟页映射到父进程的物理页框
        if (mappages(new, i, PGSIZE, pa, flags) != 0) {
            // 映射失败，调用 kfree 减少引用计数，避免内存泄漏
            kfree((void *)pa);
            goto err;   // 执行异常处理
        }
#else
        // Eager copy: allocate new page and copy data.
        void *mem = kalloc();
        if (mem == 0) {
            goto err;
        }
        memmove(mem, (void *)pa, PGSIZE);
        if (mappages(new, i, PGSIZE, (uint64)mem, flags) != 0) {
            kfree(mem);
            goto err;
        }
#endif
    }
    return 0;

err:
    uvmunmap(new, 0, PGROUNDUP(sz) / PGSIZE, 1);
    return -1;
}

int uvmcopyrange(pagetable_t old, pagetable_t new, uint64 start, uint64 len) {
    if ((start % PGSIZE) != 0 || (len % PGSIZE) != 0) {
        panic("uvmcopyrange: not aligned");
    }

    for (uint64 i = 0; i < len; i += PGSIZE) {
        uint64 va = start + i;
        pte_t *pte = walk(old, va, 0);
        if (pte == 0 || (*pte & PTE_V) == 0) {
            continue;
        }

        uint64 pa = PTE2PA(*pte);
        uint flags = (uint)PTE_FLAGS(*pte);
        void *mem = kalloc();
        if (mem == 0) {
            goto err;
        }
        memmove(mem, (void *)pa, PGSIZE);
        if (mappages(new, va, PGSIZE, (uint64)mem, flags) != 0) {
            kfree(mem);
            goto err;
        }
    }
    return 0;

err:
    uvmunmap(new, start, len / PGSIZE, 1);
    return -1;
}

// Clear user-accessible bit for the PTE at va.
// Used to create a guard page under the user stack.
void uvmclear(pagetable_t pagetable, uint64 va) {
    pte_t *pte = walk(pagetable, va, 0);
    if (pte == 0) {
        panic("uvmclear");
    }
    *pte &= ~PTE_U;
}

// Handle a COW page fault: allocate a new page, copy the old data,
// update the PTE. Returns 0 on success, -1 on failure.
// This is used by both usertrap (Store Page Fault) and copyout.
#if COW_ALLOC
// [COW] Handle a write fault by either restoring write permission when
// this process is the only owner, or copying data into a private page.
int cow_handle_fault(pagetable_t pagetable, uint64 va) {
    // 将 fault 地址对齐到页面基址
    uint64 page = PGROUNDDOWN(va);

    // 检查 fault 页面的 PTE 是否存在
    pte_t *pte = walk(pagetable, page, 0);
    if (pte == 0) {
        // PTE 不存在，不是 COW fault
        return -1;
    }

    // 检查 PTE 是否有效且处于 COW 状态
    if (!(*pte & PTE_V) || !(*pte & PTE_COW)) {
        return -1;
    }

    // 检查 PTE 是否用户态可访问
    if (!(*pte & PTE_U)) {
        return -1;
    }

    // 获取 COW 页映射到的物理页框地址和页面标志位
    uint64 pa = PTE2PA(*pte);
    uint flags = (uint)PTE_FLAGS(*pte);

    // 根据引用计数决定处理策略
    int ref = kgetref((void *)pa);
    if (ref == 1) {
        // 引用计数为 1，直接恢复当前页面的写权限、撤销 COW 状态
        *pte = PA2PTE(pa) | (flags & ~PTE_COW) | PTE_W | PTE_V;
    }
    else {
        // 引用计数大于 1，需要为当前进程复制一个物理页框并改变映射关系
        char *mem = kalloc();
        if (mem == 0) {
            // 物理页框分配失败
            return -1;
        }
        // 将 COW 页的内容复制到新页
        memmove(mem, (void *)pa, PGSIZE);
        // 更新 PTE，将页面映射到新的私有物理页框，恢复写权限，移除 COW 标记
        *pte = PA2PTE((uint64)mem) | (flags & ~PTE_COW) | PTE_W | PTE_V;
        // 减少 COW 页的引用计数
        kfree((void *)pa);
    }

    // 显式刷新 TLB，清除该页面的映射关系，确保后续访问该页面时使用新的 PTE
    sfence_vma();
    return 0;
}
#endif

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len) {
    while (len > 0) {
        uint64 va0 = PGROUNDDOWN(dstva);
        if (va0 >= MAXVA) {
            return -1;
        }
        uint64 pa0 = lazy_alloc_walkaddr(pagetable, va0, 1);
        if (pa0 == 0) {
            return -1;
        }
        pte_t *pte = walk(pagetable, va0, 0);
        if (pte == 0) {
            return -1;
        }
#if COW_ALLOC
        // 启用 COW，内核写用户 COW 页前必须先解除该页的 COW 状态

        // 检查目标页是否为 COW 页
        if (*pte & PTE_COW) {
            // 调用 cow_handle_fault 解除 COW 状态
            if (cow_handle_fault(pagetable, va0) != 0) {
                // COW fault handling 失败
                return -1;
            }
            
            // COW fault handling 后重新获取映射的物理页框和 PTE
            pa0 = walkaddr(pagetable, va0);
            if (pa0 == 0) {
                return -1;
            }
            pte = walk(pagetable, va0, 0);
            if (pte == 0) {
                return -1;
            }
        }
#endif
        if ((*pte & PTE_W) == 0) {
            return -1;
        }

        uint64 n = PGSIZE - (dstva - va0);
        if (n > len) {
            n = len;
        }
        memmove((void *)(pa0 + (dstva - va0)), src, (uint)n);

        len -= n;
        src += n;
        dstva = va0 + PGSIZE;
    }
    return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len) {
    while (len > 0) {
        uint64 va0 = PGROUNDDOWN(srcva);
        uint64 pa0 = lazy_alloc_walkaddr(pagetable, va0, 0);
        if (pa0 == 0) {
            return -1;
        }

        uint64 n = PGSIZE - (srcva - va0);
        if (n > len) {
            n = len;
        }
        memmove(dst, (void *)(pa0 + (srcva - va0)), (uint)n);

        len -= n;
        dst += n;
        srcva = va0 + PGSIZE;
    }
    return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max) {
    int got_null = 0;

    while (got_null == 0 && max > 0) {
        uint64 va0 = PGROUNDDOWN(srcva);
        uint64 pa0 = lazy_alloc_walkaddr(pagetable, va0, 0);
        if (pa0 == 0) {
            return -1;
        }

        uint64 n = PGSIZE - (srcva - va0);
        if (n > max) {
            n = max;
        }

        char *p = (char *)(pa0 + (srcva - va0));
        while (n > 0) {
            if (*p == '\0') {
                *dst = '\0';
                got_null = 1;
                break;
            }
            *dst = *p;
            dst++;
            p++;
            n--;
            max--;
        }

        srcva = va0 + PGSIZE;
    }

    return got_null ? 0 : -1;
}
