import argparse
import json
from pathlib import Path


def read_be32(data, offset):
    return int.from_bytes(data[offset:offset + 4], "big")


def describe_resource(data, base_offset=0, max_depth=2):
    if base_offset + 0x10 > len(data) or data[base_offset:base_offset + 4] != b"WII\x00":
        return {
            "offset": f"0x{base_offset:X}",
            "valid": False,
        }

    block_count = read_be32(data, base_offset + 8)
    flags = read_be32(data, base_offset + 0xC)
    blocks = []
    for i in range(block_count):
        entry = base_offset + 0x10 + i * 8
        block_offset = read_be32(data, entry)
        block_size = read_be32(data, entry + 4)
        absolute = base_offset + block_offset
        block = {
            "index": i,
            "offset": f"0x{block_offset:X}",
            "absolute_offset": f"0x{absolute:X}",
            "size": f"0x{block_size:X}",
            "size_decimal": block_size,
        }
        if absolute + 4 <= len(data):
            block["magic"] = data[absolute:absolute + 4].decode("latin1", errors="replace")
        if max_depth > 0 and absolute + 0x10 <= len(data) and data[absolute:absolute + 4] == b"WII\x00":
            block["nested"] = describe_resource(data, absolute, max_depth - 1)
        blocks.append(block)

    return {
        "offset": f"0x{base_offset:X}",
        "valid": True,
        "magic": "WII",
        "block_count": block_count,
        "flags": f"0x{flags:X}",
        "blocks": blocks,
    }


def main():
    parser = argparse.ArgumentParser(description="Dump DDRII WII resource block layout.")
    parser.add_argument("path", type=Path)
    parser.add_argument("--offset", default="0", help="base offset, decimal or hex")
    parser.add_argument("--max-depth", type=int, default=2)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    data = args.path.read_bytes()
    base_offset = int(args.offset, 0)
    result = {
        "path": str(args.path),
        "size": len(data),
        "resource": describe_resource(data, base_offset, args.max_depth),
    }

    if args.json:
        print(json.dumps(result, indent=2))
        return

    print(f"{args.path} size=0x{len(data):X} ({len(data)})")
    resource = result["resource"]
    if not resource["valid"]:
        print(f"not a WII resource at offset {resource['offset']}")
        return
    print(f"WII offset={resource['offset']} blocks={resource['block_count']} flags={resource['flags']}")
    for block in resource["blocks"]:
        print(
            f"  [{block['index']:02}] off={block['offset']} abs={block['absolute_offset']} "
            f"size={block['size']} magic={block.get('magic', '')!r}"
        )
        nested = block.get("nested")
        if nested and nested.get("valid"):
            print(f"       nested blocks={nested['block_count']} flags={nested['flags']}")


if __name__ == "__main__":
    main()
