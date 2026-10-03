import json
import tempfile
import unittest
from pathlib import Path

from scripts.jp_assist.package_local import package_audio


class PackageLocalAudioTest(unittest.TestCase):
    def test_packages_reviewed_audio_by_stable_word_identity(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            source.mkdir()
            (source / "voice.wav").write_bytes(b"RIFF-test")
            manifest = source / "chapter.json"
            manifest.write_text(json.dumps({
                "reviewStatus": "approved",
                "entries": [{"wordId": "森|もり", "wordAudio": "voice.wav"}],
            }), encoding="utf-8")
            destination = root / "bundle" / "jp_assist"
            files = package_audio([manifest], destination)
            runtime = json.loads((destination / "audio_manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(runtime["entries"][0]["wordId"], "森|もり")
        self.assertTrue(runtime["entries"][0]["wordAudio"].startswith("audio/"))
        self.assertEqual(len(files), 2)

    def test_rejects_unreviewed_audio_by_default(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            audio = root / "voice.wav"
            audio.write_bytes(b"RIFF-test")
            manifest = root / "chapter.json"
            manifest.write_text(json.dumps({
                "reviewStatus": "needs-human-review",
                "entries": [{"wordId": "森|もり", "wordAudio": "voice.wav"}],
            }), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "needs review"):
                package_audio([manifest], root / "bundle")

    def test_rejects_conflicting_clips_for_one_word(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifests = []
            for index, contents in enumerate((b"RIFF-one", b"RIFF-two")):
                source = root / f"source-{index}"
                source.mkdir()
                (source / "voice.wav").write_bytes(contents)
                manifest = source / "chapter.json"
                manifest.write_text(json.dumps({
                    "reviewStatus": "approved",
                    "entries": [{"wordId": "森|もり", "wordAudio": "voice.wav"}],
                }), encoding="utf-8")
                manifests.append(manifest)
            with self.assertRaisesRegex(ValueError, "Conflicting audio"):
                package_audio(manifests, root / "bundle")


if __name__ == "__main__":
    unittest.main()
