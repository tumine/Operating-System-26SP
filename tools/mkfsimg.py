#!/usr/bin/env python3
"""Create a filesystem image and pack files into the root directory."""

from __future__ import annotations

import argparse
import os
import struct
import sys
from dataclasses import dataclass

BSIZE = 512
FSMAGIC = 0x10203040
ROOTINO = 1
DIRSIZ = 14
NDIRECT = 12
NINDIRECT = BSIZE // 4
MAXFILE = NDIRECT + NINDIRECT
T_DIR = 1
T_FILE = 2

DINODE_FMT = "<hhhhI13I"
DINODE_SIZE = struct.calcsize(DINODE_FMT)
IPB = BSIZE // DINODE_SIZE
BPB = BSIZE * 8

SUPER_FMT = "<8I"


@dataclass
class Dinode:
    type: int = 0
    major: int = 0
    minor: int = 0
    nlink: int = 0
    size: int = 0
    addrs: list[int] | None = None

    def __post_init__(self) -> None:
        if self.addrs is None:
            self.addrs = [0] * (NDIRECT + 1)


def pack_dinode(di: Dinode) -> bytes:
    return struct.pack(
        DINODE_FMT,
        di.type,
        di.major,
        di.minor,
        di.nlink,
        di.size,
        *di.addrs,
    )


def unpack_dinode(raw: bytes) -> Dinode:
    vals = struct.unpack(DINODE_FMT, raw)
    return Dinode(
        type=vals[0],
        major=vals[1],
        minor=vals[2],
        nlink=vals[3],
        size=vals[4],
        addrs=list(vals[5:]),
    )


