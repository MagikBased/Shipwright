import sys
import unittest
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from validate_catalog_course import audit_course  # noqa: E402


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
            "chapterModel": {"coreCoverageTargetPercent": 80},
            "chapters": [{
                "id": "one", "prerequisites": [], "reviewedCards": [first],
                "terminology": [{
                    "id": "forest-name", "corpusEvidence": {
                        "identity": "森|もり|proper:forest", "messageIds": ["0x0001"],
                    },
                }],
                "deck": {"reviewedCardCount": 1, "status": "pilot", "downloadAvailable": False},
            }],
        }
        summary = {"chapters": [{"chapterId": "one", "coveragePercent": 50.0}]}
        report = audit_course(game, summary)
        self.assertEqual(report["issues"], [])
        self.assertEqual(report["readyChapterCount"], 0)
        self.assertEqual(report["terminologyCount"], 1)

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
        summary = {"chapters": [{"chapterId": "one", "coveragePercent": 70.0}]}
        issues = audit_course(game, summary, require_ready=True)["issues"]
        self.assertTrue(any("Duplicate sentenceJapanese" in issue for issue in issues))
        self.assertTrue(any("below 80%" in issue for issue in issues))
        self.assertTrue(any("status is not ready" in issue for issue in issues))
        self.assertTrue(any("download is not available" in issue for issue in issues))


if __name__ == "__main__":
    unittest.main()
