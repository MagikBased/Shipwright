#!/usr/bin/env python3
"""Decodes extracted message data, tokenizes the Japanese text with SudachiPy,
looks up dictionary senses with jamdict (JMdict), and emits the runtime
schema described in docs/JP_ASSIST_DESIGN.md section 7.3.

Requires: pip install sudachipy sudachidict_core jamdict jamdict-data
(docs/JP_ASSIST_DESIGN.md section 7.2: "The tokenizer must remain a
build-time tool, not a mandatory game dependency" - this script only runs
offline, never at runtime).
"""

import argparse
import datetime
import hashlib
import json
from pathlib import Path

from message_codes import parse_english, parse_japanese
from overrides import apply_override

# Katakana -> hiragana, to match the reading convention in the design doc's
# schema example ("あう", not "アウ"). SudachiPy's reading_form() returns
# katakana.
_KATAKANA_TO_HIRAGANA = {chr(k): chr(k - 0x60) for k in range(0x30A1, 0x30F7)}


def katakana_to_hiragana(text: str) -> str:
    return "".join(_KATAKANA_TO_HIRAGANA.get(ch, ch) for ch in text)


_POS_MAP = {
    "名詞": "noun",
    "動詞": "verb",
    "形容詞": "adjective",
    "形状詞": "adjectival noun",
    "副詞": "adverb",
    "助詞": "particle",
    "助動詞": "auxiliary verb",
    "接続詞": "conjunction",
    "感動詞": "interjection",
    "代名詞": "pronoun",
    "連体詞": "adnominal",
    "接頭辞": "prefix",
    "接尾辞": "suffix",
    "補助記号": "symbol",
    "記号": "symbol",
    "空白": "whitespace",
}


def english_part_of_speech(sudachi_pos: str) -> str:
    return _POS_MAP.get(sudachi_pos, sudachi_pos)


SCHEMA_VERSION = 1
PIPELINE_VERSION = "1"


def sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


class Tokenizer:
    def __init__(self):
        from sudachipy import dictionary

        self._tokenizer = dictionary.Dictionary().create()

        from jamdict import Jamdict

        self._jamdict = Jamdict()

    def tokenize_page(self, japanese_text: str, english_text: str) -> list[dict]:
        tokens = []
        search_from = 0
        for index, morpheme in enumerate(self._tokenizer.tokenize(japanese_text)):
            surface = morpheme.surface()
            if not surface.strip():
                continue
            pos = morpheme.part_of_speech()[0]
            if pos in ("空白",):
                continue

            lemma = morpheme.dictionary_form()
            reading = katakana_to_hiragana(morpheme.reading_form())
            sense = self._lookup_sense(lemma, reading, english_text)
            sense = apply_override(lemma, reading, sense)

            start = japanese_text.find(surface, search_from)
            if start < 0:
                start = search_from
            search_from = start + len(surface)
            vocabulary_id = f"{lemma}|{reading}"
            sense_id = hashlib.sha256(
                f"{vocabulary_id}|{sense.get('meaning', '')}".encode("utf-8")
            ).hexdigest()[:16]
            tokens.append(
                {
                    "id": vocabulary_id,
                    "occurrenceId": f"{start}:{len(surface)}:{index}",
                    "senseId": sense_id,
                    "surface": surface,
                    "lemma": lemma,
                    "reading": reading,
                    "partOfSpeech": english_part_of_speech(pos),
                    "meaning": sense.get("meaning", ""),
                    "start": start,
                    "length": len(surface),
                    "note": sense.get("note", ""),
                }
            )
        return tokens

    def _lookup_sense(self, lemma: str, reading: str, english_context: str) -> dict:
        # Design doc step 7: "Select the intended sense using the English
        # line and sentence context." This prototype does the simplest
        # possible version of that - picking the jamdict entry whose kana
        # reading matches, then its first sense - rather than real
        # cross-language sense disambiguation. Getting sense selection
        # right for ambiguous words is exactly the human-review step
        # (step 8 / overrides.py) is meant to catch.
        try:
            result = self._jamdict.lookup(lemma)
        except Exception:
            return {"meaning": ""}

        for entry in result.entries:
            kana_readings = [str(k) for k in entry.kana_forms]
            if reading in kana_readings or not entry.kanji_forms:
                if entry.senses:
                    return {"meaning": str(entry.senses[0])}
        if result.entries and result.entries[0].senses:
            return {"meaning": str(result.entries[0].senses[0])}
        return {"meaning": ""}


