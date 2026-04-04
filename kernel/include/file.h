#ifndef __FILE_H__
#define __FILE_H__

#include "types.h"
#include "sleeplock.h"
#include "fs.h"

#define NOFILE 16
#define NFILE 64
#define NINODE 64

enum filetype {
    FD_NONE = 0,
    FD_PIPE,
    FD_CONSOLE,
    FD_INODE,
    FD_GPU,
};

struct pipe;

struct inode {
    uint dev;
    uint inum;
    int ref;
    struct sleeplock lock;
    int valid;

    short type;
    short major;
    short minor;
    short nlink;
    uint size;
    uint addrs[NDIRECT + 1];
};

struct file {
    enum filetype type;
    int ref;
    char readable;
    char writable;
    struct pipe *pipe;
    struct inode *ip;
    uint off;
};

void fileinit(void);
struct file *filealloc(void);
struct file *filedup(struct file *f);
void fileclose(struct file *f);
int filestat(struct file *f, uint64 addr);
int fileread(struct file *f, uint64 addr, int n);
int filewrite(struct file *f, uint64 addr, int n);
int fileioctl(struct file *f, int cmd, uint64 arg);

#endif
