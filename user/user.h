#ifndef __USER_H__
#define __USER_H__

#include "types.h"
#include <stdarg.h>

struct stat;

int write(int fd, const void *buf, int n);
int read(int fd, void *buf, int n);
void yield(void);
void exit(int status) __attribute__((noreturn));
void *sbrk(int n);
int fork(void);
int wait(int *status);
int exec(const char *name, char *const argv[]);
int close(int fd);
int fstat(int fd, struct stat *st);
int chdir(const char *path);
int dup(int fd);
int open(const char *name, int omode);
int getpid(void);
int sleep(int ticks);
int uptime(void);
uint64 time_us(void);
int kill(int pid);
int pipe(int fd[2]);
int mkdir(const char *path);
int unlink(const char *path);
int link(const char *oldpath, const char *newpath);
int hello_id(int tag);
int dup2(int oldfd, int newfd);
int getcwd(char *buf, int max);
int ioctl(int fd, int cmd, uint64 arg);

// Wall-clock timing API for student code. Prefer these helpers over raw counters.
static inline uint64 timer_start(void) {
    return time_us();
}

static inline uint64 timer_end(void) {
    return time_us();
}

static inline uint64 timer_elapsed_us(uint64 start_us, uint64 end_us) {
    return end_us >= start_us ? end_us - start_us : 0;
}

static inline uint64 timer_elapsed_ms(uint64 start_us, uint64 end_us) {
    return timer_elapsed_us(start_us, end_us) / 1000ULL;
}

// Low-level counters for advanced profiling. These are raw hardware counters.
static inline uint64 rdcycle(void) {
    uint64 x;
    asm volatile("csrr %0, cycle" : "=r"(x));
    return x;
}

static inline uint64 rdtime(void) {
    uint64 x;
    asm volatile("csrr %0, time" : "=r"(x));
    return x;
}

static inline uint64 rdinstret(void) {
    uint64 x;
    asm volatile("csrr %0, instret" : "=r"(x));
    return x;
}

// user library helpers
int stat(const char *path, struct stat *st);
char *strcpy(char *dst, const char *src);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, uint n);
uint strlen(const char *s);
char *strchr(const char *s, char c);
char *gets(char *buf, int max);
int atoi(const char *s);
void *memset(void *dst, int c, uint n);
void *memmove(void *dst, const void *src, int n);
void *memcpy(void *dst, const void *src, uint n);
int memcmp(const void *a, const void *b, uint n);

// printf-style output helpers
void vprintf(int fd, const char *fmt, va_list ap);
void fprintf(int fd, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif