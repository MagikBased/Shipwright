#!/usr/bin/env python3
"""Create or install a local JP Assist data bundle without redistributing dialogue."""

import argparse
import hashlib
import json
import shutil
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def copy_file(source: Path, destination: Path) -> dict:
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    return {
        "path": destination.as_posix(),
        "bytes": destination.stat().st_size,
        "sha256": sha256(destination),
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-data", default=None)
    parser.add_argument("--full-deck", default=None)
    parser.add_argument("--saved-deck", default=None)
    parser.add_argument("--stage-dir", default=None, help="Local bundle output directory")
    parser.add_argument("--install-dir", default=None, help="Directory containing soh.elf/Ship of Harkinian")
    args = parser.parse_args()

    base = Path(__file__).parent
    out = base / "out"
    runtime = Path(args.runtime_data) if args.runtime_data else out / "runtime_data.json"
    full_deck = Path(args.full_deck) if args.full_deck else out / "oot_jp_assist.apkg"
    saved_deck = Path(args.saved_deck) if args.saved_deck else out / "oot_jp_assist_saved.apkg"
    scenarios = base / "test_scenarios.json"
    stage = Path(args.stage_dir) if args.stage_dir else out / "local_bundle"

    root = json.loads(runtime.read_text())
    metadata = root.get("metadata", {})
    manifest = {
        "schemaVersion": 1,
        "corpusVersion": metadata.get("corpusVersion", "unknown"),
        "generatedAt": metadata.get("generatedAt"),
        "notice": "Personal local build derived from the user's game archive; do not redistribute.",
        "files": [],
    }
    manifest["files"].append(copy_file(runtime, stage / "jp_assist" / "runtime_data.json"))
    manifest["files"].append(copy_file(scenarios, stage / "jp_assist" / "test_scenarios.json"))
    for deck in (full_deck, saved_deck):
        if deck.exists():
            manifest["files"].append(copy_file(deck, stage / "anki" / deck.name))
    (stage / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Staged local bundle at {stage}")

    if args.install_dir:
        install_dir = Path(args.install_dir).expanduser().resolve()
        destination = install_dir / "jp_assist" / "runtime_data.json"
        destination.parent.mkdir(parents=True, exist_ok=True)
        temporary = destination.with_suffix(".json.tmp")
        shutil.copy2(runtime, temporary)
        temporary.replace(destination)
        installed_scenarios = destination.parent / "test_scenarios.json"
        copy_file(scenarios, installed_scenarios)
        installed_manifest = {
            "schemaVersion": manifest["schemaVersion"],
            "corpusVersion": manifest["corpusVersion"],
            "runtimeData": {"bytes": destination.stat().st_size, "sha256": sha256(destination)},
            "testScenarios": {
                "bytes": installed_scenarios.stat().st_size,
                "sha256": sha256(installed_scenarios),
            },
        }
        (destination.parent / "manifest.json").write_text(json.dumps(installed_manifest, indent=2) + "\n")
        print(f"Installed runtime corpus at {destination}")


if __name__ == "__main__":
    main()
