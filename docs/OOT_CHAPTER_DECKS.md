# Ocarina of Time chapter decks

## Goal

The public catalog divides the game into eleven progression-aware vocabulary
chapters. Each chapter will become an Anki-compatible deck whose cards contain:

- a stable game-scoped card identity;
- written form, reading, part of speech, and concise meaning;
- a reviewed original Japanese example sentence with a fantasy-adventure tone;
- an English translation of that original sentence; and
- optional word and sentence audio.

The examples must not reproduce or lightly rewrite game dialogue. Extracted game
text can be used locally to determine which vocabulary belongs in a chapter, but
published cards use newly written examples.

## Chapter model

The canonical machine-readable roadmap is
`services/learning_platform/learning_platform/content/games/ocarina-of-time.json`.
It uses story and dungeon milestones rather than save-file names: OoT has no
first-class chapter field, and the adult portion allows some nonlinear progress.
Each chapter therefore declares hard prerequisites separately from recommended
ordering.

A word is introduced in the earliest chapter where it is needed. Later
occurrences become chapter tags rather than duplicate notes. The Anki hierarchy
is:

```text
JP Assist::Ocarina of Time::01 The Boy Without a Fairy
...
JP Assist::Ocarina of Time::11 The Hero of Time
```

## Content workflow

1. Map dialogue and location identifiers to one or more chapter ranges.
2. Aggregate vocabulary by stable lemma, reading, and sense identity.
3. Assign each identity to its earliest chapter and record later occurrences.
4. Rank candidates by chapter frequency, usefulness, and learner level.
5. Write an original example and translation; do not publish extracted dialogue.
6. Review the Japanese, sense choice, reading, translation, and spoiler level.
7. Mark the card reviewed in catalog content and regenerate the deck.
8. Optionally generate and review audio, then add only repository-relative media
   paths to `wordAudio` and `sentenceAudio`.

The catalog must distinguish `planned`, `pilot`, and `ready`; it must not present
an automatically extracted or unreviewed vocabulary list as a finished course.

## Audio policy

Audio is an optional enhancement, never a build or study requirement. ElevenLabs
is deliberately outside this MVP. The default provider is `none`, represented by
null audio fields and an ordinary text-only Anki card.

Before enabling a free or local provider, pin the engine, model, voice, and their
license versions; retain the required attribution; confirm that generated output
may be distributed for the intended commercial use; and review pronunciation in
context. A provider adapter must output deterministic media names and may only
populate the existing optional fields. This keeps deck identities unchanged if
audio is added or replaced.

## Chapter 1 vertical slice

Chapter 1 currently includes a deliberately small set of reviewed pilot cards.
Build it with:

```bash
python3 scripts/jp_assist/build_catalog_deck.py --chapter 1
```

This writes an `.apkg` and review TSV under `scripts/jp_assist/out/`. The package
is text-only until reviewed audio is supplied. Planned chapters refuse to export
an empty package.

## Expansion and release gates

A chapter becomes `ready` only when:

- its progression boundaries and prerequisite graph are reviewed;
- its corpus mapping is complete for every supported dialogue variant;
- every included card passes language and spoiler review;
- stable-ID, duplicate, package-import, and regeneration tests pass;
- the site reports the true card and audio counts; and
- text-only generation remains supported even when an audio provider exists.
