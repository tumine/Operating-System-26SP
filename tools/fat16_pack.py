#!/usr/bin/env python3
"""
Pack regular files into the root directory of a FAT16 image.

The script is intentionally simple: it supports only 8.3 names and
root-directory files (no subdirectories), which is enough for loading
`/init` and simple test binaries in this project.
"""

from __future__ import annotations

import argparse
import os
import struct
import subprocess
import sys
from dataclasses import dataclass


FAT16_EOC = 0xFFF8


@dataclass
class BPB:
    byts_per_sec: int
    sec_per_clus: int
    rsvd_sec_cnt: int
    num_fats: int
    root_ent_cnt: int
    tot_sec: int
    fat_sz: int
    root_dir_sectors: int
    root_start_sector: int
    first_data_sector: int
    data_clusters: int


def parse_bpb(img: bytes) -> BPB:
    if len(img) < 512:
        raise ValueError("image too small")

    byts_per_sec = struct.unpack_from("<H", img, 11)[0]
    sec_per_clus = img[13]
    rsvd_sec_cnt = struct.unpack_from("<H", img, 14)[0]
    num_fats = img[16]
    root_ent_cnt = struct.unpack_from("<H", img, 17)[0]
    tot16 = struct.unpack_from("<H", img, 19)[0]
    fat_sz = struct.unpack_from("<H", img, 22)[0]
    tot32 = struct.unpack_from("<I", img, 32)[0]
    tot_sec = tot16 if tot16 != 0 else tot32

    if byts_per_sec != 512:
        raise ValueError(f"unsupported bytes/sector: {byts_per_sec}")
    if sec_per_clus == 0 or fat_sz == 0:
        raise ValueError("invalid FAT16 geometry")

    root_dir_sectors = (root_ent_cnt * 32 + byts_per_sec - 1) // byts_per_sec
    root_start_sector = rsvd_sec_cnt + num_fats * fat_sz
    first_data_sector = root_start_sector + root_dir_sectors
    data_sectors = tot_sec - first_data_sector
    data_clusters = data_sectors // sec_per_clus

    if data_clusters < 1:
        raise ValueError("no data clusters")

    return BPB(
        byts_per_sec=byts_per_sec,
        sec_per_clus=sec_per_clus,
        rsvd_sec_cnt=rsvd_sec_cnt,
        num_fats=num_fats,
        root_ent_cnt=root_ent_cnt,
        tot_sec=tot_sec,
        fat_sz=fat_sz,
        root_dir_sectors=root_dir_sectors,
        root_start_sector=root_start_sector,
        first_data_sector=first_data_sector,
        data_clusters=data_clusters,
    )


def format_83(name: str) -> bytes:
    n = name.strip().strip("/")
    if not n or "/" in n:
        raise ValueError(f"unsupported path '{name}' (root files only)")
    n = n.upper()

    if "." in n:
        base, ext = n.split(".", 1)
    else:
        base, ext = n, ""

    if not base or len(base) > 8 or len(ext) > 3:
        raise ValueError(f"name '{name}' does not fit 8.3")

    # Keep charset permissive but deterministic.
    for ch in base + ext:
        if ord(ch) < 0x21 or ord(ch) > 0x7E:
            raise ValueError(f"invalid character '{ch}' in '{name}'")

    return (base.ljust(8) + ext.ljust(3)).encode("ascii")


def cluster_to_offset(bpb: BPB, cluster: int) -> int:
    sector = bpb.first_data_sector + (cluster - 2) * bpb.sec_per_clus
    return sector * bpb.byts_per_sec


def is_eoc(v: int) -> bool:
    return v >= FAT16_EOC


def load_fat(img: bytearray, bpb: BPB) -> list[int]:
    fat_off = bpb.rsvd_sec_cnt * bpb.byts_per_sec
    fat_len = bpb.fat_sz * bpb.byts_per_sec
    fat_raw = img[fat_off : fat_off + fat_len]
    entries = fat_len // 2
    return [struct.unpack_from("<H", fat_raw, i * 2)[0] for i in range(entries)]


def store_fat(img: bytearray, bpb: BPB, fat: list[int]) -> None:
    fat_len = bpb.fat_sz * bpb.byts_per_sec
    fat_raw = bytearray(fat_len)
    entries = fat_len // 2
    for i in range(min(entries, len(fat))):
        struct.pack_into("<H", fat_raw, i * 2, fat[i] & 0xFFFF)

    for n in range(bpb.num_fats):
        off = (bpb.rsvd_sec_cnt + n * bpb.fat_sz) * bpb.byts_per_sec
        img[off : off + fat_len] = fat_raw


def root_offset_and_size(bpb: BPB) -> tuple[int, int]:
    off = bpb.root_start_sector * bpb.byts_per_sec
    size = bpb.root_dir_sectors * bpb.byts_per_sec
    return off, size


