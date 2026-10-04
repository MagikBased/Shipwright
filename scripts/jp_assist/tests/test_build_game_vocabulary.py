import json
import tempfile
import unittest
from pathlib import Path

from scripts.jp_assist.build_game_vocabulary import build


class BuildGameVocabularyTest(unittest.TestCase):
    def test_deduplicates_occurrences_and_keeps_unclassified_words(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            runtime = root / "runtime.json"
            runtime.write_text(json.dumps({"messages": {"0x1": {"pages": [{"tokens": [
                {"id": "森|もり", "lemma": "森", "dictionaryReading": "もり", "senseId": "forest", "partOfSpeech": "noun", "meaning": "forest"},
                {"id": "森|もり", "lemma": "森", "dictionaryReading": "もり", "senseId": "forest", "partOfSpeech": "noun", "meaning": "forest"},
                {"id": "森|もり", "lemma": "森", "dictionaryReading": "もり", "senseId": "shrine-grove", "partOfSpeech": "noun", "meaning": "sacred grove"},
                {"id": "ハイラル|はいらる", "lemma": "ハイラル", "dictionaryReading": "はいらる", "senseId": "proper:ハイラル|はいらる", "partOfSpeech": "noun", "meaning": "proper name"},
                {"id": "ゴロ|ごろ", "lemma": "ゴロ", "dictionaryReading": "ごろ", "senseId": "override:ゴロ|ごろ", "partOfSpeech": "suffix", "meaning": "speech ending"},
            ]}]}}}), encoding="utf-8")
            for level in ("n5", "n4", "n3", "n2", "n1"):
                payload = [{"word": "森", "reading": "もり", "level": "N4"}] if level == "n4" else []
                (root / f"{level}.json").write_text(json.dumps(payload), encoding="utf-8")
            result = build(runtime, root, "test-game")
        self.assertEqual(result["summary"]["uniqueWords"], 1)
        self.assertEqual(result["summary"]["uniqueByLevel"]["N4"], 1)
        self.assertEqual(result["summary"]["uniqueByLevel"]["unclassified"], 0)
        self.assertEqual(result["words"][0]["occurrenceCount"], 3)
        self.assertEqual(result["schemaVersion"], 2)
        self.assertEqual(
            [(sense["senseId"], sense["occurrenceCount"]) for sense in result["words"][0]["senses"]],
            [("forest", 2), ("shrine-grove", 1)],
        )

    def test_conjugated_surfaces_collapse_to_one_dictionary_form_word(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            runtime = root / "runtime.json"
            runtime.write_text(json.dumps({"messages": {"0x1": {"pages": [{"tokens": [
                {
                    "surface": "開けた", "lemma": "開ける", "dictionaryReading": "あける",
                    "senseId": "open", "partOfSpeech": "verb", "meaning": "to open",
                },
                {
                    "surface": "開ける", "lemma": "開ける", "dictionaryReading": "あける",
                    "senseId": "open", "partOfSpeech": "verb", "meaning": "to open",
                },
            ]}]}}}), encoding="utf-8")
            for level in ("n5", "n4", "n3", "n2", "n1"):
                (root / f"{level}.json").write_text("[]", encoding="utf-8")

            result = build(runtime, root, "test-game")

        self.assertEqual(result["summary"]["uniqueWords"], 1)
        self.assertEqual(result["words"][0]["wordId"], "開ける|あける")
        self.assertEqual(result["words"][0]["occurrenceCount"], 2)
        self.assertEqual(result["words"][0]["senses"][0]["occurrenceCount"], 2)


if __name__ == "__main__":
    unittest.main()
