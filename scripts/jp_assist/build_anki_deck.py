#!/usr/bin/env python3
"""Builds an Anki deck from tokenize_dialogue.py's runtime_data.json
(docs/JP_ASSIST_DESIGN.md section 8). Produces a .apkg, a TSV for
inspection, and a plain-text coverage report.

Requires: pip install genanki (see requirements.txt).
"""

import argparse
import csv
import hashlib
import json
from collections import defaultdict
from pathlib import Path

import genanki

MODEL_ID = 1607000001  # Fixed, arbitrary - genanki needs a stable model id
# across regenerations so Anki treats notes as updates, not new cards.

MODEL = genanki.Model(
    MODEL_ID,
    "OoT JP Assist Vocabulary",
    fields=[
        {"name": "Written"},
        {"name": "Reading"},
        {"name": "DictionaryForm"},
        {"name": "PartOfSpeech"},
        {"name": "Meaning"},
        {"name": "JapaneseExample"},
        {"name": "EnglishReference"},
        {"name": "Note"},
        {"name": "MessageIds"},
    ],
    templates=[
        {
            "name": "Recognition",
            "qfmt": "<div style='font-size:32px'>{{Written}}</div>",
            "afmt": (
                "{{FrontSide}}<hr>"
                "<div>{{Reading}} - {{PartOfSpeech}}</div>"
                "<div>{{Meaning}}</div>"
                "<hr><div style='font-size:20px'>{{JapaneseExample}}</div>"
                "<div style='color:#888'>{{EnglishReference}}</div>"
                "{{#Note}}<div style='color:#888'><i>{{Note}}</i></div>{{/Note}}"
            ),
        }
    ],
)


def stable_note_guid(lemma: str, reading: str, sense_id: str) -> str:
    # Design doc 8.3: "Stable IDs should derive from lemma, reading, and
    # selected sense rather than list position. Regenerating the deck must
    # update existing notes instead of creating duplicates." genanki's
    # default guid is a hash of the field values, which would change (and
    # so create a *new* note) if the meaning text is edited even slightly -
    # deriving it explicitly from just lemma+reading+meaning-sense means a
    # wording tweak to JapaneseExample/EnglishReference doesn't fork the
    # note, but a genuinely different sense does get a new note rather than
    # silently overwriting the old one under the same id.
    key = f"{lemma}|{reading}|{sense_id}"
    digest = hashlib.sha256(key.encode("utf-8")).hexdigest()
    return genanki.guid_for(digest)


def collect_unique_words(runtime_data: dict) -> dict[tuple, dict]:
    """Collapses every token occurrence across every message into one
    record per (lemma, reading, meaning) - the same identity stable_note_guid
    uses - keeping the first example sentence seen and the full list of
    message ids the word appeared in."""
    words: dict[tuple, dict] = {}
    for key, record in runtime_data.items():
        for page in record["pages"]:
            for token in page["tokens"]:
                sense_id = token.get("senseId") or hashlib.sha256(
                    token.get("meaning", "").encode("utf-8")
                ).hexdigest()[:16]
                identity = (token["lemma"], token["reading"], sense_id)
                if identity not in words:
                    words[identity] = {
                        "surface": token["surface"],
                        "lemma": token["lemma"],
                        "reading": token["reading"],
                        "partOfSpeech": token["partOfSpeech"],
                        "meaning": token["meaning"],
                        "senseId": sense_id,
                        "note": token.get("note", ""),
                        "japaneseExample": page["japanese"],
                        "englishReference": page["english"],
                        "messageIds": set(),
                        "frequency": 0,
                    }
                words[identity]["messageIds"].add(record["source"]["messageId"])
                words[identity]["frequency"] += 1
    return words


