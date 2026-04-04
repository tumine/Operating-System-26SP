#!/usr/bin/env python3
"""Create a filesystem image and pack files into the root directory."""

from __future__ import annotations

import argparse
import os
import struct
import sys
from dataclasses import dataclass

BSIZE = 16384
FSMAGIC = 0x10203040
ROOTINO = 1
DIRSIZ = 14
NDIRECT = 12
NINDIRECT = BSIZE // 4
NDINDIRECT = NINDIRECT * NINDIRECT
NTINDIRECT = NDINDIRECT * NINDIRECT
SINDIRECT = NDIRECT
DINDIRECT = NDIRECT + 1
TINDIRECT = NDIRECT + 2
NADDRS = NDIRECT + 3
MAXFILE = NDIRECT + NINDIRECT + NDINDIRECT + NTINDIRECT
T_DIR = 1
T_FILE = 2

DINODE_FMT = "<hhhhI15I"
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
            self.addrs = [0] * NADDRS


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
        self.rootino = 0
        self.path_inums: dict[str, int] = {}

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
            elif fbn < NDIRECT + NINDIRECT:
                ind_idx = fbn - NDIRECT
                if di.addrs[SINDIRECT] == 0:
                    di.addrs[SINDIRECT] = self.alloc_block()
                ibno = di.addrs[SINDIRECT]
                indirect = self.read_indirect(ibno)
                if indirect[ind_idx] == 0:
                    indirect[ind_idx] = self.alloc_block()
                    self.write_indirect(ibno, indirect)
                bno = indirect[ind_idx]
            elif fbn < NDIRECT + NINDIRECT + NDINDIRECT:
                rem = fbn - NDIRECT - NINDIRECT
                i1 = rem // NINDIRECT
                i2 = rem % NINDIRECT
                if di.addrs[DINDIRECT] == 0:
                    di.addrs[DINDIRECT] = self.alloc_block()
                l1b = di.addrs[DINDIRECT]
                l1 = self.read_indirect(l1b)
                if l1[i1] == 0:
                    l1[i1] = self.alloc_block()
                    self.write_indirect(l1b, l1)
                l2b = l1[i1]
                l2 = self.read_indirect(l2b)
                if l2[i2] == 0:
                    l2[i2] = self.alloc_block()
                    self.write_indirect(l2b, l2)
                bno = l2[i2]
            else:
                rem = fbn - NDIRECT - NINDIRECT - NDINDIRECT
                i1 = rem // NDINDIRECT
                rem2 = rem % NDINDIRECT
                i2 = rem2 // NINDIRECT
                i3 = rem2 % NINDIRECT
                if di.addrs[TINDIRECT] == 0:
                    di.addrs[TINDIRECT] = self.alloc_block()
                l1b = di.addrs[TINDIRECT]
                l1 = self.read_indirect(l1b)
                if l1[i1] == 0:
                    l1[i1] = self.alloc_block()
                    self.write_indirect(l1b, l1)
                l2b = l1[i1]
                l2 = self.read_indirect(l2b)
                if l2[i2] == 0:
                    l2[i2] = self.alloc_block()
                    self.write_indirect(l2b, l2)
                l3b = l2[i2]
                l3 = self.read_indirect(l3b)
                if l3[i3] == 0:
                    l3[i3] = self.alloc_block()
                    self.write_indirect(l3b, l3)
                bno = l3[i3]

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

    def ensure_dir(self, target_dir: str) -> int:
        target_dir = target_dir.strip().strip("/")
        if target_dir == "":
            return self.rootino

        cached = self.path_inums.get(target_dir)
        if cached is not None:
            return cached

        if "/" in target_dir:
            parent_dir, name = target_dir.rsplit("/", 1)
        else:
            parent_dir, name = "", target_dir
        parent_inum = self.ensure_dir(parent_dir)

        inum = self.alloc_inode(T_DIR)
        di = self.read_dinode(inum)
        di.nlink = 2
        self.write_dinode(inum, di)

        self.dirlink(inum, inum, ".")
        self.dirlink(inum, parent_inum, "..")
        self.dirlink(parent_inum, inum, name)

        parent = self.read_dinode(parent_inum)
        parent.nlink += 1
        self.write_dinode(parent_inum, parent)

        self.path_inums[target_dir] = inum
        print(f"created dir {target_dir} (inode={inum})")
        return inum

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

    def add_host_path(self, target_path: str, host_path: str) -> None:
        target_path = target_path.strip().strip("/")
        if not target_path:
            raise ValueError("target file path is empty")
        if "/" in target_path:
            parent_dir, name = target_path.rsplit("/", 1)
        else:
            parent_dir, name = "", target_path
        dir_inum = self.ensure_dir(parent_dir)
        self.add_host_file(dir_inum, name, host_path)

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


def parse_add_dir(spec: str) -> list[tuple[str, str]]:
    target_prefix = ""
    dir_path = spec
    if "=" in spec:
        target_prefix, dir_path = spec.split("=", 1)
        target_prefix = target_prefix.strip().strip("/")
    dir_path = dir_path.strip()

    adds: list[tuple[str, str]] = []
    for root, dirs, files in os.walk(dir_path):
        dirs.sort()
        files.sort()
        rel = os.path.relpath(root, dir_path)
        if rel == ".":
            rel = ""
        else:
            rel = rel.replace(os.sep, "/")
        for entry in files:
            host = os.path.join(root, entry)
            parts = [target_prefix, rel, entry]
            target = "/".join(part for part in parts if part)
            adds.append((target, host))
    return adds


def main() -> int:
    ap = argparse.ArgumentParser(description="Build filesystem image")
    ap.add_argument("--image", required=True, help="output image path")
    ap.add_argument("--size-blocks", type=int, default=2048, help="filesystem size in filesystem blocks")
    ap.add_argument("--ninodes", type=int, default=1024, help="inode count")
    ap.add_argument("--nlog", type=int, default=0, help="log block count")
    ap.add_argument("--add", action="append", default=[], metavar="NAME=HOST_PATH", help="pack host file as NAME")
    ap.add_argument(
        "--add-dir",
        action="append",
        default=[],
        metavar="[TARGET=]DIR",
        help="pack all regular files from DIR, optionally under TARGET/",
    )
    args = ap.parse_args()

    mk = Mkfs(size_blocks=args.size_blocks, ninodes=args.ninodes, nlog=args.nlog)

    rootino = mk.alloc_inode(T_DIR)
    if rootino != ROOTINO:
        raise ValueError("root inode allocation mismatch")
    mk.rootino = rootino
    mk.path_inums[""] = rootino

    root = mk.read_dinode(rootino)
    root.nlink = 2
    mk.write_dinode(rootino, root)

    mk.dirlink(rootino, rootino, ".")
    mk.dirlink(rootino, rootino, "..")

    adds = [parse_add(spec) for spec in args.add]
    for dir_path in args.add_dir:
        adds.extend(parse_add_dir(dir_path))

    for target, host in adds:
        if not os.path.exists(host):
            raise FileNotFoundError(host)
        mk.add_host_path(target, host)

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
