#include "types.h"
#include "defs.h"
#include "buf.h"
#include "fs.h"
#include "file.h"
#include "spinlock.h"

static struct superblock sb;
static int rootdev = 0;

static struct {
    struct spinlock lock;
    struct inode inode[NINODE];
} itable;

static int kstrncmp(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        uchar ac = (uchar)a[i];
        uchar bc = (uchar)b[i];
        if (ac != bc) {
            return (int)ac - (int)bc;
        }
        if (ac == 0) {
            return 0;
        }
    }
    return 0;
}

static uint kstrlen(const char *s) {
    uint n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

static uint min_uint(uint a, uint b) {
    return a < b ? a : b;
}

static void readsb(int dev, struct superblock *sbp) {
    struct buf *bp = bread((uint)dev, 1);
    memmove(sbp, bp->data, sizeof(*sbp));
    brelse(bp);
}

static void bzero(uint dev, uint bno) {
    struct buf *bp = bread(dev, bno);
    memset(bp->data, 0, BSIZE);
    bwrite(bp);
    brelse(bp);
}

static uint balloc(uint dev) {
    for (uint b = 0; b < sb.size; b += BPB) {
        struct buf *bp = bread(dev, BBLOCK(b, sb));
        for (uint bi = 0; bi < BPB && (b + bi) < sb.size; bi++) {
            uint m = 1U << (bi % 8);
            if ((bp->data[bi / 8] & m) == 0) {
                bp->data[bi / 8] |= m;
                bwrite(bp);
                brelse(bp);
                bzero(dev, b + bi);
                return b + bi;
            }
        }
        brelse(bp);
    }

    printf("balloc: out of blocks\n");
    return 0;
}

static void bfree(uint dev, uint b) {
    struct buf *bp = bread(dev, BBLOCK(b, sb));
    uint bi = b % BPB;
    uint m = 1U << (bi % 8);
    if ((bp->data[bi / 8] & m) == 0) {
        brelse(bp);
        panic("bfree: freeing free block");
    }
    bp->data[bi / 8] &= ~m;
    bwrite(bp);
    brelse(bp);
}

void fsinit(int dev) {
    static int once = 0;

    if (once == 0) {
        initlock(&itable.lock, "itable");
        for (int i = 0; i < NINODE; i++) {
            initsleeplock(&itable.inode[i].lock, "inode");
            itable.inode[i].ref = 0;
            itable.inode[i].valid = 0;
        }
        once = 1;
    }

    rootdev = dev;
    readsb(dev, &sb);
    if (sb.magic != FSMAGIC) {
        panic("fsinit: bad superblock");
    }

}

static struct inode *iget(uint dev, uint inum) {
    struct inode *empty = 0;

    acquire(&itable.lock);
    for (int i = 0; i < NINODE; i++) {
        struct inode *ip = &itable.inode[i];
        if (ip->ref > 0 && ip->dev == dev && ip->inum == inum) {
            ip->ref++;
            release(&itable.lock);
            return ip;
        }
        if (empty == 0 && ip->ref == 0) {
            empty = ip;
        }
    }

    if (empty == 0) {
        release(&itable.lock);
        panic("iget: no inodes");
    }

    empty->dev = dev;
    empty->inum = inum;
    empty->ref = 1;
    empty->valid = 0;
    empty->type = 0;
    empty->size = 0;
    memset(empty->addrs, 0, sizeof(empty->addrs));
    release(&itable.lock);

    return empty;
}

struct inode *idup(struct inode *ip) {
    if (ip == 0) {
        return 0;
    }
    acquire(&itable.lock);
    if (ip->ref < 1) {
        release(&itable.lock);
        panic("idup");
    }
    ip->ref++;
    release(&itable.lock);
    return ip;
}

struct inode *ialloc(uint dev, short type) {
    for (uint inum = 1; inum < sb.ninodes; inum++) {
        struct buf *bp = bread(dev, IBLOCK(inum, sb));
        struct dinode *dip = (struct dinode *)bp->data + inum % IPB;
        if (dip->type == 0) {
            memset(dip, 0, sizeof(*dip));
            dip->type = type;
            bwrite(bp);
            brelse(bp);
            return iget(dev, inum);
        }
        brelse(bp);
    }

    printf("ialloc: no inodes\n");
    return 0;
}

void iupdate(struct inode *ip) {
    if (ip == 0) {
        return;
    }
    int need_unlock = 0;
    if (!holdingsleep(&ip->lock)) {
        ilock(ip);
        need_unlock = 1;
    }

    struct buf *bp = bread(ip->dev, IBLOCK(ip->inum, sb));
    struct dinode *dip = (struct dinode *)bp->data + ip->inum % IPB;

    dip->type = ip->type;
    dip->major = ip->major;
    dip->minor = ip->minor;
    dip->nlink = ip->nlink;
    dip->size = ip->size;
    memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));

    bwrite(bp);
    brelse(bp);
    if (need_unlock) {
        iunlock(ip);
    }
}

