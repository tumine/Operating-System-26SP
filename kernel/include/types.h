#ifndef __TYPES_H__
#define __TYPES_H__

#ifndef __ASSEMBLER__

// 基础无符号类型
typedef unsigned int   uint;
typedef unsigned short ushort;
typedef unsigned char  uchar;

// 定长类型 (在 RISC-V 64位架构下)
// 这一点非常重要：在 RV64 中，unsigned long 是 64 位的
typedef unsigned char  uint8;
typedef unsigned short uint16;
typedef unsigned int   uint32;
typedef unsigned long  uint64;

// 基础有符号类型
typedef char           int8;
typedef short          int16;
typedef int            int32;
typedef long           int64;

// 地址类型别名 (为了代码可读性，Lab 2/4 会用到)
typedef uint64 pde_t; // 页表项
typedef uint64 paddr; // 物理地址
typedef uint64 vaddr; // 虚拟地址

// 指针与布尔值
#define NULL ((void *)0)

typedef uint8 bool;
#define true 1
#define false 0

#endif // __ASSEMBLER__

#endif
