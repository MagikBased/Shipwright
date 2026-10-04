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

Anki note GUIDs are stable across regeneration and scoped by game, chapter, and
card identity. Chapter scope is required because parallel optional branches may
legitimately contain the same vocabulary; importing both packages must not move
or overwrite a note from the other branch.

## Chapter model

The canonical machine-readable roadmap is
`services/learning_platform/learning_platform/content/games/ocarina-of-time.json`.
The complete reviewed card corpus is stored separately in
`ocarina-of-time.cards.json`; `sampleCards` in the roadmap are only lightweight
catalog previews. Export, audio, coverage, and prerequisite tools always load
the complete card manifest.

Game-specific names are maintained as separate terminology entries rather than
ordinary sentence cards. They are excluded from the core vocabulary coverage
target, and their glossary definitions may name the game concept. This keeps
every reviewed sentence card's example original and useful outside the source
game's intellectual property. Controller labels and other interface tokens are
likewise excluded from language-learning coverage.

It uses story and dungeon milestones rather than save-file names: OoT has no
first-class chapter field, and the adult portion allows some nonlinear progress.
Each chapter therefore declares hard prerequisites separately from recommended
ordering.

A deck introduces the most important vocabulary needed for its chapter after
subtracting every word taught by any transitive hard-prerequisite deck.
Recommended ordering does not count as a prerequisite: parallel optional
branches may both teach a shared word so either route remains self-contained.
"Complete core deck" has an objective corpus gate: its own reviewed cards plus
cards taught by transitive hard prerequisites must account for at least 80% of
the token occurrences in that chapter's mapped dialogue. This deliberately
measures recurring reading value rather than requiring a card for every rare
name, typo, interjection, or dictionary sense. Reviewers may still add rarer
story-essential terms beyond the threshold.
Later occurrences in dependent chapters become chapter tags rather than
duplicate notes. The Anki hierarchy is:

```text
JP Assist::Ocarina of Time::01 The Boy Without a Fairy
...
JP Assist::Ocarina of Time::11 The Hero of Time
```

## Content workflow

1. Map dialogue and location identifiers to one or more chapter ranges.
2. Aggregate vocabulary by stable lemma, reading, and sense identity.
3. Review decks in prerequisite order. For each chapter, remove identities
   already taught by any transitive hard-prerequisite deck; do not remove an
   identity merely because it occurs in prerequisite dialogue or in a
   numerically earlier parallel branch.
4. Rank the remaining candidates by frequency in the chapter, then recurrence
   across the full game corpus. Reviewers use usefulness and learner level to
   choose from that ranked queue before publication.
5. Write an original example and translation; do not publish extracted dialogue.
6. Review the Japanese, sense choice, reading, translation, and spoiler level.
7. Mark the card reviewed in catalog content and regenerate the deck.
8. Optionally generate and review audio, then add only repository-relative media
   paths to `wordAudio` and `sentenceAudio`.

The committed non-text mapping lives at
`scripts/jp_assist/chapter_mapping/ocarina-of-time.json`. Generate local review
queues from the private corpus with:

```bash
python3 scripts/jp_assist/audit_chapter_mapping.py
python3 scripts/jp_assist/build_chapter_candidates.py
```

The audit writes an ignored `chapter_mapping_audit/` review queue containing
every unmapped message ID plus any actor/gameplay source files that reference
it. It deliberately does not infer chapter ownership from the numeric ID. The
strict release gate is:

```bash
python3 scripts/jp_assist/audit_chapter_mapping.py --require-complete
```

That command fails until every corpus message is explicitly accounted for and
the mapping contains no stale IDs.

It writes one ignored TSV per chapter plus `summary.json` under
`scripts/jp_assist/out/chapter_candidates/`. These files contain dictionary
metadata and message IDs but deliberately omit Japanese and English dialogue.
Each TSV includes an explicit importance rank. Regenerating after cards are
published removes those identities from dependent queues, and the summary
reports how many repeats were removed because a prerequisite already teaches
them. The summary also reports current token coverage, prerequisite coverage,
and the number of additional ranked cards needed to meet the 80% core gate.
Catalog deck
generation independently rejects a published card that duplicates any
transitive hard prerequisite, so an editorial mistake cannot silently create a
redundant deck.
Numeric ID ranges are never treated as story order implicitly: a reviewer must
place explicit IDs or bounded ranges in the mapping and advance its status from
`planned` to `seeded` to `reviewed`.

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

The first opt-in adapter is the locally run Apache-licensed Kokoro stack. It stages
clips for review and never changes the catalog automatically. Installation,
license evidence, quality limitations, and commands are documented in
[`KOKORO_TTS.md`](KOKORO_TTS.md). `none` remains the default provider.

## Complete chapter course

All eleven chapters meet the configured 80% core-token coverage target. The
course contains 802 prerequisite-aware sentence cards and 21 game-specific
terminology entries across all 2,002 mapped dialogue messages. Every sentence
card has stable corpus provenance, a newly written non-IP example sentence, and
a stable Anki note identity. Audio remains optional, so every chapter is
available as a fully usable text-only deck.

Each ready chapter can be downloaded from the game catalog. The public download
endpoint builds the package from the same repository-owned card manifest used by
the local tooling:

```text
/v1/catalog/games/ocarina-of-time/chapters/{chapter-id}/deck
```

Build an individual chapter with:

```bash
python3 scripts/jp_assist/build_catalog_deck.py --chapter 1 --require-corpus-evidence
```

To reproduce all eleven decks locally:

```bash
for chapter in $(seq 1 11); do
  python3 scripts/jp_assist/build_catalog_deck.py \
    --chapter "$chapter" --require-corpus-evidence \
    --output-prefix "oot_jp_assist_chapter_$(printf '%02d' "$chapter")"
done
```

This writes an `.apkg` and review TSV under `scripts/jp_assist/out/`. The package
is text-only until reviewed audio is supplied. Empty chapters refuse to export
a package. Each published card cites only a stable corpus identity and
message IDs; generation verifies those references against the local corpus while
keeping extracted dialogue out of the catalog. The same gate normalizes and
compares every Japanese and English example against every extracted page and
rejects an exact corpus sentence.

## Expansion and release gates

A chapter becomes `ready` only when:

- its progression boundaries and prerequisite graph are reviewed;
- the strict mapping audit accounts for every message in every supported
  dialogue variant;
- every included card passes language and spoiler review;
- stable-ID, duplicate, package-import, and regeneration tests pass;
- the site reports the true card and audio counts; and
- text-only generation remains supported even when an audio provider exists.

Run the course-wide structural and provenance audit at any point with:

```bash
python3 scripts/jp_assist/validate_catalog_course.py
```

For a release, add `--require-ready`. The strict form fails unless every
chapter reaches its configured coverage target, is marked ready, exposes a
download, has unique examples and Anki GUIDs, contains no prerequisite repeats,
retains valid corpus evidence, and has a reviewed dialogue mapping. The mapping
partition can also be checked independently with:

```bash
python3 scripts/jp_assist/audit_chapter_mapping.py \
  --require-complete --require-reviewed
```
