#ifndef __GPU_H__
#define __GPU_H__

#include "types.h"

// 模拟 GPU 张量计算：单次矩阵乘法最大维度（每矩阵一页内，故 ≤32）
#define GPU_MAX_DIM 32

// 设备主次号，用于 T_DEVICE inode
#define GPU_DEV_MAJOR 1
#define GPU_DEV_MINOR 0

// ioctl 命令
#define GPU_IOC_GET_INFO 1  // 查询设备能力
#define GPU_IOC_ALLOC    2  // 分配显存并映射到用户空间
#define GPU_IOC_FREE     3  // 释放显存并取消映射
#define GPU_IOC_MATMUL   4  // 在显存上的矩阵做乘法
#define GPU_IOC_GET_STATS 5  // 查询驱动统计（如 matmul 调用次数）

// 查询设备能力：最大矩阵维度
struct gpu_info {
    int max_dim;
};

// ioctl(GPU_IOC_GET_STATS) 返回：自启动以来完成的矩阵乘法次数
struct gpu_stats {
    uint64 matmul_ops;
};

// 显存块描述：npages 页，从 uva 开始（用户虚拟地址，页对齐）
struct gpu_mem {
    int npages;
    uint64 uva;
};

// 矩阵乘法 C[m][n] = A[m][k] * B[k][n]，行主序，元素为 int
// buf_a/buf_b/buf_c 是用户虚拟地址，通常来自 GPU_IOC_ALLOC 返回的显存映射
struct gpu_matmul {
    int m;
    int n;
    int k;
    uint64 buf_a;
    uint64 buf_b;
    uint64 buf_c;
};

#endif
