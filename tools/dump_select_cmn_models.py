import argparse
from pathlib import Path


def be16(data, offset):
    if offset < 0 or offset + 2 > len(data):
        return None
    return int.from_bytes(data[offset:offset + 2], "big")


def be32(data, offset):
    if offset < 0 or offset + 4 > len(data):
        return None
    return int.from_bytes(data[offset:offset + 4], "big")


def as_s32(value):
    if value is None:
        return None
    return value - 0x100000000 if value & 0x80000000 else value


class WiiResource:
    def __init__(self, data, base, label):
        self.data = data
        self.base = base
        self.label = label
        self.valid = base + 0x10 <= len(data) and data[base:base + 4] == b"WII\x00"
        self.count = be32(data, base + 8) if self.valid else 0
        self.flags = be32(data, base + 0xC) if self.valid else 0

    def block(self, index):
        if not self.valid or index < 0 or index >= self.count:
            return None
        entry = self.base + 0x10 + index * 8
        rel = be32(self.data, entry)
        size = be32(self.data, entry + 4)
        if rel is None or size is None:
            return None
        return {
            "index": index,
            "rel": rel,
            "base": self.base + rel,
            "size": size,
            "end": self.base + rel + size,
            "magic": self.data[self.base + rel:self.base + rel + 4],
        }

    def blocks(self):
        for i in range(self.count):
            block = self.block(i)
            if block is not None:
                yield block


def in_block(block, rel):
    return rel is not None and 0 <= rel < block["size"]


def describe_model_block(data, block):
    base = block["base"]
    size = block["size"]
    raw = {
        "texture_frame_table": be32(data, base + 0x18),
        "material_table": be32(data, base + 0x1C),
        "object_table": be32(data, base + 0x20),
        "relocated_flag": be32(data, base + 0x24),
    }
    lines = [
        "      model raw +18/+1C/+20/+24 = "
        f"{fmt(raw['texture_frame_table'])}, {fmt(raw['material_table'])}, "
        f"{fmt(raw['object_table'])}, {fmt(raw['relocated_flag'])}"
    ]

    object_table = raw["object_table"]
    if in_block(block, object_table):
        header = base + object_table
        count = be32(data, header)
        word1 = be32(data, header + 4)
        entries = be32(data, header + 8)
        lines.append(
            "      object table candidate: "
            f"rel={fmt(object_table)} count={fmt(count)} word1={fmt(word1)} entries={fmt(entries)}"
        )
        if in_block(block, entries):
            entries_abs = base + entries
            lines.append(
                "      first object entry words: "
                + " ".join(fmt(be32(data, entries_abs + i * 4)) for i in range(8))
            )
    else:
        lines.append("      object table candidate: not a relative offset in this block")

    material_table = raw["material_table"]
    if in_block(block, material_table):
        header = base + material_table
        lines.append(
            "      material table candidate words: "
            + " ".join(fmt(be32(data, header + i * 4)) for i in range(6))
        )

    return lines


def fmt(value):
    if value is None:
        return "?"
    return f"0x{value:X}"


def dump_resource(data, resource, indent=""):
    print(f"{indent}{resource.label}: WII blocks={resource.count} flags={fmt(resource.flags)} base={fmt(resource.base)}")
    for block in resource.blocks():
        magic = block["magic"].decode("latin1", errors="replace")
        print(
            f"{indent}  [{block['index']:02}] rel={fmt(block['rel'])} "
            f"abs={fmt(block['base'])} size={fmt(block['size'])} magic={magic!r}"
        )


def looks_like_model_collection(resource):
    first = resource.block(0)
    second = resource.block(1)
    return (
        resource.valid
        and resource.count >= 2
        and first is not None
        and second is not None
        and first["magic"] == b"ZMB "
    )


def dump_model_collection(data, resource, indent):
    print(f"{indent}model/resource collection candidate:")
    for block in resource.blocks():
        magic = block["magic"].decode("latin1", errors="replace")
        kind = "unknown"
        if block["magic"] == b"ZMB ":
            kind = "model"
        elif block["magic"] == b"ZAB ":
            kind = "animation"
        elif block["magic"] == b"WII\x00":
            kind = "nested resource"
        elif block["size"] > 0x20:
            kind = "texture/other"
        print(
            f"{indent}  [{block['index']:02}] {kind:15} "
            f"rel={fmt(block['rel'])} abs={fmt(block['base'])} size={fmt(block['size'])} magic={magic!r}"
        )
        if block["magic"] == b"ZMB ":
            for line in describe_model_block(data, block):
                print(indent + line[6:])


def inspect_select_cmn(path):
    data = path.read_bytes()
    root = WiiResource(data, 0, "select_cmn root")
    if not root.valid:
        raise SystemExit(f"{path} is not a WII resource")

    print(f"{path} size={fmt(len(data))} ({len(data)})")
    dump_resource(data, root)

    for top in root.blocks():
        if top["magic"] != b"WII\x00":
            continue
        nested = WiiResource(data, top["base"], f"top block {top['index']}")
        dump_resource(data, nested, "  ")

        if looks_like_model_collection(nested):
            dump_model_collection(data, nested, "    ")

        # CzanModelManager_LoadResource treats nested block 2 as the model collection when present.
        collection_block = nested.block(2) if nested.count > 2 else None
        if collection_block is None or collection_block["magic"] != b"WII\x00":
            continue

        collection = WiiResource(data, collection_block["base"], f"top {top['index']} model collection")
        print(f"    model collection from nested block 2:")
        dump_resource(data, collection, "      ")

        metadata_block = nested.block(0)
        model_count = None
        if metadata_block is not None and metadata_block["size"] >= 0x18:
            model_count = be16(data, metadata_block["base"] + 0x10 + 6)
        print(f"      model count from metadata payload +6: {model_count}")

        pair_count = collection.count // 2
        for i in range(pair_count):
            model = collection.block(i * 2)
            texture = collection.block(i * 2 + 1)
            print(
                f"      pair {i}: modelBlock={fmt(model['rel'])}/{fmt(model['size'])} "
                f"textureBlock={fmt(texture['rel'])}/{fmt(texture['size'])} "
                f"textureMagic={texture['magic'].decode('latin1', errors='replace')!r}"
            )
            for line in describe_model_block(data, model):
                print(line)


def main():
    parser = argparse.ArgumentParser(description="Inspect DDRII select_cmn.bin Czan model collections.")
    parser.add_argument("path", type=Path)
    args = parser.parse_args()
    inspect_select_cmn(args.path)


if __name__ == "__main__":
    main()
