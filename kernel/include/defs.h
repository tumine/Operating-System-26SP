#ifndef __DEFS_H__
#define __DEFS_H__

#include "types.h"
#include "riscv.h"

// drivers/uart.c
void uart_init(void);
void uart_putc(char ch);
void uart_puts(char *s);
int uart_getc(void);
void uart_isr(void);

// drivers/plic.c
void plic_init(void);
void plic_inithart(void);
int plic_claim(void);
void plic_complete(int irq);

// core/console.c
void consoleinit(void);
void consolepanic(void);
void consoleacquire(void);
void consolerelease(void);
void consputc(int c);
void consoleintr(int c);
int consolegetc(void);
int consoleread(pagetable_t pagetable, uint64 dstva, int n);

// core/file.c
struct file;
void fileinit(void);
struct file *filealloc(void);
struct file *filedup(struct file *f);
void fileclose(struct file *f);
int filestat(struct file *f, uint64 addr);
int fileread(struct file *f, uint64 addr, int n);
int filewrite(struct file *f, uint64 addr, int n);

// core/pipe.c
struct pipe;
int pipealloc(struct file **f0, struct file **f1);
void pipeclose(struct pipe *pi, int writable);
int pipewrite(struct pipe *pi, uint64 addr, int n);
int piperead(struct pipe *pi, uint64 addr, int n);

// fs/fs.c
struct inode;
void fsinit(int dev);
struct inode *ialloc(uint dev, short type);
struct inode *namei(const char *path);
struct inode *nameiparent(const char *path, char *name);
struct inode *idup(struct inode *ip);
void ilock(struct inode *ip);
void iunlock(struct inode *ip);
void iput(struct inode *ip);
void iunlockput(struct inode *ip);
void iupdate(struct inode *ip);
void itrunc(struct inode *ip);
int readi(struct inode *ip, uint64 off, void *dst, uint n);
int writei(struct inode *ip, uint64 off, const void *src, uint n);
int namecmp(const char *s, const char *t);
struct inode *dirlookup(struct inode *dp, const char *name, uint *poff);
int dirlink(struct inode *dp, const char *name, uint inum);
void fs_read_file(const char *path);

// lib/printf.c
void printfinit(void);
void printf(char *fmt, ...);
void panic(char *s);

// lib/string.c
void *memset(void *dst, int c, uint n);
void *memmove(void *dst, const void *src, uint n);

// core/kalloc.c
void kinit(void);
void kfree(void *pa);
void *kalloc(void);

// core/vm.c
void kvminit(void);
void kvminithart(void);
extern pagetable_t kernel_pagetable;
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm);
pagetable_t uvmcreate(void);
uint64 walkaddr(pagetable_t pagetable, uint64 va);
void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free);
uint64 uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm);
uint64 uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz);
int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz);
void uvmfree(pagetable_t pagetable, uint64 sz);
void uvmclear(pagetable_t pagetable, uint64 va);
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len);
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len);
int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max);

// core/syscall.c
void syscall(void);

// core/proc.c
struct spinlock;
struct proc;
int kill(int pid);
struct proc *myproc(void);
struct inode *proc_cwddup(void);
void sleep(void *chan, struct spinlock *lk);
void wakeup(void *chan);

// core/sleeplock.c
struct sleeplock;
void initsleeplock(struct sleeplock *lk, char *name);
void acquiresleep(struct sleeplock *lk);
void releasesleep(struct sleeplock *lk);
int holdingsleep(struct sleeplock *lk);

// core/trap.c
void usertrap(void);
void usertrapret(void);
uint ticks_get(void);
int ticks_sleep(uint n);

// drivers/virtio_disk.c
struct buf;
void virtio_disk_init(void);
void virtio_disk_rw(struct buf *b, int write);
void virtio_disk_isr(void);

// core/bio.c
void binit(void);
struct buf *bread(uint dev, uint blockno);
void bwrite(struct buf *b);
void brelse(struct buf *b);

#endif
