#!/usr/bin/env python3
"""Build a content-neutral game vocabulary index with estimated JLPT levels."""

from __future__ import annotations

import argparse
import json
from collections import Counter
from pathlib import Path
from typing import Any

try:
    from .vocabulary_policy import is_transferable_identity
except ImportError:  # Direct script execution.
    from vocabulary_policy import is_transferable_identity


LEVELS = ("N5", "N4", "N3", "N2", "N1")


def load_jlpt_index(directory: Path) -> tuple[dict[tuple[str, str], str], dict[str, str]]:
    exact: dict[tuple[str, str], str] = {}
    spellings: dict[str, set[str]] = {}
    for level in LEVELS:
        path = directory / f"{level.lower()}.json"
        entries = json.loads(path.read_text(encoding="utf-8"))
        for entry in entries:
            forms = [entry["word"], *(entry.get("other_forms") or [])]
            readings = [entry["reading"], *(entry.get("other_readings") or [])]
            for written in forms:
                spellings.setdefault(written, set()).add(level)
                for reading in readings:
                    exact.setdefault((written, reading), level)
    unambiguous = {word: next(iter(levels)) for word, levels in spellings.items() if len(levels) == 1}
    return exact, unambiguous


def load_reused_level_index(path: Path) -> tuple[dict[tuple[str, str], str], dict[str, str]]:
    """Reuse classifications from an existing generated manifest."""
    manifest = json.loads(path.read_text(encoding="utf-8"))
    exact = {
        (entry["written"], entry["reading"]): entry["jlptLevel"]
        for entry in manifest.get("words", [])
        if entry.get("jlptLevel")
    }
    return exact, manifest.get("methodology", {})


def build(
    runtime_path: Path, jlpt_directory: Path | None, game_id: str,
    level_source_version: str = "unrecorded",
    reused_level_manifest: Path | None = None,
) -> dict[str, Any]:
    runtime = json.loads(runtime_path.read_text(encoding="utf-8"))
    if reused_level_manifest is not None:
        exact, previous_methodology = load_reused_level_index(reused_level_manifest)
        unambiguous: dict[str, str] = {}
    elif jlpt_directory is not None:
        exact, unambiguous = load_jlpt_index(jlpt_directory)
        previous_methodology = {}
    else:
        raise ValueError("A JLPT directory or reusable level manifest is required")
    words: dict[str, dict[str, Any]] = {}
    for message in runtime["messages"].values():
        for page in message["pages"]:
            for token in page["tokens"]:
                reading = token.get("dictionaryReading") or token.get("reading") or ""
                identity = (token["lemma"], reading, token.get("senseId", ""))
                if not is_transferable_identity(identity):
                    continue
                # Cross-game identity deliberately excludes the dictionary
                # sense: homographs share a lexeme when both normalized lemma
                # and reading match, while `senses` keeps meanings distinct.
                word_id = f"{token['lemma']}|{reading}"
                entry = words.setdefault(word_id, {
                    "wordId": word_id,
                    "written": token["lemma"],
                    "reading": reading,
                    "occurrenceCount": 0,
                    "jlptLevel": None,
                    "_senses": {},
                })
                entry["occurrenceCount"] += 1
                sense_id = token.get("senseId") or ""
                sense = entry["_senses"].setdefault(sense_id, {
                    "senseId": sense_id,
                    "partOfSpeech": token.get("partOfSpeech") or "",
                    "meaning": token.get("meaning") or "",
                    "occurrenceCount": 0,
                })
                sense["occurrenceCount"] += 1

    for entry in words.values():
        key = (entry["written"], entry["reading"])
        entry["jlptLevel"] = exact.get(key) or unambiguous.get(entry["written"])
        senses = sorted(
            entry.pop("_senses").values(),
            key=lambda sense: (-sense["occurrenceCount"], sense["senseId"]),
        )
        entry["senses"] = senses
        primary = senses[0]
        # Retain these convenience fields for existing catalog clients while
        # exposing every distinct meaning through `senses`.
        entry["partOfSpeech"] = primary["partOfSpeech"]
        entry["meaning"] = primary["meaning"]

    ordered = sorted(words.values(), key=lambda item: (-item["occurrenceCount"], item["wordId"]))
    unique_counts = Counter(entry["jlptLevel"] or "unclassified" for entry in ordered)
    occurrence_counts = Counter()
    for entry in ordered:
        occurrence_counts[entry["jlptLevel"] or "unclassified"] += entry["occurrenceCount"]
    return {
        "schemaVersion": 2,
        "gameId": game_id,
        "sourceCorpusVersion": runtime.get("metadata", {}).get("corpusVersion", ""),
        "methodology": previous_methodology or {
            "unit": "unique lemma and reading identities",
            "levelSource": "OpenJLPT",
            "levelSourceUrl": "https://github.com/evanclan/OpenJLPT",
            "levelSourceLicense": "CC BY-SA 4.0",
            "levelSourceVersion": level_source_version,
            "levelStatus": "Community estimates; the modern JLPT does not publish official vocabulary lists.",
        },
        "summary": {
            "uniqueWords": len(ordered),
            "classifiedWords": len(ordered) - unique_counts["unclassified"],
            "occurrences": sum(entry["occurrenceCount"] for entry in ordered),
            "uniqueByLevel": {level: unique_counts[level] for level in (*LEVELS, "unclassified")},
            "occurrencesByLevel": {level: occurrence_counts[level] for level in (*LEVELS, "unclassified")},
        },
        "words": ordered,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, default=Path("scripts/jp_assist/out/runtime_data.json"))
    sources = parser.add_mutually_exclusive_group(required=True)
    sources.add_argument("--jlpt-dir", type=Path)
    sources.add_argument(
        "--reuse-levels-from", type=Path,
        help="Reuse JLPT classifications and methodology from an existing manifest",
    )
    parser.add_argument("--game-id", default="ocarina-of-time")
    parser.add_argument("--jlpt-version", default="unrecorded", help="OpenJLPT release or commit identifier")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = build(
        args.runtime, args.jlpt_dir, args.game_id, args.jlpt_version,
        args.reuse_levels_from,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
    print(f"Wrote {result['summary']['uniqueWords']} words to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
