#!/usr/bin/env python3
"""Audit the complete chapter course and enforce its final release gates."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any

from build_catalog_deck import (
    DEFAULT_CATALOG_ROOT,
    DEFAULT_RUNTIME_DATA,
    load_game,
    reviewed_cards,
    stable_note_guid,
    validate_corpus_evidence,
    validate_prerequisite_uniqueness,
)


DEFAULT_CANDIDATE_SUMMARY = Path(__file__).parent / "out" / "chapter_candidates" / "summary.json"
REQUIRED_REVIEW_CRITERIA = {
    "japanese-usage-and-reading",
    "english-translation-and-sense",
    "original-non-corpus-example",
    "fantasy-tone-without-unrelated-ip-names",
    "spoiler-appropriate-for-chapter",
}
JAPANESE_SENTENCE_END = re.compile(r"[。！？][」』”’]?$")
ENGLISH_SENTENCE_END = re.compile(r"[.!?][\"'”’]?$")
JAPANESE_SCRIPT = re.compile(r"[ぁ-んァ-ヶ一-龯]")
ENGLISH_TEXT = re.compile(r"[A-Za-z]")
IP_NAMES = {
    "ハイラル": "Hyrule",
    "ゼルダ": "Zelda",
    "ガノンドロフ": "Ganondorf",
    "ガノン": "Ganon",
    "コキリ": "Kokiri",
    "デク": "Deku",
    "ゴロン": "Goron",
    "ゾラ": "Zora",
    "ゲルド": "Gerudo",
    "トライフォース": "Triforce",
    "ナビィ": "Navi",
    "エポナ": "Epona",
    "ラウル": "Rauru",
    "サリア": "Saria",
    "ダルニア": "Darunia",
    "ルト": "Ruto",
    "シーク": "Sheik",
    "インパ": "Impa",
    "ナボール": "Nabooru",
}


def validate_content_review(game: dict[str, Any]) -> list[str]:
    """Require a review record that covers the exact published corpus."""
    review = game.get("contentReview") or {}
    issues: list[str] = []
    card_count = sum(len(reviewed_cards(chapter)) for chapter in game["chapters"])
    if review.get("status") != "reviewed":
        issues.append("card manifest content review status is not reviewed")
    if review.get("criteriaVersion") != 1:
        issues.append("card manifest content review criteria version is not 1")
    if review.get("reviewedCardCount") != card_count:
        issues.append(
            "card manifest content review count "
            f"{review.get('reviewedCardCount')} does not match {card_count} cards"
        )
    criteria = set(review.get("criteria", []))
    missing = sorted(REQUIRED_REVIEW_CRITERIA - criteria)
    if missing:
        issues.append(f"card manifest review is missing criteria: {', '.join(missing)}")
    return issues


def validate_card_content(chapter_id: str, card: dict[str, Any]) -> list[str]:
    """Check objective editorial invariants that can be enforced automatically."""
    label = f"{chapter_id}/{card.get('id', '<missing-id>')}"
    issues: list[str] = []
    required = (
        "id", "written", "reading", "partOfSpeech", "meaning",
        "sentenceJapanese", "sentenceEnglish",
    )
    missing = [field for field in required if not str(card.get(field, "")).strip()]
    if missing:
        return [f"{label}: missing card fields: {', '.join(missing)}"]
    japanese = card["sentenceJapanese"].strip()
    english = card["sentenceEnglish"].strip()
    if not JAPANESE_SCRIPT.search(japanese) or not JAPANESE_SENTENCE_END.search(japanese):
        issues.append(f"{label}: Japanese example is not a complete sentence")
    if not ENGLISH_TEXT.search(english) or not ENGLISH_SENTENCE_END.search(english):
        issues.append(f"{label}: English example is not a complete sentence")
    written = card["written"]
    for japanese_name, english_name in IP_NAMES.items():
        japanese_pattern = rf"(?<![ァ-ヶー]){re.escape(japanese_name)}(?![ァ-ヶー])"
        if re.search(japanese_pattern, japanese) and japanese_name != written:
            issues.append(f"{label}: Japanese example contains unrelated IP name {japanese_name}")
        if re.search(rf"\b{re.escape(english_name)}\b", english, re.IGNORECASE):
            if japanese_name != written:
                issues.append(f"{label}: English example contains unrelated IP name {english_name}")
    return issues


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
    if require_ready:
        issues.extend(validate_content_review(game))
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
            if require_ready:
                issues.extend(validate_card_content(chapter_id, card))
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
            "coveragePercent": coverage_percent,
            "targetPercent": target,
            "mappingStatus": mapping_status,
            "ready": ready,
        })
    return {
        "gameId": game["id"],
        "cardCount": sum(item["cardCount"] for item in chapter_results),
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
        except ValueError as error:
            report["issues"].append(str(error))
    for chapter in report["chapters"]:
        print(
            f"{chapter['chapterId']}: {chapter['cardCount']} cards, "
            f"{chapter['coveragePercent']:.2f}%/{chapter['targetPercent']}% coverage, "
            f"{'ready' if chapter['ready'] else 'in progress'}"
        )
    print(
        f"Total: {report['cardCount']} cards; "
        f"{report['readyChapterCount']}/{report['chapterCount']} chapters ready"
    )
    if report["issues"]:
        for issue in report["issues"]:
            print(f"ERROR: {issue}")
        raise SystemExit(1)


if __name__ == "__main__":
    main()
