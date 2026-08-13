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
```

Ship of Harkinian's application-directory lookup then finds it at startup.
The exact application data directory varies by installation.

To export only words saved with C-Right in Study Mode:

```bash
python scripts/jp_assist/build_anki_deck.py \
  --progress-file /path/to/jp_assist_progress.json \
  --output-prefix oot_jp_assist_saved \
  --deck-name "OoT JP Assist — Saved Words"
```

`package_local.py` stages a checksummed personal bundle under ignored `out/`
and can install the runtime corpus into a specific Ship directory. It does not
package a font: supported Shipwright builds already contain the licensed
`fonts/NotoSansJP-Regular.ttf` asset in `soh.o2r`, which JP Assist reuses.

`overrides.py` is the human-review layer for correcting tokenization, readings,
definitions, and game-specific usages. Dictionary output is a draft; sense
selection should be reviewed before calling the deck complete.

## Data contract

`schema/runtime_data.schema.json` documents the runtime boundary. Word identity
is `lemma|reading`; occurrence identity additionally uses offsets. Anki note
identity includes a stable sense ID so regenerating a deck updates existing
notes instead of duplicating them.
