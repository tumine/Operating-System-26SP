#ifndef __ELF_H__
#define __ELF_H__

#include "types.h"

// Minimal ELF64 definitions (enough for loading a statically-linked user program).

#define ELF_MAGIC 0x464c457fU // "\x7fELF" in little endian

// Program header types.
#define PT_LOAD 1

// Program header flags.
#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

// ELF64 header.
struct elfhdr {
    uint32 magic;     // must be ELF_MAGIC
    uint8  elf[12];   // rest of e_ident[]
    uint16 type;
    uint16 machine;
    uint32 version;
    uint64 entry;
    uint64 phoff;
    uint64 shoff;
    uint32 flags;
    uint16 ehsize;
    uint16 phentsize;
    uint16 phnum;
    uint16 shentsize;
    uint16 shnum;
    uint16 shstrndx;
};

// ELF64 program header.
struct proghdr {
    uint32 type;
    uint32 flags;
    uint64 off;
    uint64 vaddr;
    uint64 paddr;
    uint64 filesz;
    uint64 memsz;
    uint64 align;
};

#endif

