import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from export_saved_deck import discover_manifest, saved_word_count  # noqa: E402


class ExportSavedDeckTest(unittest.TestCase):
    def test_modern_manifest_counts_unique_saved_senses(self):
        manifest = {
            "words": [
                {"wordId": "武器|ぶき", "senseId": "weapon", "saved": True},
                {"wordId": "武器|ぶき", "senseId": "weapon", "saved": True},
                {"wordId": "武器|ぶき", "senseId": "arms", "saved": True},
                {"wordId": "森|もり", "senseId": "forest", "saved": False},
            ]
        }
        self.assertEqual(saved_word_count(manifest), 2)

    def test_discovery_selects_newest_download(self):
        with tempfile.TemporaryDirectory(prefix="jp-assist-export-test-") as temporary:
            root = Path(temporary)
            downloads = root / "Downloads"
            current = root / "current"
            downloads.mkdir()
            current.mkdir()
            older = downloads / "jp_assist_cloud_progress.json"
            newer = downloads / "jp_assist_cloud_progress (1).json"
            older.write_text("{}")
            newer.write_text("{}")
            os.utime(older, (1, 1))
            os.utime(newer, (2, 2))
            with patch.object(Path, "home", return_value=root), patch.object(Path, "cwd", return_value=current):
                self.assertEqual(discover_manifest(None), newer)


if __name__ == "__main__":
    unittest.main()
