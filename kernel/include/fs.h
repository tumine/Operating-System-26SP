#ifndef __FS_H__
#define __FS_H__

#include "types.h"
#include "buf.h"

// Filesystem on-disk format.
#define FSMAGIC 0x10203040

#define ROOTINO 1
#define DIRSIZ 14
#define MAXPATH 128

#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint))
#define MAXFILE (NDIRECT + NINDIRECT)

#define T_DIR 1
#define T_FILE 2
#define T_DEVICE 3

struct superblock {
    uint magic;      // Must be FSMAGIC
    uint size;       // Total blocks in image
    uint nblocks;    // Data blocks
    uint ninodes;    // Number of inodes
    uint nlog;       // Log blocks (unused here)
    uint logstart;   // First log block
    uint inodestart; // First inode block
    uint bmapstart;  // First free map block
};

struct dinode {
    short type;
    short major;
    short minor;
    short nlink;
    uint size;
    uint addrs[NDIRECT + 1];
};

struct dirent {
    ushort inum;
    char name[DIRSIZ];
};

#define IPB (BSIZE / sizeof(struct dinode))
#define BPB (BSIZE * 8)
#define IBLOCK(i, sb) ((i) / IPB + (sb).inodestart)
#define BBLOCK(b, sb) ((b) / BPB + (sb).bmapstart)

#endif
