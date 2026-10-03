#!/usr/bin/env python3
"""Run the personal corpus, validation, Anki, packaging, and optional install pipeline."""

import argparse
import subprocess
import sys
from pathlib import Path


def run(script_dir: Path, script: str, *arguments: str) -> None:
    command = [sys.executable, "-u", str(script_dir / script), *arguments]
    print("+", " ".join(command), flush=True)
    subprocess.run(command, check=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oot-o2r", default="oot.o2r")
    parser.add_argument("--variant", default="N64_NTSC_12")
    parser.add_argument("--progress-file", default=None)
    parser.add_argument("--install-dir", default=None)
    parser.add_argument("--strict", action="store_true")
    args = parser.parse_args()

    scripts = Path(__file__).parent
    run(scripts, "extract_dialogue.py", "--oot-o2r", args.oot_o2r, "--variant", args.variant)
    extracted = str(scripts / "out" / f"{args.variant}.json")
    manifest = str(scripts / "alignment" / f"{args.variant}.json")
    run(scripts, "align_dialogue.py", "--extracted", extracted, "--manifest", manifest)
    run(scripts, "tokenize_dialogue.py", "--extracted", extracted)
    validation_args = ["--strict"] if args.strict else []
    run(scripts, "validate_corpus.py", *validation_args)
    run(scripts, "build_anki_deck.py")
    run(scripts, "validate_anki.py", str(scripts / "out" / "oot_jp_assist.apkg"))

    if args.progress_file:
        run(
            scripts,
            "build_anki_deck.py",
            "--progress-file", args.progress_file,
            "--output-prefix", "oot_jp_assist_saved",
            "--deck-name", "OoT JP Assist — Saved Words",
        )
        run(scripts, "validate_anki.py", str(scripts / "out" / "oot_jp_assist_saved.apkg"))

    package_args = []
    if args.install_dir:
        package_args.extend(("--install-dir", args.install_dir))
    run(scripts, "package_local.py", *package_args)


if __name__ == "__main__":
    main()
