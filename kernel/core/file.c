#include "types.h"
#include "file.h"
#include "defs.h"
#include "proc.h"
#include "stat.h"

static struct {
    struct spinlock lock;
    struct file file[NFILE];
} ftable;

static int min_int(int a, int b) {
    return a < b ? a : b;
}

void fileinit(void) {
    initlock(&ftable.lock, "ftable");
    for (int i = 0; i < NFILE; i++) {
        ftable.file[i].type = FD_NONE;
        ftable.file[i].ref = 0;
        ftable.file[i].readable = 0;
        ftable.file[i].writable = 0;
        ftable.file[i].pipe = 0;
        ftable.file[i].ip = 0;
        ftable.file[i].off = 0;
    }
}

struct file *filealloc(void) {
    acquire(&ftable.lock);
    for (int i = 0; i < NFILE; i++) {
        if (ftable.file[i].ref == 0) {
            ftable.file[i].ref = 1;
            ftable.file[i].type = FD_NONE;
            ftable.file[i].readable = 0;
            ftable.file[i].writable = 0;
            ftable.file[i].pipe = 0;
            ftable.file[i].ip = 0;
            ftable.file[i].off = 0;
            release(&ftable.lock);
            return &ftable.file[i];
        }
    }
    release(&ftable.lock);
    return 0;
}

struct file *filedup(struct file *f) {
    if (f == 0) {
        return 0;
    }
    acquire(&ftable.lock);
    if (f->ref < 1) {
        release(&ftable.lock);
        return 0;
    }
    f->ref++;
    release(&ftable.lock);
    return f;
}

void fileclose(struct file *f) {
    if (f == 0) {
        return;
    }

    struct file ff;

    acquire(&ftable.lock);
    if (f->ref < 1) {
        release(&ftable.lock);
        return;
    }

    f->ref--;
    if (f->ref > 0) {
        release(&ftable.lock);
        return;
    }

    ff = *f;
    f->type = FD_NONE;
    f->readable = 0;
    f->writable = 0;
    f->pipe = 0;
    f->ip = 0;
    f->off = 0;
    release(&ftable.lock);

    if (ff.type == FD_PIPE && ff.pipe) {
        pipeclose(ff.pipe, ff.writable);
        return;
    }
    if (ff.type == FD_INODE && ff.ip) {
        iput(ff.ip);
    }
}

int filestat(struct file *f, uint64 addr) {
    struct proc *p = myproc();
    if (f == 0 || p == 0 || p->pagetable == 0) {
        return -1;
    }

    if (f->type != FD_INODE || f->ip == 0) {
        return -1;
    }

    struct stat st;
    ilock(f->ip);
    st.dev = (int)f->ip->dev;
    st.ino = f->ip->inum;
    st.type = f->ip->type;
    st.nlink = f->ip->nlink;
    st.size = f->ip->size;
    iunlock(f->ip);

    if (copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0) {
        return -1;
    }
    return 0;
}

int fileread(struct file *f, uint64 addr, int n) {
    if (f == 0 || f->readable == 0 || n < 0) {
        return -1;
    }

    if (f->type == FD_CONSOLE) {
        struct proc *p = myproc();
        if (p == 0) {
            return -1;
        }
        return consoleread(p->pagetable, addr, n);
    }

    if (f->type == FD_PIPE) {
        return piperead(f->pipe, addr, n);
    }

    if (f->type == FD_INODE) {
        struct proc *p = myproc();
        if (p == 0 || p->pagetable == 0) {
            return -1;
        }

        int tot = 0;
        char buf[BSIZE];

        while (tot < n) {
            int m = min_int(n - tot, BSIZE);
            int r = readi(f->ip, f->off, buf, (uint)m);
            if (r < 0) {
                return tot > 0 ? tot : -1;
            }
            if (r == 0) {
                break;
            }
            if (copyout(p->pagetable, addr + (uint64)tot, buf, (uint64)r) < 0) {
                return -1;
            }
            f->off += (uint)r;
            tot += r;
            if (r < m) {
                break;
            }
        }

        return tot;
    }

    return -1;
}

int filewrite(struct file *f, uint64 addr, int n) {
    if (f == 0 || f->writable == 0 || n < 0) {
        return -1;
    }

    if (f->type == FD_CONSOLE) {
        struct proc *p = myproc();
        if (p == 0 || p->pagetable == 0) {
            return -1;
        }

        consoleacquire();
        for (int i = 0; i < n; i++) {
            char c;
            if (copyin(p->pagetable, &c, addr + (uint64)i, 1) < 0) {
                consolerelease();
                return -1;
            }
            consputc(c);
        }
        consolerelease();
        return n;
    }

    if (f->type == FD_PIPE) {
        return pipewrite(f->pipe, addr, n);
    }

    if (f->type == FD_INODE) {
        struct proc *p = myproc();
        if (p == 0 || p->pagetable == 0) {
            return -1;
        }

        int tot = 0;
        char buf[BSIZE];

        while (tot < n) {
            int m = min_int(n - tot, BSIZE);
            if (copyin(p->pagetable, buf, addr + (uint64)tot, (uint64)m) < 0) {
                return tot > 0 ? tot : -1;
            }

            int w = writei(f->ip, f->off, buf, (uint)m);
            if (w < 0) {
                return tot > 0 ? tot : -1;
            }
            if (w == 0) {
                break;
            }

            f->off += (uint)w;
            tot += w;
            if (w < m) {
                break;
            }
        }

        return tot;
    }

    return -1;
}

int fileioctl(struct file *f, int cmd, uint64 arg) {
    if (f == 0) {
        return -1;
    }

    switch (f->type) {
    case FD_GPU:
        return gpu_ioctl(f, cmd, arg);
    default:
        break;
    }

    return -1;
}
