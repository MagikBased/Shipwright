#!/usr/bin/env python3
"""Validate the generated JP Assist runtime corpus and emit an actionable report."""

import argparse
import csv
import json
import re
import sys
import unicodedata
from collections import Counter
from pathlib import Path

from jsonschema import Draft7Validator

_ALLOWED_RANGES = (
    (0x3040, 0x309F),  # Hiragana
    (0x30A0, 0x30FF),  # Katakana
    (0x3400, 0x4DBF),  # CJK Extension A
    (0x4E00, 0x9FFF),  # CJK Unified Ideographs
    (0x3000, 0x303F),  # CJK punctuation
    (0xFF00, 0xFFEF),  # Fullwidth forms
)
_HASH_RE = re.compile(r"^[0-9a-f]{64}$")


def is_expected_japanese_char(ch: str) -> bool:
    return ch.isascii() or ch in "…−" or any(low <= ord(ch) <= high for low, high in _ALLOWED_RANGES)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-data", default=None)
    parser.add_argument("--schema", default=None)
    parser.add_argument("--report", default=None)
    parser.add_argument("--review-queue", default=None)
    parser.add_argument("--strict", action="store_true", help="Treat review warnings as failures")
    args = parser.parse_args()

    base = Path(__file__).parent
    runtime_path = Path(args.runtime_data) if args.runtime_data else base / "out" / "runtime_data.json"
    schema_path = Path(args.schema) if args.schema else base / "schema" / "runtime_data.schema.json"
    report_path = Path(args.report) if args.report else base / "out" / "validation_report.md"
    review_path = Path(args.review_queue) if args.review_queue else base / "out" / "review_queue.tsv"

    root = json.loads(runtime_path.read_text())
    schema = json.loads(schema_path.read_text())
    errors: list[str] = []
    warnings: list[str] = []

    # jsonschema 3.x does not understand Draft 2020-12's tuple-valued "type".
    # The corpus currently uses only Draft 7-compatible keywords, so validate
    # the structural contract with its mature Draft 7 validator as well as
    # retaining the newer declaration for documentation/tooling.
    for error in sorted(Draft7Validator(schema).iter_errors(root), key=lambda item: list(item.path)):
        location = ".".join(str(part) for part in error.absolute_path) or "root"
        errors.append(f"Schema: {location}: {error.message}")

    messages = root.get("messages", root)
    metadata = root.get("metadata", {})
    if metadata and metadata.get("messageCount") != len(messages):
        errors.append(f"metadata.messageCount is {metadata.get('messageCount')}, expected {len(messages)}")

    totals = Counter()
    vocabulary_ids: set[str] = set()
    occurrence_ids: set[str] = set()
    suspicious: dict[str, set[str]] = {}
    missing_definitions: dict[str, dict] = {}
    missing_readings: dict[str, dict] = {}

    for key, record in messages.items():
        totals["messages"] += 1
        source = record.get("source", {})
        if source.get("messageId", "").lower() != key.lower():
            errors.append(f"{key}: source.messageId does not match its map key")
        if source.get("japaneseMessageId", "").lower() != key.lower():
            errors.append(f"{key}: source.japaneseMessageId does not match its map key")
        alignment_status = source.get("alignmentStatus")
        totals[f"alignment_{alignment_status}"] += 1
        english_ids = source.get("englishMessageIds", [])
        english_hashes = source.get("englishHashes", [])
        if len(english_ids) != len(english_hashes):
            errors.append(f"{key}: englishMessageIds and englishHashes have different lengths")
        if alignment_status == "unresolved" and english_ids:
            errors.append(f"{key}: unresolved alignment must not select English messages")
        if alignment_status == "unresolved":
            warnings.append(f"{key}: English alignment is unresolved and intentionally suppressed")
        if alignment_status in ("exact", "reviewed") and not english_ids:
            errors.append(f"{key}: resolved alignment has no English message ID")
        for language in ("japanese", "english"):
            digest = source.get(f"{language}Hash")
            if digest is not None and not _HASH_RE.fullmatch(digest):
                errors.append(f"{key}: invalid {language}Hash")

        pages = record.get("pages", [])
        if not pages:
            warnings.append(f"{key}: contains no pages")
        for page_index, page in enumerate(pages):
            totals["pages"] += 1
            japanese = page.get("japanese", "")
            english = page.get("english", "")
            english_source = page.get("englishSource")
            if english_source and english_source.get("messageId") not in english_ids:
                errors.append(f"{key} page {page_index}: English source is not declared by the message")
            if alignment_status == "unresolved" and (english.strip() or english_source is not None):
                errors.append(f"{key} page {page_index}: unresolved alignment leaked English text")
            if alignment_status != "unresolved" and bool(japanese.strip()) != bool(english.strip()):
                warnings.append(f"{key} page {page_index}: only one language contains text")
            unexpected = {ch for ch in japanese if not is_expected_japanese_char(ch)}
            if unexpected:
                suspicious.setdefault(key, set()).update(unexpected)

            tokens = page.get("tokens", [])
            if japanese.strip() and not tokens:
                warnings.append(f"{key} page {page_index}: Japanese text has no selectable tokens")
            previous_end = 0
            for token_index, token in enumerate(tokens):
                totals["tokens"] += 1
                location = f"{key} page {page_index} token {token_index}"
                start = token.get("start", -1)
                length = token.get("length", -1)
                surface = token.get("surface", "")
                if start < previous_end:
                    errors.append(f"{location}: offsets overlap or are out of order")
                if start < 0 or length < 1 or japanese[start : start + length] != surface:
                    errors.append(f"{location}: surface does not match its sentence offset")
                previous_end = max(previous_end, start + max(length, 0))

                expected_id = f"{token.get('lemma', '')}|{token.get('dictionaryReading', token.get('reading', ''))}"
                if token.get("id") != expected_id:
                    errors.append(f"{location}: vocabulary id is not lemma|reading")
                vocabulary_ids.add(expected_id)
                occurrence_key = f"{key}:{page_index}:{token.get('occurrenceId', '')}"
                if occurrence_key in occurrence_ids:
                    errors.append(f"{location}: duplicate occurrenceId")
                occurrence_ids.add(occurrence_key)

                if not token.get("reading"):
                    totals["missing_reading"] += 1
                    item = missing_readings.setdefault(expected_id, {
                        "lemma": token.get("lemma", ""), "reading": token.get("dictionaryReading", ""), "surface": surface,
                        "partOfSpeech": token.get("partOfSpeech", ""), "count": 0,
                        "messageId": key, "japanese": japanese, "english": english,
                    })
                    item["count"] += 1
                if not token.get("meaning"):
                    totals["missing_definition"] += 1
                    item = missing_definitions.setdefault(expected_id, {
                        "lemma": token.get("lemma", ""), "reading": token.get("dictionaryReading", token.get("reading", "")),
                        "surface": surface, "partOfSpeech": token.get("partOfSpeech", ""), "count": 0,
                        "messageId": key, "japanese": japanese, "english": english,
                    })
                    item["count"] += 1

    for key, chars in suspicious.items():
        descriptions = ", ".join(
            f"{ch!r} U+{ord(ch):04X} {unicodedata.name(ch, '?')}" for ch in sorted(chars)
        )
        warnings.append(f"{key}: unexpected characters: {descriptions}")
    for token_id, item in sorted(missing_readings.items(), key=lambda pair: -pair[1]["count"]):
        warnings.append(f"{token_id}: missing reading in {item['count']} occurrence(s)")
    for token_id, item in sorted(missing_definitions.items(), key=lambda pair: -pair[1]["count"]):
        warnings.append(f"{token_id}: missing definition in {item['count']} occurrence(s)")

    expected_alignment_counts = {
        "exact": totals["alignment_exact"],
        "reviewed": totals["alignment_reviewed"],
        "unresolved": totals["alignment_unresolved"],
    }
    if metadata and metadata.get("alignmentCounts") != expected_alignment_counts:
        errors.append(
            f"metadata.alignmentCounts is {metadata.get('alignmentCounts')}, expected {expected_alignment_counts}"
        )

    review_path.parent.mkdir(parents=True, exist_ok=True)
    with review_path.open("w", newline="", encoding="utf-8") as review_file:
        fields = ["VocabularyId", "Surface", "Lemma", "Reading", "PartOfSpeech", "Occurrences",
                  "MessageId", "JapaneseExample", "EnglishReference", "Issue"]
        writer = csv.DictWriter(review_file, fieldnames=fields, delimiter="\t", quoting=csv.QUOTE_ALL,
                                escapechar="\\")
        writer.writeheader()
        for issue, items in (("missing definition", missing_definitions), ("missing reading", missing_readings)):
            for token_id, item in sorted(items.items(), key=lambda pair: -pair[1]["count"]):
                writer.writerow({
                    "VocabularyId": token_id, "Surface": item["surface"], "Lemma": item["lemma"],
                    "Reading": item["reading"], "PartOfSpeech": item["partOfSpeech"],
                    "Occurrences": item["count"], "MessageId": item["messageId"],
                    "JapaneseExample": item["japanese"], "EnglishReference": item["english"], "Issue": issue,
                })

    report_path.parent.mkdir(parents=True, exist_ok=True)
    with report_path.open("w", encoding="utf-8") as report:
        report.write("# JP Assist corpus validation\n\n")
        report.write(f"- Messages: {totals['messages']}\n- Pages: {totals['pages']}\n")
        report.write(f"- Token occurrences: {totals['tokens']}\n- Distinct vocabulary IDs: {len(vocabulary_ids)}\n")
        defined = totals["tokens"] - totals["missing_definition"]
        coverage = (defined / totals["tokens"] * 100) if totals["tokens"] else 100.0
        report.write(f"- Definition coverage: {defined}/{totals['tokens']} ({coverage:.2f}%)\n")
        report.write(f"- Unique definitions needing review: {len(missing_definitions)}\n")
        report.write(
            f"- Dialogue alignment: {totals['alignment_exact']} exact, "
            f"{totals['alignment_reviewed']} reviewed, {totals['alignment_unresolved']} unresolved\n"
        )
        report.write(f"- Errors: {len(errors)}\n- Review warnings: {len(warnings)}\n\n")
        report.write("## Errors\n\n")
        report.write("\n".join(f"- {item}" for item in errors) or "None")
        report.write("\n\n## Review warnings\n\n")
        report.write("\n".join(f"- {item}" for item in warnings) or "None")
        report.write("\n")

    print(f"Validated {totals['messages']} messages, {totals['pages']} pages, {totals['tokens']} tokens")
    print(f"Errors: {len(errors)}; review warnings: {len(warnings)}; report: {report_path}")
    print(f"Review queue: {review_path}")
    if errors or (args.strict and warnings):
        sys.exit(1)


if __name__ == "__main__":
    main()
