from __future__ import annotations

import hashlib
import tempfile
from pathlib import Path
from typing import Any

import genanki


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
    templates=[{
        "name": "Recognition",
        "qfmt": "<div class='written'>{{Written}}</div><div class='word-audio'>{{WordAudio}}</div>",
        "afmt": (
            "{{FrontSide}}<hr><div class='reading'>{{Reading}} · {{PartOfSpeech}}</div>"
            "<div class='meaning'>{{Meaning}}</div><hr>"
            "<div class='sentence'>{{SentenceJapanese}} {{SentenceAudio}}</div>"
            "<div class='translation'>{{SentenceEnglish}}</div>"
        ),
    }],
    css=(
        ".card{font-family:sans-serif;text-align:center;color:#eee;background:#202124;}"
        ".written{font-size:40px;color:#f4bf3a}.reading{color:#aaa;margin:8px}"
        ".meaning{font-size:22px}.sentence{font-size:21px;margin:14px}"
        ".translation{color:#aaa}.word-audio{margin-top:8px}"
    ),
)


def stable_deck_id(game_id: str, chapter_id: str) -> int:
    digest = hashlib.sha256(f"jp-assist-catalog|{game_id}|{chapter_id}".encode()).digest()
    return int.from_bytes(digest[:8], "big") & 0x7FFF_FFFF_FFFF_FFFF


def stable_note_guid(game_id: str, chapter_id: str, card_id: str) -> str:
    digest = hashlib.sha256(
        f"jp-assist-catalog|{game_id}|{chapter_id}|{card_id}".encode()
    ).hexdigest()
    return genanki.guid_for(digest)


def _audio_field(value: str | None, content_root: Path, media_files: list[str]) -> str:
    if not value:
        return ""
    path = (content_root / value).resolve()
    if not path.is_file() or not path.is_relative_to(content_root.resolve()):
        raise ValueError(f"Catalog audio file is missing or outside the content root: {value}")
    media_files.append(str(path))
    return f"[sound:{path.name}]"


def build_chapter_package(
    game: dict[str, Any], chapter: dict[str, Any], content_root: Path,
) -> bytes:
    cards = chapter.get("reviewedCards", [])
    if not cards:
        raise ValueError(f"Chapter {chapter['id']} has no reviewed cards")
    deck_name = f"JP Assist::{game['title']}::{chapter['order']:02d} {chapter['title']}"
    deck = genanki.Deck(stable_deck_id(game["id"], chapter["id"]), deck_name)
    media_files: list[str] = []
    for card in cards:
        deck.add_note(genanki.Note(
            model=MODEL,
            fields=[
                card["written"], card["reading"], card["partOfSpeech"], card["meaning"],
                card["sentenceJapanese"], card["sentenceEnglish"],
                _audio_field(card.get("wordAudio"), content_root, media_files),
                _audio_field(card.get("sentenceAudio"), content_root, media_files),
                chapter["id"], card["id"],
            ],
            guid=stable_note_guid(game["id"], chapter["id"], card["id"]),
            tags=["jp-assist", game["id"], chapter["id"], "reviewed"],
        ))
    package = genanki.Package(deck)
    package.media_files = media_files
    with tempfile.TemporaryDirectory(prefix="jp-assist-deck-") as temporary:
        output = Path(temporary) / "chapter.apkg"
        package.write_to_file(str(output))
        return output.read_bytes()
