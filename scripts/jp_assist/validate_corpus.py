#!/usr/bin/env python3
"""Validates tokenize_dialogue.py's output (docs/JP_ASSIST_DESIGN.md section
7.2 step 9: "Validate token coverage and page alignment").

Checks:
  - Every page has at least one token, or is legitimately empty.
  - No token surface contains characters outside the expected Japanese
    Unicode ranges (hiragana, katakana, CJK ideographs, common punctuation) -
    this catches control-code/button-icon glyphs that were accidentally
    Shift-JIS-decoded as if they were real text instead of being
    special-cased. Found live: raw code 0x83A5 (the in-game "C-Up/triangle
    button" icon) decodes via standard Shift-JIS to the Greek letter "Η",
    which looks enough like ordinary text to pass silently if not checked
    for explicitly.
"""

import argparse
import json
import re
import unicodedata
from pathlib import Path

_ALLOWED_RANGES = (
    (0x3040, 0x309F),  # Hiragana
    (0x30A0, 0x30FF),  # Katakana
    (0x4E00, 0x9FFF),  # CJK Unified Ideographs
    (0x3000, 0x303F),  # CJK punctuation
    (0xFF00, 0xFFEF),  # Fullwidth forms
)


def is_expected_japanese_char(ch: str) -> bool:
    if ch.isascii():
        return True
    code = ord(ch)
    return any(low <= code <= high for low, high in _ALLOWED_RANGES)


def find_suspicious_chars(text: str) -> set[str]:
    return {ch for ch in text if not is_expected_japanese_char(ch)}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-data", default=None, help="Path to tokenize_dialogue.py's output")
    args = parser.parse_args()

    out_dir = Path(__file__).parent / "out"
    runtime_path = Path(args.runtime_data) if args.runtime_data else out_dir / "runtime_data.json"
    runtime_root = json.loads(runtime_path.read_text())
    runtime_data = runtime_root.get("messages", runtime_root)

    empty_pages = 0
    suspicious_found: dict[str, set[str]] = {}

    for key, record in runtime_data.items():
        for i, page in enumerate(record["pages"]):
            if page["japanese"].strip() and not page["tokens"]:
                empty_pages += 1
                print(f"WARNING: {key} page {i} has Japanese text but no tokens")

            suspicious = find_suspicious_chars(page["japanese"])
            if suspicious:
                suspicious_found.setdefault(key, set()).update(suspicious)

    print(f"\nChecked {len(runtime_data)} message(s).")
    print(f"Pages with text but no tokens: {empty_pages}")
    if suspicious_found:
        print(f"\nMessages with unexpected (likely mis-decoded button-icon) characters:")
        for key, chars in suspicious_found.items():
            names = [f"{ch!r} (U+{ord(ch):04X}, {unicodedata.name(ch, '?')})" for ch in chars]
            print(f"  {key}: {', '.join(names)}")
    else:
        print("No unexpected characters found.")


if __name__ == "__main__":
    main()