void ilock(struct inode *ip) {
    if (ip == 0 || ip->ref < 1) {
        panic("ilock");
    }
    acquiresleep(&ip->lock);
    if (ip->valid == 0) {
        struct buf *bp = bread(ip->dev, IBLOCK(ip->inum, sb));
        struct dinode *dip = (struct dinode *)bp->data;
        dip += ip->inum % IPB;

        ip->type = dip->type;
        ip->major = dip->major;
        ip->minor = dip->minor;
        ip->nlink = dip->nlink;
        ip->size = dip->size;
        memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
        ip->valid = 1;
        brelse(bp);
        if (ip->type == 0) {
            panic("ilock: no type");
        }
    }
}

void iunlock(struct inode *ip) {
    if (ip == 0 || !holdingsleep(&ip->lock) || ip->ref < 1) {
        panic("iunlock");
    }
    releasesleep(&ip->lock);
}

void itrunc(struct inode *ip);

void iput(struct inode *ip) {
    if (ip == 0) {
        return;
    }

    acquire(&itable.lock);
    if (ip->ref < 1) {
        release(&itable.lock);
        panic("iput");
    }

    if (ip->ref == 1 && ip->valid && ip->nlink == 0) {
        release(&itable.lock);

        ilock(ip);
        itrunc(ip);
        ip->type = 0;
        iupdate(ip);
        ip->valid = 0;
        iunlock(ip);

        acquire(&itable.lock);
    }

    ip->ref--;
    if (ip->ref == 0) {
        ip->valid = 0;
        ip->type = 0;
        ip->size = 0;
    }
    release(&itable.lock);
}

void iunlockput(struct inode *ip) {
    iunlock(ip);
    iput(ip);
}

static int inode_lock_if_needed(struct inode *ip) {
    if (holdingsleep(&ip->lock)) {
        return 0;
    }
    ilock(ip);
    return 1;
}

static uint bmap(struct inode *ip, uint bn, int alloc) {
    if (bn < NDIRECT) {
        if (ip->addrs[bn] == 0 && alloc) {
            ip->addrs[bn] = balloc(ip->dev);
        }
        return ip->addrs[bn];
    }

    bn -= NDIRECT;
    if (bn < NINDIRECT) {
        uint indirect = ip->addrs[NDIRECT];
        if (indirect == 0) {
            if (!alloc) {
                return 0;
            }
            indirect = balloc(ip->dev);
            if (indirect == 0) {
                return 0;
            }
            ip->addrs[NDIRECT] = indirect;
        }

        struct buf *bp = bread(ip->dev, indirect);
        uint *a = (uint *)bp->data;
        uint addr = a[bn];

        if (addr == 0 && alloc) {
            addr = balloc(ip->dev);
            if (addr != 0) {
                a[bn] = addr;
                bwrite(bp);
            }
        }

        brelse(bp);
        return addr;
    }

    return 0;
}

void itrunc(struct inode *ip) {
    if (ip == 0) {
        return;
    }
    int need_unlock = inode_lock_if_needed(ip);

    for (int i = 0; i < NDIRECT; i++) {
        if (ip->addrs[i]) {
            bfree(ip->dev, ip->addrs[i]);
            ip->addrs[i] = 0;
        }
    }

    if (ip->addrs[NDIRECT]) {
        struct buf *bp = bread(ip->dev, ip->addrs[NDIRECT]);
        uint *a = (uint *)bp->data;
        for (int j = 0; j < NINDIRECT; j++) {
            if (a[j]) {
                bfree(ip->dev, a[j]);
            }
        }
        brelse(bp);
        bfree(ip->dev, ip->addrs[NDIRECT]);
        ip->addrs[NDIRECT] = 0;
    }

    ip->size = 0;
    iupdate(ip);
    if (need_unlock) {
        iunlock(ip);
    }
}

