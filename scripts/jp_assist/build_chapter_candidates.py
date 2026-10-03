#!/usr/bin/env python3
"""Build review queues for story-ordered chapter vocabulary.

Outputs contain dictionary metadata and message IDs, but never game dialogue.
The committed mapping is deliberately reviewable and does not infer story order
from numeric message IDs.
"""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_RUNTIME_DATA = Path(__file__).parent / "out" / "runtime_data.json"
DEFAULT_CATALOG = (
    REPOSITORY_ROOT / "services" / "learning_platform" / "learning_platform"
    / "content" / "games" / "ocarina-of-time.json"
)
DEFAULT_MAPPING = Path(__file__).parent / "chapter_mapping" / "ocarina-of-time.json"
DEFAULT_OUT_DIR = Path(__file__).parent / "out" / "chapter_candidates"


def message_number(value: str) -> int:
    return int(value, 0)


def expand_message_ids(chapter: dict[str, Any]) -> set[str]:
    result = {f"{message_number(value):#06x}" for value in chapter.get("messageIds", [])}
    for interval in chapter.get("messageRanges", []):
        first = message_number(interval["first"])
        last = message_number(interval["last"])
        if last < first:
            raise ValueError(f"Reversed message range in {chapter['chapterId']}")
        result.update(f"{value:#06x}" for value in range(first, last + 1))
    return result


def validate_mapping(mapping: dict[str, Any], catalog: dict[str, Any]) -> None:
    if mapping.get("gameId") != catalog.get("id"):
        raise ValueError("Chapter mapping and catalog game ids do not match")
    expected = [chapter["id"] for chapter in catalog["chapters"]]
    actual = [chapter["chapterId"] for chapter in mapping.get("chapters", [])]
    if actual != expected:
        raise ValueError("Chapter mapping must contain every catalog chapter in catalog order")
    ownership: dict[str, str] = {}
    for chapter in mapping["chapters"]:
        for message_id in expand_message_ids(chapter):
            previous = ownership.get(message_id)
            if previous:
                raise ValueError(f"Message {message_id} belongs to both {previous} and {chapter['chapterId']}")
            ownership[message_id] = chapter["chapterId"]


