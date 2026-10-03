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

    def test_oira_is_singular_and_controller_label_is_not_vocabulary(self):
        oira = apply_override("オイラ", "おいら", {"meaning": "we; us"})
        self.assertEqual(oira["meaning"], "I; me (casual, rustic)")
        self.assertEqual(oira["partOfSpeech"], "pronoun")
        control = apply_override("c", "c", {"meaning": "letter C"})
        self.assertEqual(control["senseId"], "interface:c-button")
        self.assertEqual(control["partOfSpeech"], "interface label")

    def test_proper_names_are_identified_and_contextual_senses_win(self):
        proper = apply_override("サリア", "さりあ", {"meaning": "unknown"})
        self.assertTrue(proper["senseId"].startswith("proper:"))
        slingshot = apply_override("パチンコ", "ぱちんこ", {"meaning": "pachinko"})
        self.assertEqual(slingshot["meaning"], "slingshot")
        good = apply_override("よい", "よい", {"meaning": "evening"})
        self.assertEqual(good["meaning"], "good; fine; all right")
        buy = apply_override("かう", "かう", {"meaning": "raise an animal"})
        self.assertEqual(buy["partOfSpeech"], "verb")
        fruit = apply_override("実", "じつ", {"meaning": "truth"})
        self.assertEqual(fruit["dictionaryReading"], "み")
        self.assertEqual(fruit["senseId"], "override:実|み")


if __name__ == "__main__":
    unittest.main()
