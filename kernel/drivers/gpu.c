// 模拟 GPU 设备驱动：显存分配/释放、矩阵乘法（张量计算）
// 设备通过 /gpu 字符设备暴露，用户态用 ioctl 交互

#include "types.h"
#include "spinlock.h"
#include "defs.h"
#include "gpu.h"
#include "file.h"
#include "proc.h"

struct gpu_device {
    struct spinlock lock;
    uint64 matmul_ops;
};

static struct gpu_device gpu;

void gpuinit(void) {
    initlock(&gpu.lock, "gpu");
    gpu.matmul_ops = 0;
}

// 内核内做矩阵乘法：C = A * B，行主序
// A 为 m*k, B 为 k*n, C 为 m*n
static void matmul_kernel(const int *A, const int *B, int *C, int m, int n, int k) {
    for (int i = 0; i < m; i++) {
        for (int j = 0; j < n; j++) {
            int sum = 0;
            for (int r = 0; r < k; r++) {
                sum += A[i * k + r] * B[r * n + j];
            }
            C[i * n + j] = sum;
        }
    }
}

int gpu_ioctl(struct file *f, int cmd, uint64 arg) {
    (void)f;
    struct proc *p = myproc();
    if (p == 0) {
        return -1;
    }

    switch (cmd) {
    case GPU_IOC_GET_INFO: {
        struct gpu_info info;
        info.max_dim = GPU_MAX_DIM;
        if (copyout(p->pagetable, arg, (char *)&info, sizeof(info)) < 0) {
            return -1;
        }
        return 0;
    }
    case GPU_IOC_ALLOC: {
        struct gpu_mem mem;
        if (copyin(p->pagetable, (char *)&mem, arg, sizeof(mem)) < 0) {
            return -1;
        }
        int npages = mem.npages;
        if (npages <= 0) {
            return -1;
        }

        uint64 oldsz;
        uint64 newsz;
        acquire(&p->lock);
        oldsz = p->sz;
        release(&p->lock);

        newsz = uvmalloc(p->pagetable, oldsz, oldsz + (uint64)npages * PGSIZE, PTE_W);
        if (newsz == 0) {
            return -1;
        }

        acquire(&p->lock);
        p->sz = newsz;
        release(&p->lock);

        mem.uva = newsz - (uint64)npages * PGSIZE;
        if (copyout(p->pagetable, arg, (char *)&mem, sizeof(mem)) < 0) {
            return -1;
        }
        return 0;
    }
    case GPU_IOC_FREE: {
        struct gpu_mem mem;
        if (copyin(p->pagetable, (char *)&mem, arg, sizeof(mem)) < 0) {
            return -1;
        }
        int npages = mem.npages;
        uint64 uva = mem.uva;
        if (npages <= 0 || (uva % PGSIZE) != 0) {
            return -1;
        }

        uvmunmap(p->pagetable, uva, (uint64)npages, 1);
        return 0;
    }
    case GPU_IOC_MATMUL: {
        struct gpu_matmul args;
        if (copyin(p->pagetable, (char *)&args, arg, sizeof(args)) < 0) {
            return -1;
        }
        int m = args.m;
        int n = args.n;
        int k = args.k;
        if (m <= 0 || n <= 0 || k <= 0 ||
            m > GPU_MAX_DIM || n > GPU_MAX_DIM || k > GPU_MAX_DIM) {
            return -1;
        }

        uint64 size_a = (uint64)(m * k) * sizeof(int);
        uint64 size_b = (uint64)(k * n) * sizeof(int);
        uint64 size_c = (uint64)(m * n) * sizeof(int);

        uint64 off_a = args.buf_a & (PGSIZE - 1);
        uint64 off_b = args.buf_b & (PGSIZE - 1);
        uint64 off_c = args.buf_c & (PGSIZE - 1);

        if (off_a + size_a > PGSIZE || off_b + size_b > PGSIZE || off_c + size_c > PGSIZE) {
            return -1;
        }

        uint64 pa_a = walkaddr(p->pagetable, args.buf_a);
        uint64 pa_b = walkaddr(p->pagetable, args.buf_b);
        uint64 pa_c = walkaddr(p->pagetable, args.buf_c);
        if (pa_a == 0 || pa_b == 0 || pa_c == 0) {
            return -1;
        }

        int *A = (int *)(pa_a + off_a);
        int *B = (int *)(pa_b + off_b);
        int *C = (int *)(pa_c + off_c);

        acquire(&gpu.lock);
        matmul_kernel(A, B, C, m, n, k);
        gpu.matmul_ops++;
        release(&gpu.lock);
        return 0;
    }
    case GPU_IOC_GET_STATS: {
        struct gpu_stats st;
        acquire(&gpu.lock);
        st.matmul_ops = gpu.matmul_ops;
        release(&gpu.lock);
        if (copyout(p->pagetable, arg, (char *)&st, sizeof(st)) < 0) {
            return -1;
        }
        return 0;
    }
    default:
        break;
    }

    return -1;
}

void gpudev_mount(void) {
    struct inode *root = namei("/");
    if (root == 0) {
        printf("gpudev_mount: no root\n");
        return;
    }

    struct inode *ip = dirlookup(root, "gpu", 0);
    if (ip != 0) {
        iput(ip);
        iput(root);
        return;
    }

    struct inode *dev = ialloc(root->dev, T_DEVICE);
    if (dev == 0) {
        printf("gpudev_mount: ialloc failed\n");
        iput(root);
        return;
    }

    ilock(dev);
    dev->type = T_DEVICE;
    dev->major = GPU_DEV_MAJOR;
    dev->minor = GPU_DEV_MINOR;
    dev->nlink = 1;
    dev->size = 0;
    memset(dev->addrs, 0, sizeof(dev->addrs));
    iupdate(dev);
    iunlock(dev);

    if (dirlink(root, "gpu", dev->inum) < 0) {
        printf("gpudev_mount: dirlink failed\n");
        ilock(dev);
        dev->nlink = 0;
        iupdate(dev);
        iunlock(dev);
        iput(dev);
        iput(root);
        return;
    }

    iput(dev);
    iput(root);
}
