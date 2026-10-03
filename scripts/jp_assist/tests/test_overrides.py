import sys
import unittest
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from overrides import apply_override  # noqa: E402


class OverrideTest(unittest.TestCase):
    def test_washi_pronoun_does_not_use_eagle_dictionary_sense(self):
        result = apply_override(
            "ワシ", "わし", {"meaning": "eagle", "senseId": "jmdict:eagle"}
        )
        self.assertEqual(result["meaning"], "I; me (typically used by an older man)")
        self.assertEqual(result["senseId"], "override:ワシ|わし")
        self.assertEqual(result["partOfSpeech"], "pronoun")
        self.assertIn("not 鷲", result["note"])


if __name__ == "__main__":
    unittest.main()
