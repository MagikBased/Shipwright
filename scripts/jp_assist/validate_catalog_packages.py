#!/usr/bin/env python3
"""Validate every catalog deck across CLI regeneration and public export paths."""

from __future__ import annotations

import argparse
import importlib.util
import tempfile
from pathlib import Path
from typing import Any

import genanki

from build_catalog_deck import DEFAULT_CATALOG_ROOT, build_deck, load_game, reviewed_cards
from validate_anki import read_notes


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
SERVICE_EXPORT_PATH = (
    REPOSITORY_ROOT
    / "services" / "learning_platform" / "learning_platform" / "anki_export.py"
)


def load_service_export() -> Any:
    spec = importlib.util.spec_from_file_location(
        "learning_platform_anki_export_validation", SERVICE_EXPORT_PATH
    )
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Cannot load public exporter: {SERVICE_EXPORT_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def write_cli_package(game: dict[str, Any], chapter: dict[str, Any], root: Path, path: Path) -> None:
    deck, media_files = build_deck(game, chapter, root)
    package = genanki.Package(deck)
    package.media_files = media_files
    package.write_to_file(str(path))


def validate_packages(
    game: dict[str, Any], catalog_root: Path = DEFAULT_CATALOG_ROOT,
) -> list[dict[str, Any]]:
    service_export = load_service_export()
    results: list[dict[str, Any]] = []
    with tempfile.TemporaryDirectory(prefix="jp-assist-catalog-packages-") as temporary:
        work = Path(temporary)
        for chapter in game["chapters"]:
            chapter_id = chapter["id"]
            first = work / f"{chapter_id}-first.apkg"
            second = work / f"{chapter_id}-second.apkg"
            public = work / f"{chapter_id}-public.apkg"
            write_cli_package(game, chapter, catalog_root, first)
            write_cli_package(game, chapter, catalog_root, second)
            public.write_bytes(
                service_export.build_chapter_package(game, chapter, catalog_root)
            )
            first_notes = read_notes(first)
            second_notes = read_notes(second)
            public_notes = read_notes(public)
            expected = len(reviewed_cards(chapter))
            if len(first_notes) != expected:
                raise ValueError(
                    f"{chapter_id}: expected {expected} notes, found {len(first_notes)}"
                )
            if first_notes != second_notes:
                raise ValueError(f"{chapter_id}: CLI regeneration changed note content or GUIDs")
            if first_notes != public_notes:
                raise ValueError(f"{chapter_id}: public package differs from CLI package")
            results.append({"chapterId": chapter_id, "noteCount": expected})
    return results


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", default="ocarina-of-time")
    parser.add_argument("--catalog-root", type=Path, default=DEFAULT_CATALOG_ROOT)
    args = parser.parse_args()
    game = load_game(args.game, args.catalog_root)
    results = validate_packages(game, args.catalog_root)
    for result in results:
        print(f"{result['chapterId']}: {result['noteCount']} stable notes")
    print(
        f"Validated {sum(result['noteCount'] for result in results)} notes across "
        f"{len(results)} reproducible CLI and public packages"
    )


if __name__ == "__main__":
    main()
