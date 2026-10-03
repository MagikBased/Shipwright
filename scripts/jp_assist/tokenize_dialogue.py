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
import functools
import hashlib
import json
import re
from pathlib import Path

from message_codes import parse_english, parse_japanese
from overrides import apply_override

# Katakana -> hiragana, to match the reading convention in the design doc's
        # schema example ("あう", not "アウ"). SudachiPy's reading_form() returns
# katakana.
_KATAKANA_TO_HIRAGANA = {chr(k): chr(k - 0x60) for k in range(0x30A1, 0x30F7)}


def katakana_to_hiragana(text: str) -> str:
    return "".join(_KATAKANA_TO_HIRAGANA.get(ch, ch) for ch in text)


def japanese_number_reading(value: int) -> str:
    """Return a standard native reading for an integer used in UI/dialogue."""
    if value < 0 or value >= 10_000:
        raise ValueError("Japanese number reading supports 0 through 9999")
    if value == 0:
        return "れい"
    digits = ("", "いち", "に", "さん", "よん", "ご", "ろく", "なな", "はち", "きゅう")
    parts: list[str] = []
    thousands, remainder = divmod(value, 1000)
    hundreds, remainder = divmod(remainder, 100)
    tens, ones = divmod(remainder, 10)
    if thousands:
        parts.append({1: "せん", 3: "さんぜん", 8: "はっせん"}.get(
            thousands, digits[thousands] + "せん"
        ))
    if hundreds:
        parts.append({1: "ひゃく", 3: "さんびゃく", 6: "ろっぴゃく", 8: "はっぴゃく"}.get(
            hundreds, digits[hundreds] + "ひゃく"
        ))
    if tens:
        parts.append("じゅう" if tens == 1 else digits[tens] + "じゅう")
    if ones:
        parts.append(digits[ones])
    return "".join(parts)


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


SCHEMA_VERSION = 2
PIPELINE_VERSION = "3"


def sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def is_internal_message_id_echo(text_id: int, japanese_text: str, english_text: str) -> bool:
    """Detect archive/debug records whose entire contents are their own ID."""
    fullwidth = str.maketrans("０１２３４５６７８９ＡＢＣＤＥＦａｂｃｄｅｆ", "0123456789ABCDEFabcdef")
    japanese_id = japanese_text.strip().translate(fullwidth).lower()
    english_id = english_text.strip().lower()
    expected = f"{text_id:04x}"
    return japanese_id == expected and (not english_id or english_id == expected)


