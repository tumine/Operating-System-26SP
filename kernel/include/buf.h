#ifndef __BUF_H__
#define __BUF_H__

#include "types.h"
#include "sleeplock.h"

#define BSIZE 512  // 块大小

struct buf {
    int valid;    // 数据是否已从磁盘读入
    int disk;     // 是否已被驱动程序拥有
    uint dev;
    uint blockno; // 扇区号
    struct sleeplock lock;
    uint refcnt;
    struct buf *prev;
    struct buf *next;
    uchar data[BSIZE];
};

void binit(void);
struct buf* bread(uint dev, uint blockno);
void bwrite(struct buf* b);
void brelse(struct buf* b);

#endif
