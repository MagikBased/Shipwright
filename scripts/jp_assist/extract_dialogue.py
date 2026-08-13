#!/usr/bin/env python3
"""Extracts the Japanese and English message tables directly from a locally
generated oot.o2r, without running the game.

Output goes to scripts/jp_assist/out/ (gitignored - see docs/JP_ASSIST_DESIGN.md
section 7.4: extracted dialogue must never be committed).

Archive format (verified against the real file, not just documentation):
a 64-byte OTR resource header (libultraship ResourceLoader.cpp) followed by
a Text-resource payload (soh/soh/resource/importer/TextFactory.cpp):
    u32 msgCount
    msgCount * {
        u16 id
        u8 textboxType
        u8 textboxYPos
        u32 stringLength
        u8[stringLength] stringBytes
    }
All fields little-endian, per the header's own ByteOrder byte (offset 0).
"""

import argparse
import hashlib
import json
import struct
import zipfile
from pathlib import Path

OTR_HEADER_SIZE = 64

# Archive-relative paths for each language's table, as they exist in the
# currently supported N64 NTSC 1.2 archive (docs/JP_ASSIST_DESIGN.md
# section 17: "generate the corpus locally from the user's N64 NTSC 1.2
# archive first").
TABLE_PATHS = {
    "jpn": "text/jpn_message_data_static/jpn_message_data_static",
    "eng": "text/nes_message_data_static/ntsc_nes_message_data_static",
}


def read_text_resource(data: bytes) -> dict[int, dict]:
    byte_order = data[0]
    if byte_order != 0:
        raise ValueError(f"Unexpected ByteOrder {byte_order}: only little-endian archives are supported")

    offset = OTR_HEADER_SIZE
    (msg_count,) = struct.unpack_from("<I", data, offset)
    offset += 4

    entries: dict[int, dict] = {}
    for _ in range(msg_count):
        msg_id, textbox_type, textbox_y_pos = struct.unpack_from("<HBB", data, offset)
        offset += 4
        (string_length,) = struct.unpack_from("<I", data, offset)
        offset += 4
        raw = data[offset : offset + string_length]
        offset += string_length
        entries[msg_id] = {
            "textboxType": textbox_type,
            "textboxYPos": textbox_y_pos,
            "raw": raw.hex(),
        }
    return entries


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oot-o2r", default="oot.o2r", help="Path to a locally generated oot.o2r")
    parser.add_argument("--variant", default="N64_NTSC_12", help="ROM variant label stamped into the output")
    parser.add_argument("--out-dir", default=None, help="Output directory (default: scripts/jp_assist/out)")
    args = parser.parse_args()

    out_dir = Path(args.out_dir) if args.out_dir else Path(__file__).parent / "out"
    out_dir.mkdir(parents=True, exist_ok=True)

    with zipfile.ZipFile(args.oot_o2r) as archive:
        tables = {}
        for language, path in TABLE_PATHS.items():
            data = archive.read(path)
            tables[language] = read_text_resource(data)
            print(f"{language}: {len(tables[language])} messages")

    jpn_ids = set(tables["jpn"])
    eng_ids = set(tables["eng"])
    only_jpn = jpn_ids - eng_ids
    only_eng = eng_ids - jpn_ids
    if only_jpn:
        print(f"Warning: {len(only_jpn)} textId(s) present in Japanese table only")
    if only_eng:
        print(f"Warning: {len(only_eng)} textId(s) present in English table only")

    merged = {}
    for msg_id in sorted(jpn_ids | eng_ids):
        jpn_entry = tables["jpn"].get(msg_id)
        eng_entry = tables["eng"].get(msg_id)
        merged[f"{msg_id:#06x}"] = {
            "textId": msg_id,
            "japanese": jpn_entry,
            "english": eng_entry,
            # Composite identity components (design doc section 6.5) - the
            # variant/textId are stamped here; per-message source hashes are
            # computed in tokenize_dialogue.py once the text is decoded.
            "variant": args.variant,
        }

    out_path = out_dir / f"{args.variant}.json"
    out_path.write_text(json.dumps(merged, ensure_ascii=False, indent=1))
    print(f"Wrote {len(merged)} messages to {out_path}")


if __name__ == "__main__":
    main()