class Tokenizer:
    def __init__(self, cache_path: Path):
        from sudachipy import dictionary

        self._tokenizer = dictionary.Dictionary().create()

        from jamdict import Jamdict

        self._jamdict = Jamdict()
        self._cache_path = cache_path
        try:
            self._persistent_cache = json.loads(cache_path.read_text())
        except (OSError, json.JSONDecodeError):
            self._persistent_cache = {}

    def save_cache(self) -> None:
        temporary = self._cache_path.with_suffix(".json.tmp")
        temporary.write_text(json.dumps(self._persistent_cache, ensure_ascii=False))
        temporary.replace(self._cache_path)

    def tokenize_page(self, japanese_text: str, english_text: str) -> list[dict]:
        tokens = []
        placeholder_spans = [match.span() for match in re.finditer(r"\[[^\]]+\]", japanese_text)]
        search_from = 0
        for index, morpheme in enumerate(self._tokenizer.tokenize(japanese_text)):
            surface = morpheme.surface()
            if not surface.strip():
                continue
            pos = morpheme.part_of_speech()[0]
            if pos in ("空白", "補助記号", "記号"):
                continue

            lemma = morpheme.dictionary_form()
            reading = katakana_to_hiragana(morpheme.reading_form())
            normalized_pos = english_part_of_speech(pos)
            if lemma.isascii() and lemma.isdigit():
                dictionary_reading = japanese_number_reading(int(lemma))
                sense = {
                    "meaning": f"number {lemma}",
                    "senseId": f"number:{lemma}",
                    "partOfSpeech": "noun",
                    "note": "Arabic numeral as read in context.",
                }
            else:
                dictionary_reading = self._dictionary_reading(lemma) or reading
                sense = self._lookup_sense(lemma, dictionary_reading, normalized_pos)
            sense = apply_override(lemma, dictionary_reading, sense)

            start = japanese_text.find(surface, search_from)
            if start < 0:
                start = search_from
            search_from = start + len(surface)
            if any(span_start <= start < span_end for span_start, span_end in placeholder_spans):
                continue
            vocabulary_id = f"{lemma}|{dictionary_reading}"
            sense_id = sense.get("senseId", f"unresolved:{vocabulary_id}")
            tokens.append(
                {
                    "id": vocabulary_id,
                    "occurrenceId": f"{start}:{len(surface)}:{index}",
                    "senseId": sense_id,
                    "surface": surface,
                    "lemma": lemma,
                    "reading": reading,
                    "dictionaryReading": dictionary_reading,
                    "partOfSpeech": sense.get("partOfSpeech", normalized_pos),
                    "meaning": sense.get("meaning", ""),
                    "start": start,
                    "length": len(surface),
                    "note": sense.get("note", ""),
                }
            )
        return tokens

    @functools.lru_cache(maxsize=None)
    def _dictionary_reading(self, lemma: str) -> str:
        return "".join(katakana_to_hiragana(item.reading_form()) for item in self._tokenizer.tokenize(lemma))

    @functools.lru_cache(maxsize=None)
    def _lookup_sense(self, lemma: str, reading: str, part_of_speech: str) -> dict:
        # Design doc step 7: "Select the intended sense using the English
        # line and sentence context." Rank JMdict candidates by exact reading,
        # written form, and Sudachi part of speech. English-context
        # disambiguation still belongs to the human-review override step.
        cache_key = "\u001f".join((lemma, reading, part_of_speech))
        if cache_key in self._persistent_cache:
            return self._persistent_cache[cache_key]

        entries = []
        for query in dict.fromkeys((lemma, reading)):
            try:
                result = self._jamdict.lookup(query)
            except Exception:
                continue
            if result.entries:
                entries = result.entries
                break

        candidates = []
        for entry in entries:
            kana_readings = [str(k) for k in entry.kana_forms]
            reading_score = 4 if reading in kana_readings else 0
            lemma_score = 3 if any(str(k) == lemma for k in entry.kanji_forms) else 0
            for sense_index, sense in enumerate(entry.senses):
                pos_text = " ".join(str(value).lower() for value in sense.pos)
                pos_score = 0
                if part_of_speech == "particle" and "particle" in pos_text:
                    pos_score = 8
                elif part_of_speech == "pronoun" and "pronoun" in pos_text:
                    pos_score = 8
                elif part_of_speech == "adverb" and "adverb" in pos_text:
                    pos_score = 8
                elif part_of_speech == "suffix" and "suffix" in pos_text:
                    pos_score = 8
                elif part_of_speech == "auxiliary verb" and "auxiliary" in pos_text:
                    pos_score = 8
                elif part_of_speech == "verb" and "verb" in pos_text:
                    pos_score = 6
                elif part_of_speech in ("noun", "adjectival noun") and "noun" in pos_text:
                    pos_score = 5
                candidates.append((reading_score + lemma_score + pos_score, entry, sense_index, sense))

        if candidates:
            _, entry, sense_index, sense = max(candidates, key=lambda item: item[0])
            selected = {
                "meaning": "; ".join(str(gloss) for gloss in sense.gloss),
                "senseId": f"jmdict:{entry.idseq}:{sense_index}",
            }
            self._persistent_cache[cache_key] = selected
            return selected
        self._persistent_cache[cache_key] = {"meaning": ""}
        return self._persistent_cache[cache_key]


