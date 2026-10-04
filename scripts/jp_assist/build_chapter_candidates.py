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
CORE_COVERAGE_TARGET_PERCENT = 80.0
CANONICAL_IDENTITY_ALIASES = {
    # Dialogue sometimes uses katakana for emphasis. It remains one learnable
    # particle rather than becoming a second card solely because of styling.
    ("ヨ", "よ", "override:ヨ|よ"): ("よ", "よ", "override:よ|よ"),
    ("ネ", "ね", "jmdict:2029080:0"): ("ね", "ね", "jmdict:2029080:0"),
    ("オマエ", "おまえ", "jmdict:1002290:0"): ("お前", "おまえ", "jmdict:1002290:0"),
    ("ピー", "ぴー", "override:ピー|ぴー"): ("ッピ", "っぴ", "override:ッピ|っぴ"),
    ("ッピー", "っぴー", "override:ッピー|っぴー"): ("ッピ", "っぴ", "override:ッピ|っぴ"),
    ("ナ", "な", "jmdict:2029110:0"): ("な", "な", "override:な|な"),
    ("サ", "さ", "jmdict:2029120:1"): ("さ", "さ", "jmdict:2029120:1"),
}


def reviewed_cards(chapter: dict[str, Any]) -> list[dict[str, Any]]:
    return chapter.get("reviewedCards", chapter.get("sampleCards", []))


def load_catalog(path: Path) -> dict[str, Any]:
    catalog = json.loads(path.read_text(encoding="utf-8"))
    manifest_name = catalog.get("cardManifest")
    if not manifest_name:
        return catalog
    manifest = json.loads((path.parent / manifest_name).read_text(encoding="utf-8"))
    if manifest.get("gameId") != catalog.get("id"):
        raise ValueError("Card manifest and catalog game ids do not match")
    cards_by_chapter = {
        item["chapterId"]: item["cards"] for item in manifest.get("chapters", [])
    }
    for chapter in catalog["chapters"]:
        chapter["reviewedCards"] = cards_by_chapter.get(chapter["id"], [])
    return catalog


def token_identity(token: dict[str, Any]) -> tuple[str, str, str]:
    reading = token.get("dictionaryReading", token["reading"])
    identity = (token["lemma"], reading, token["senseId"])
    return CANONICAL_IDENTITY_ALIASES.get(identity, identity)


def canonical_identity_text(value: str) -> str:
    """Apply display-variant aliases while retaining exact card provenance."""
    parts = value.split("|", 2)
    if len(parts) != 3:
        return value
    identity = CANONICAL_IDENTITY_ALIASES.get(tuple(parts), tuple(parts))
    return "|".join(identity)


def core_eligible(identity: tuple[str, str, str]) -> bool:
    """Exclude controls/markup while retaining names and speech learners see."""
    return not identity[2].startswith(("interface:", "proper:"))


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


def prerequisite_closures(catalog: dict[str, Any]) -> dict[str, set[str]]:
    """Return every chapter's transitive hard prerequisites.

    Recommended ordering is intentionally ignored: two optional branches may
    teach the same word when neither branch is required before the other.
    """
    chapters = {chapter["id"]: chapter for chapter in catalog["chapters"]}
    closures: dict[str, set[str]] = {}
    visiting: set[str] = set()

    def visit(chapter_id: str) -> set[str]:
        if chapter_id in closures:
            return closures[chapter_id]
        if chapter_id in visiting:
            raise ValueError(f"Chapter prerequisite cycle includes {chapter_id}")
        if chapter_id not in chapters:
            raise ValueError(f"Unknown chapter prerequisite: {chapter_id}")
        visiting.add(chapter_id)
        result: set[str] = set()
        for prerequisite in chapters[chapter_id].get("prerequisites", []):
            if prerequisite not in chapters:
                raise ValueError(
                    f"Chapter {chapter_id} has unknown prerequisite {prerequisite}"
                )
            result.add(prerequisite)
            result.update(visit(prerequisite))
        visiting.remove(chapter_id)
        closures[chapter_id] = result
        return result

    for chapter_id in chapters:
        visit(chapter_id)
    return closures


