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
DEFAULT_RUNTIME_DATA = Path(__file__).parent / "out" / "runtime_data.json"
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
    game = json.loads(path.read_text(encoding="utf-8"))
    manifest_name = game.get("cardManifest")
    if manifest_name:
        manifest_path = catalog_root / manifest_name
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        if manifest.get("gameId") != game_id:
            raise ValueError(f"Card manifest game id does not match {game_id}")
        cards_by_chapter = {
            item["chapterId"]: item["cards"] for item in manifest.get("chapters", [])
        }
        for chapter in game["chapters"]:
            chapter["reviewedCards"] = cards_by_chapter.get(chapter["id"], [])
    return game


def reviewed_cards(chapter: dict[str, Any]) -> list[dict[str, Any]]:
    """Return the complete reviewed corpus, falling back for legacy fixtures."""
    return chapter.get("reviewedCards", chapter.get("sampleCards", []))


def select_chapter(game: dict[str, Any], chapter_selector: str) -> dict[str, Any]:
    for chapter in game["chapters"]:
        if chapter["id"] == chapter_selector or str(chapter["order"]) == chapter_selector:
            return chapter
    raise ValueError(f"Unknown chapter for {game['id']}: {chapter_selector}")


def stable_deck_id(game_id: str, chapter_id: str) -> int:
    digest = hashlib.sha256(f"jp-assist-catalog|{game_id}|{chapter_id}".encode()).digest()
    return int.from_bytes(digest[:8], "big") & 0x7FFF_FFFF_FFFF_FFFF


def stable_note_guid(game_id: str, chapter_id: str, card_id: str) -> str:
    digest = hashlib.sha256(
        f"jp-assist-catalog|{game_id}|{chapter_id}|{card_id}".encode()
    ).hexdigest()
    return genanki.guid_for(digest)


def card_identity(card: dict[str, Any]) -> str:
    return card.get("corpusEvidence", {}).get("identity") or card["id"]


def validate_prerequisite_uniqueness(
    game: dict[str, Any], chapter: dict[str, Any]
) -> None:
    """Reject cards already taught by a transitive hard prerequisite."""
    chapters = {item["id"]: item for item in game["chapters"]}
    prerequisites: set[str] = set()
    visiting: set[str] = set()

    def collect(chapter_id: str) -> None:
        if chapter_id in prerequisites:
            return
        if chapter_id in visiting:
            raise ValueError(f"Chapter prerequisite cycle includes {chapter_id}")
        if chapter_id not in chapters:
            raise ValueError(f"Unknown chapter prerequisite: {chapter_id}")
        visiting.add(chapter_id)
        for prerequisite in chapters[chapter_id].get("prerequisites", []):
            collect(prerequisite)
            prerequisites.add(prerequisite)
        visiting.remove(chapter_id)

    collect(chapter["id"])
    previously_taught = {
        card_identity(card)
        for prerequisite in prerequisites
        for card in reviewed_cards(chapters[prerequisite])
    }
    repeated = sorted(
        card_identity(card)
        for card in reviewed_cards(chapter)
        if card_identity(card) in previously_taught
    )
    if repeated:
        raise ValueError(
            f"Chapter {chapter['id']} repeats prerequisite card(s): {', '.join(repeated)}"
        )


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
    cards = reviewed_cards(chapter)
    if not cards:
        raise ValueError(f"Chapter {chapter['id']} has no reviewed cards to export")
    validate_prerequisite_uniqueness(game, chapter)
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
            guid=stable_note_guid(game["id"], chapter["id"], card["id"]),
            tags=["jp-assist", game["id"], chapter["id"], "reviewed"],
        )
        deck.add_note(note)
    return deck, media_files


def validate_corpus_evidence(chapter: dict[str, Any], runtime_root: dict[str, Any]) -> None:
    messages = runtime_root.get("messages", runtime_root)
    corpus_japanese = {
        "".join(page.get("japanese", "").split())
        for record in messages.values()
        for page in record["pages"]
        if page.get("japanese", "").strip()
    }
    corpus_english = {
        " ".join(page.get("english", "").split()).casefold()
        for record in messages.values()
        for page in record["pages"]
        if page.get("english", "").strip()
    }
    for card in reviewed_cards(chapter):
        evidence = card.get("corpusEvidence")
        if not evidence:
            raise ValueError(f"Reviewed card {card['id']} has no corpus evidence")
        expected = evidence["identity"]
        found = False
        for message_id in evidence.get("messageIds", []):
            record = messages.get(message_id)
            if record is None:
                raise ValueError(f"Reviewed card {card['id']} cites missing message {message_id}")
            for page in record["pages"]:
                for token in page["tokens"]:
                    reading = token.get("dictionaryReading", token["reading"])
                    actual = f"{token['lemma']}|{reading}|{token['senseId']}"
                    found = found or actual == expected
        if not found:
            raise ValueError(f"Reviewed card {card['id']} has stale corpus evidence")
        japanese_example = "".join(card["sentenceJapanese"].split())
        english_example = " ".join(card["sentenceEnglish"].split()).casefold()
        if japanese_example in corpus_japanese or english_example in corpus_english:
            raise ValueError(f"Reviewed card {card['id']} copies a corpus sentence")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", default="ocarina-of-time", help="Catalog game id")
    parser.add_argument("--chapter", default="1", help="Chapter number or id")
    parser.add_argument("--catalog-root", type=Path, default=DEFAULT_CATALOG_ROOT)
    parser.add_argument(
        "--runtime-data", type=Path, default=DEFAULT_RUNTIME_DATA,
        help="Local corpus used to verify non-text card provenance when present",
    )
    parser.add_argument(
        "--require-corpus-evidence", action="store_true",
        help="Fail instead of warning if the local runtime corpus is unavailable",
    )
    parser.add_argument("--out-dir", type=Path, default=Path(__file__).parent / "out")
    parser.add_argument("--output-prefix", default=None)
    args = parser.parse_args()

    game = load_game(args.game, args.catalog_root)
    chapter = select_chapter(game, args.chapter)
    if args.runtime_data.is_file():
        validate_corpus_evidence(
            chapter, json.loads(args.runtime_data.read_text(encoding="utf-8"))
        )
        print(f"Verified card provenance against {args.runtime_data}")
    elif args.require_corpus_evidence:
        raise ValueError(f"Runtime corpus is required but missing: {args.runtime_data}")
    else:
        print(f"Warning: corpus provenance not verified; file is missing: {args.runtime_data}")
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
        for card in reviewed_cards(chapter):
            writer.writerow([
                card["id"], card["written"], card["reading"], card["partOfSpeech"],
                card["meaning"], card["sentenceJapanese"], card["sentenceEnglish"],
                card.get("wordAudio") or "", card.get("sentenceAudio") or "",
            ])

    print(f"Wrote {len(reviewed_cards(chapter))} reviewed note(s) to {package_path}")
    print(f"Wrote review TSV to {tsv_path}")
    if not media_files:
        print("Audio: none (the deck remains fully usable)")


if __name__ == "__main__":
    main()