def build_runtime_record(
    text_id: int,
    entry: dict,
    tokenizer: Tokenizer,
    extracted: dict,
    alignment: dict,
) -> dict | None:
    jpn_raw = bytes.fromhex(entry["japanese"]["raw"]) if entry.get("japanese") else b""
    if not jpn_raw:
        return None

    alignment_entry = alignment["messages"].get(f"{text_id:#06x}")
    if alignment_entry is None:
        raise ValueError(f"No dialogue alignment record for {text_id:#06x}")

    english_ids = [int(value, 0) for value in alignment_entry.get("englishMessageIds", [])]
    english_raw_by_id = {}
    english_pages_by_id = {}
    for english_id in english_ids:
        english_entry = extracted.get(f"{english_id:#06x}", {}).get("english")
        if not english_entry:
            raise ValueError(f"Aligned English message {english_id:#06x} is absent")
        raw = bytes.fromhex(english_entry["raw"])
        english_raw_by_id[english_id] = raw
        english_pages_by_id[english_id] = parse_english(raw)

    page_map = {
        item["japanesePageIndex"]: (int(item["englishMessageId"], 0), item["englishPageIndex"])
        for item in alignment_entry.get("pageMap", [])
    }
    jpn_pages = parse_japanese(jpn_raw)

    pages = []
    for i, jpn_page in enumerate(jpn_pages):
        english_source = None
        eng_page = None
        if i in page_map:
            english_id, english_page_index = page_map[i]
            eng_page = english_pages_by_id[english_id][english_page_index]
            english_source = {
                "messageId": f"{english_id:#06x}",
                "pageIndex": english_page_index,
            }

        jpn_text = jpn_page.text
        eng_text = eng_page.text if eng_page else ""

        internal_id_echo = is_internal_message_id_echo(text_id, jpn_text, eng_text)
        pages.append(
            {
                "japanese": jpn_text,
                "english": eng_text,
                "isChoice": bool(jpn_page and jpn_page.is_choice) or bool(eng_page and eng_page.is_choice),
                "choiceCount": (jpn_page.choice_count if jpn_page else 0) or (eng_page.choice_count if eng_page else 0),
                "englishSource": english_source,
                "tokens": tokenizer.tokenize_page(jpn_text, eng_text)
                if jpn_text.strip() and not internal_id_echo
                else [],
            }
        )

    # Some message-table slots are internal labels rather than dialogue: both
    # languages contain only the slot's own four-digit hexadecimal ID. Keep
    # them out of the runtime corpus/deck entirely so validation and coverage
    # represent text a player can actually study.
    if pages and all(is_internal_message_id_echo(text_id, page["japanese"], page["english"]) for page in pages):
        return None

    return {
        "schemaVersion": SCHEMA_VERSION,
        "source": {
            "variant": entry["variant"],
            "messageId": f"{text_id:#06x}",
            "japaneseMessageId": f"{text_id:#06x}",
            "englishMessageIds": [f"{value:#06x}" for value in english_ids],
            "alignmentStatus": alignment_entry["status"],
            "alignmentConfidence": alignment_entry["confidence"],
            "japaneseHash": sha256_hex(jpn_raw),
            "englishHash": sha256_hex(b"\x00".join(english_raw_by_id.values())) if english_raw_by_id else None,
            "englishHashes": [sha256_hex(english_raw_by_id[value]) for value in english_ids],
        },
        "pages": pages,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--extracted", default=None, help="Path to extract_dialogue.py's output JSON")
    parser.add_argument("--alignment", default=None, help="Path to align_dialogue.py's output JSON")
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
    alignment_path = Path(args.alignment) if args.alignment else out_dir / "dialogue_alignment.json"
    alignment = json.loads(alignment_path.read_text())
    extracted_variants = {entry["variant"] for entry in extracted.values()}
    if extracted_variants != {alignment.get("variant")}:
        raise ValueError(f"Alignment variant {alignment.get('variant')} does not match {sorted(extracted_variants)}")

    # The archive ends with internal font/debug/sentinel records (0xFFFC+
    # rather than player-facing dialogue). Including the font glyph table as
    # a sentence creates meaningless vocabulary and a replacement character.
    extracted = {key: value for key, value in extracted.items() if value["textId"] < 0xFFFC}

    if args.text_ids:
        wanted = {int(t, 16) for t in args.text_ids}
        entries = {k: v for k, v in extracted.items() if v["textId"] in wanted}
    else:
        entries = extracted

    print(f"Tokenizing {len(entries)} message(s)...")
    tokenizer = Tokenizer(out_dir / "dictionary_cache.json")

    runtime_messages = {}
    for index, (key, entry) in enumerate(entries.items(), start=1):
        record = build_runtime_record(entry["textId"], entry, tokenizer, extracted, alignment)
        if record is not None:
            runtime_messages[key] = record
        if index % 100 == 0 or index == len(entries):
            tokenizer.save_cache()
            print(f"  {index}/{len(entries)} messages")

    source_digest = sha256_hex(extracted_path.read_bytes())
    pipeline_digest = sha256_hex(b"".join(
        (Path(__file__).read_bytes(), (Path(__file__).parent / "message_codes.py").read_bytes(),
         (Path(__file__).parent / "overrides.py").read_bytes(), alignment_path.read_bytes())
    ))
    corpus_version = f"{extracted_path.stem}-{PIPELINE_VERSION}-{source_digest[:8]}-{pipeline_digest[:8]}"
    alignment_counts = {"exact": 0, "reviewed": 0, "unresolved": 0}
    for record in runtime_messages.values():
        alignment_counts[record["source"]["alignmentStatus"]] += 1
    runtime_data = {
        "metadata": {
            "schemaVersion": SCHEMA_VERSION,
            "pipelineVersion": PIPELINE_VERSION,
            "corpusVersion": corpus_version,
            "generatedAt": datetime.datetime.now(datetime.timezone.utc).isoformat(),
            "messageCount": len(runtime_messages),
            "alignmentCounts": alignment_counts,
        },
        "messages": runtime_messages,
    }

    out_path = out_dir / "runtime_data.json"
    out_path.write_text(json.dumps(runtime_data, ensure_ascii=False, indent=1))
    print(f"Wrote {len(runtime_messages)} message(s) to {out_path} ({corpus_version})")


if __name__ == "__main__":
    main()