def load_saved_word_ids(progress_path: Path | None) -> set[str]:
    if progress_path is None or not progress_path.exists():
        return set()
    try:
        progress = json.loads(progress_path.read_text())
        return set(progress.get("savedTokenIds", []))
    except (json.JSONDecodeError, OSError):
        return set()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-data", default=None, help="Path to tokenize_dialogue.py's output")
    parser.add_argument(
        "--progress-file",
        default=None,
        help="Optional jp_assist_progress.json to restrict export to saved words only",
    )
    parser.add_argument("--out-dir", default=None, help="Output directory (default: scripts/jp_assist/out)")
    args = parser.parse_args()

    out_dir = Path(args.out_dir) if args.out_dir else Path(__file__).parent / "out"
    out_dir.mkdir(parents=True, exist_ok=True)
    runtime_path = Path(args.runtime_data) if args.runtime_data else out_dir / "runtime_data.json"
    runtime_root = json.loads(runtime_path.read_text())
    runtime_data = runtime_root.get("messages", runtime_root)

    words = collect_unique_words(runtime_data)

    saved_ids = load_saved_word_ids(Path(args.progress_file)) if args.progress_file else None
    if saved_ids is not None:
        words = {k: v for k, v in words.items() if f"{k[0]}|{k[1]}" in saved_ids}
        print(f"Restricting export to {len(words)} saved word(s)")

    deck = genanki.Deck(2059400001, "OoT JP Assist")
    for (lemma, reading, sense_id), word in words.items():
        note = genanki.Note(
            model=MODEL,
            fields=[
                word["surface"],
                word["reading"],
                word["lemma"],
                word["partOfSpeech"],
                word["meaning"],
                word["japaneseExample"],
                word["englishReference"],
                word["note"],
                ", ".join(sorted(word["messageIds"])),
            ],
            guid=stable_note_guid(lemma, reading, sense_id),
            tags=["oot-jp-assist"],
        )
        deck.add_note(note)

    apkg_path = out_dir / "oot_jp_assist.apkg"
    genanki.Package(deck).write_to_file(str(apkg_path))
    print(f"Wrote {len(words)} note(s) to {apkg_path}")

    tsv_path = out_dir / "oot_jp_assist.tsv"
    with open(tsv_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f, delimiter="\t")
        writer.writerow(
            ["Written", "Reading", "DictionaryForm", "PartOfSpeech", "Meaning", "Frequency", "MessageIds"]
        )
        for (lemma, reading, sense_id), word in sorted(words.items(), key=lambda kv: -kv[1]["frequency"]):
            writer.writerow(
                [
                    word["surface"],
                    reading,
                    lemma,
                    word["partOfSpeech"],
                    word["meaning"],
                    word["frequency"],
                    ", ".join(sorted(word["messageIds"])),
                ]
            )
    print(f"Wrote TSV to {tsv_path}")

    # Frequency-ranked token coverage tiers become meaningful automatically
    # when this is run over the full extracted corpus.
    report_path = out_dir / "coverage_report.md"
    total_occurrences = sum(w["frequency"] for w in words.values())
    with open(report_path, "w", encoding="utf-8") as f:
        f.write("# JP Assist Coverage Report\n\n")
        f.write(f"Distinct words: {len(words)}\n\n")
        f.write(f"Total token occurrences: {total_occurrences}\n\n")
        ranked = sorted(words.items(), key=lambda kv: -kv[1]["frequency"])
        f.write("## Frequency-ranked coverage tiers\n\n")
        cumulative = 0
        tier_index = 0
        tiers = (0.80, 0.90, 0.95, 0.99)
        reached = []
        for index, (_, word) in enumerate(ranked, start=1):
            cumulative += word["frequency"]
            while tier_index < len(tiers) and total_occurrences and cumulative / total_occurrences >= tiers[tier_index]:
                reached.append((tiers[tier_index], index))
                tier_index += 1
        for threshold, count in reached:
            f.write(f"- {threshold:.0%} of token occurrences: {count} distinct words\n")
        f.write("\nThese figures reflect the supplied corpus; run the pipeline over every message for whole-game claims.\n\n")
        f.write("| Word | Reading | Meaning | Frequency |\n|---|---|---|---|\n")
        for (lemma, reading, sense_id), word in ranked:
            f.write(f"| {word['surface']} | {reading} | {word['meaning']} | {word['frequency']} |\n")
    print(f"Wrote coverage report to {report_path}")


if __name__ == "__main__":
    main()
