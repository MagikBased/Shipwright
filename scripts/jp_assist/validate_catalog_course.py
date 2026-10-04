#!/usr/bin/env python3
"""Audit the complete chapter course and enforce its final release gates."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from build_catalog_deck import (
    DEFAULT_CATALOG_ROOT,
    DEFAULT_RUNTIME_DATA,
    load_game,
    reviewed_cards,
    stable_note_guid,
    terminology_entries,
    validate_corpus_evidence,
    validate_prerequisite_uniqueness,
    validate_terminology_evidence,
)


DEFAULT_CANDIDATE_SUMMARY = Path(__file__).parent / "out" / "chapter_candidates" / "summary.json"


def audit_course(
    game: dict[str, Any], candidate_summary: dict[str, Any], require_ready: bool = False,
) -> dict[str, Any]:
    issues: list[str] = []
    summary_by_chapter = {
        item["chapterId"]: item for item in candidate_summary.get("chapters", [])
    }
    guids: dict[str, str] = {}
    japanese_examples: dict[str, str] = {}
    english_examples: dict[str, str] = {}
    chapter_results = []
    for chapter in game["chapters"]:
        chapter_id = chapter["id"]
        cards = reviewed_cards(chapter)
        expected_count = chapter.get("deck", {}).get("reviewedCardCount")
        if expected_count != len(cards):
            issues.append(
                f"{chapter_id}: catalog count {expected_count} does not match {len(cards)} cards"
            )
        card_ids = [card.get("id") for card in cards]
        if len(card_ids) != len(set(card_ids)):
            issues.append(f"{chapter_id}: duplicate card id")
        try:
            validate_prerequisite_uniqueness(game, chapter)
        except ValueError as error:
            issues.append(str(error))
        for card in cards:
            card_id = card["id"]
            guid = stable_note_guid(game["id"], chapter_id, card_id)
            previous = guids.setdefault(guid, f"{chapter_id}/{card_id}")
            if previous != f"{chapter_id}/{card_id}":
                issues.append(f"Anki GUID collision: {previous} and {chapter_id}/{card_id}")
            for field, seen in (
                ("sentenceJapanese", japanese_examples),
                ("sentenceEnglish", english_examples),
            ):
                normalized = " ".join(card[field].split()).casefold()
                previous_example = seen.setdefault(normalized, f"{chapter_id}/{card_id}")
                if previous_example != f"{chapter_id}/{card_id}":
                    issues.append(
                        f"Duplicate {field}: {previous_example} and {chapter_id}/{card_id}"
                    )
        terms = terminology_entries(chapter)
        term_ids = [entry.get("id") for entry in terms]
        if len(term_ids) != len(set(term_ids)) or any(not value for value in term_ids):
            issues.append(f"{chapter_id}: terminology ids must be present and unique")
        if any(not entry.get("corpusEvidence") for entry in terms):
            issues.append(f"{chapter_id}: terminology entry has no corpus evidence")
        coverage = summary_by_chapter.get(chapter_id)
        if coverage is None:
            issues.append(f"{chapter_id}: missing candidate coverage summary")
            coverage_percent = 0.0
        else:
            coverage_percent = coverage["coveragePercent"]
        mapping_status = coverage.get("mappingStatus") if coverage else None
        target = game.get("chapterModel", {}).get("coreCoverageTargetPercent", 80)
        ready = (
            coverage_percent >= target
            and mapping_status == "reviewed"
            and chapter["deck"].get("status") == "ready"
            and chapter["deck"].get("downloadAvailable") is True
        )
        if require_ready and coverage_percent < target:
            issues.append(
                f"{chapter_id}: {coverage_percent:.2f}% coverage is below {target}%"
            )
        if require_ready and chapter["deck"].get("status") != "ready":
            issues.append(f"{chapter_id}: deck status is not ready")
        if require_ready and chapter["deck"].get("downloadAvailable") is not True:
            issues.append(f"{chapter_id}: deck download is not available")
        if require_ready and mapping_status != "reviewed":
            issues.append(f"{chapter_id}: dialogue mapping is not reviewed")
        chapter_results.append({
            "chapterId": chapter_id,
            "cardCount": len(cards),
            "terminologyCount": len(terms),
            "coveragePercent": coverage_percent,
            "targetPercent": target,
            "mappingStatus": mapping_status,
            "ready": ready,
        })
    return {
        "gameId": game["id"],
        "cardCount": sum(item["cardCount"] for item in chapter_results),
        "terminologyCount": sum(item["terminologyCount"] for item in chapter_results),
        "readyChapterCount": sum(item["ready"] for item in chapter_results),
        "chapterCount": len(chapter_results),
        "chapters": chapter_results,
        "issues": issues,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", default="ocarina-of-time")
    parser.add_argument("--catalog-root", type=Path, default=DEFAULT_CATALOG_ROOT)
    parser.add_argument("--runtime-data", type=Path, default=DEFAULT_RUNTIME_DATA)
    parser.add_argument("--candidate-summary", type=Path, default=DEFAULT_CANDIDATE_SUMMARY)
    parser.add_argument("--require-ready", action="store_true")
    args = parser.parse_args()

    game = load_game(args.game, args.catalog_root)
    summary = json.loads(args.candidate_summary.read_text(encoding="utf-8"))
    report = audit_course(game, summary, args.require_ready)
    runtime = json.loads(args.runtime_data.read_text(encoding="utf-8"))
    for chapter in game["chapters"]:
        try:
            validate_corpus_evidence(chapter, runtime)
            validate_terminology_evidence(chapter, runtime)
        except ValueError as error:
            report["issues"].append(str(error))
    for chapter in report["chapters"]:
        print(
            f"{chapter['chapterId']}: {chapter['cardCount']} cards, "
            f"{chapter['terminologyCount']} terms, "
            f"{chapter['coveragePercent']:.2f}%/{chapter['targetPercent']}% coverage, "
            f"{'ready' if chapter['ready'] else 'in progress'}"
        )
    print(
        f"Total: {report['cardCount']} cards, {report['terminologyCount']} terms; "
        f"{report['readyChapterCount']}/{report['chapterCount']} chapters ready"
    )
    if report["issues"]:
        for issue in report["issues"]:
            print(f"ERROR: {issue}")
        raise SystemExit(1)


if __name__ == "__main__":
    main()
