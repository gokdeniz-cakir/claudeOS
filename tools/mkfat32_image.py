#!/usr/bin/env python3
"""
Create a deterministic minimal FAT32 superfloppy image for ClaudeOS Task 27.

Image layout:
- FAT32 boot sector at LBA 0 (no MBR/partition table)
- Root directory contains:
  - HELLO.TXT
  - DOCS/ (directory)
- DOCS contains:
  - INFO.TXT
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path


SECTOR_SIZE = 512
TOTAL_SECTORS = 131072  # 64 MiB
SECTORS_PER_CLUSTER = 1
RESERVED_SECTORS = 32
FAT_COUNT = 2
MEDIA_DESCRIPTOR = 0xF8
ROOT_CLUSTER = 2
FSINFO_SECTOR = 1
BACKUP_BOOT_SECTOR = 6

HELLO_TEXT = b"Hello from ClaudeOS FAT32 via ATA PIO.\n"
INFO_TEXT = b"Subdirectory read path works.\n"


def compute_fat_sectors(total_sectors: int) -> int:
    fat_sectors = 1
    while True:
        data_sectors = total_sectors - RESERVED_SECTORS - FAT_COUNT * fat_sectors
        cluster_count = data_sectors // SECTORS_PER_CLUSTER
        needed_bytes = (cluster_count + 2) * 4
        new_fat_sectors = (needed_bytes + SECTOR_SIZE - 1) // SECTOR_SIZE
        if new_fat_sectors <= fat_sectors:
            return fat_sectors
        fat_sectors = new_fat_sectors


def short_name_83(name: str) -> bytes:
    if name == ".":
        return b".          "
    if name == "..":
        return b"..         "

    upper = name.upper()
    if "." in upper:
        base, ext = upper.rsplit(".", 1)
    else:
        base, ext = upper, ""

    if not (1 <= len(base) <= 8 and len(ext) <= 3):
        raise ValueError(f"Invalid 8.3 name: {name}")

    allowed = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_$~!#%&-{}()@'^`")
    if any(c not in allowed for c in base + ext):
        raise ValueError(f"Unsupported character in 8.3 name: {name}")

    return (base.ljust(8) + ext.ljust(3)).encode("ascii")


def make_dir_entry(name11: bytes, attr: int, first_cluster: int, size: int) -> bytes:
    if len(name11) != 11:
        raise ValueError("name11 must be exactly 11 bytes")

    entry = bytearray(32)
    entry[0:11] = name11
    entry[11] = attr & 0xFF
    struct.pack_into("<H", entry, 20, (first_cluster >> 16) & 0xFFFF)
    struct.pack_into("<H", entry, 26, first_cluster & 0xFFFF)
    struct.pack_into("<I", entry, 28, size & 0xFFFFFFFF)
    return bytes(entry)


def write_sector(image: bytearray, lba: int, data: bytes) -> None:
    if len(data) != SECTOR_SIZE:
        raise ValueError("sector write must be exactly 512 bytes")
    start = lba * SECTOR_SIZE
    image[start : start + SECTOR_SIZE] = data


def write_cluster(
    image: bytearray, data_start_lba: int, cluster: int, cluster_data: bytes
) -> None:
    max_cluster_bytes = SECTOR_SIZE * SECTORS_PER_CLUSTER
    if len(cluster_data) > max_cluster_bytes:
        raise ValueError("cluster data too large")
    lba = data_start_lba + (cluster - 2) * SECTORS_PER_CLUSTER
    padded = cluster_data.ljust(max_cluster_bytes, b"\x00")
    for i in range(SECTORS_PER_CLUSTER):
        write_sector(image, lba + i, padded[i * SECTOR_SIZE : (i + 1) * SECTOR_SIZE])


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: mkfat32_image.py <output.img>", file=sys.stderr)
        return 1

    output = Path(sys.argv[1])
    output.parent.mkdir(parents=True, exist_ok=True)

    fat_sectors = compute_fat_sectors(TOTAL_SECTORS)
    fat_start_lba = RESERVED_SECTORS
    data_start_lba = RESERVED_SECTORS + FAT_COUNT * fat_sectors

    # Cluster assignment
    cluster_docs_dir = 3
    cluster_hello = 4
    cluster_info = 5

    image = bytearray(TOTAL_SECTORS * SECTOR_SIZE)

    # Boot Sector
    boot = bytearray(SECTOR_SIZE)
    boot[0:3] = b"\xEB\x58\x90"
    boot[3:11] = b"CLAUDEOS"
    struct.pack_into("<H", boot, 11, SECTOR_SIZE)
    boot[13] = SECTORS_PER_CLUSTER
    struct.pack_into("<H", boot, 14, RESERVED_SECTORS)
    boot[16] = FAT_COUNT
    struct.pack_into("<H", boot, 17, 0)  # FAT32 root entries
    struct.pack_into("<H", boot, 19, 0)  # use TotSec32
    boot[21] = MEDIA_DESCRIPTOR
    struct.pack_into("<H", boot, 22, 0)  # FAT16 only
    struct.pack_into("<H", boot, 24, 63)
    struct.pack_into("<H", boot, 26, 255)
    struct.pack_into("<I", boot, 28, 0)  # hidden sectors
    struct.pack_into("<I", boot, 32, TOTAL_SECTORS)
    struct.pack_into("<I", boot, 36, fat_sectors)
    struct.pack_into("<H", boot, 40, 0)
    struct.pack_into("<H", boot, 42, 0)
    struct.pack_into("<I", boot, 44, ROOT_CLUSTER)
    struct.pack_into("<H", boot, 48, FSINFO_SECTOR)
    struct.pack_into("<H", boot, 50, BACKUP_BOOT_SECTOR)
    boot[64] = 0x80
    boot[66] = 0x29
    struct.pack_into("<I", boot, 67, 0x1234ABCD)
    boot[71:82] = b"CLAUDEOSVOL"
    boot[82:90] = b"FAT32   "
    struct.pack_into("<H", boot, 510, 0xAA55)
    write_sector(image, 0, boot)

    # FSInfo
    fsinfo = bytearray(SECTOR_SIZE)
    struct.pack_into("<I", fsinfo, 0, 0x41615252)
    struct.pack_into("<I", fsinfo, 484, 0x61417272)
    struct.pack_into("<I", fsinfo, 488, 0xFFFFFFFF)
    struct.pack_into("<I", fsinfo, 492, 0xFFFFFFFF)
    struct.pack_into("<I", fsinfo, 508, 0xAA550000)
    write_sector(image, FSINFO_SECTOR, fsinfo)

    # Backup boot + backup fsinfo
    write_sector(image, BACKUP_BOOT_SECTOR, boot)
    write_sector(image, BACKUP_BOOT_SECTOR + 1, fsinfo)

    # FAT tables
    fat = bytearray(fat_sectors * SECTOR_SIZE)
    fat_entries = {
        0: 0x0FFFFFF8,
        1: 0xFFFFFFFF,
        ROOT_CLUSTER: 0x0FFFFFFF,
        cluster_docs_dir: 0x0FFFFFFF,
        cluster_hello: 0x0FFFFFFF,
        cluster_info: 0x0FFFFFFF,
    }
    for clus, value in fat_entries.items():
        struct.pack_into("<I", fat, clus * 4, value)

    for fat_index in range(FAT_COUNT):
        start_lba = fat_start_lba + fat_index * fat_sectors
        for s in range(fat_sectors):
            write_sector(
                image,
                start_lba + s,
                fat[s * SECTOR_SIZE : (s + 1) * SECTOR_SIZE],
            )

    # Root directory
    root_dir = bytearray(SECTOR_SIZE * SECTORS_PER_CLUSTER)
    root_entries = [
        make_dir_entry(short_name_83("HELLO.TXT"), 0x20, cluster_hello, len(HELLO_TEXT)),
        make_dir_entry(short_name_83("DOCS"), 0x10, cluster_docs_dir, 0),
    ]
    for i, ent in enumerate(root_entries):
        root_dir[i * 32 : (i + 1) * 32] = ent
    root_dir[len(root_entries) * 32] = 0x00
    write_cluster(image, data_start_lba, ROOT_CLUSTER, bytes(root_dir))

    # DOCS directory
    docs_dir = bytearray(SECTOR_SIZE * SECTORS_PER_CLUSTER)
    docs_entries = [
        make_dir_entry(short_name_83("."), 0x10, cluster_docs_dir, 0),
        make_dir_entry(short_name_83(".."), 0x10, ROOT_CLUSTER, 0),
        make_dir_entry(short_name_83("INFO.TXT"), 0x20, cluster_info, len(INFO_TEXT)),
    ]
    for i, ent in enumerate(docs_entries):
        docs_dir[i * 32 : (i + 1) * 32] = ent
    docs_dir[len(docs_entries) * 32] = 0x00
    write_cluster(image, data_start_lba, cluster_docs_dir, bytes(docs_dir))

    # File data
    write_cluster(image, data_start_lba, cluster_hello, HELLO_TEXT)
    write_cluster(image, data_start_lba, cluster_info, INFO_TEXT)

    output.write_bytes(image)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