int readi(struct inode *ip, uint64 off, void *dst, uint n) {
    if (ip == 0 || dst == 0) {
        return -1;
    }
    if (n == 0) {
        return 0;
    }
    int need_unlock = inode_lock_if_needed(ip);

    if (ip->type == 0) {
        if (need_unlock) {
            iunlock(ip);
        }
        return -1;
    }

    if (off > ip->size) {
        if (need_unlock) {
            iunlock(ip);
        }
        return 0;
    }
    if (off + n < off) {
        if (need_unlock) {
            iunlock(ip);
        }
        return -1;
    }
    if (off + n > ip->size) {
        n = (uint)(ip->size - off);
    }

    uint tot = 0;
    uchar *p = (uchar *)dst;
    while (tot < n) {
        uint bn = (uint)((off + tot) / BSIZE);
        uint addr = bmap(ip, bn, 0);
        if (addr == 0) {
            break;
        }

        struct buf *bp = bread(ip->dev, addr);
        uint boff = (uint)((off + tot) % BSIZE);
        uint m = min_uint(n - tot, BSIZE - boff);
        memmove(p + tot, bp->data + boff, m);
        brelse(bp);
        tot += m;
    }

    if (need_unlock) {
        iunlock(ip);
    }
    return (int)tot;
}

int writei(struct inode *ip, uint64 off, const void *src, uint n) {
    if (ip == 0 || src == 0) {
        return -1;
    }
    if (n == 0) {
        return 0;
    }
    int need_unlock = inode_lock_if_needed(ip);

    if (ip->type == 0) {
        if (need_unlock) {
            iunlock(ip);
        }
        return -1;
    }
    if (off > ip->size || off + n < off) {
        if (need_unlock) {
            iunlock(ip);
        }
        return -1;
    }
    if (off + n > (uint64)MAXFILE * BSIZE) {
        if (need_unlock) {
            iunlock(ip);
        }
        return -1;
    }

    uint tot = 0;
    const uchar *p = (const uchar *)src;
    while (tot < n) {
        uint bn = (uint)((off + tot) / BSIZE);
        uint addr = bmap(ip, bn, 1);
        if (addr == 0) {
            break;
        }

        struct buf *bp = bread(ip->dev, addr);
        uint boff = (uint)((off + tot) % BSIZE);
        uint m = min_uint(n - tot, BSIZE - boff);
        memmove(bp->data + boff, p + tot, m);
        bwrite(bp);
        brelse(bp);
        tot += m;
    }

    uint64 end = off + tot;
    if (end > ip->size) {
        ip->size = (uint)end;
    }
    iupdate(ip);
    if (need_unlock) {
        iunlock(ip);
    }
    return (int)tot;
}

int namecmp(const char *s, const char *t) {
    return kstrncmp(s, t, DIRSIZ);
}

struct inode *dirlookup(struct inode *dp, const char *name, uint *poff) {
    int need_unlock = inode_lock_if_needed(dp);
    if (dp->type != T_DIR) {
        if (need_unlock) {
            iunlock(dp);
        }
        return 0;
    }

    struct dirent de;
    for (uint off = 0; off + sizeof(de) <= dp->size; off += sizeof(de)) {
        int n = readi(dp, off, &de, sizeof(de));
        if (n != (int)sizeof(de)) {
            if (need_unlock) {
                iunlock(dp);
            }
            return 0;
        }
        if (de.inum == 0) {
            continue;
        }

        char dename[DIRSIZ + 1];
        memmove(dename, de.name, DIRSIZ);
        dename[DIRSIZ] = 0;
        if (namecmp(name, dename) == 0) {
            if (poff) {
                *poff = off;
            }
            if (need_unlock) {
                iunlock(dp);
            }
            return iget(dp->dev, de.inum);
        }
    }