def collect_candidates(
    runtime_root: dict[str, Any],
    catalog: dict[str, Any],
    mapping: dict[str, Any],
) -> tuple[dict[str, list[dict[str, Any]]], dict[str, Any]]:
    validate_mapping(mapping, catalog)
    messages = runtime_root.get("messages", runtime_root)
    chapter_order = {chapter["id"]: chapter["order"] for chapter in catalog["chapters"]}
    prerequisites = prerequisite_closures(catalog)
    message_chapter: dict[str, str] = {}
    mapped_existing: dict[str, set[str]] = defaultdict(set)
    for mapped in mapping["chapters"]:
        chapter_id = mapped["chapterId"]
        for message_id in expand_message_ids(mapped):
            if message_id in messages:
                message_chapter[message_id] = chapter_id
                mapped_existing[chapter_id].add(message_id)

    occurrences: dict[tuple[str, str, str], Counter[str]] = defaultdict(Counter)
    game_occurrences: Counter[tuple[str, str, str]] = Counter()
    metadata: dict[tuple[str, str, str], dict[str, str]] = {}
    evidence: dict[tuple[str, str, str], dict[str, set[str]]] = defaultdict(lambda: defaultdict(set))
    for record in messages.values():
        for page in record["pages"]:
            for token in page["tokens"]:
                game_occurrences[token_identity(token)] += 1
    for message_id, chapter_id in message_chapter.items():
        for page in messages[message_id]["pages"]:
            for token in page["tokens"]:
                identity = token_identity(token)
                occurrences[identity][chapter_id] += 1
                evidence[identity][chapter_id].add(message_id)
                metadata.setdefault(identity, {
                    "written": identity[0],
                    "reading": identity[1],
                    "senseId": token["senseId"],
                    "partOfSpeech": token["partOfSpeech"],
                    "meaning": token["meaning"],
                })

    published = {
        (canonical_identity_text(card["corpusEvidence"]["identity"]), chapter["id"])
        for chapter in catalog["chapters"]
        for card in reviewed_cards(chapter)
        if card.get("corpusEvidence")
    }
    by_chapter: dict[str, list[dict[str, Any]]] = {chapter["id"]: [] for chapter in catalog["chapters"]}
    excluded_by_prerequisite: Counter[str] = Counter()
    prerequisite_covered_occurrences: Counter[str] = Counter()
    excluded_interface_occurrences: Counter[str] = Counter()
    for identity, counts in occurrences.items():
        identity_text = "|".join(identity)
        mapped_frequency = sum(counts.values())
        for chapter_id in sorted(counts, key=chapter_order.get):
            if not core_eligible(identity):
                excluded_interface_occurrences[chapter_id] += counts[chapter_id]
            taught_by_prerequisite = any(
                (identity_text, prerequisite) in published
                for prerequisite in prerequisites[chapter_id]
            )
            if taught_by_prerequisite:
                excluded_by_prerequisite[chapter_id] += 1
                if core_eligible(identity):
                    prerequisite_covered_occurrences[chapter_id] += counts[chapter_id]
                continue
            later = sorted(
                (
                    other_id for other_id in counts
                    if chapter_order[other_id] > chapter_order[chapter_id]
                ),
                key=chapter_order.get,
            )
            row = {
                **metadata[identity],
                "identity": identity_text,
                "chapterFrequency": counts[chapter_id],
                "mappedFrequency": mapped_frequency,
                "gameFrequency": game_occurrences[identity],
                "messageIds": sorted(evidence[identity][chapter_id], key=message_number),
                "laterChapters": later,
                "reviewStatus": (
                    "published" if (identity_text, chapter_id) in published else "candidate"
                ),
                "coreEligible": core_eligible(identity),
            }
            by_chapter[chapter_id].append(row)
    for rows in by_chapter.values():
        rows.sort(key=lambda row: (
            -row["chapterFrequency"], -row["gameFrequency"], -row["mappedFrequency"],
            row["written"], row["reading"], row["senseId"],
        ))
        for rank, row in enumerate(rows, 1):
            row["importanceRank"] = rank

    chapter_summaries = []
    for chapter in catalog["chapters"]:
        chapter_id = chapter["id"]
        rows = by_chapter[chapter_id]
        total_occurrences = prerequisite_covered_occurrences[chapter_id] + sum(
            row["chapterFrequency"] for row in rows if row["coreEligible"]
        )
        published_occurrences = sum(
            row["chapterFrequency"]
            for row in rows
            if row["reviewStatus"] == "published" and row["coreEligible"]
        )
        covered_occurrences = prerequisite_covered_occurrences[chapter_id] + published_occurrences
        target_occurrences = int(
            total_occurrences * CORE_COVERAGE_TARGET_PERCENT / 100.0 + 0.999999
        )
        additional_cards_to_target = 0
        projected_occurrences = covered_occurrences
        for row in rows:
            if projected_occurrences >= target_occurrences:
                break
            if row["reviewStatus"] == "published":
                continue
            if not row["coreEligible"]:
                continue
            projected_occurrences += row["chapterFrequency"]
            additional_cards_to_target += 1
        chapter_summaries.append({
            "chapterId": chapter_id,
            "mappingStatus": next(
                item["status"] for item in mapping["chapters"]
                if item["chapterId"] == chapter_id
            ),
            "mappedMessageCount": len(mapped_existing[chapter_id]),
            "candidateCount": len(rows),
            "excludedByPrerequisiteCount": excluded_by_prerequisite[chapter_id],
            "publishedCount": sum(
                row["reviewStatus"] == "published" for row in rows
            ),
            "totalTokenOccurrences": total_occurrences,
            "prerequisiteCoveredOccurrences": prerequisite_covered_occurrences[chapter_id],
            "publishedCoveredOccurrences": published_occurrences,
            "coveredTokenOccurrences": covered_occurrences,
            "coveragePercent": round(
                covered_occurrences * 100.0 / total_occurrences, 2
            ) if total_occurrences else 100.0,
            "coreCoverageTargetPercent": CORE_COVERAGE_TARGET_PERCENT,
            "additionalCardsToCoreTarget": additional_cards_to_target,
            "projectedCoreCardCount": (
                sum(row["reviewStatus"] == "published" for row in rows)
                + additional_cards_to_target
            ),
            "excludedInterfaceOccurrences": excluded_interface_occurrences[chapter_id],
        })

    summary = {
        "schemaVersion": 2,
        "gameId": catalog["id"],
        "sourceVariant": mapping["sourceVariant"],
        "sourceCorpusVersion": runtime_root.get("metadata", {}).get("corpusVersion"),
        "mappedMessageCount": len(message_chapter),
        "totalMessageCount": len(messages),
        "selectionRule": (
            "Rank by frequency in this chapter, then full-game recurrence; "
            "exclude words taught by any transitive hard-prerequisite deck. "
            "A core deck is complete at 80% of mapped token occurrences; "
            "reviewers may add rarer story-essential vocabulary beyond that gate."
        ),
        "chapters": chapter_summaries,
    }
    return by_chapter, summary


