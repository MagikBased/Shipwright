import sys
import unittest
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from tokenize_dialogue import japanese_number_reading  # noqa: E402


class JapaneseNumberReadingTest(unittest.TestCase):
    def test_common_dialogue_numbers_have_cardinal_readings(self):
        expected = {
            0: "れい",
            1: "いち",
            10: "じゅう",
            15: "じゅうご",
            20: "にじゅう",
            100: "ひゃく",
            300: "さんびゃく",
            600: "ろっぴゃく",
            800: "はっぴゃく",
            1000: "せん",
            3000: "さんぜん",
            8000: "はっせん",
            9999: "きゅうせんきゅうひゃくきゅうじゅうきゅう",
        }
        for value, reading in expected.items():
            with self.subTest(value=value):
                self.assertEqual(japanese_number_reading(value), reading)

    def test_out_of_range_number_is_rejected(self):
        with self.assertRaises(ValueError):
            japanese_number_reading(10_000)


if __name__ == "__main__":
    unittest.main()
