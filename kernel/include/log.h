#ifndef __LOG_H__
#define __LOG_H__

/**
 * Kernel Debug Logger
 *
 * 用法:
 *   LOG_DEBUG("message %d", value);
 *   LOG_INFO("booting on hart %d", id);
 *   LOG_WARN("resource low: %d pages", n);
 *   LOG_ERROR("failed to map page at %p", va);
 *
 * 编译时通过 -DLOG_LEVEL=<n> 控制最低输出级别 (默认 DEBUG=0):
 *   make LOG_LEVEL=0   全部输出
 *   make LOG_LEVEL=1   INFO 及以上
 *   make LOG_LEVEL=2   WARN 及以上
 *   make LOG_LEVEL=3   仅 ERROR
 *   make LOG_LEVEL=4   关闭所有日志
 *
 * 示例输出:
 *   [INFO] [CPU 1] [PID 3] (kernel/core/vm.c:125) Mapping pages...
 *   [WARN] [CPU 0] [PID -] (kernel/core/kalloc.c:42) Low memory
 */

#include "defs.h"
#include "cpu.h"

/* ---- 日志级别常量 ---- */
#define LOG_LEVEL_DEBUG  0
#define LOG_LEVEL_INFO   1
#define LOG_LEVEL_WARN   2
#define LOG_LEVEL_ERROR  3
#define LOG_LEVEL_NONE   4  /* 关闭所有日志 */

/* 编译时未指定则默认为 DEBUG (输出全部) */
#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_DEBUG
#endif

/* ---- 内部实现宏 (请勿直接调用) ---- */

/**
 * _LOG_IMPL: 带上下文前缀的日志输出核心宏
 *
 * 会自动附带: 级别标识, CPU Hart ID, 当前进程 PID, 文件名:行号
 * 当 myproc() 返回非空时打印 PID, 否则打印 "-"
 */
#define _LOG_IMPL(level_str, fmt, ...)                                          \
    do {                                                                        \
        struct proc *_log_p = myproc();                                         \
        if (_log_p)                                                             \
            printf("[%s] [CPU %d] [PID %d] (%s:%d) " fmt "\n",                 \
                   level_str, cpuid(), _log_p->pid,                             \
                   __FILE__, __LINE__, ##__VA_ARGS__);                          \
        else                                                                    \
            printf("[%s] [CPU %d] [PID -] (%s:%d) " fmt "\n",                  \
                   level_str, cpuid(),                                          \
                   __FILE__, __LINE__, ##__VA_ARGS__);                          \
    } while (0)

/* ---- 对外接口宏 ---- */

/*
 * 编译时过滤: 当 LOG_LEVEL > 该级别时, 宏展开为空操作 ((void)0),
 * 编译器会将其完全优化掉, 实现零运行时开销.
 */

#if LOG_LEVEL <= LOG_LEVEL_DEBUG
#define LOG_DEBUG(fmt, ...) _LOG_IMPL("DEBUG", fmt, ##__VA_ARGS__)
#else
#define LOG_DEBUG(fmt, ...) ((void)0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_INFO
#define LOG_INFO(fmt, ...)  _LOG_IMPL("INFO",  fmt, ##__VA_ARGS__)
#else
#define LOG_INFO(fmt, ...)  ((void)0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_WARN
#define LOG_WARN(fmt, ...)  _LOG_IMPL("WARN",  fmt, ##__VA_ARGS__)
#else
#define LOG_WARN(fmt, ...)  ((void)0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_ERROR
#define LOG_ERROR(fmt, ...) _LOG_IMPL("ERROR", fmt, ##__VA_ARGS__)
#else
#define LOG_ERROR(fmt, ...) ((void)0)
#endif

#endif /* __LOG_H__ */
