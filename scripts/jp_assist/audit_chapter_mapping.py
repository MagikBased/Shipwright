#!/usr/bin/env python3
"""Audit chapter-message coverage and collect source-code provenance.

The generated review queue is local/ignored. It contains identifiers and source
paths, but no extracted dialogue, so reviewers can trace an ID to actors and
gameplay logic without treating numeric message ranges as story order.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any, Iterable

from build_chapter_candidates import expand_message_ids, validate_mapping


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_RUNTIME = Path(__file__).parent / "out" / "runtime_data.json"
DEFAULT_CATALOG = (
    REPOSITORY_ROOT / "services" / "learning_platform" / "learning_platform"
    / "content" / "games" / "ocarina-of-time.json"
)
DEFAULT_MAPPING = Path(__file__).parent / "chapter_mapping" / "ocarina-of-time.json"
DEFAULT_SOURCE_ROOT = REPOSITORY_ROOT / "soh"
DEFAULT_OUT_DIR = Path(__file__).parent / "out" / "chapter_mapping_audit"
SOURCE_SUFFIXES = {".c", ".cc", ".cpp"}
HEX_ID = re.compile(r"(?<![0-9A-Fa-f])0x([0-9A-Fa-f]{1,4})(?![0-9A-Fa-f])")
TEXT_CONTEXT = re.compile(
    r"text_?id|message_?id|msg_?id|Message_(?:Start|Continue)Textbox|CS_TEXT",
    re.IGNORECASE,
)


def mapping_ownership(mapping: dict[str, Any]) -> dict[str, str]:
    ownership: dict[str, str] = {}
    for chapter in mapping["chapters"]:
        for message_id in expand_message_ids(chapter):
            ownership[message_id] = chapter["chapterId"]
    return ownership


def source_files(source_root: Path) -> Iterable[Path]:
    for path in source_root.rglob("*"):
        if path.suffix.lower() not in SOURCE_SUFFIXES or not path.is_file():
            continue
        relative = path.relative_to(source_root).as_posix()
        if relative.startswith("soh/Enhancements/JPAssist/"):
            continue
        yield path


def collect_source_references(
    message_ids: set[str], source_root: Path
) -> dict[str, list[str]]:
    references: dict[str, set[str]] = defaultdict(set)
    wanted = {int(message_id, 0): message_id for message_id in message_ids}
    for path in source_files(source_root):
        try:
            content = path.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        relative = path.relative_to(source_root).as_posix()
        for match in HEX_ID.finditer(content):
            value = int(match.group(1), 16)
            message_id = wanted.get(value)
            if not message_id:
                continue
            line_start = content.rfind("\n", 0, match.start()) + 1
            line_end = content.find("\n", match.end())
            if line_end < 0:
                line_end = len(content)
            line = content[line_start:line_end]
            actor_return = (
                value >= 0x1000
                and relative.startswith("src/overlays/actors/")
                and re.search(r"\breturn\b", line)
            )
            if TEXT_CONTEXT.search(line) or actor_return:
                references[message_id].add(relative)
    return {
        message_id: sorted(paths)
        for message_id, paths in references.items()
    }


def build_audit(
    runtime: dict[str, Any],
    catalog: dict[str, Any],
    mapping: dict[str, Any],
    references: dict[str, list[str]],
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    validate_mapping(mapping, catalog)
    messages = runtime.get("messages", runtime)
    ownership = mapping_ownership(mapping)
    runtime_ids = set(messages)
    mapped_ids = runtime_ids.intersection(ownership)
    unmapped_ids = sorted(runtime_ids - mapped_ids, key=lambda value: int(value, 0))
    stale_ids = sorted(set(ownership) - runtime_ids, key=lambda value: int(value, 0))
    mapping_statuses = {
        chapter["chapterId"]: chapter.get("status", "unknown")
        for chapter in mapping["chapters"]
    }
    alignment_counts = Counter(
        messages[message_id]["source"].get("alignmentStatus", "unknown")
        for message_id in unmapped_ids
    )
    prefix_counts = Counter(message_id[2] for message_id in unmapped_ids)
    queue = []
    for message_id in unmapped_ids:
        record = messages[message_id]
        paths = references.get(message_id, [])
        queue.append({
            "messageId": message_id,
            "alignmentStatus": record["source"].get("alignmentStatus", "unknown"),
            "pageCount": len(record.get("pages", [])),
            "tokenCount": sum(len(page.get("tokens", [])) for page in record.get("pages", [])),
            "sourceReferenceCount": len(paths),
            "sourceReferences": paths,
        })
    summary = {
        "schemaVersion": 1,
        "gameId": catalog["id"],
        "sourceVariant": mapping["sourceVariant"],
        "totalMessageCount": len(runtime_ids),
        "mappedMessageCount": len(mapped_ids),
        "unmappedMessageCount": len(unmapped_ids),
        "staleMappedMessageIds": stale_ids,
        "coveragePercent": round(100 * len(mapped_ids) / len(runtime_ids), 2) if runtime_ids else 100.0,
        "unmappedWithSourceReferenceCount": sum(bool(row["sourceReferences"]) for row in queue),
        "unmappedWithoutSourceReferenceCount": sum(not row["sourceReferences"] for row in queue),
        "unmappedAlignmentCounts": dict(sorted(alignment_counts.items())),
        "unmappedIdPrefixCounts": dict(sorted(prefix_counts.items())),
        "mappingStatuses": mapping_statuses,
        "reviewedChapterCount": sum(
            status == "reviewed" for status in mapping_statuses.values()
        ),
        "chapterCount": len(mapping_statuses),
        "complete": not unmapped_ids and not stale_ids,
        "reviewed": all(status == "reviewed" for status in mapping_statuses.values()),
    }
    return summary, queue


def write_outputs(summary: dict[str, Any], queue: list[dict[str, Any]], out_dir: Path) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    with (out_dir / "unmapped_messages.tsv").open("w", newline="", encoding="utf-8") as output:
        writer = csv.writer(output, delimiter="\t")
        writer.writerow([
            "MessageId", "AlignmentStatus", "PageCount", "TokenCount",
            "SourceReferenceCount", "SourceReferences",
        ])
        for row in queue:
            writer.writerow([
                row["messageId"], row["alignmentStatus"], row["pageCount"],
                row["tokenCount"], row["sourceReferenceCount"],
                ",".join(row["sourceReferences"]),
            ])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, default=DEFAULT_RUNTIME)
    parser.add_argument("--catalog", type=Path, default=DEFAULT_CATALOG)
    parser.add_argument("--mapping", type=Path, default=DEFAULT_MAPPING)
    parser.add_argument("--source-root", type=Path, default=DEFAULT_SOURCE_ROOT)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR)
    parser.add_argument(
        "--require-complete", action="store_true",
        help="Exit unsuccessfully unless every runtime message is mapped and no mapping is stale",
    )
    parser.add_argument(
        "--require-reviewed", action="store_true",
        help="Exit unsuccessfully unless every chapter mapping is marked reviewed",
    )
    args = parser.parse_args()
    runtime = json.loads(args.runtime.read_text(encoding="utf-8"))
    catalog = json.loads(args.catalog.read_text(encoding="utf-8"))
    mapping = json.loads(args.mapping.read_text(encoding="utf-8"))
    message_ids = set(runtime.get("messages", runtime))
    references = collect_source_references(message_ids, args.source_root)
    summary, queue = build_audit(runtime, catalog, mapping, references)
    write_outputs(summary, queue, args.out_dir)
    print(
        f"Mapped {summary['mappedMessageCount']} of {summary['totalMessageCount']} messages "
        f"({summary['coveragePercent']:.2f}%); {summary['unmappedMessageCount']} remain"
    )
    print(
        f"Source references found for {summary['unmappedWithSourceReferenceCount']} unmapped messages; "
        f"{summary['unmappedWithoutSourceReferenceCount']} need another provenance source"
    )
    print(
        f"Reviewed mappings: {summary['reviewedChapterCount']} of "
        f"{summary['chapterCount']} chapters"
    )
    if args.require_complete and not summary["complete"]:
        return 1
    if args.require_reviewed and not summary["reviewed"]:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
