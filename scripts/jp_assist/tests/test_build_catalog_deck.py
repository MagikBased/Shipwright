import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))
SERVICE_DIR = SCRIPT_DIR.parents[1] / "services" / "learning_platform"

from build_catalog_deck import (  # noqa: E402
    build_deck,
    load_game,
    select_chapter,
    stable_deck_id,
    stable_note_guid,
    reviewed_cards,
    validate_corpus_evidence,
    validate_prerequisite_uniqueness,
)
SERVICE_EXPORT_PATH = SERVICE_DIR / "learning_platform" / "anki_export.py"
SERVICE_EXPORT_SPEC = importlib.util.spec_from_file_location(
    "learning_platform_anki_export_contract", SERVICE_EXPORT_PATH
)
assert SERVICE_EXPORT_SPEC and SERVICE_EXPORT_SPEC.loader
SERVICE_EXPORT = importlib.util.module_from_spec(SERVICE_EXPORT_SPEC)
SERVICE_EXPORT_SPEC.loader.exec_module(SERVICE_EXPORT)


class BuildCatalogDeckTest(unittest.TestCase):
    def test_every_chapter_builds_reviewed_text_only_notes(self):
        game = load_game("ocarina-of-time")
        self.assertEqual(
            sum(chapter["deck"]["reviewedCardCount"] for chapter in game["chapters"]),
            sum(len(reviewed_cards(chapter)) for chapter in game["chapters"]),
        )
        for chapter in game["chapters"]:
            deck, media = build_deck(game, chapter)
            self.assertEqual(len(deck.notes), len(reviewed_cards(chapter)))
            self.assertEqual(media, [])
            self.assertTrue(all(note.fields[6:8] == ["", ""] for note in deck.notes))
            self.assertTrue(all(note.fields[8] == chapter["id"] for note in deck.notes))

    def test_complete_manifest_is_distinct_from_public_previews(self):
        game = load_game("ocarina-of-time")
        opening = game["chapters"][0]
        self.assertGreater(len(reviewed_cards(opening)), len(opening["sampleCards"]))
        self.assertEqual(len(reviewed_cards(opening)), opening["deck"]["reviewedCardCount"])
        self.assertIn(
            "アイテム|あいてむ|item",
            {card["id"] for card in reviewed_cards(opening)},
        )

    def test_identities_are_stable_and_chapter_scoped(self):
        self.assertEqual(
            stable_deck_id("ocarina-of-time", "01-boy-without-a-fairy"),
            stable_deck_id("ocarina-of-time", "01-boy-without-a-fairy"),
        )
        self.assertEqual(
            stable_note_guid("ocarina-of-time", "one", "森|もり|forest"),
            stable_note_guid("ocarina-of-time", "one", "森|もり|forest"),
        )
        self.assertNotEqual(
            stable_note_guid("ocarina-of-time", "one", "森|もり|forest"),
            stable_note_guid("ocarina-of-time", "two", "森|もり|forest"),
        )
        self.assertNotEqual(
            stable_note_guid("ocarina-of-time", "one", "森|もり|forest"),
            stable_note_guid("another-game", "one", "森|もり|forest"),
        )

    def test_cli_and_public_download_use_the_same_stable_id_contract(self):
        game_id = "ocarina-of-time"
        chapter_id = "01-boy-without-a-fairy"
        card_id = "森|もり|forest"
        self.assertEqual(
            stable_deck_id(game_id, chapter_id),
            SERVICE_EXPORT.stable_deck_id(game_id, chapter_id),
        )
        self.assertEqual(
            stable_note_guid(game_id, chapter_id, card_id),
            SERVICE_EXPORT.stable_note_guid(game_id, chapter_id, card_id),
        )

    def test_chapter_without_reviewed_content_refuses_empty_package(self):
        game = load_game("ocarina-of-time")
        chapter = {"id": "empty", "sampleCards": []}
        with self.assertRaisesRegex(ValueError, "no reviewed cards"):
            build_deck(game, chapter)

    def test_rejects_cards_already_taught_by_transitive_prerequisite(self):
        card = {"id": "forest", "corpusEvidence": {"identity": "森|もり|forest"}}
        game = {"chapters": [
            {"id": "one", "sampleCards": [card]},
            {"id": "two", "prerequisites": ["one"], "sampleCards": []},
            {"id": "three", "prerequisites": ["two"], "sampleCards": [card]},
        ]}
        with self.assertRaisesRegex(ValueError, "repeats prerequisite"):
            validate_prerequisite_uniqueness(game, game["chapters"][2])

    def test_parallel_chapters_may_contain_the_same_card(self):
        card = {"id": "water", "corpusEvidence": {"identity": "水|みず|water"}}
        game = {"chapters": [
            {"id": "one", "sampleCards": []},
            {"id": "left", "prerequisites": ["one"], "sampleCards": [card]},
            {"id": "right", "prerequisites": ["one"], "sampleCards": [card]},
        ]}
        validate_prerequisite_uniqueness(game, game["chapters"][1])
        validate_prerequisite_uniqueness(game, game["chapters"][2])

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
            "sentenceJapanese": "深い森を歩く。",
            "sentenceEnglish": "Walk through the deep forest.",
            "corpusEvidence": {
                "identity": "森|もり|jmdict:forest",
                "messageIds": ["0x1000"],
            },
        }]}
        runtime = {"messages": {"0x1000": {"pages": [{
            "japanese": "ゲームの台詞。", "english": "Game dialogue.", "tokens": [{
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
            "sentenceJapanese": "深い森を歩く。",
            "sentenceEnglish": "Walk through the deep forest.",
            "corpusEvidence": {"identity": "森|もり|sense", "messageIds": ["0x9999"]},
        }]}
        with self.assertRaisesRegex(ValueError, "missing message"):
            validate_corpus_evidence(chapter, {"messages": {}})

    def test_corpus_evidence_rejects_copied_example_sentence(self):
        chapter = {"sampleCards": [{
            "id": "森|もり|forest",
            "sentenceJapanese": " ゲームの 台詞。",
            "sentenceEnglish": "Original translation.",
            "corpusEvidence": {"identity": "森|もり|sense", "messageIds": ["0x1000"]},
        }]}
        runtime = {"messages": {"0x1000": {"pages": [{
            "japanese": "ゲームの台詞。", "english": "Game dialogue.",
            "tokens": [{"lemma": "森", "reading": "もり", "senseId": "sense"}],
        }]}}}
        with self.assertRaisesRegex(ValueError, "copies a corpus sentence"):
            validate_corpus_evidence(chapter, runtime)

if __name__ == "__main__":
    unittest.main()
