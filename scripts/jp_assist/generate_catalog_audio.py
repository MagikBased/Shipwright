#!/usr/bin/env python3
"""Stage optional audio for reviewed catalog cards.

The default `none` provider performs no synthesis and needs no dependencies.
`kokoro-local` is an explicit, opt-in local provider; generated files stay in
the ignored review directory until a human promotes them into catalog content.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Protocol

from build_catalog_deck import DEFAULT_CATALOG_ROOT, load_game, reviewed_cards, select_chapter


DEFAULT_OUT_DIR = Path(__file__).parent / "out" / "catalog_audio"
KOKORO_VOICE = "jf_alpha"
KOKORO_SAMPLE_RATE = 24_000


class AudioProvider(Protocol):
    provider_id: str

    def synthesize(self, text: str, destination: Path) -> None: ...


@dataclass
class NoneProvider:
    provider_id: str = "none"

    def synthesize(self, text: str, destination: Path) -> None:
        raise RuntimeError("The none provider never synthesizes audio")


class KokoroLocalProvider:
    provider_id = "kokoro-local"

    def __init__(self, voice: str = KOKORO_VOICE, speed: float = 1.0):
        try:
            from kokoro import KPipeline
            import numpy as np
            import soundfile as sf
        except ImportError as error:
            raise RuntimeError(
                "Kokoro audio dependencies are missing. Install "
                "scripts/jp_assist/requirements-tts-kokoro.txt in a separate environment."
            ) from error
        self.voice = voice
        self.speed = speed
        self._np = np
        self._sf = sf
        self._pipeline = KPipeline(lang_code="j")

    def synthesize(self, text: str, destination: Path) -> None:
        chunks = [audio for _graphemes, _phonemes, audio in self._pipeline(
            text, voice=self.voice, speed=self.speed, split_pattern=r"\n+",
        )]
        if not chunks:
            raise RuntimeError(f"Kokoro produced no audio for {text!r}")
        audio = chunks[0] if len(chunks) == 1 else self._np.concatenate(chunks)
        destination.parent.mkdir(parents=True, exist_ok=True)
        self._sf.write(destination, audio, KOKORO_SAMPLE_RATE)


def safe_stem(card_id: str) -> str:
    return hashlib.sha256(card_id.encode("utf-8")).hexdigest()[:16]


def stage_audio(
    game: dict[str, Any],
    chapter: dict[str, Any],
    provider: AudioProvider,
    out_dir: Path,
) -> dict[str, Any]:
    target = out_dir / game["id"] / chapter["id"]
    entries = []
    for card in reviewed_cards(chapter):
        entry = {
            "cardId": card["id"],
            "wordId": f"{card['written']}|{card['reading']}",
            "wordAudio": None,
            "sentenceAudio": None,
        }
        if provider.provider_id != "none":
            stem = safe_stem(card["id"])
            word_path = target / f"{stem}-word.wav"
            sentence_path = target / f"{stem}-sentence.wav"
            provider.synthesize(card["written"], word_path)
            provider.synthesize(card["sentenceJapanese"], sentence_path)
            entry["wordAudio"] = str(word_path.relative_to(out_dir))
            entry["sentenceAudio"] = str(sentence_path.relative_to(out_dir))
        entries.append(entry)
    manifest = {
        "schemaVersion": 1,
        "gameId": game["id"],
        "chapterId": chapter["id"],
        "provider": provider.provider_id,
        "voice": getattr(provider, "voice", None),
        "sampleRate": KOKORO_SAMPLE_RATE if provider.provider_id == "kokoro-local" else None,
        "reviewStatus": "not-applicable" if provider.provider_id == "none" else "needs-human-review",
        "entries": entries,
    }
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / f"{game['id']}-{chapter['order']:02d}-manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", default="ocarina-of-time")
    parser.add_argument("--chapter", default="1")
    parser.add_argument("--provider", choices=("none", "kokoro-local"), default="none")
    parser.add_argument("--voice", default=KOKORO_VOICE)
    parser.add_argument("--speed", type=float, default=1.0)
    parser.add_argument("--catalog-root", type=Path, default=DEFAULT_CATALOG_ROOT)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR)
    parser.add_argument(
        "--accept-kokoro-license-review", action="store_true",
        help="Confirms review of docs/KOKORO_TTS.md; required for synthesis",
    )
    args = parser.parse_args()
    if args.provider == "kokoro-local" and not args.accept_kokoro_license_review:
        parser.error("kokoro-local requires --accept-kokoro-license-review")
    game = load_game(args.game, args.catalog_root)
    chapter = select_chapter(game, args.chapter)
    provider: AudioProvider = (
        NoneProvider() if args.provider == "none"
        else KokoroLocalProvider(args.voice, args.speed)
    )
    manifest = stage_audio(game, chapter, provider, args.out_dir)
    print(
        f"Staged {len(manifest['entries'])} card(s) with provider {manifest['provider']}; "
        f"review status: {manifest['reviewStatus']}"
    )


if __name__ == "__main__":
    main()
