#!/usr/bin/env python3
"""Build a reviewed, chapter-scoped Anki deck from the public game catalog.

Catalog examples are original learning content rather than extracted game dialogue.
Audio is optional: empty audio fields produce a completely usable text-only deck.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
from pathlib import Path
from typing import Any

import genanki


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CATALOG_ROOT = (
    REPOSITORY_ROOT / "services" / "learning_platform" / "learning_platform" / "content" / "games"
)
MODEL_ID = 1607000011

MODEL = genanki.Model(
    MODEL_ID,
    "JP Assist Reviewed Chapter Vocabulary",
    fields=[
        {"name": "Written"},
        {"name": "Reading"},
        {"name": "PartOfSpeech"},
        {"name": "Meaning"},
        {"name": "SentenceJapanese"},
        {"name": "SentenceEnglish"},
        {"name": "WordAudio"},
        {"name": "SentenceAudio"},
        {"name": "ChapterId"},
        {"name": "CardId"},
    ],
    templates=[
        {
            "name": "Recognition",
            "qfmt": (
                "<div class='written'>{{Written}}</div>"
                "<div class='word-audio'>{{WordAudio}}</div>"
            ),
            "afmt": (
                "{{FrontSide}}<hr>"
                "<div class='reading'>{{Reading}} · {{PartOfSpeech}}</div>"
                "<div class='meaning'>{{Meaning}}</div>"
                "<hr><div class='sentence'>{{SentenceJapanese}} {{SentenceAudio}}</div>"
                "<div class='translation'>{{SentenceEnglish}}</div>"
            ),
        }
    ],
    css=(
        ".card{font-family:sans-serif;text-align:center;color:#eee;background:#202124;}"
        ".written{font-size:40px;color:#f4bf3a}.reading{color:#aaa;margin:8px}"
        ".meaning{font-size:22px}.sentence{font-size:21px;margin:14px}"
        ".translation{color:#aaa}.word-audio{margin-top:8px}"
    ),
)


def load_game(game_id: str, catalog_root: Path = DEFAULT_CATALOG_ROOT) -> dict[str, Any]:
    path = catalog_root / f"{game_id}.json"
    if not path.is_file():
        raise ValueError(f"Unknown catalog game: {game_id}")
    return json.loads(path.read_text(encoding="utf-8"))


def select_chapter(game: dict[str, Any], chapter_selector: str) -> dict[str, Any]:
    for chapter in game["chapters"]:
        if chapter["id"] == chapter_selector or str(chapter["order"]) == chapter_selector:
            return chapter
    raise ValueError(f"Unknown chapter for {game['id']}: {chapter_selector}")


def stable_deck_id(game_id: str, chapter_id: str) -> int:
    digest = hashlib.sha256(f"jp-assist-catalog|{game_id}|{chapter_id}".encode()).digest()
    return int.from_bytes(digest[:8], "big") & 0x7FFF_FFFF_FFFF_FFFF


def stable_note_guid(game_id: str, card_id: str) -> str:
    digest = hashlib.sha256(f"jp-assist-catalog|{game_id}|{card_id}".encode()).hexdigest()
    return genanki.guid_for(digest)


def audio_field(
    value: str | None,
    catalog_root: Path,
    media_files: list[str],
) -> str:
    if not value:
        return ""
    path = (catalog_root / value).resolve()
    if not path.is_file() or not path.is_relative_to(catalog_root.resolve()):
        raise ValueError(f"Catalog audio file is missing or outside the content root: {value}")
    media_files.append(str(path))
    return f"[sound:{path.name}]"


def build_deck(
    game: dict[str, Any],
    chapter: dict[str, Any],
    catalog_root: Path = DEFAULT_CATALOG_ROOT,
) -> tuple[genanki.Deck, list[str]]:
    cards = chapter.get("sampleCards", [])
    if not cards:
        raise ValueError(f"Chapter {chapter['id']} has no reviewed cards to export")
    deck_name = (
        f"JP Assist::{game['title']}::"
        f"{chapter['order']:02d} {chapter['title']}"
    )
    deck = genanki.Deck(stable_deck_id(game["id"], chapter["id"]), deck_name)
    media_files: list[str] = []
    for card in cards:
        note = genanki.Note(
            model=MODEL,
            fields=[
                card["written"],
                card["reading"],
                card["partOfSpeech"],
                card["meaning"],
                card["sentenceJapanese"],
                card["sentenceEnglish"],
                audio_field(card.get("wordAudio"), catalog_root, media_files),
                audio_field(card.get("sentenceAudio"), catalog_root, media_files),
                chapter["id"],
                card["id"],
            ],
            guid=stable_note_guid(game["id"], card["id"]),
            tags=["jp-assist", game["id"], chapter["id"], "reviewed"],
        )
        deck.add_note(note)
    return deck, media_files


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", default="ocarina-of-time", help="Catalog game id")
    parser.add_argument("--chapter", default="1", help="Chapter number or id")
    parser.add_argument("--catalog-root", type=Path, default=DEFAULT_CATALOG_ROOT)
    parser.add_argument("--out-dir", type=Path, default=Path(__file__).parent / "out")
    parser.add_argument("--output-prefix", default=None)
    args = parser.parse_args()

    game = load_game(args.game, args.catalog_root)
    chapter = select_chapter(game, args.chapter)
    deck, media_files = build_deck(game, chapter, args.catalog_root)
    args.out_dir.mkdir(parents=True, exist_ok=True)
    prefix = args.output_prefix or f"{game['id']}_{chapter['order']:02d}"

    package_path = args.out_dir / f"{prefix}.apkg"
    package = genanki.Package(deck)
    package.media_files = media_files
    package.write_to_file(str(package_path))

    tsv_path = args.out_dir / f"{prefix}.tsv"
    with tsv_path.open("w", newline="", encoding="utf-8") as output:
        writer = csv.writer(output, delimiter="\t")
        writer.writerow([
            "CardId", "Written", "Reading", "PartOfSpeech", "Meaning",
            "SentenceJapanese", "SentenceEnglish", "WordAudio", "SentenceAudio",
        ])
        for card in chapter["sampleCards"]:
            writer.writerow([
                card["id"], card["written"], card["reading"], card["partOfSpeech"],
                card["meaning"], card["sentenceJapanese"], card["sentenceEnglish"],
                card.get("wordAudio") or "", card.get("sentenceAudio") or "",
            ])

    print(f"Wrote {len(chapter['sampleCards'])} reviewed note(s) to {package_path}")
    print(f"Wrote review TSV to {tsv_path}")
    if not media_files:
        print("Audio: none (the deck remains fully usable)")


if __name__ == "__main__":
    main()