def find_root_entry(root: bytearray, name83: bytes) -> tuple[int | None, int | None]:
    first_free = None
    for i in range(len(root) // 32):
        e = root[i * 32 : (i + 1) * 32]
        first = e[0]
        if first in (0x00, 0xE5):
            if first_free is None:
                first_free = i
            if first == 0x00:
                # End marker, but first_free is usable.
                continue
            continue
        if e[11] == 0x0F:
            continue
        if bytes(e[:11]) == name83:
            return i, first_free
    return None, first_free


def free_chain(fat: list[int], first_cluster: int) -> None:
    c = first_cluster
    visited = 0
    while c >= 2 and c < len(fat) and not is_eoc(c):
        nxt = fat[c]
        fat[c] = 0
        c = nxt
        visited += 1
        if visited > len(fat):
            raise ValueError("corrupted FAT chain (loop)")
    if c >= 2 and c < len(fat):
        fat[c] = 0


def alloc_clusters(fat: list[int], count: int, max_cluster: int) -> list[int]:
    if count == 0:
        return []
    free = []
    for c in range(2, max_cluster + 2):
        if c < len(fat) and fat[c] == 0:
            free.append(c)
            if len(free) == count:
                break
    if len(free) != count:
        raise ValueError("not enough free clusters in image")

    for i, c in enumerate(free):
        fat[c] = 0xFFFF if i == len(free) - 1 else free[i + 1]
    return free


def write_file_into_image(img: bytearray, bpb: BPB, fat: list[int], root: bytearray,
                          target_name: str, host_path: str) -> None:
    name83 = format_83(target_name)
    with open(host_path, "rb") as f:
        data = f.read()

    existing_idx, first_free = find_root_entry(root, name83)
    if existing_idx is None and first_free is None:
        raise ValueError("root directory is full")
    idx = existing_idx if existing_idx is not None else first_free
    assert idx is not None

    old_entry = root[idx * 32 : (idx + 1) * 32]
    old_cluster = struct.unpack_from("<H", old_entry, 26)[0]
    if old_cluster >= 2:
        free_chain(fat, old_cluster)

    clus_bytes = bpb.sec_per_clus * bpb.byts_per_sec
    nclus = (len(data) + clus_bytes - 1) // clus_bytes
    clusters = alloc_clusters(fat, nclus, bpb.data_clusters)

    for i, c in enumerate(clusters):
        off = cluster_to_offset(bpb, c)
        chunk = data[i * clus_bytes : (i + 1) * clus_bytes]
        img[off : off + len(chunk)] = chunk
        if len(chunk) < clus_bytes:
            img[off + len(chunk) : off + clus_bytes] = b"\x00" * (clus_bytes - len(chunk))

    entry = bytearray(32)
    entry[:11] = name83
    entry[11] = 0x20  # archive
    first_cluster = clusters[0] if clusters else 0
    struct.pack_into("<H", entry, 26, first_cluster)
    struct.pack_into("<I", entry, 28, len(data))
    root[idx * 32 : (idx + 1) * 32] = entry

    print(f"packed {host_path} -> {target_name} ({len(data)} bytes, cluster {first_cluster})")


def ensure_image(image: str, size_mib: int) -> None:
    if os.path.exists(image):
        return
    if size_mib <= 0:
        raise ValueError("image does not exist and size is invalid")
    size_kib = size_mib * 1024
    subprocess.run(
        ["mkfs.fat", "-F", "16", "-C", image, str(size_kib)],
        check=True,
    )


def main() -> int:
    ap = argparse.ArgumentParser(description="Pack files into FAT16 root directory")
    ap.add_argument("--image", required=True, help="FAT16 image path")
    ap.add_argument(
        "--add",
        action="append",
        default=[],
        metavar="TARGET=HOST",
        help="add/update file in root dir",
    )
    ap.add_argument("--create-size-mib", type=int, default=32,
                    help="create image if missing (MiB)")
    args = ap.parse_args()

    if not args.add:
        print("nothing to do: no --add entries")
        return 0

    ensure_image(args.image, args.create_size_mib)

    with open(args.image, "rb") as f:
        img = bytearray(f.read())

    bpb = parse_bpb(img)
    fat = load_fat(img, bpb)
    root_off, root_sz = root_offset_and_size(bpb)
    root = bytearray(img[root_off : root_off + root_sz])

    for spec in args.add:
        if "=" not in spec:
            raise ValueError(f"bad --add value '{spec}', expected TARGET=HOST")
        target, host = spec.split("=", 1)
        write_file_into_image(img, bpb, fat, root, target, host)

    img[root_off : root_off + root_sz] = root
    store_fat(img, bpb, fat)

    with open(args.image, "wb") as f:
        f.write(img)

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as e:
        print(f"fat16_pack.py: {e}", file=sys.stderr)
        raise