def build_runtime_record(text_id: int, entry: dict, tokenizer: Tokenizer) -> dict:
    jpn_raw = bytes.fromhex(entry["japanese"]["raw"]) if entry.get("japanese") else b""
    eng_raw = bytes.fromhex(entry["english"]["raw"]) if entry.get("english") else b""

    jpn_pages = parse_japanese(jpn_raw) if jpn_raw else []
    eng_pages = parse_english(eng_raw) if eng_raw else []
    page_count = max(len(jpn_pages), len(eng_pages))

    pages = []
    for i in range(page_count):
        # Page-index mapping fallback (design doc 4.2): clamp to the last
        # available page on either side if JP/EN page counts don't match.
        jpn_page = jpn_pages[min(i, len(jpn_pages) - 1)] if jpn_pages else None
        eng_page = eng_pages[min(i, len(eng_pages) - 1)] if eng_pages else None

        jpn_text = jpn_page.text if jpn_page else ""
        eng_text = eng_page.text if eng_page else ""

        pages.append(
            {
                "japanese": jpn_text,
                "english": eng_text,
                "isChoice": bool(jpn_page and jpn_page.is_choice) or bool(eng_page and eng_page.is_choice),
                "choiceCount": (jpn_page.choice_count if jpn_page else 0) or (eng_page.choice_count if eng_page else 0),
                "tokens": tokenizer.tokenize_page(jpn_text, eng_text) if jpn_text.strip() else [],
            }
        )

    return {
        "schemaVersion": 1,
        "source": {
            "variant": entry["variant"],
            "messageId": f"{text_id:#06x}",
            "japaneseHash": sha256_hex(jpn_raw) if jpn_raw else None,
            "englishHash": sha256_hex(eng_raw) if eng_raw else None,
        },
        "pages": pages,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--extracted", default=None, help="Path to extract_dialogue.py's output JSON")
    parser.add_argument(
        "--text-id",
        action="append",
        dest="text_ids",
        help="Hex textId to process (e.g. 0x1001). May be given multiple times. Default: all extracted messages.",
    )
    parser.add_argument("--out-dir", default=None, help="Output directory (default: scripts/jp_assist/out)")
    args = parser.parse_args()

    out_dir = Path(args.out_dir) if args.out_dir else Path(__file__).parent / "out"
    out_dir.mkdir(parents=True, exist_ok=True)
    extracted_path = Path(args.extracted) if args.extracted else out_dir / "N64_NTSC_12.json"
    extracted = json.loads(extracted_path.read_text())

    if args.text_ids:
        wanted = {int(t, 16) for t in args.text_ids}
        entries = {k: v for k, v in extracted.items() if v["textId"] in wanted}
    else:
        entries = extracted

    print(f"Tokenizing {len(entries)} message(s)...")
    tokenizer = Tokenizer()

    runtime_messages = {}
    for key, entry in entries.items():
        runtime_messages[key] = build_runtime_record(entry["textId"], entry, tokenizer)

    source_digest = sha256_hex(extracted_path.read_bytes())
    corpus_version = f"{extracted_path.stem}-{PIPELINE_VERSION}-{source_digest[:12]}"
    runtime_data = {
        "metadata": {
            "schemaVersion": SCHEMA_VERSION,
            "pipelineVersion": PIPELINE_VERSION,
            "corpusVersion": corpus_version,
            "generatedAt": datetime.datetime.now(datetime.timezone.utc).isoformat(),
            "messageCount": len(runtime_messages),
        },
        "messages": runtime_messages,
    }

    out_path = out_dir / "runtime_data.json"
    out_path.write_text(json.dumps(runtime_data, ensure_ascii=False, indent=1))
    print(f"Wrote {len(runtime_messages)} message(s) to {out_path} ({corpus_version})")


if __name__ == "__main__":
    main()
