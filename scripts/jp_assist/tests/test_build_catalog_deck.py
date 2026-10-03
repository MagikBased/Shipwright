import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from build_catalog_deck import (  # noqa: E402
    build_deck,
    load_game,
    select_chapter,
    stable_deck_id,
    stable_note_guid,
    validate_corpus_evidence,
)


class BuildCatalogDeckTest(unittest.TestCase):
    def test_chapter_one_builds_reviewed_text_only_notes(self):
        game = load_game("ocarina-of-time")
        chapter = select_chapter(game, "1")

        deck, media = build_deck(game, chapter)

        self.assertEqual(len(deck.notes), chapter["deck"]["reviewedCardCount"])
        self.assertEqual(media, [])
        self.assertTrue(all(note.fields[6:8] == ["", ""] for note in deck.notes))
        self.assertTrue(all(note.fields[8] == chapter["id"] for note in deck.notes))

    def test_identities_are_stable_and_chapter_scoped(self):
        self.assertEqual(
            stable_deck_id("ocarina-of-time", "01-boy-without-a-fairy"),
            stable_deck_id("ocarina-of-time", "01-boy-without-a-fairy"),
        )
        self.assertNotEqual(
            stable_note_guid("ocarina-of-time", "森|もり|forest"),
            stable_note_guid("another-game", "森|もり|forest"),
        )

    def test_chapter_without_reviewed_content_refuses_empty_package(self):
        game = load_game("ocarina-of-time")
        chapter = select_chapter(game, "2")
        with self.assertRaisesRegex(ValueError, "no reviewed cards"):
            build_deck(game, chapter)

    def test_unknown_game_and_chapter_are_rejected(self):
        with tempfile.TemporaryDirectory() as empty:
            with self.assertRaisesRegex(ValueError, "Unknown catalog game"):
                load_game("missing", Path(empty))
        game = load_game("ocarina-of-time")
        with self.assertRaisesRegex(ValueError, "Unknown chapter"):
            select_chapter(game, "99")

    def test_corpus_evidence_is_bound_to_message_and_stable_identity(self):
        chapter = {"sampleCards": [{
            "id": "森|もり|forest",
            "corpusEvidence": {
                "identity": "森|もり|jmdict:forest",
                "messageIds": ["0x1000"],
            },
        }]}
        runtime = {"messages": {"0x1000": {"pages": [{"tokens": [{
            "lemma": "森", "reading": "もり", "dictionaryReading": "もり",
            "senseId": "jmdict:forest",
        }]}]}}}
        validate_corpus_evidence(chapter, runtime)
        runtime["messages"]["0x1000"]["pages"][0]["tokens"][0]["senseId"] = "changed"
        with self.assertRaisesRegex(ValueError, "stale corpus evidence"):
            validate_corpus_evidence(chapter, runtime)

    def test_corpus_evidence_rejects_missing_message(self):
        chapter = {"sampleCards": [{
            "id": "森|もり|forest",
            "corpusEvidence": {"identity": "森|もり|sense", "messageIds": ["0x9999"]},
        }]}
        with self.assertRaisesRegex(ValueError, "missing message"):
            validate_corpus_evidence(chapter, {"messages": {}})


if __name__ == "__main__":
    unittest.main()
