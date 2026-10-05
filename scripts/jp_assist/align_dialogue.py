#!/usr/bin/env python3
"""Build and review Japanese-to-English message alignment.

Only numeric IDs, page indexes, and review state belong in the committed
manifest. Extracted dialogue remains in the ignored output directory.
"""

import argparse
import csv
import hashlib
import html
import json
from dataclasses import dataclass
from pathlib import Path

from message_codes import Page, parse_english, parse_japanese


ALIGNMENT_SCHEMA_VERSION = 1
_REVIEWED_STATUSES = {"reviewed", "unresolved"}


@dataclass(frozen=True)
class MessageShape:
    pages: tuple[Page, ...]
    textbox_type: int
    textbox_y_pos: int
    source_hash: str

    @property
    def choice_signature(self) -> tuple[tuple[bool, int], ...]:
        return tuple((page.is_choice, page.choice_count) for page in self.pages)


def _shape(entry: dict | None, language: str) -> MessageShape | None:
    if not entry:
        return None
    raw = bytes.fromhex(entry["raw"])
    pages = parse_japanese(raw) if language == "japanese" else parse_english(raw)
    return MessageShape(tuple(pages), entry["textboxType"], entry["textboxYPos"], hashlib.sha256(raw).hexdigest())


def structurally_compatible(japanese: MessageShape, english: MessageShape) -> bool:
    return (
        len(japanese.pages) == len(english.pages)
        and japanese.choice_signature == english.choice_signature
    )


def candidate_score(japanese_id: int, english_id: int, japanese: MessageShape, english: MessageShape) -> int:
    score = max(0, 20 - abs(japanese_id - english_id) * 2)
    score += 25 if len(japanese.pages) == len(english.pages) else max(
        0, 15 - abs(len(japanese.pages) - len(english.pages)) * 5
    )
    score += 40 if japanese.choice_signature == english.choice_signature else 0
    score += 8 if japanese.textbox_type == english.textbox_type else 0
    score += 4 if japanese.textbox_y_pos == english.textbox_y_pos else 0
    score += 3 if any(page.text.strip() for page in english.pages) else 0
    return score


def _id(value: int) -> str:
    return f"{value:#06x}"


def _default_page_map(english_id: int, page_count: int) -> list[dict]:
    return [
        {"japanesePageIndex": index, "englishMessageId": _id(english_id), "englishPageIndex": index}
        for index in range(page_count)
    ]


def _validate_reviewed_override(
    japanese_id: int,
    override: dict,
    japanese_shape: MessageShape,
    english_shapes: dict[int, MessageShape],
) -> dict:
    status = override.get("status")
    if status not in _REVIEWED_STATUSES:
        raise ValueError(f"{_id(japanese_id)}: override status must be reviewed or unresolved")
    if override.get("japaneseHash") != japanese_shape.source_hash:
        raise ValueError(f"{_id(japanese_id)}: reviewed Japanese source hash is missing or stale")
    if status == "unresolved":
        return {
            "japaneseMessageId": _id(japanese_id),
            "englishMessageIds": [],
            "pageMap": [],
            "status": "unresolved",
            "confidence": 0,
            "reviewedJapaneseHash": japanese_shape.source_hash,
        }

    english_ids = [int(value, 0) for value in override.get("englishMessageIds", [])]
    if not english_ids:
        raise ValueError(f"{_id(japanese_id)}: reviewed override needs englishMessageIds")
    if len(set(english_ids)) != len(english_ids):
        raise ValueError(f"{_id(japanese_id)}: englishMessageIds contains duplicates")
    missing = [value for value in english_ids if value not in english_shapes]
    if missing:
        raise ValueError(f"{_id(japanese_id)}: English IDs are absent: {', '.join(_id(v) for v in missing)}")
    expected_english_hashes = {_id(value): english_shapes[value].source_hash for value in english_ids}
    if override.get("englishHashes") != expected_english_hashes:
        raise ValueError(f"{_id(japanese_id)}: reviewed English source hashes are missing or stale")

    page_map = override.get("pageMap")
    if page_map is None and len(english_ids) == 1:
        english_shape = english_shapes[english_ids[0]]
        if len(japanese_shape.pages) == len(english_shape.pages):
            page_map = _default_page_map(english_ids[0], len(japanese_shape.pages))
    if not isinstance(page_map, list) or len(page_map) != len(japanese_shape.pages):
        raise ValueError(f"{_id(japanese_id)}: reviewed pageMap must contain one entry per Japanese page")

    normalized_map = []
    seen_japanese_pages = set()
    for mapping in page_map:
        japanese_page = int(mapping["japanesePageIndex"])
        english_id = int(mapping["englishMessageId"], 0)
        english_page = int(mapping["englishPageIndex"])
        if japanese_page in seen_japanese_pages or not 0 <= japanese_page < len(japanese_shape.pages):
            raise ValueError(f"{_id(japanese_id)}: invalid or duplicate Japanese page {japanese_page}")
        if english_id not in english_ids:
            raise ValueError(f"{_id(japanese_id)}: pageMap references undeclared English ID {_id(english_id)}")
        if not 0 <= english_page < len(english_shapes[english_id].pages):
            raise ValueError(f"{_id(japanese_id)}: invalid English page {english_page} for {_id(english_id)}")
        seen_japanese_pages.add(japanese_page)
        normalized_map.append({
            "japanesePageIndex": japanese_page,
            "englishMessageId": _id(english_id),
            "englishPageIndex": english_page,
        })
    normalized_map.sort(key=lambda item: item["japanesePageIndex"])
    return {
        "japaneseMessageId": _id(japanese_id),
        "englishMessageIds": [_id(value) for value in english_ids],
        "pageMap": normalized_map,
        "status": "reviewed",
            "confidence": 100,
            "reviewedJapaneseHash": japanese_shape.source_hash,
            "reviewedEnglishHashes": expected_english_hashes,
        **({"note": override["note"]} if override.get("note") else {}),
    }


