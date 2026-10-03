import sys
import unittest
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from message_codes import parse_japanese  # noqa: E402


class JapaneseMessageCodesTest(unittest.TestCase):
    def test_sfx_before_shift_does_not_leak_shift_operand_into_text(self):
        # Real control prefix from message 0x0218:
        # unskippable, SFX + sound ID, shift + amount, quick text, then text.
        raw = bytes.fromhex(
            "9981 f381 6d68 c786 1800 8981 "
            "6e83 6081 4383 4181 4f87 4981 8a81 0a00 "
            "b182 c182 bf82 e682 4981 7081"
        )

        pages = parse_japanese(raw)

        self.assertEqual(len(pages), 1)
        self.assertEqual(pages[0].text, "ハ〜イ、！\nこっちよ！")
        self.assertFalse(pages[0].text.startswith("\x00\x18"))


if __name__ == "__main__":
    unittest.main()
