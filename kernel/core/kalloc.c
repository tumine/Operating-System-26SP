#include "types.h"
#include "riscv.h"
#include "spinlock.h"
#include "memlayout.h"
#include "defs.h"
#include "param.h"
#include "log.h"

// Physical page allocator.
//
// Frees physical memory between the end of the kernel and PHYSTOP, and
// hands out 4096-byte pages.

extern char end[]; // provided by kernel.ld

struct run {
    struct run *next;
};

static struct {
    struct spinlock lock;
    struct run *freelist;
} kmem;

// COW reference-counting state. Students may keep this representation or
// replace it with an equivalent one.
#if COW_ALLOC
static struct spinlock ref_lock;
static int ref_cnt[PHYSTOP / PGSIZE];

// Increase the reference count for a physical page.
void kaddref(void *pa) {
    // 将物理地址转换为页索引，用于在 ref_cnt 数组中定位该页的引用计数
    uint64 idx = (uint64)pa / PGSIZE;
    // 对 ref_lock 加锁保护，防止 fork/exit 或 COW fault handling 破坏计数结果
    acquire(&ref_lock);
    ref_cnt[idx]++;     // 一个新进程中的某个页面映射到此页框
    release(&ref_lock);
}

// Get the reference count for a physical page.
int kgetref(void *pa) {
    // 将物理地址转换为页索引，用于在 ref_cnt 数组中定位该页的引用计数
    uint64 idx = (uint64)pa / PGSIZE;
    acquire(&ref_lock);
    // 读取当前引用计数，确定有多少个地址空间映射共享该物理页
    // COW fault handling 过程使用 cnt 的值判断是否需要复制进程私有的物理页
    int cnt = ref_cnt[idx];
    release(&ref_lock);
    return cnt;
}
#else
// Stubs when COW is disabled.
void kaddref(void *pa) { (void)pa; }
int kgetref(void *pa) { (void)pa; return 1; }
#endif

void kfree(void *pa) {
    uint64 a = (uint64)pa;
    if ((a % PGSIZE) != 0) {
        panic("kfree: not aligned");
    }
    if (a < (uint64)end || a >= PHYSTOP) {
        panic("kfree: bad pa");
    }

#if COW_ALLOC
    uint64 idx = (uint64)pa / PGSIZE;   // 将物理地址转换为页索引，用于在 ref_cnt 数组中定位该页的引用计数
    acquire(&ref_lock);
    // 引用计数减 1
    ref_cnt[idx]--;
    // 判断是否还有其他进程共享该页
    int cnt = ref_cnt[idx];
    release(&ref_lock);
    // 如果引用计数仍大于 0，说明该页还被其他进程共享，不能释放回 freelist
    if (cnt > 0) {
        return;
    }
    // 引用计数为 0，继续执行下方的释放逻辑
#endif

    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    struct run *r = (struct run *)pa;

    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
}

void kinit(void) {
    initlock(&kmem.lock, "kmem");
    kmem.freelist = 0;

#if COW_ALLOC
    // COW metadata must be initialized before physical pages are handed out.
    initlock(&ref_lock, "ref");
    for (int i = 0; i < PHYSTOP / PGSIZE; i++) {
        ref_cnt[i] = 0;
    }
#endif

    // Free every page after the kernel.
    uint64 p = PGROUNDUP((uint64)end);
    for (; p + PGSIZE <= PHYSTOP; p += PGSIZE) {
        kfree((void *)p);
    }
}

void *kalloc(void) {
    acquire(&kmem.lock);
    struct run *r = kmem.freelist;  // 获取第一个空闲物理页框基址
    if (r) {
        kmem.freelist = r->next;
    }
    release(&kmem.lock);

    if (r) {
        // Fill with junk to help spot uninitialized use.
        memset((void *)r, 5, PGSIZE);
#if COW_ALLOC
        uint64 idx = (uint64)r / PGSIZE;    // 将物理地址转换为页索引，用于在 ref_cnt 数组中定位该页的引用计数
        // 在启用 COW 时，新分配的物理页初始引用计数为 1，
        // 表示当前在该页框上只建立一个映射关系（即调用 kalloc 的地址空间）
        acquire(&ref_lock);
        ref_cnt[(uint64)r / PGSIZE] = 1;
        release(&ref_lock);
#endif
        LOG_DEBUG("Allocated physical page at %p", r); // [埋点]
    } else {
        LOG_ERROR("kalloc out of memory!");
    }
    return (void *)r;
}

int kfreepage_count(void) {
    int n = 0;

    acquire(&kmem.lock);
    for (struct run *r = kmem.freelist; r != 0; r = r->next) {
        n++;
    }
    release(&kmem.lock);

    return n;
}

int ktotalpage_count(void) {
    uint64 first = PGROUNDUP((uint64)end);
    return (int)((PHYSTOP - first) / PGSIZE);
}
