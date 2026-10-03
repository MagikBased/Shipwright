#!/usr/bin/env python3
"""Build and validate an Anki deck from a downloaded saved-word manifest."""

import argparse
import importlib.util
import json
import subprocess
import sys
from pathlib import Path


DEFAULT_DECK_NAME = "OoT JP Assist — Saved Words"
DEFAULT_MANIFEST_PATTERN = "jp_assist_cloud_progress*.json"


def discover_manifest(explicit_path: str | None) -> Path:
    if explicit_path:
        path = Path(explicit_path).expanduser()
        if not path.is_file():
            raise SystemExit(f"Saved-word manifest not found: {path}")
        return path

    candidates = []
    for directory in (Path.home() / "Downloads", Path.cwd()):
        if directory.is_dir():
            candidates.extend(path for path in directory.glob(DEFAULT_MANIFEST_PATTERN) if path.is_file())
    if not candidates:
        raise SystemExit(
            "No saved-word manifest found. Download it from the learning dashboard "
            "or pass its path as the first argument."
        )
    return max(candidates, key=lambda path: path.stat().st_mtime)


def saved_word_count(manifest: dict) -> int | None:
    words = manifest.get("words")
    if isinstance(words, list):
        identities = {
            (word.get("wordId"), word.get("senseId") or "")
            for word in words
            if isinstance(word, dict) and word.get("wordId") and word.get("saved", True)
        }
        return len(identities)

    saved_ids = manifest.get("savedTokenIds")
    if isinstance(saved_ids, list):
        # A legacy token ID can resolve to multiple senses, so this count is
        # useful for detecting an empty manifest but not strict validation.
        return len(set(saved_ids))
    return None


def run(command: list[str]) -> None:
    print("+", " ".join(command), flush=True)
    subprocess.run(command, check=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "manifest",
        nargs="?",
        help=f"Downloaded manifest (default: newest ~/{'Downloads'}/{DEFAULT_MANIFEST_PATTERN})",
    )
    parser.add_argument("--runtime-data", default=None, help="Local runtime_data.json")
    parser.add_argument("--out-dir", default=None, help="Output directory")
    parser.add_argument("--output-prefix", default="oot_jp_assist_cloud")
    parser.add_argument("--deck-name", default=DEFAULT_DECK_NAME)
    parser.add_argument("--allow-empty", action="store_true", help="Permit creation of a zero-note deck")
    args = parser.parse_args()

    if importlib.util.find_spec("genanki") is None:
        raise SystemExit(
            "The Anki export dependency is not installed for this Python interpreter.\n"
            "Run: pip install -r scripts/jp_assist/requirements.txt"
        )

    scripts = Path(__file__).resolve().parent
    manifest_path = discover_manifest(args.manifest)
    out_dir = Path(args.out_dir).expanduser() if args.out_dir else scripts / "out"
    runtime_path = (
        Path(args.runtime_data).expanduser() if args.runtime_data else scripts / "out" / "runtime_data.json"
    )
    if not runtime_path.is_file():
        raise SystemExit(
            f"Local runtime corpus not found: {runtime_path}\n"
            "Build/install your personal corpus first, or pass --runtime-data."
        )

    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise SystemExit(f"Could not read saved-word manifest {manifest_path}: {error}") from error

    expected_count = saved_word_count(manifest)
    if expected_count == 0 and not args.allow_empty:
        raise SystemExit(
            f"The selected manifest contains no saved words: {manifest_path}\n"
            "Download a fresh manifest after synchronization, or use --allow-empty intentionally."
        )

    out_dir.mkdir(parents=True, exist_ok=True)
    package_path = out_dir / f"{args.output_prefix}.apkg"
    run(
        [
            sys.executable,
            "-u",
            str(scripts / "build_anki_deck.py"),
            "--runtime-data",
            str(runtime_path),
            "--progress-file",
            str(manifest_path),
            "--out-dir",
            str(out_dir),
            "--output-prefix",
            args.output_prefix,
            "--deck-name",
            args.deck_name,
        ]
    )

    validation = [sys.executable, "-u", str(scripts / "validate_anki.py"), str(package_path)]
    # The modern cloud manifest is sense-specific, so every saved identity
    # should produce exactly one note. Legacy manifests receive structural
    # validation without a potentially incorrect strict count.
    if isinstance(manifest.get("words"), list) and expected_count is not None:
        validation.extend(("--expected-notes", str(expected_count)))
    run(validation)
    print(f"Saved-word deck is ready: {package_path.resolve()}")


if __name__ == "__main__":
    main()