def write_outputs(by_chapter: dict[str, list[dict[str, Any]]], summary: dict[str, Any], out_dir: Path) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    fields = [
        "ImportanceRank", "Identity", "Written", "Reading", "PartOfSpeech", "Meaning", "CoreEligible", "ChapterFrequency",
        "GameFrequency", "MappedFrequency", "MessageIds", "LaterChapters", "ReviewStatus",
    ]
    for chapter in summary["chapters"]:
        chapter_id = chapter["chapterId"]
        with (out_dir / f"{chapter_id}.tsv").open("w", newline="", encoding="utf-8") as output:
            writer = csv.writer(output, delimiter="\t")
            writer.writerow(fields)
            for row in by_chapter[chapter_id]:
                writer.writerow([
                    row["importanceRank"], row["identity"], row["written"], row["reading"], row["partOfSpeech"],
                    row["meaning"], row["coreEligible"], row["chapterFrequency"], row["gameFrequency"], row["mappedFrequency"],
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
    catalog = load_catalog(args.catalog)
    mapping = json.loads(args.mapping.read_text(encoding="utf-8"))
    by_chapter, summary = collect_candidates(runtime, catalog, mapping)
    write_outputs(by_chapter, summary, args.out_dir)
    print(f"Mapped {summary['mappedMessageCount']} of {summary['totalMessageCount']} messages")
    for chapter in summary["chapters"]:
        print(
            f"{chapter['chapterId']}: {chapter['candidateCount']} candidates, "
            f"{chapter['excludedByPrerequisiteCount']} prerequisite repeats excluded, "
            f"{chapter['publishedCount']} published, "
            f"{chapter['coveragePercent']:.2f}% token coverage, "
            f"{chapter['additionalCardsToCoreTarget']} cards to 80% "
            f"({chapter['mappingStatus']})"
        )


if __name__ == "__main__":
    main()
