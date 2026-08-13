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
PIPELINE_VERSION = "2"


def sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


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
            dictionary_reading = self._dictionary_reading(lemma) or reading
            normalized_pos = english_part_of_speech(pos)
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
                    "partOfSpeech": normalized_pos,
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
        runtime_messages[key] = build_runtime_record(entry["textId"], entry, tokenizer)
        if index % 100 == 0 or index == len(entries):
            tokenizer.save_cache()
            print(f"  {index}/{len(entries)} messages")

    source_digest = sha256_hex(extracted_path.read_bytes())
    pipeline_digest = sha256_hex(b"".join(
        (Path(__file__).read_bytes(), (Path(__file__).parent / "message_codes.py").read_bytes(),
         (Path(__file__).parent / "overrides.py").read_bytes())
    ))
    corpus_version = f"{extracted_path.stem}-{PIPELINE_VERSION}-{source_digest[:8]}-{pipeline_digest[:8]}"
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