def collect_candidates(
    runtime_root: dict[str, Any],
    catalog: dict[str, Any],
    mapping: dict[str, Any],
) -> tuple[dict[str, list[dict[str, Any]]], dict[str, Any]]:
    validate_mapping(mapping, catalog)
    messages = runtime_root.get("messages", runtime_root)
    chapter_order = {chapter["id"]: chapter["order"] for chapter in catalog["chapters"]}
    message_chapter: dict[str, str] = {}
    mapped_existing: dict[str, set[str]] = defaultdict(set)
    for mapped in mapping["chapters"]:
        chapter_id = mapped["chapterId"]
        for message_id in expand_message_ids(mapped):
            if message_id in messages:
                message_chapter[message_id] = chapter_id
                mapped_existing[chapter_id].add(message_id)

    occurrences: dict[tuple[str, str, str], Counter[str]] = defaultdict(Counter)
    metadata: dict[tuple[str, str, str], dict[str, str]] = {}
    evidence: dict[tuple[str, str, str], dict[str, set[str]]] = defaultdict(lambda: defaultdict(set))
    for message_id, chapter_id in message_chapter.items():
        for page in messages[message_id]["pages"]:
            for token in page["tokens"]:
                reading = token.get("dictionaryReading", token["reading"])
                identity = (token["lemma"], reading, token["senseId"])
                occurrences[identity][chapter_id] += 1
                evidence[identity][chapter_id].add(message_id)
                metadata.setdefault(identity, {
                    "written": token["lemma"],
                    "reading": reading,
                    "senseId": token["senseId"],
                    "partOfSpeech": token["partOfSpeech"],
                    "meaning": token["meaning"],
                })

    published = {
        card["corpusEvidence"]["identity"]: chapter["id"]
        for chapter in catalog["chapters"]
        for card in chapter.get("sampleCards", [])
        if card.get("corpusEvidence")
    }
    by_chapter: dict[str, list[dict[str, Any]]] = {chapter["id"]: [] for chapter in catalog["chapters"]}
    for identity, counts in occurrences.items():
        earliest = min(counts, key=lambda chapter_id: chapter_order[chapter_id])
        identity_text = "|".join(identity)
        later = sorted((chapter_id for chapter_id in counts if chapter_id != earliest), key=chapter_order.get)
        row = {
            **metadata[identity],
            "identity": identity_text,
            "chapterFrequency": counts[earliest],
            "mappedFrequency": sum(counts.values()),
            "messageIds": sorted(evidence[identity][earliest], key=message_number),
            "laterChapters": later,
            "reviewStatus": "published" if published.get(identity_text) == earliest else "candidate",
        }
        by_chapter[earliest].append(row)
    for rows in by_chapter.values():
        rows.sort(key=lambda row: (-row["chapterFrequency"], row["written"], row["reading"], row["senseId"]))

    summary = {
        "schemaVersion": 1,
        "gameId": catalog["id"],
        "sourceVariant": mapping["sourceVariant"],
        "sourceCorpusVersion": runtime_root.get("metadata", {}).get("corpusVersion"),
        "mappedMessageCount": len(message_chapter),
        "totalMessageCount": len(messages),
        "chapters": [
            {
                "chapterId": chapter["id"],
                "mappingStatus": next(item["status"] for item in mapping["chapters"] if item["chapterId"] == chapter["id"]),
                "mappedMessageCount": len(mapped_existing[chapter["id"]]),
                "candidateCount": len(by_chapter[chapter["id"]]),
                "publishedCount": sum(row["reviewStatus"] == "published" for row in by_chapter[chapter["id"]]),
            }
            for chapter in catalog["chapters"]
        ],
    }
    return by_chapter, summary


def write_outputs(by_chapter: dict[str, list[dict[str, Any]]], summary: dict[str, Any], out_dir: Path) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    fields = [
        "Identity", "Written", "Reading", "PartOfSpeech", "Meaning", "ChapterFrequency",
        "MappedFrequency", "MessageIds", "LaterChapters", "ReviewStatus",
    ]
    for chapter in summary["chapters"]:
        chapter_id = chapter["chapterId"]
        with (out_dir / f"{chapter_id}.tsv").open("w", newline="", encoding="utf-8") as output:
            writer = csv.writer(output, delimiter="\t")
            writer.writerow(fields)
            for row in by_chapter[chapter_id]:
                writer.writerow([
                    row["identity"], row["written"], row["reading"], row["partOfSpeech"],
                    row["meaning"], row["chapterFrequency"], row["mappedFrequency"],
                    ",".join(row["messageIds"]), ",".join(row["laterChapters"]), row["reviewStatus"],
                ])
    (out_dir / "summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-data", type=Path, default=DEFAULT_RUNTIME_DATA)
    parser.add_argument("--catalog", type=Path, default=DEFAULT_CATALOG)
    parser.add_argument("--mapping", type=Path, default=DEFAULT_MAPPING)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR)
    args = parser.parse_args()
    runtime = json.loads(args.runtime_data.read_text(encoding="utf-8"))
    catalog = json.loads(args.catalog.read_text(encoding="utf-8"))
    mapping = json.loads(args.mapping.read_text(encoding="utf-8"))
    by_chapter, summary = collect_candidates(runtime, catalog, mapping)
    write_outputs(by_chapter, summary, args.out_dir)
    print(f"Mapped {summary['mappedMessageCount']} of {summary['totalMessageCount']} messages")
    for chapter in summary["chapters"]:
        print(
            f"{chapter['chapterId']}: {chapter['candidateCount']} candidates, "
            f"{chapter['publishedCount']} published ({chapter['mappingStatus']})"
        )


if __name__ == "__main__":
    main()
