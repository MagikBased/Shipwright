import sys
import unittest
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from build_anki_deck import (  # noqa: E402
    collect_unique_words,
    stable_deck_id,
    stable_note_guid,
)


class BuildAnkiDeckTest(unittest.TestCase):
    def test_custom_decks_have_stable_separate_identities(self):
        saved_name = "OoT JP Assist — Saved Words"
        self.assertEqual(stable_deck_id(saved_name), stable_deck_id(saved_name))
        self.assertNotEqual(stable_deck_id(saved_name), stable_deck_id("OoT JP Assist"))
        self.assertNotEqual(
            stable_note_guid("借り", "かり", "borrow"),
            stable_note_guid("借り", "かり", "borrow", saved_name),
        )

    def test_saved_context_replaces_first_corpus_occurrence(self):
        runtime_data = {
            "first": {
                "source": {"messageId": "0x0001"},
                "pages": [self.page("最初の武器", "First weapon")],
            },
            "saved": {
                "source": {"messageId": "0x0002"},
                "pages": [
                    self.page("別の話", "Another topic", include_token=False),
                    self.page("保存した武器", "Saved weapon"),
                ],
            },
        }
        contexts = {("武器|ぶき", "weapon"): ("0x0002", 1)}

        words = collect_unique_words(runtime_data, contexts)

        card = words[("武器", "ぶき", "weapon")]
        self.assertEqual(card["japaneseExample"], "保存した武器")
        self.assertEqual(card["englishReference"], "Saved weapon")

    @staticmethod
    def page(japanese, english, include_token=True):
        tokens = []
        if include_token:
            tokens.append(
                {
                    "surface": "武器",
                    "lemma": "武器",
                    "reading": "ぶき",
                    "dictionaryReading": "ぶき",
                    "senseId": "weapon",
                    "partOfSpeech": "noun",
                    "meaning": "weapon",
                    "note": "",
                }
            )
        return {"japanese": japanese, "english": english, "tokens": tokens}


if __name__ == "__main__":
    unittest.main()
