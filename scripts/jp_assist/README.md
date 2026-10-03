# JP Assist corpus and Anki pipeline

These tools convert dialogue extracted from your own `oot.o2r` into the local
runtime corpus used by the mod and an Anki deck. Generated files reproduce game
text, so `out/` is intentionally ignored by Git and must not be distributed.

## Setup

From the repository root:

```bash
python3 -m venv .venv-jp-assist
. .venv-jp-assist/bin/activate
pip install -r scripts/jp_assist/requirements.txt
```

## Build the local corpus

The one-command path builds everything and optionally installs the corpus:

```bash
python scripts/jp_assist/build_all.py \
  --oot-o2r oot.o2r \
  --progress-file build-cmake/soh/jp_assist_progress.json \
  --install-dir build-cmake/soh
```

For individual stages or troubleshooting:

```bash
python scripts/jp_assist/extract_dialogue.py --help
python scripts/jp_assist/extract_dialogue.py --o2r /path/to/oot.o2r
python scripts/jp_assist/align_dialogue.py
python scripts/jp_assist/tokenize_dialogue.py
python scripts/jp_assist/validate_corpus.py
python scripts/jp_assist/build_anki_deck.py
python scripts/jp_assist/validate_anki.py scripts/jp_assist/out/oot_jp_assist.apkg
python scripts/jp_assist/package_local.py --install-dir build-cmake/soh
```

Use each command's `--help` if your paths differ. The output directory will
contain `runtime_data.json`, `oot_jp_assist.apkg`, an inspection TSV, and a
coverage report. Validation also emits `validation_report.md` and a deduplicated
`review_queue.tsv` for missing definitions/readings. Cards without a reviewed
definition are tagged `needs-definition` in Anki. Copy the runtime corpus beside
the game data as:

```text
jp_assist/runtime_data.json
jp_assist/test_scenarios.json
```

Ship of Harkinian's application-directory lookup then finds it at startup.
The exact application data directory varies by installation.

## Review Japanese/English alignment

The extractor cannot assume that the same numeric ID or page number represents
the same content in both language tables. `align_dialogue.py` accepts only
structurally compatible same-ID pairs automatically. It writes suspicious
pairs to ignored local files:

```text
scripts/jp_assist/out/alignment_review.html
scripts/jp_assist/out/alignment_review.tsv
```

Review the Japanese line and nearby English candidates, then add the numeric
mapping to `alignment/<variant>.json`. Never copy dialogue into that committed
file. A one-English-message override can omit `pageMap` when page counts match:

```json
"0x1000": {
  "status": "reviewed",
  "englishMessageIds": ["0x1001"],
  "japaneseHash": "<JapaneseHash from alignment_review.tsv>",
  "englishHashes": {
    "0x1001": "<CandidateEnglishHash from alignment_review.tsv>"
  },
  "note": "Reviewed against the local archive"
}
```

For split/combined entries, list every English ID and provide one mapping for
each Japanese page:

```json
"pageMap": [
  { "japanesePageIndex": 0, "englishMessageId": "0x1001", "englishPageIndex": 0 },
  { "japanesePageIndex": 1, "englishMessageId": "0x1002", "englishPageIndex": 0 }
]
```

Use `"status": "unresolved"` plus the TSV's `japaneseHash` to record that a
pair was reviewed but has no safe English equivalent. Unresolved entries retain
Japanese study tokens while showing “Translation unavailable”; they never fall
back to a same-ID guess.
Reviewed mappings are bound to the local source hashes printed in the TSV; a
changed archive fails the build instead of silently reusing a stale mapping.
`validate_corpus.py --strict` treats every unresolved alignment as a release
failure, while the normal development build reports them without stopping.

To export only words saved with C-Right in Study Mode:

```bash
python scripts/jp_assist/build_anki_deck.py \
  --progress-file /path/to/jp_assist_progress.json \
  --output-prefix oot_jp_assist_saved \
  --deck-name "OoT JP Assist — Saved Words"
```

For a manifest downloaded from the learning website, the shorter command finds
the newest `jp_assist_cloud_progress*.json` in `~/Downloads`, builds the saved
deck, rejects an unexpectedly empty manifest, and validates the package:

```bash
python scripts/jp_assist/export_saved_deck.py
```

Pass the manifest path explicitly if it is stored elsewhere. Use
`--runtime-data` if the local corpus is not at `scripts/jp_assist/out/runtime_data.json`.

To exercise the complete MVP across a temporary real HTTP server and database,
including device pairing, duplicate event delivery, manifest download,
contextual deck generation, and stable Anki IDs:

```bash
python scripts/jp_assist/run_mvp_acceptance.py
```

The acceptance run uses only temporary data and does not access the player's
account, game progress, or normal Anki output.

`package_local.py` stages a checksummed personal bundle under ignored `out/`
and can install the runtime corpus and Test Lab scenarios into a specific Ship
directory. It does not package a font: supported Shipwright builds already
contain the licensed `fonts/NotoSansJP-Regular.ttf` asset in `soh.o2r`, which
JP Assist reuses.

`overrides.py` is the human-review layer for correcting tokenization, readings,
definitions, and game-specific usages. Dictionary output is a draft; sense
selection should be reviewed before calling the deck complete.

## Data contract

`schema/runtime_data.schema.json` documents the runtime boundary. Word identity
is `lemma|reading`; occurrence identity additionally uses offsets. Anki note
identity includes a stable sense ID so regenerating a deck updates existing
notes instead of duplicating them. Custom deck names receive separate stable
deck and note namespaces, allowing a saved-word deck to coexist with the full
game deck. Cloud manifests can also select the exact message and page where a
word was saved for the card's example context.