class Mkfs:
    def __init__(self, size_blocks: int, ninodes: int, nlog: int) -> None:
        if size_blocks <= 0:
            raise ValueError("size_blocks must be > 0")
        if ninodes < ROOTINO + 1:
            raise ValueError("ninodes too small")
        if nlog < 0:
            raise ValueError("nlog must be >= 0")

        self.size = size_blocks
        self.ninodes = ninodes
        self.nlog = nlog

        self.ninodeblocks = (ninodes + IPB - 1) // IPB
        self.logstart = 2
        self.inodestart = self.logstart + nlog
        self.nbitmap = (size_blocks + BPB - 1) // BPB
        self.bmapstart = self.inodestart + self.ninodeblocks
        self.datastart = self.bmapstart + self.nbitmap

        if self.datastart >= size_blocks:
            raise ValueError("filesystem metadata consumes entire image")

        self.nblocks = size_blocks - self.datastart
        self.img = bytearray(size_blocks * BSIZE)

        self.freeblock = self.datastart
        self.next_inum = ROOTINO

    def inode_pos(self, inum: int) -> int:
        if inum < 0 or inum >= self.ninodes:
            raise ValueError(f"bad inode number: {inum}")
        blockno = self.inodestart + (inum // IPB)
        off = (inum % IPB) * DINODE_SIZE
        return blockno * BSIZE + off

    def read_dinode(self, inum: int) -> Dinode:
        pos = self.inode_pos(inum)
        raw = bytes(self.img[pos : pos + DINODE_SIZE])
        return unpack_dinode(raw)

    def write_dinode(self, inum: int, di: Dinode) -> None:
        pos = self.inode_pos(inum)
        self.img[pos : pos + DINODE_SIZE] = pack_dinode(di)

    def alloc_inode(self, typ: int) -> int:
        if self.next_inum >= self.ninodes:
            raise ValueError("out of inodes")
        inum = self.next_inum
        self.next_inum += 1
        di = Dinode(type=typ, nlink=1)
        self.write_dinode(inum, di)
        return inum

    def alloc_block(self) -> int:
        if self.freeblock >= self.size:
            raise ValueError("out of data blocks")
        bno = self.freeblock
        self.freeblock += 1
        start = bno * BSIZE
        self.img[start : start + BSIZE] = b"\x00" * BSIZE
        return bno

    def read_indirect(self, bno: int) -> list[int]:
        start = bno * BSIZE
        raw = self.img[start : start + BSIZE]
        return list(struct.unpack("<" + "I" * NINDIRECT, raw))

    def write_indirect(self, bno: int, arr: list[int]) -> None:
        if len(arr) != NINDIRECT:
            raise ValueError("indirect array size mismatch")
        start = bno * BSIZE
        self.img[start : start + BSIZE] = struct.pack("<" + "I" * NINDIRECT, *arr)

    def iappend(self, inum: int, data: bytes) -> None:
        if not data:
            return

        di = self.read_dinode(inum)
        off = di.size
        idx = 0

        while idx < len(data):
            fbn = off // BSIZE
            if fbn >= MAXFILE:
                raise ValueError("file too large for filesystem format")

            if fbn < NDIRECT:
                if di.addrs[fbn] == 0:
                    di.addrs[fbn] = self.alloc_block()
                bno = di.addrs[fbn]
            else:
                ind_idx = fbn - NDIRECT
                if di.addrs[NDIRECT] == 0:
                    di.addrs[NDIRECT] = self.alloc_block()
                ibno = di.addrs[NDIRECT]
                indirect = self.read_indirect(ibno)
                if indirect[ind_idx] == 0:
                    indirect[ind_idx] = self.alloc_block()
                    self.write_indirect(ibno, indirect)
                bno = indirect[ind_idx]

            n = min(len(data) - idx, BSIZE - (off % BSIZE))
            dst = bno * BSIZE + (off % BSIZE)
            self.img[dst : dst + n] = data[idx : idx + n]
            off += n
            idx += n

        di.size = off
        self.write_dinode(inum, di)

    def dirent_bytes(self, inum: int, name: str) -> bytes:
        if "/" in name:
            raise ValueError(f"directory entry '{name}' must not contain '/'")
        raw = name.encode("ascii")
        if len(raw) == 0:
            raise ValueError("directory entry name is empty")
        if len(raw) > DIRSIZ:
            raise ValueError(f"'{name}' exceeds DIRSIZ={DIRSIZ}")
        return struct.pack("<H14s", inum, raw.ljust(DIRSIZ, b"\x00"))

    def dirlink(self, dir_inum: int, child_inum: int, name: str) -> None:
        self.iappend(dir_inum, self.dirent_bytes(child_inum, name))

    def add_host_file(self, root_inum: int, target_name: str, host_path: str) -> None:
        with open(host_path, "rb") as f:
            data = f.read()

        inum = self.alloc_inode(T_FILE)
        self.iappend(inum, data)
        self.dirlink(root_inum, inum, target_name)
        print(f"packed {host_path} -> {target_name} (inode={inum}, bytes={len(data)})")

    def add_text_file(self, root_inum: int, target_name: str, content: bytes) -> None:
        inum = self.alloc_inode(T_FILE)
        self.iappend(inum, content)
        self.dirlink(root_inum, inum, target_name)
        print(f"packed inline -> {target_name} (inode={inum}, bytes={len(content)})")

    def write_superblock(self) -> None:
        sb = struct.pack(
            SUPER_FMT,
            FSMAGIC,
            self.size,
            self.nblocks,
            self.ninodes,
            self.nlog,
            self.logstart,
            self.inodestart,
            self.bmapstart,
        )
        start = BSIZE
        self.img[start : start + len(sb)] = sb

    def write_bitmap(self) -> None:
        # Bitmap marks allocated absolute block numbers.
        for b in range(self.freeblock):
            byte_index = b // 8
            bit = b % 8
            bmap_block = self.bmapstart + (byte_index // BSIZE)
            bmap_off = byte_index % BSIZE
            pos = bmap_block * BSIZE + bmap_off
            self.img[pos] |= 1 << bit

    def emit(self, image_path: str) -> None:
        self.write_superblock()
        self.write_bitmap()
        with open(image_path, "wb") as f:
            f.write(self.img)


def parse_add(spec: str) -> tuple[str, str]:
    if "=" not in spec:
        raise ValueError(f"bad --add value '{spec}', expected NAME=PATH")
    name, path = spec.split("=", 1)
    name = name.strip().lstrip("/")
    path = path.strip()
    if not name:
        raise ValueError("target file name is empty")
    if not path:
        raise ValueError("host file path is empty")
    return name, path


def main() -> int:
    ap = argparse.ArgumentParser(description="Build filesystem image")
    ap.add_argument("--image", required=True, help="output image path")
    ap.add_argument("--size-blocks", type=int, default=65536, help="filesystem size in 512-byte blocks")
    ap.add_argument("--ninodes", type=int, default=1024, help="inode count")
    ap.add_argument("--nlog", type=int, default=0, help="log block count")
    ap.add_argument("--add", action="append", default=[], metavar="NAME=HOST_PATH", help="pack host file as NAME")
    args = ap.parse_args()

    mk = Mkfs(size_blocks=args.size_blocks, ninodes=args.ninodes, nlog=args.nlog)

    rootino = mk.alloc_inode(T_DIR)
    if rootino != ROOTINO:
        raise ValueError("root inode allocation mismatch")

    root = mk.read_dinode(rootino)
    root.nlink = 2
    mk.write_dinode(rootino, root)

    mk.dirlink(rootino, rootino, ".")
    mk.dirlink(rootino, rootino, "..")

    for spec in args.add:
        target, host = parse_add(spec)
        if not os.path.exists(host):
            raise FileNotFoundError(host)
        mk.add_host_file(rootino, target, host)

    mk.add_text_file(rootino, "TEST.TXT", b"Hello from File System")

    mk.emit(args.image)
    print(
        "wrote {} (blocks={}, ninodes={}, data_start={}, used_blocks={})".format(
            args.image,
            mk.size,
            mk.ninodes,
            mk.datastart,
            mk.freeblock,
        )
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as e:
        print(f"mkfsimg.py: {e}", file=sys.stderr)
        raise
