#!/usr/bin/env python3
import argparse
import json
from pathlib import Path


def parse_hex_bytes(text):
    cleaned = "".join(str(text).split())
    if len(cleaned) % 2:
        raise ValueError(f"hex string has odd length: {text!r}")
    return bytes.fromhex(cleaned)


def parse_offset(value):
    return int(str(value).strip().removeprefix("0x"), 16)


def apply_patch(data, patch_file):
    patch_doc = json.loads(Path(patch_file).read_text(encoding="utf-8"))
    if not patch_doc.get("enabled", True):
        print(f"skipping disabled patch: {patch_doc.get('name', patch_file)}")
        return data

    for index, patch in enumerate(patch_doc.get("patches", []), start=1):
        offset = parse_offset(patch["file_offset"])
        original = parse_hex_bytes(patch.get("original", ""))
        patched = parse_hex_bytes(patch["patched"])
        if original:
            actual = data[offset : offset + len(original)]
            if actual != original:
                raise RuntimeError(
                    f"{patch_file} patch {index}: original mismatch at 0x{offset:08X}; "
                    f"expected {original.hex().upper()}, found {actual.hex().upper()}"
                )
        if offset + len(patched) > len(data):
            raise RuntimeError(f"{patch_file} patch {index}: patch extends past end of file")
        data[offset : offset + len(patched)] = patched
        print(f"applied {patch_doc.get('name', patch_file)} patch {index} at 0x{offset:08X}")
    return data


def main():
    parser = argparse.ArgumentParser(description="Apply JSON byte patches to a DOL copy.")
    parser.add_argument("input_dol", type=Path)
    parser.add_argument("output_dol", type=Path)
    parser.add_argument("patches", nargs="+", type=Path)
    args = parser.parse_args()

    data = bytearray(args.input_dol.read_bytes())
    for patch_file in args.patches:
        data = apply_patch(data, patch_file)
    args.output_dol.parent.mkdir(parents=True, exist_ok=True)
    args.output_dol.write_bytes(data)
    print(f"wrote {args.output_dol} ({len(data)} bytes)")


if __name__ == "__main__":
    main()