    if (need_unlock) {
        iunlock(dp);
    }
    return 0;
}

int dirlink(struct inode *dp, const char *name, uint inum) {
    int need_unlock = inode_lock_if_needed(dp);
    if (dp->type != T_DIR) {
        if (need_unlock) {
            iunlock(dp);
        }
        return -1;
    }

    struct inode *ip = dirlookup(dp, name, 0);
    if (ip != 0) {
        iput(ip);
        if (need_unlock) {
            iunlock(dp);
        }
        return -1;
    }

    struct dirent de;
    uint off = 0;
    for (off = 0; off + sizeof(de) <= dp->size; off += sizeof(de)) {
        int n = readi(dp, off, &de, sizeof(de));
        if (n != (int)sizeof(de)) {
            if (need_unlock) {
                iunlock(dp);
            }
            return -1;
        }
        if (de.inum == 0) {
            break;
        }
    }

    memset(&de, 0, sizeof(de));
    de.inum = (ushort)inum;
    memmove(de.name, name, min_uint((uint)DIRSIZ, (uint)kstrlen(name)));
    if (writei(dp, off, &de, sizeof(de)) != (int)sizeof(de)) {
        if (need_unlock) {
            iunlock(dp);
        }
        return -1;
    }
    if (need_unlock) {
        iunlock(dp);
    }
    return 0;
}

static const char *skipelem(const char *path, char *name) {
    while (*path == '/') {
        path++;
    }
    if (*path == 0) {
        return 0;
    }

    const char *s = path;
    while (*path != '/' && *path != 0) {
        path++;
    }
    int len = (int)(path - s);

    if (len >= DIRSIZ) {
        memmove(name, s, DIRSIZ);
        name[DIRSIZ] = 0;
    } else {
        memmove(name, s, (uint)len);
        name[len] = 0;
    }

    while (*path == '/') {
        path++;
    }
    return path;
}

static struct inode *namex(const char *path, int wantparent, char *name) {
    if (path == 0 || path[0] == 0) {
        return 0;
    }

    struct inode *ip = 0;
    if (path[0] == '/') {
        ip = iget((uint)rootdev, ROOTINO);
    } else {
        ip = proc_cwddup();
        if (ip == 0) {
            ip = iget((uint)rootdev, ROOTINO);
        }
    }
    const char *p = path;

    while ((p = skipelem(p, name)) != 0) {
        ilock(ip);
        if (ip->type != T_DIR) {
            iunlockput(ip);
            return 0;
        }

        if (wantparent && *p == 0) {
            iunlock(ip);
            return ip;
        }

        struct inode *next = dirlookup(ip, name, 0);
        iunlockput(ip);
        ip = next;
        if (ip == 0) {
            return 0;
        }
    }

    if (wantparent) {
        iput(ip);
        return 0;
    }
    return ip;
}

struct inode *namei(const char *path) {
    char name[DIRSIZ + 1];
    return namex(path, 0, name);
}

struct inode *nameiparent(const char *path, char *name) {
    return namex(path, 1, name);
}

void fs_read_file(const char *path) {
    if (path == 0) {
        return;
    }

    struct inode *ip = namei(path);
    if (ip == 0) {
        printf("[fs] %s not found\n", (char *)path);
        return;
    }

    ilock(ip);
    if (ip->type != T_FILE) {
        printf("[fs] %s is not a regular file\n", (char *)path);
        iunlockput(ip);
        return;
    }

    uint size = ip->size;
    printf("[fs] Reading %s (%d bytes)\n", (char *)path, size);
    printf("--- File Content Start ---\n");

    uint off = 0;
    uchar chunk[128];
    while (off < size) {
        uint n = min_uint((uint)sizeof(chunk), size - off);
        int rd = readi(ip, off, chunk, n);
        if (rd < 0 || (uint)rd != n) {
            printf("\n[fs] read failed at off=%d\n", off);
            break;
        }
        for (uint i = 0; i < n; i++) {
            uart_putc((char)chunk[i]);
        }
        off += n;
    }

    printf("\n--- File Content End ---\n");
    iunlock(ip);
    iput(ip);
}
