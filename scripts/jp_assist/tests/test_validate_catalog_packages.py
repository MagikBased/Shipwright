import sys
import unittest
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from build_catalog_deck import load_game  # noqa: E402
from validate_catalog_packages import validate_packages  # noqa: E402


class ValidateCatalogPackagesTest(unittest.TestCase):
    def test_all_cli_and_public_packages_have_identical_stable_notes(self):
        game = load_game("ocarina-of-time")
        results = validate_packages(game)
        self.assertEqual(len(results), 11)
        self.assertEqual(sum(result["noteCount"] for result in results), 796)


if __name__ == "__main__":
    unittest.main()
