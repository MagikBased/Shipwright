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


def stable_note_guid(
    lemma: str,
    reading: str,
    sense_id: str,
    deck_namespace: str = "OoT JP Assist",
) -> str:
    # Design doc 8.3: "Stable IDs should derive from lemma, reading, and
    # selected sense rather than list position. Regenerating the deck must
    # update existing notes instead of creating duplicates." genanki's
    # default guid is a hash of the field values, which would change (and
    # so create a *new* note) if the meaning text is edited even slightly -
    # deriving it explicitly from just lemma+reading+meaning-sense means a
    # wording tweak to JapaneseExample/EnglishReference doesn't fork the
    # note, but a genuinely different sense does get a new note rather than
    # silently overwriting the old one under the same id.
    # Preserve the original default-deck identities while namespacing custom
    # exports. Otherwise importing a six-card saved deck after the full deck
    # makes Anki merge/move those notes because both packages claim the same
    # note and deck IDs.
    key = f"{lemma}|{reading}|{sense_id}"
    if deck_namespace != "OoT JP Assist":
        key = f"{deck_namespace}|{key}"
    digest = hashlib.sha256(key.encode("utf-8")).hexdigest()
    return genanki.guid_for(digest)


def stable_deck_id(deck_name: str) -> int:
    if deck_name == "OoT JP Assist":
        return 2059400001
    digest = hashlib.sha256(f"jp-assist-deck|{deck_name}".encode("utf-8")).digest()
    return int.from_bytes(digest[:8], "big") & 0x7FFF_FFFF_FFFF_FFFF


def collect_unique_words(
    runtime_data: dict,
    preferred_contexts: dict[tuple[str, str], tuple[str, int | None]] | None = None,
) -> dict[tuple, dict]:
    """Collapses every token occurrence across every message into one
    record per (lemma, reading, meaning) - the same identity stable_note_guid
    uses - keeping the first example sentence seen and the full list of
    message ids the word appeared in."""
    words: dict[tuple, dict] = {}
    preferred_contexts = preferred_contexts or {}
    for key, record in runtime_data.items():
        message_id = record["source"]["messageId"]
        for page_index, page in enumerate(record["pages"]):
            for token in page["tokens"]:
                sense_id = token.get("senseId") or hashlib.sha256(
                    token.get("meaning", "").encode("utf-8")
                ).hexdigest()[:16]
                dictionary_reading = token.get("dictionaryReading", token["reading"])
                identity = (token["lemma"], dictionary_reading, sense_id)
                token_id = f"{token['lemma']}|{dictionary_reading}"
                if identity not in words:
                    words[identity] = {
                        "surface": token["surface"],
                        "lemma": token["lemma"],
                        "reading": dictionary_reading,
                        "partOfSpeech": token["partOfSpeech"],
                        "meaning": token["meaning"],
                        "senseId": sense_id,
                        "note": token.get("note", ""),
                        "japaneseExample": page["japanese"],
                        "englishReference": page["english"],
                        "messageIds": set(),
                        "frequency": 0,
                        "preferredContextMatched": False,
                    }
                preferred = preferred_contexts.get((token_id, sense_id))
                if preferred and not words[identity]["preferredContextMatched"]:
                    preferred_message, preferred_page = preferred
                    if message_id == preferred_message and (preferred_page is None or page_index == preferred_page):
                        words[identity]["japaneseExample"] = page["japanese"]
                        words[identity]["englishReference"] = page["english"]
                        words[identity]["preferredContextMatched"] = True
                words[identity]["messageIds"].add(message_id)
                words[identity]["frequency"] += 1
    return words


def load_saved_word_selection(
    progress_path: Path | None,
) -> tuple[
    set[str],
    set[tuple[str, str]] | None,
    dict[tuple[str, str], tuple[str, int | None]],
]:
    if progress_path is None or not progress_path.exists():
        return set(), None, {}
    try:
        progress = json.loads(progress_path.read_text())
        saved_ids = set(progress.get("savedTokenIds", []))
        cloud_words = progress.get("words")
        if not isinstance(cloud_words, list) or not cloud_words:
            return saved_ids, None, {}
        saved_senses = set()
        contexts = {}
        for word in cloud_words:
            word_id = word.get("wordId")
            sense_id = word.get("senseId") or ""
            if not word_id or not word.get("saved", True):
                continue
            saved_senses.add((word_id, sense_id))
            message_id = word.get("contextMessageId")
            page_index = word.get("contextPageIndex")
            if message_id:
                contexts[(word_id, sense_id)] = (
                    message_id,
                    page_index if isinstance(page_index, int) else None,
                )
        return saved_ids, saved_senses, contexts
    except (json.JSONDecodeError, OSError):
        return set(), None, {}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-data", default=None, help="Path to tokenize_dialogue.py's output")
    parser.add_argument(
        "--progress-file",
        default=None,
        help="Optional jp_assist_progress.json to restrict export to saved words only",
    )
    parser.add_argument("--out-dir", default=None, help="Output directory (default: scripts/jp_assist/out)")
    parser.add_argument("--output-prefix", default="oot_jp_assist", help="Filename prefix for .apkg and .tsv")
    parser.add_argument("--deck-name", default="OoT JP Assist", help="Deck name shown by Anki")
    args = parser.parse_args()

    out_dir = Path(args.out_dir) if args.out_dir else Path(__file__).parent / "out"
    out_dir.mkdir(parents=True, exist_ok=True)
    runtime_path = Path(args.runtime_data) if args.runtime_data else out_dir / "runtime_data.json"
    runtime_root = json.loads(runtime_path.read_text())
    runtime_data = runtime_root.get("messages", runtime_root)

    saved_ids = None
    saved_senses = None
    preferred_contexts = {}
    if args.progress_file:
        saved_ids, saved_senses, preferred_contexts = load_saved_word_selection(Path(args.progress_file))
    words = collect_unique_words(runtime_data, preferred_contexts)

    if saved_ids is not None:
        words = {
            k: v
            for k, v in words.items()
            if (
                (f"{k[0]}|{k[1]}", k[2]) in saved_senses
                if saved_senses is not None
                else f"{k[0]}|{k[1]}" in saved_ids
            )
        }
        print(f"Restricting export to {len(words)} saved word(s)")

    deck = genanki.Deck(stable_deck_id(args.deck_name), args.deck_name)
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
            guid=stable_note_guid(lemma, reading, sense_id, args.deck_name),
            tags=["oot-jp-assist", "defined" if word["meaning"] else "needs-definition"],
        )
        deck.add_note(note)

    apkg_path = out_dir / f"{args.output_prefix}.apkg"
    genanki.Package(deck).write_to_file(str(apkg_path))
    print(f"Wrote {len(words)} note(s) to {apkg_path}")

    tsv_path = out_dir / f"{args.output_prefix}.tsv"
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
    report_path = out_dir / f"{args.output_prefix}_coverage_report.md"
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
