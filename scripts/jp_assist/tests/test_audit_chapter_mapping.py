import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from audit_chapter_mapping import build_audit, collect_source_references


class AuditChapterMappingTest(unittest.TestCase):
    def setUp(self):
        self.catalog = {
            "id": "game",
            "chapters": [{"id": "one", "order": 1}],
        }
        self.mapping = {
            "gameId": "game",
            "sourceVariant": "test",
            "chapters": [{
                "chapterId": "one", "status": "seeded",
                "messageIds": ["0x1000"], "messageRanges": [],
            }],
        }
        self.runtime = {"messages": {
            "0x1000": self.message("exact"),
            "0x2000": self.message("unresolved"),
        }}

    def test_reports_complete_partition_and_stale_ids(self):
        summary, queue = build_audit(
            self.runtime, self.catalog, self.mapping,
            {"0x2000": ["src/actor.c"]},
        )
        self.assertEqual(summary["mappedMessageCount"], 1)
        self.assertEqual(summary["unmappedMessageCount"], 1)
        self.assertEqual(summary["coveragePercent"], 50.0)
        self.assertEqual(summary["unmappedWithSourceReferenceCount"], 1)
        self.assertFalse(summary["complete"])
        self.assertFalse(summary["reviewed"])
        self.assertEqual(summary["reviewedChapterCount"], 0)
        self.assertEqual(queue[0]["sourceReferences"], ["src/actor.c"])

    def test_reports_reviewed_mapping_state(self):
        self.mapping["chapters"][0]["status"] = "reviewed"
        self.runtime["messages"].pop("0x2000")
        summary, queue = build_audit(self.runtime, self.catalog, self.mapping, {})
        self.assertTrue(summary["complete"])
        self.assertTrue(summary["reviewed"])
        self.assertEqual(summary["reviewedChapterCount"], 1)
        self.assertEqual(queue, [])

    def test_collects_only_exact_runtime_ids_and_skips_jp_assist(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "src").mkdir()
            (root / "src" / "actor.c").write_text(
                "actor.textId = 0x2000;\nunrelated = 0x1000;\nother = 0x20000;"
            )
            excluded = root / "soh" / "Enhancements" / "JPAssist"
            excluded.mkdir(parents=True)
            (excluded / "test.cpp").write_text("id = 0x1000;")
            references = collect_source_references({"0x1000", "0x2000"}, root)
        self.assertEqual(references, {"0x2000": ["src/actor.c"]})

    @staticmethod
    def message(status):
        return {
            "source": {"alignmentStatus": status},
            "pages": [{"tokens": [{"lemma": "森"}]}],
        }


if __name__ == "__main__":
    unittest.main()
