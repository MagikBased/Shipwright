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

```bash
python scripts/jp_assist/extract_dialogue.py --help
python scripts/jp_assist/extract_dialogue.py --o2r /path/to/oot.o2r
python scripts/jp_assist/tokenize_dialogue.py
python scripts/jp_assist/validate_corpus.py
python scripts/jp_assist/build_anki_deck.py
```

Use each command's `--help` if your paths differ. The output directory will
contain `runtime_data.json`, `oot_jp_assist.apkg`, an inspection TSV, and a
coverage report. Copy the runtime corpus beside the game data as:

```text
jp_assist/runtime_data.json
```

Ship of Harkinian's application-directory lookup then finds it at startup.
The exact application data directory varies by installation.

To export only words saved with C-Right in Study Mode:

```bash
python scripts/jp_assist/build_anki_deck.py \
  --progress-file /path/to/jp_assist_progress.json
```

`overrides.py` is the human-review layer for correcting tokenization, readings,
definitions, and game-specific usages. Dictionary output is a draft; sense
selection should be reviewed before calling the deck complete.

## Data contract

`schema/runtime_data.schema.json` documents the runtime boundary. Word identity
is `lemma|reading`; occurrence identity additionally uses offsets. Anki note
identity includes a stable sense ID so regenerating a deck updates existing
notes instead of duplicating them.

