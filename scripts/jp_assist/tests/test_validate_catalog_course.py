import sys
import unittest
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from validate_catalog_course import (  # noqa: E402
    REQUIRED_REVIEW_CRITERIA,
    audit_course,
    validate_card_content,
)


def card(card_id: str, sentence: str) -> dict:
    return {
        "id": card_id,
        "sentenceJapanese": sentence,
        "sentenceEnglish": f"English {sentence}",
        "corpusEvidence": {"identity": card_id, "messageIds": ["0x0001"]},
    }


class ValidateCatalogCourseTest(unittest.TestCase):
    def test_structural_audit_allows_an_in_progress_course(self):
        first = card("森|もり|forest", "森を歩く。")
        game = {
            "id": "game",
            "contentReview": {
                "status": "reviewed", "reviewedCardCount": 2,
                "criteriaVersion": 1,
                "criteria": sorted(REQUIRED_REVIEW_CRITERIA),
            },
            "chapterModel": {"coreCoverageTargetPercent": 80},
            "chapters": [{
                "id": "one", "prerequisites": [], "reviewedCards": [first],
                "deck": {"reviewedCardCount": 1, "status": "pilot", "downloadAvailable": False},
            }],
        }
        summary = {"chapters": [{
            "chapterId": "one", "coveragePercent": 50.0, "mappingStatus": "seeded",
        }]}
        report = audit_course(game, summary)
        self.assertEqual(report["issues"], [])
        self.assertEqual(report["readyChapterCount"], 0)

    def test_strict_audit_reports_readiness_and_duplicate_examples(self):
        first = card("森|もり|forest", "同じ文。")
        second = card("水|みず|water", "同じ文。")
        game = {
            "id": "game",
            "chapterModel": {"coreCoverageTargetPercent": 80},
            "chapters": [{
                "id": "one", "prerequisites": [], "reviewedCards": [first, second],
                "deck": {"reviewedCardCount": 2, "status": "pilot", "downloadAvailable": False},
            }],
        }
        summary = {"chapters": [{
            "chapterId": "one", "coveragePercent": 70.0, "mappingStatus": "seeded",
        }]}
        issues = audit_course(game, summary, require_ready=True)["issues"]
        self.assertTrue(any("Duplicate sentenceJapanese" in issue for issue in issues))
        self.assertTrue(any("below 80%" in issue for issue in issues))
        self.assertTrue(any("status is not ready" in issue for issue in issues))
        self.assertTrue(any("download is not available" in issue for issue in issues))
        self.assertTrue(any("dialogue mapping is not reviewed" in issue for issue in issues))

    def test_strict_audit_rejects_stale_or_incomplete_content_review(self):
        first = card("森|もり|forest", "森を歩く。")
        game = {
            "id": "game",
            "contentReview": {
                "status": "reviewed", "reviewedCardCount": 0,
                "criteriaVersion": 1, "criteria": [],
            },
            "chapterModel": {"coreCoverageTargetPercent": 80},
            "chapters": [{
                "id": "one", "prerequisites": [], "reviewedCards": [first],
                "deck": {"reviewedCardCount": 1, "status": "ready", "downloadAvailable": True},
            }],
        }
        summary = {"chapters": [{
            "chapterId": "one", "coveragePercent": 80.0, "mappingStatus": "reviewed",
        }]}
        issues = audit_course(game, summary, require_ready=True)["issues"]
        self.assertTrue(any("review count" in issue for issue in issues))
        self.assertTrue(any("missing criteria" in issue for issue in issues))

    def test_card_content_gate_rejects_fragments_and_unrelated_ip_names(self):
        malformed = {
            "id": "森|もり|forest", "written": "森", "reading": "もり",
            "partOfSpeech": "noun", "meaning": "forest",
            "sentenceJapanese": "ハイラルの森",
            "sentenceEnglish": "A forest in Hyrule",
        }
        issues = validate_card_content("one", malformed)
        self.assertTrue(any("Japanese example is not a complete sentence" in issue for issue in issues))
        self.assertTrue(any("English example is not a complete sentence" in issue for issue in issues))
        self.assertTrue(any("unrelated IP name ハイラル" in issue for issue in issues))
        self.assertTrue(any("unrelated IP name Hyrule" in issue for issue in issues))

    def test_card_content_gate_allows_the_target_term_itself(self):
        target = {
            "id": "ゾラ|ぞら|ending", "written": "ゾラ", "reading": "ぞら",
            "partOfSpeech": "suffix", "meaning": "speech ending",
            "sentenceJapanese": "水の王は「待つゾラ」と告げた。",
            "sentenceEnglish": "The water king declared, ‘Wait, Zora.’",
        }
        self.assertEqual(validate_card_content("one", target), [])


if __name__ == "__main__":
    unittest.main()
