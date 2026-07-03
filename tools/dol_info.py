#!/usr/bin/env python3
import argparse
import csv
import hashlib
import struct
from pathlib import Path


def read_u32_be(data, offset):
    return struct.unpack_from(">I", data, offset)[0]


def parse_dol(path):
    data = Path(path).read_bytes()
    text_offsets = [read_u32_be(data, i * 4) for i in range(7)]
    data_offsets = [read_u32_be(data, 0x1C + i * 4) for i in range(11)]
    text_addrs = [read_u32_be(data, 0x48 + i * 4) for i in range(7)]
    data_addrs = [read_u32_be(data, 0x64 + i * 4) for i in range(11)]
    text_sizes = [read_u32_be(data, 0x90 + i * 4) for i in range(7)]
    data_sizes = [read_u32_be(data, 0xAC + i * 4) for i in range(11)]
    bss_addr = read_u32_be(data, 0xD8)
    bss_size = read_u32_be(data, 0xDC)
    entry = read_u32_be(data, 0xE0)

    sections = []
    for index, (off, addr, size) in enumerate(zip(text_offsets, text_addrs, text_sizes)):
        if off and addr and size:
            sections.append(("text", index, off, addr, size))
    for index, (off, addr, size) in enumerate(zip(data_offsets, data_addrs, data_sizes)):
        if off and addr and size:
            sections.append(("data", index, off, addr, size))
    return data, sections, bss_addr, bss_size, entry


def main():
    parser = argparse.ArgumentParser(description="Inspect a GameCube/Wii main.dol layout.")
    parser.add_argument("dol", type=Path)
    parser.add_argument("--csv", type=Path)
    args = parser.parse_args()

    data, sections, bss_addr, bss_size, entry = parse_dol(args.dol)
    digest = hashlib.sha256(data).hexdigest()

    print(f"path: {args.dol}")
    print(f"size: {len(data)} bytes")
    print(f"sha256: {digest}")
    print(f"entry_point: 0x{entry:08X}")
    print(f"bss: 0x{bss_addr:08X}-0x{bss_addr + bss_size:08X} ({bss_size} bytes)")
    print()
    print("sections:")
    print("name,file_offset,ram_start,ram_end,size")
    for kind, index, off, addr, size in sections:
        print(f"{kind}{index},0x{off:08X},0x{addr:08X},0x{addr + size:08X},0x{size:X}")

    if args.csv:
        args.csv.parent.mkdir(parents=True, exist_ok=True)
        with args.csv.open("w", newline="") as f:
            writer = csv.writer(f)
            writer.writerow(["name", "file_offset", "ram_start", "ram_end", "size"])
            for kind, index, off, addr, size in sections:
                writer.writerow(
                    [
                        f"{kind}{index}",
                        f"0x{off:08X}",
                        f"0x{addr:08X}",
                        f"0x{addr + size:08X}",
                        f"0x{size:X}",
                    ]
                )


if __name__ == "__main__":
    main()
