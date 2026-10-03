import sys
import unittest
import hashlib
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from align_dialogue import build_alignment  # noqa: E402
from tokenize_dialogue import build_runtime_record  # noqa: E402


def japanese_raw(*pages: str) -> str:
    data = bytearray()
    for page_index, text in enumerate(pages):
        for character in text:
            encoded = character.encode("shift_jis")
            if len(encoded) == 1:
                encoded = bytes((0, encoded[0]))
            value = (encoded[0] << 8) | encoded[1]
            data.extend((value & 0xFF, value >> 8))
        if page_index + 1 < len(pages):
            data.extend((0xA5, 0x81))
    data.extend((0x70, 0x81))
    return data.hex()


def english_raw(*pages: str) -> str:
    return b"\x04".join(page.encode("ascii") for page in pages).hex() + "02"


def entry(message_id: int, japanese=None, english=None) -> dict:
    def language(raw):
        return {"textboxType": 0, "textboxYPos": 1, "raw": raw} if raw else None

    return {
        "textId": message_id,
        "variant": "TEST",
        "japanese": language(japanese),
        "english": language(english),
    }


class FakeTokenizer:
    def tokenize_page(self, japanese_text, _english_text):
        return [{"surface": japanese_text}] if japanese_text else []


class DialogueAlignmentTest(unittest.TestCase):
    def setUp(self):
        self.manifest = {"schemaVersion": 1, "variant": "TEST", "messages": {}}

    def test_structurally_compatible_same_id_is_exact(self):
        extracted = {"0x1000": entry(0x1000, japanese_raw("森"), english_raw("forest"))}

        alignment, review = build_alignment(extracted, self.manifest)

        record = alignment["messages"]["0x1000"]
        self.assertEqual(record["status"], "exact")
        self.assertEqual(record["englishMessageIds"], ["0x1000"])
        self.assertEqual(review, [])

    def test_structural_mismatch_is_safely_unresolved_with_candidates(self):
        extracted = {
            "0x1000": entry(0x1000, japanese_raw("森", "剣"), english_raw("wrong")),
            "0x1001": entry(0x1001, None, english_raw("forest", "sword")),
        }

        alignment, review = build_alignment(extracted, self.manifest)

        record = alignment["messages"]["0x1000"]
        self.assertEqual(record["status"], "unresolved")
        self.assertEqual(record["englishMessageIds"], [])
        self.assertEqual(record["suggestions"][0]["englishMessageId"], "0x1001")
        self.assertEqual(len(review), 1)

    def test_reviewed_override_supports_multiple_english_messages(self):
        extracted = {
            "0x1000": entry(0x1000, japanese_raw("森", "剣"), None),
            "0x1001": entry(0x1001, None, english_raw("forest")),
            "0x1002": entry(0x1002, None, english_raw("sword")),
        }
        self.manifest["messages"]["0x1000"] = {
            "status": "reviewed",
            "englishMessageIds": ["0x1001", "0x1002"],
            "japaneseHash": hashlib.sha256(bytes.fromhex(extracted["0x1000"]["japanese"]["raw"])).hexdigest(),
            "englishHashes": {
                key: hashlib.sha256(bytes.fromhex(extracted[key]["english"]["raw"])).hexdigest()
                for key in ("0x1001", "0x1002")
            },
            "pageMap": [
                {"japanesePageIndex": 0, "englishMessageId": "0x1001", "englishPageIndex": 0},
                {"japanesePageIndex": 1, "englishMessageId": "0x1002", "englishPageIndex": 0},
            ],
        }

        alignment, _ = build_alignment(extracted, self.manifest)
        record = build_runtime_record(
            0x1000,
            extracted["0x1000"],
            FakeTokenizer(),
            extracted,
            alignment,
        )

        self.assertEqual(record["source"]["alignmentStatus"], "reviewed")
        self.assertEqual(record["source"]["englishMessageIds"], ["0x1001", "0x1002"])
        self.assertEqual([page["english"] for page in record["pages"]], ["forest", "sword"])
        self.assertEqual(record["pages"][1]["englishSource"], {"messageId": "0x1002", "pageIndex": 0})

    def test_unresolved_runtime_record_never_leaks_same_id_english(self):
        extracted = {"0x1000": entry(0x1000, japanese_raw("森", "剣"), english_raw("wrong"))}
        alignment, _ = build_alignment(extracted, self.manifest)

        record = build_runtime_record(0x1000, extracted["0x1000"], FakeTokenizer(), extracted, alignment)

        self.assertEqual([page["english"] for page in record["pages"]], ["", ""])
        self.assertTrue(all(page["englishSource"] is None for page in record["pages"]))

    def test_manifest_cannot_reference_missing_japanese_source(self):
        self.manifest["messages"]["0x1001"] = {"status": "unresolved"}

        with self.assertRaisesRegex(ValueError, "no Japanese source"):
            build_alignment(
                {"0x1000": entry(0x1000, japanese_raw("森"), english_raw("forest"))},
                self.manifest,
            )


if __name__ == "__main__":
    unittest.main()
