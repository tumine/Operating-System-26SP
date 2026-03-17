#ifndef __STAT_H__
#define __STAT_H__

#include "types.h"

struct stat {
    int dev;
    uint ino;
    short type;
    short nlink;
    uint64 size;
};

#endif
