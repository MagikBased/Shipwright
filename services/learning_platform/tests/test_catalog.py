import json
import tempfile
import unittest
from pathlib import Path

from learning_platform.catalog import GameCatalog, REQUIRED_CONTENT_REVIEW_CRITERIA


class GameCatalogReviewTest(unittest.TestCase):
    def write_catalog(self, review: dict) -> Path:
        root = Path(self.temporary.name)
        game = {
            "id": "game", "title": "Game", "series": "Series", "platform": "PC",
            "status": "ready", "summary": "Summary", "cardManifest": "game.cards.json",
            "chapters": [{
                "id": "one", "order": 1, "title": "One", "prerequisites": [],
                "recommendedAfter": [], "sampleCards": [],
                "deck": {"reviewedCardCount": 1},
            }],
        }
        manifest = {
            "gameId": "game", "contentReview": review,
            "chapters": [{
                "chapterId": "one", "cards": [{
                    "id": "森|もり|forest",
                    "corpusEvidence": {"identity": "森|もり|sense", "messageIds": ["0x0001"]},
                }],
            }],
        }
        (root / "game.json").write_text(json.dumps(game), encoding="utf-8")
        (root / "game.cards.json").write_text(json.dumps(manifest), encoding="utf-8")
        return root

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()

    def tearDown(self):
        self.temporary.cleanup()

    def test_catalog_exposes_current_content_review(self):
        review = {
            "status": "reviewed", "reviewedCardCount": 1,
            "criteriaVersion": 1,
            "criteria": sorted(REQUIRED_CONTENT_REVIEW_CRITERIA),
        }
        catalog = GameCatalog(self.write_catalog(review))
        self.assertEqual(catalog.get_game("game")["contentReview"], review)

    def test_catalog_rejects_stale_review_count(self):
        review = {
            "status": "reviewed", "reviewedCardCount": 0,
            "criteriaVersion": 1,
            "criteria": sorted(REQUIRED_CONTENT_REVIEW_CRITERIA),
        }
        with self.assertRaisesRegex(ValueError, "stale review card count"):
            GameCatalog(self.write_catalog(review))


if __name__ == "__main__":
    unittest.main()
