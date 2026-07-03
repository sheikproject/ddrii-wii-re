#!/usr/bin/env python3
import argparse
import csv
from pathlib import Path


def parse_int(text):
    return int(str(text).strip().removeprefix("0x"), 16)


def load_sections(path):
    rows = []
    with Path(path).open(newline="") as f:
        for row in csv.DictReader(f):
            rows.append(
                {
                    "name": row["name"],
                    "file_offset": parse_int(row["file_offset"]),
                    "ram_start": parse_int(row["ram_start"]),
                    "ram_end": parse_int(row["ram_end"]),
                    "size": parse_int(row["size"]),
                }
            )
    return rows


def ram_to_file(sections, address):
    for row in sections:
        if row["ram_start"] <= address < row["ram_end"]:
            return row, row["file_offset"] + address - row["ram_start"]
    return None, None


def file_to_ram(sections, offset):
    for row in sections:
        start = row["file_offset"]
        end = start + row["size"]
        if start <= offset < end:
            return row, row["ram_start"] + offset - start
    return None, None


def main():
    parser = argparse.ArgumentParser(description="Convert DOL RAM addresses and file offsets.")
    parser.add_argument("value", help="RAM address or file offset, e.g. 0x80006310")
    parser.add_argument("--sections", default="outputs/dol_sections.csv")
    parser.add_argument("--file-offset", action="store_true", help="Treat value as a file offset.")
    args = parser.parse_args()

    sections = load_sections(args.sections)
    value = parse_int(args.value)
    if args.file_offset:
        section, address = file_to_ram(sections, value)
        if section:
            print(f"file 0x{value:08X} -> RAM 0x{address:08X} ({section['name']})")
        else:
            print(f"file 0x{value:08X} is not inside a DOL section")
    else:
        section, offset = ram_to_file(sections, value)
        if section:
            print(f"RAM 0x{value:08X} -> file 0x{offset:08X} ({section['name']})")
        else:
            print(f"RAM 0x{value:08X} is not inside a DOL section")


if __name__ == "__main__":
    main()