def build_alignment(extracted: dict, manifest: dict, candidate_window: int = 8) -> tuple[dict, list[dict]]:
    japanese_shapes = {
        entry["textId"]: shape
        for entry in extracted.values()
        if (shape := _shape(entry.get("japanese"), "japanese")) is not None
    }
    english_shapes = {
        entry["textId"]: shape
        for entry in extracted.values()
        if (shape := _shape(entry.get("english"), "english")) is not None
    }
    overrides = manifest.get("messages", {})
    canonical_override_keys = {_id(int(key, 0)) for key in overrides}
    if canonical_override_keys != set(overrides):
        raise ValueError("Alignment override keys must use canonical lowercase 0x0000 formatting")
    unknown_overrides = set(overrides) - {_id(value) for value in japanese_shapes}
    if unknown_overrides:
        raise ValueError(f"Alignment overrides have no Japanese source: {', '.join(sorted(unknown_overrides))}")
    records = {}
    review_rows = []

    for japanese_id, japanese_shape in sorted(japanese_shapes.items()):
        key = _id(japanese_id)
        if key in overrides:
            record = _validate_reviewed_override(japanese_id, overrides[key], japanese_shape, english_shapes)
        elif japanese_id in english_shapes and structurally_compatible(japanese_shape, english_shapes[japanese_id]):
            record = {
                "japaneseMessageId": key,
                "englishMessageIds": [key],
                "pageMap": _default_page_map(japanese_id, len(japanese_shape.pages)),
                "status": "exact",
                "confidence": 100,
            }
        else:
            candidates = []
            for english_id in range(japanese_id - candidate_window, japanese_id + candidate_window + 1):
                english_shape = english_shapes.get(english_id)
                if english_shape is None:
                    continue
                candidates.append((candidate_score(japanese_id, english_id, japanese_shape, english_shape), english_id))
            candidates.sort(key=lambda item: (-item[0], abs(item[1] - japanese_id), item[1]))
            suggestions = [
                {"englishMessageId": _id(english_id), "score": score}
                for score, english_id in candidates[:5]
            ]
            record = {
                "japaneseMessageId": key,
                "englishMessageIds": [],
                "pageMap": [],
                "status": "unresolved",
                "confidence": 0,
                "suggestions": suggestions,
            }
            review_rows.append({
                "japanese_id": key,
                "japanese_hash": japanese_shape.source_hash,
                "japanese": "\n--- page ---\n".join(page.text for page in japanese_shape.pages),
                "candidates": [
                    {
                        "id": item["englishMessageId"],
                        "score": item["score"],
                        "hash": english_shapes[int(item["englishMessageId"], 0)].source_hash,
                        "text": "\n--- page ---\n".join(
                            page.text for page in english_shapes[int(item["englishMessageId"], 0)].pages
                        ),
                    }
                    for item in suggestions
                ],
            })
        records[key] = record

    return {
        "schemaVersion": ALIGNMENT_SCHEMA_VERSION,
        "variant": manifest["variant"],
        "messages": records,
    }, review_rows


