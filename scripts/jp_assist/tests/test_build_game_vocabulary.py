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
                {"id": "森|もり", "lemma": "森", "dictionaryReading": "もり", "partOfSpeech": "noun", "meaning": "forest"},
                {"id": "森|もり", "lemma": "森", "dictionaryReading": "もり", "partOfSpeech": "noun", "meaning": "forest"},
                {"id": "ハイラル|はいらる", "lemma": "ハイラル", "dictionaryReading": "はいらる", "partOfSpeech": "noun", "meaning": "proper name"},
            ]}]}}}), encoding="utf-8")
            for level in ("n5", "n4", "n3", "n2", "n1"):
                payload = [{"word": "森", "reading": "もり", "level": "N4"}] if level == "n4" else []
                (root / f"{level}.json").write_text(json.dumps(payload), encoding="utf-8")
            result = build(runtime, root, "test-game")
        self.assertEqual(result["summary"]["uniqueWords"], 2)
        self.assertEqual(result["summary"]["uniqueByLevel"]["N4"], 1)
        self.assertEqual(result["summary"]["uniqueByLevel"]["unclassified"], 1)
        self.assertEqual(result["words"][0]["occurrenceCount"], 2)


if __name__ == "__main__":
    unittest.main()
