import sys
import unittest
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from build_chapter_candidates import collect_candidates, expand_message_ids, validate_mapping


class BuildChapterCandidatesTest(unittest.TestCase):
    def setUp(self):
        self.catalog = {
            "id": "game",
            "chapters": [
                {"id": "one", "order": 1, "sampleCards": [{
                    "corpusEvidence": {"identity": "森|もり|sense-forest"}
                }]},
                {"id": "two", "order": 2},
            ],
        }
        self.mapping = {
            "gameId": "game",
            "sourceVariant": "test",
            "chapters": [
                {"chapterId": "one", "status": "reviewed", "messageIds": ["0x1000"], "messageRanges": []},
                {"chapterId": "two", "status": "seeded", "messageIds": [], "messageRanges": [{"first": "0x2000", "last": "0x2001"}]},
            ],
        }

    def test_assigns_shared_word_to_earliest_chapter_without_dialogue_output(self):
        runtime = {"metadata": {"corpusVersion": "v1"}, "messages": {
            "0x1000": self.message("森", "もり", "sense-forest", "forest"),
            "0x2000": self.message("森", "もり", "sense-forest", "forest"),
            "0x2001": self.message("水", "みず", "sense-water", "water"),
        }}
        by_chapter, summary = collect_candidates(runtime, self.catalog, self.mapping)
        forest = by_chapter["one"][0]
        self.assertEqual(forest["reviewStatus"], "published")
        self.assertEqual(forest["laterChapters"], ["two"])
        self.assertEqual(by_chapter["two"][0]["written"], "水")
        self.assertNotIn("japanese", forest)
        self.assertNotIn("english", forest)
        self.assertEqual(summary["mappedMessageCount"], 3)

    def test_mapping_rejects_overlap_and_catalog_drift(self):
        self.mapping["chapters"][1]["messageIds"] = ["0x1000"]
        with self.assertRaisesRegex(ValueError, "belongs to both"):
            validate_mapping(self.mapping, self.catalog)
        self.mapping["chapters"][1]["messageIds"] = []
        self.mapping["chapters"].reverse()
        with self.assertRaisesRegex(ValueError, "catalog order"):
            validate_mapping(self.mapping, self.catalog)

    def test_expands_inclusive_ranges_and_rejects_reverse(self):
        self.assertEqual(
            expand_message_ids({"chapterId": "x", "messageRanges": [{"first": "0x10", "last": "0x12"}]}),
            {"0x0010", "0x0011", "0x0012"},
        )
        with self.assertRaisesRegex(ValueError, "Reversed"):
            expand_message_ids({"chapterId": "x", "messageRanges": [{"first": "0x12", "last": "0x10"}]})

    @staticmethod
    def message(lemma, reading, sense_id, meaning):
        return {"pages": [{
            "japanese": "private game dialogue",
            "english": "private translation",
            "tokens": [{
                "lemma": lemma, "reading": reading, "dictionaryReading": reading,
                "senseId": sense_id, "partOfSpeech": "noun", "meaning": meaning,
            }],
        }]}


if __name__ == "__main__":
    unittest.main()