def write_review_reports(rows: list[dict], tsv_path: Path, html_path: Path) -> None:
    with tsv_path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream, delimiter="\t", quoting=csv.QUOTE_ALL)
        writer.writerow([
            "JapaneseMessageId", "JapaneseHash", "Japanese", "CandidateEnglishMessageId",
            "CandidateEnglishHash", "Score", "English",
        ])
        for row in rows:
            if not row["candidates"]:
                writer.writerow([row["japanese_id"], row["japanese_hash"], row["japanese"], "", "", "", ""])
            for candidate in row["candidates"]:
                writer.writerow([
                    row["japanese_id"], row["japanese_hash"], row["japanese"], candidate["id"],
                    candidate["hash"], candidate["score"], candidate["text"],
                ])

    sections = []
    for row in rows:
        candidates = "".join(
            f"<article><h3>{html.escape(candidate['id'])} · score {candidate['score']}</h3>"
            f"<pre>{html.escape(candidate['text'])}</pre></article>"
            for candidate in row["candidates"]
        ) or "<p>No nearby English candidate.</p>"
        sections.append(
            f"<section><h2>{html.escape(row['japanese_id'])}</h2>"
            f"<pre class='jp'>{html.escape(row['japanese'])}</pre>{candidates}</section>"
        )
    html_path.write_text(
        "<!doctype html><meta charset='utf-8'><title>JP Assist alignment review</title>"
        "<style>body{font:16px sans-serif;max-width:1100px;margin:auto;background:#111;color:#eee}"
        "section{border:1px solid #555;padding:1rem;margin:1rem 0}article{margin-left:2rem}"
        "pre{white-space:pre-wrap}.jp{font-size:1.2rem;color:#ffd84a}</style>"
        f"<h1>Unresolved dialogue alignment ({len(rows)})</h1>{''.join(sections)}",
        encoding="utf-8",
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--extracted", default=None)
    parser.add_argument("--manifest", default=None)
    parser.add_argument("--out-dir", default=None)
    parser.add_argument("--candidate-window", type=int, default=8)
    args = parser.parse_args()

    scripts = Path(__file__).parent
    out_dir = Path(args.out_dir) if args.out_dir else scripts / "out"
    out_dir.mkdir(parents=True, exist_ok=True)
    extracted_path = Path(args.extracted) if args.extracted else out_dir / "N64_NTSC_12.json"
    extracted = json.loads(extracted_path.read_text())
    variant = next(iter(extracted.values()))["variant"] if extracted else extracted_path.stem
    manifest_path = (
        Path(args.manifest) if args.manifest else
        scripts.parents[1] / "games" / "ocarina-of-time" / "pipelines"
        / "alignment" / f"{variant}.json"
    )
    manifest = json.loads(manifest_path.read_text())
    if manifest.get("schemaVersion") != ALIGNMENT_SCHEMA_VERSION or manifest.get("variant") != variant:
        raise ValueError(f"Alignment manifest does not match schema/variant for {variant}")

    alignment, review_rows = build_alignment(extracted, manifest, args.candidate_window)
    alignment_path = out_dir / "dialogue_alignment.json"
    alignment_path.write_text(json.dumps(alignment, ensure_ascii=False, indent=1), encoding="utf-8")
    write_review_reports(review_rows, out_dir / "alignment_review.tsv", out_dir / "alignment_review.html")
    exact = sum(item["status"] == "exact" for item in alignment["messages"].values())
    reviewed = sum(item["status"] == "reviewed" for item in alignment["messages"].values())
    print(f"Aligned {exact} exact and {reviewed} reviewed message(s); {len(review_rows)} need review")
    print(f"Wrote {alignment_path} and local HTML/TSV review reports")


if __name__ == "__main__":
    main()
