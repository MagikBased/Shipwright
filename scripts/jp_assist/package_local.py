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


def package_audio(manifest_paths: list[Path], destination_root: Path, allow_unreviewed: bool = False) -> list[dict]:
    """Copy reviewed word audio and write the runtime identity index."""
    entries: dict[str, dict] = {}
    source_digests: dict[str, str] = {}
    copied = []
    for manifest_path in manifest_paths:
        manifest_path = manifest_path.expanduser().resolve()
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        if manifest.get("reviewStatus") != "approved" and not allow_unreviewed:
            raise ValueError(f"Audio manifest needs review before packaging: {manifest_path}")
        source_root = manifest_path.parent
        for entry in manifest.get("entries", []):
            word_id = entry.get("wordId")
            relative_audio = entry.get("wordAudio")
            if not word_id or not relative_audio:
                continue
            source = (source_root / relative_audio).resolve()
            if not source.is_relative_to(source_root) or not source.is_file():
                raise ValueError(f"Audio path is missing or outside its manifest: {relative_audio}")
            if source.suffix.lower() != ".wav":
                raise ValueError(f"Only WAV word audio can be packaged: {relative_audio}")
            source_digest = sha256(source)
            if word_id in source_digests and source_digests[word_id] != source_digest:
                raise ValueError(f"Conflicting audio entries for {word_id}")
            source_digests[word_id] = source_digest
            suffix = source.suffix.lower() or ".wav"
            filename = f"{hashlib.sha256(word_id.encode('utf-8')).hexdigest()[:20]}{suffix}"
            destination = destination_root / "audio" / filename
            runtime_entry = {"wordId": word_id, "wordAudio": f"audio/{filename}"}
            entries[word_id] = runtime_entry
            if not destination.exists() or sha256(destination) != source_digest:
                copied.append(copy_file(source, destination))
    if not entries:
        return copied
    runtime_manifest = destination_root / "audio_manifest.json"
    runtime_manifest.parent.mkdir(parents=True, exist_ok=True)
    runtime_manifest.write_text(json.dumps({
        "schemaVersion": 1,
        "sampleType": "spoken-word",
        "entries": [entries[word_id] for word_id in sorted(entries)],
    }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    copied.append({
        "path": runtime_manifest.as_posix(),
        "bytes": runtime_manifest.stat().st_size,
        "sha256": sha256(runtime_manifest),
    })
    return copied


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-data", default=None)
    parser.add_argument("--full-deck", default=None)
    parser.add_argument("--saved-deck", default=None)
    parser.add_argument("--stage-dir", default=None, help="Local bundle output directory")
    parser.add_argument("--install-dir", default=None, help="Directory containing soh.elf/Ship of Harkinian")
    parser.add_argument("--audio-manifest", action="append", default=[],
                        help="Reviewed catalog audio manifest; may be supplied once per chapter")
    parser.add_argument("--accept-unreviewed-audio", action="store_true",
                        help="Development-only override for manifests still marked needs-human-review")
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
    audio_manifests = [Path(path) for path in args.audio_manifest]
    manifest["files"].extend(package_audio(
        audio_manifests, stage / "jp_assist", args.accept_unreviewed_audio,
    ))
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
        installed_audio = package_audio(
            audio_manifests, destination.parent, args.accept_unreviewed_audio,
        )
        installed_manifest = {
            "schemaVersion": manifest["schemaVersion"],
            "corpusVersion": manifest["corpusVersion"],
            "runtimeData": {"bytes": destination.stat().st_size, "sha256": sha256(destination)},
            "testScenarios": {
                "bytes": installed_scenarios.stat().st_size,
                "sha256": sha256(installed_scenarios),
            },
            "audioFiles": [{"bytes": item["bytes"], "sha256": item["sha256"]} for item in installed_audio],
        }
        (destination.parent / "manifest.json").write_text(json.dumps(installed_manifest, indent=2) + "\n")
        print(f"Installed runtime corpus at {destination}")


if __name__ == "__main__":
    main()
