# JP Assist development status

This document complements `JP_ASSIST_DESIGN.md`. It records the implementation
boundary so deferred testing can start from a known point.

## Scaffolded vertical slice

- Read-only access to both Ship of Harkinian dialogue tables.
- L and Z as interchangeable aliases for one language-toggle action.
- Corpus-backed Japanese and English page display without changing the save's
  global language or mutating the active message context.
- R enters Study Mode on a Japanese, non-choice page with token data.
- D-Left/D-Right select occurrences; D-Up/D-Down scroll long cards; C-Right
  saves a vocabulary item; R or B closes the panel. Native dialogue advance
  inputs are consumed while focused.
- Frame-safe ImGui dialogue overlay and Anki-style study card.
- Japanese overlay text uses Shipwright's bundled Noto Sans Japanese font and
  logs a graceful fallback warning if the asset is unavailable.
- Separate JSON progress for saved words, encounter counts, timestamps, and a
  bounded dialogue history, stamped with the active corpus version.
- Local extraction, parsing, Sudachi tokenization, JMdict lookup/override,
  validation, coverage reporting, and `.apkg`/TSV generation tools.
- Runtime JSON Schema and generated/copyrighted-data ignore rules.
- One-command local builds, checksummed staging, atomic corpus installation,
  and automated Anki package/GUID validation.
- A temporary-save Test Lab with named progression profiles, arbitrary entrance
  and message controls, manifest-backed scenarios, smoke result reporting, and
  engine-independent unit tests. See `JP_ASSIST_TESTING.md`.

## Latest local corpus build

Generated from the user's N64 NTSC 1.2 archive on 2026-08-12. These ignored
local artifacts are not distributable game data.

- 2,116 player-facing message records and 3,501 normalized pages.
- 40,488 token occurrences and 4,224 dictionary-form vocabulary identities.
- Zero structural/schema/offset errors.
- 39,558 token occurrences have definitions (97.70%).
- 450 unique unresolved definitions are listed in `out/review_queue.tsv` and
  tagged `needs-definition` in the generated Anki deck.
- Full deck: 4,303 sense-specific notes; regenerated GUID set verified stable.
- Saved-word deck: generated from the actual local progress file (one note at
  the time of this build).

## Deliberately deferred validation

No gameplay claims should be made until these are exercised in-game:

1. Dialogue lifecycle: open, close, page transitions, text-ID jumps, cutscenes,
   choices, shops, ocarina prompts, and rapidly toggling near transitions.
2. Controls: L-only and Z-only controllers, both aliases enabled, R focus,
   D-pad/C-Right consumption, remapping, and conflicts with enhancements.
3. Presentation: resolutions, UI scaling, long sentences, long definitions,
   visual verification of the bundled Japanese font, accessibility, and labels.
4. Corpus: every message/page aligns across languages; control glyphs decode;
   hashes match the supported ROM revision; missing/malformed files fail safely.
5. Persistence/Anki: reload, malformed JSON, interrupted writes, corpus upgrades,
   saved-only export, repeat generation, and Anki update-versus-duplicate behavior.

## Known implementation limits

- The overlay mirrors alternate text instead of replacing glyphs inside the
  native textbox. This is safer scaffolding, but native-layout integration is
  still a future milestone.
- Choice pages cannot enter Study Mode yet. Supporting them needs an explicit
  focus model that freezes native choice selection.
- Dictionary sense selection is heuristic and requires human review through
  `scripts/jp_assist/overrides.py`; the generated review queue tracks the
  remaining 450 unresolved definitions.
- Runtime source-hash verification is represented in the corpus but not yet
  compared against raw message-entry hashes exposed by the game.
- History has a console command (`jpassist_history`) but no searchable UI.
- The full-game vocabulary deck is generated locally; it is not committed or
  distributed because the corpus contains copyrighted dialogue.

## Runtime data location

The game loads `jp_assist/runtime_data.json` using Ship's application-directory
search. Build it using `scripts/jp_assist/README.md`, then copy that directory
into the relevant Ship of Harkinian application data directory before testing.
