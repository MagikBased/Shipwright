#!/usr/bin/env python3
"""Validate generated Anki packages and optionally compare stable note GUIDs."""

import argparse
import sqlite3
import sys
import tempfile
import zipfile
from pathlib import Path


def read_notes(package_path: Path) -> dict[str, list[str]]:
    with tempfile.TemporaryDirectory(prefix="jp-assist-anki-") as temporary:
        with zipfile.ZipFile(package_path) as package:
            database_name = next(
                (name for name in ("collection.anki2", "collection.anki21") if name in package.namelist()), None
            )
            if database_name is None:
                raise ValueError(f"{package_path} has no Anki collection database")
            package.extract(database_name, temporary)
        database = sqlite3.connect(Path(temporary) / database_name)
        try:
            return {guid: fields.split("\x1f") for guid, fields in database.execute("SELECT guid, flds FROM notes")}
        finally:
            database.close()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("--compare", type=Path)
    parser.add_argument("--expected-notes", type=int)
    args = parser.parse_args()

    errors = []
    notes = read_notes(args.package)
    if args.expected_notes is not None and len(notes) != args.expected_notes:
        errors.append(f"expected {args.expected_notes} notes, found {len(notes)}")
    for guid, fields in notes.items():
        if len(fields) < 9:
            errors.append(f"note {guid} has {len(fields)} fields, expected at least 9")
        elif not fields[0] or not fields[1] or not fields[2]:
            errors.append(f"note {guid} is missing Written, Reading, or DictionaryForm")

    if args.compare:
        comparison = read_notes(args.compare)
        if set(notes) != set(comparison):
            errors.append(
                f"GUID sets differ: {len(set(notes) - set(comparison))} removed, "
                f"{len(set(comparison) - set(notes))} added"
            )

    print(f"Validated {len(notes)} unique notes in {args.package}")
    if args.compare:
        print(f"Stable GUID comparison passed against {args.compare}" if not errors else "GUID comparison failed")
    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        sys.exit(1)


if __name__ == "__main__":
    main()
