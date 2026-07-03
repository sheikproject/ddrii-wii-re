#!/usr/bin/env python3
import argparse
import csv
import json
from pathlib import Path


TYPE_NAMES = {
    0: "u8/i8",
    1: "u16/i16",
    2: "u32/i32",
    3: "u64/i64",
    4: "float",
    5: "double",
    6: "string",
    7: "bytes",
}


def walk_entries(entries, group=""):
    for entry in entries:
        label = entry.get("label", "")
        next_group = group
        if "groupEntries" in entry:
            next_group = label if not group else f"{group}/{label}"
            yield from walk_entries(entry.get("groupEntries", []), next_group)
            continue
        yield group, entry


def parse_hex_address(value):
    if value is None:
        return None
    text = str(value).strip()
    if not text:
        return None
    return int(text, 16)


def load_dol_sections(csv_path):
    if not csv_path:
        return []
    sections = []
    with Path(csv_path).open(newline="") as f:
        for row in csv.DictReader(f):
            sections.append(
                {
                    "name": row["name"],
                    "file_offset": int(row["file_offset"], 16),
                    "ram_start": int(row["ram_start"], 16),
                    "ram_end": int(row["ram_end"], 16),
                    "size": int(row["size"], 16),
                }
            )
    return sections


def map_section(address, sections):
    for section in sections:
        if section["ram_start"] <= address < section["ram_end"]:
            file_offset = section["file_offset"] + (address - section["ram_start"])
            return section["name"], file_offset
    return "", None


def main():
    parser = argparse.ArgumentParser(description="Export Dolphin Memory Engine watch labels.")
    parser.add_argument("dmw", type=Path)
    parser.add_argument("--dol-sections", type=Path)
    parser.add_argument("--csv", type=Path)
    args = parser.parse_args()

    data = json.loads(args.dmw.read_text(encoding="utf-8"))
    sections = load_dol_sections(args.dol_sections)
    rows = []
    for group, entry in walk_entries(data.get("watchList", [])):
        address = parse_hex_address(entry.get("address"))
        if address is None:
            continue
        section, file_offset = map_section(address, sections)
        rows.append(
            {
                "address": f"0x{address:08X}",
                "label": entry.get("label", ""),
                "group": group,
                "type_index": entry.get("typeIndex", ""),
                "type_guess": TYPE_NAMES.get(entry.get("typeIndex"), ""),
                "unsigned": entry.get("unsigned", ""),
                "section": section,
                "file_offset": "" if file_offset is None else f"0x{file_offset:08X}",
            }
        )

    rows.sort(key=lambda row: int(row["address"], 16))
    print(f"watch_entries: {len(rows)}")
    print(f"inside_dol_sections: {sum(1 for row in rows if row['section'])}")
    print(f"outside_dol_sections: {sum(1 for row in rows if not row['section'])}")
    print()
    for row in rows[:25]:
        location = row["section"] or "runtime"
        print(f"{row['address']} {location:8} {row['label']}")

    if args.csv:
        args.csv.parent.mkdir(parents=True, exist_ok=True)
        with args.csv.open("w", newline="", encoding="utf-8") as f:
            writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()) if rows else [])
            writer.writeheader()
            writer.writerows(rows)


if __name__ == "__main__":
    main()
