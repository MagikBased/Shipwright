# JP Assist development status

This document complements `JP_ASSIST_DESIGN.md`. It records the implementation
boundary so deferred testing can start from a known point.

## Scaffolded vertical slice

- Read-only access to both Ship of Harkinian dialogue tables.
- Native Japanese dialogue plus a corpus-backed English reference inside the
  combined Study card, without changing the save's global language or mutating
  the active message context.
- R enters Study Mode on any Japanese page with token data. Choice selection
  is frozen while the study panel owns focus, including analog-stick input.
- D-Left/D-Right select occurrences; D-Up/D-Down scroll long cards; C-Right
  saves a vocabulary item; R or B closes the panel. A and C-Up continue native
  dialogue while the card follows page and chained text-ID changes.
- Frame-safe, bottom-centered ImGui English-reference and vocabulary card.
- Japanese overlay text uses Shipwright's bundled Noto Sans Japanese font and
  logs a graceful fallback warning if the asset is unavailable.
- Separate JSON progress for saved words, encounter counts, timestamps, and a
  bounded dialogue history, stamped with the active corpus version.
- Searchable newest-first dialogue-history window with Japanese, English,
  timestamp, and text-ID filtering.
- Local extraction, parsing, Sudachi tokenization, JMdict lookup/override,
  validation, coverage reporting, and `.apkg`/TSV generation tools.
- Runtime JSON Schema and generated/copyrighted-data ignore rules.
- One-command local builds, checksummed staging, atomic corpus installation,
  and automated Anki package/GUID validation.
- A temporary-save Test Lab with named progression profiles, arbitrary entrance
  and message controls, manifest-backed scenarios, automated controller/focus
  smoke checks, result reporting, and engine-independent unit tests. See
  `JP_ASSIST_TESTING.md`.

## Latest local corpus build

Generated from the user's N64 NTSC 1.2 archive on 2026-10-02. These ignored
local artifacts are not distributable game data.

- 2,035 player-facing message records and 3,420 normalized pages; 81
  archive-internal message-ID echo records are excluded.
- 40,351 token occurrences and 4,168 dictionary-form vocabulary identities.
- Zero structural/schema/offset errors.
- 39,828 token occurrences have definitions (98.70%).
- 352 unique unresolved definitions are listed in `out/review_queue.tsv` and
  tagged `needs-definition` in the generated Anki deck.
- Full deck: 4,247 sense-specific notes; package validation confirms unique GUIDs.
- Saved-word deck: generated from the actual local progress file (one note at
  the time of this build).

## Deliberately deferred validation

No gameplay claims should be made until these are exercised in-game:

1. Dialogue lifecycle: open, close, page transitions, text-ID jumps, cutscenes,
   choices, shops, ocarina prompts, and rapidly toggling near transitions.
2. Controls: physical L-only and Z-only controllers, remapping, ergonomics,
   and enhancement conflicts. Automated smoke checks now cover C-Right
   save/restore and D-Up/D-Down scroll dispatch and consumption.
3. Presentation: visual confirmation of scrolling and tested layout bounds at
   each resolution/UI scale, long sentences, long definitions, bundled
   Japanese font rendering, accessibility, and labels.
4. Corpus: every message/page aligns across languages; control glyphs decode;
   hashes match the supported ROM revision; missing/malformed files fail safely.
5. Persistence/Anki: reload, malformed JSON, interrupted writes, corpus upgrades,
   saved-only export, repeat generation, and Anki update-versus-duplicate behavior.

## Cross-game direction

The game-neutral presentation contract and adapter roadmap live in
[`DIALOGUE_STUDY_PLATFORM.md`](DIALOGUE_STUDY_PLATFORM.md). New game ports
should implement the host capability interface rather than copying SoH's
message-state or ImGui assumptions.

## Known implementation limits

- The overlay mirrors alternate text instead of replacing glyphs inside the
  native textbox. This is safer scaffolding, but native-layout integration is
  still a future milestone.
- Study Mode does integrate one presentation element into the native renderer:
  a selected token's normalized corpus span is mapped to the already-decoded
  Japanese glyph positions and receives a blue backlight before glyph drawing.
- Dictionary sense selection is heuristic and requires human review through
  `scripts/jp_assist/overrides.py`; the generated review queue tracks remaining
  unresolved definitions.
- Runtime source-hash verification is represented in the corpus but not yet
  compared against raw message-entry hashes exposed by the game.
- The full-game vocabulary deck is generated locally; it is not committed or
  distributed because the corpus contains copyrighted dialogue.

## Runtime data location

The game loads `jp_assist/runtime_data.json` using Ship's application-directory
search. Build it using `scripts/jp_assist/README.md`, then copy that directory
into the relevant Ship of Harkinian application data directory before testing.
