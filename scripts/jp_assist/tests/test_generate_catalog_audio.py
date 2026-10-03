import json
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from generate_catalog_audio import NoneProvider, safe_stem, stage_audio  # noqa: E402


class RecordingProvider:
    provider_id = "test-local"
    voice = "test-voice"

    def __init__(self):
        self.calls = []

    def synthesize(self, text, destination):
        self.calls.append((text, destination.name))
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(b"RIFF-test")


class GenerateCatalogAudioTest(unittest.TestCase):
    game = {"id": "game"}
    chapter = {"id": "01-start", "order": 1, "sampleCards": [{
        "id": "森|もり|forest",
        "written": "森",
        "reading": "もり",
        "sentenceJapanese": "森を歩く。",
    }]}

    def test_none_provider_stages_explicit_text_only_manifest(self):
        with tempfile.TemporaryDirectory() as temporary:
            out = Path(temporary)
            manifest = stage_audio(self.game, self.chapter, NoneProvider(), out)
            self.assertEqual(manifest["provider"], "none")
            self.assertEqual(manifest["reviewStatus"], "not-applicable")
            self.assertEqual(manifest["entries"][0]["wordId"], "森|もり")
            self.assertIsNone(manifest["entries"][0]["wordAudio"])
            saved = json.loads((out / "game-01-manifest.json").read_text())
            self.assertEqual(saved, manifest)
            self.assertEqual(list(out.rglob("*.wav")), [])

    def test_provider_stages_both_word_and_sentence_for_review(self):
        with tempfile.TemporaryDirectory() as temporary:
            provider = RecordingProvider()
            out = Path(temporary)
            manifest = stage_audio(self.game, self.chapter, provider, out)
            self.assertEqual([text for text, _ in provider.calls], ["森", "森を歩く。"])
            self.assertEqual(manifest["reviewStatus"], "needs-human-review")
            self.assertTrue((out / manifest["entries"][0]["wordAudio"]).is_file())
            self.assertTrue((out / manifest["entries"][0]["sentenceAudio"]).is_file())

    def test_audio_names_are_stable_and_do_not_expose_card_text(self):
        self.assertEqual(safe_stem("森|もり|forest"), safe_stem("森|もり|forest"))
        self.assertNotIn("森", safe_stem("森|もり|forest"))


if __name__ == "__main__":
    unittest.main()
