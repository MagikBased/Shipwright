# OoT JP Assist — Design Document

Status: Implementation scaffold complete; gameplay validation deferred  
Target: Ship of Harkinian 9.2.x / Shipwright `develop`  
Primary platform: Desktop (Linux and Windows)  
Last updated: 2026-08-12

The milestone “spike findings” below preserve the path taken during early
prototyping. For the current scaffold, runtime boundaries, and deferred test
matrix, see [JP_ASSIST_DEVELOPMENT.md](JP_ASSIST_DEVELOPMENT.md).

## 1. Summary

OoT JP Assist is an in-game Japanese learning aid for Ship of Harkinian. It lets a player:

1. Switch the active dialogue between the original Japanese and English text.
2. Pause dialogue in a study mode.
3. Navigate Japanese dialogue word by word.
4. Open a side panel with a context-appropriate dictionary card.
5. Save encountered words to a personal study list.
6. Generate an Anki deck covering the vocabulary needed to understand Ocarina of Time.

The mod should preserve the feel and pacing of the original game. Help is available immediately, but it should not occupy the screen or interrupt play until requested.

## 2. Goals

- Make Japanese dialogue approachable without requiring an external dictionary.
- Make language switching fast enough to use during normal play.
- Keep Japanese as the primary learning surface rather than replacing it with English.
- Provide definitions selected for the word's meaning in the current sentence.
- Preserve dialogue state when changing display language.
- Support controller-first operation.
- Build a reusable, versioned vocabulary corpus from the game's dialogue.
- Export stable, deduplicated Anki notes.
- Keep ROM-derived text and other copyrighted assets out of Git.

## 3. Non-goals for the first release

- A complete Japanese grammar course.
- Automatic translation using a network service.
- Live synchronization with Anki or AnkiWeb.
- Speech recognition or pronunciation grading.
- Replacing Ship of Harkinian's entire message renderer.
- Supporting every Shipwright randomizer-generated message initially.
- In-game spaced repetition scheduling.
- Automatic dictionary-sense selection with no human review.

## 4. User experience

### 4.1 Normal dialogue

When a textbox is open, JP Assist observes the current message ID and page.

Recommended default controls:

| Input | Action |
|---|---|
| N64 L or N64 Z | Toggle Japanese / English |
| N64 R | Enter or exit Study Mode while Japanese is displayed |

L and Z are interchangeable aliases for the same Language Toggle action. A player can use whichever is comfortable without changing a setting, and JP Assist never requires an L+Z chord. Individual aliases may still be disabled in settings if another enhancement creates a conflict.

Shipwright's standard message input uses A, B, and C-Up to advance text and the stick or D-pad for choices. Its normal message code does not assign a dialogue action to L, Z, or R. JP Assist should nevertheless capture these buttons only during ordinary textbox states. It must not intercept them during ocarina input, the pause menu, developer message tools, or other non-dialogue interfaces.

A short on-screen indicator should confirm the result after each press, for example `日本語` or `English`. This makes a toggle predictable even if the player returns to a dialogue after some time away.

All bindings must be configurable. JP Assist should use Ship of Harkinian's abstract controller inputs rather than raw keyboard or SDL scancodes.

When no equivalent message exists in the selected language, the mod should keep the current text visible and briefly display a non-intrusive “Translation unavailable” indicator.

### 4.2 Language-switch behavior

Switching languages should preserve:

- Message ID.
- Current page where practical.
- Current choice and choice cursor.
- Whether the page was fully revealed.
- The actor and conversation state.
- The game's logical dialogue progression.

Japanese and English messages do not always use identical page breaks. The first implementation may map pages by page index and clamp to the last available page. The corpus can later include explicit page-alignment metadata for exceptions.

The display language should be independent from the user's normal Ship of Harkinian menu language. Closing the dialogue should not unexpectedly change menus or future non-dialogue UI.

### 4.3 Study Mode

Study Mode pauses dialogue advancement and consumes its own controller input. The game world may continue rendering, but gameplay inputs must not leak through.

Suggested controls:

| Input | Action |
|---|---|
| D-pad Left / Right | Select previous or next token |
| D-pad Up / Down | Move between dictionary senses or card sections |
| A | Reveal or expand the definition |
| B | Close the card, then exit Study Mode |
| C-Right | Add or remove the word from the study list |
| R | Exit Study Mode |
| L or Z | Toggle the Japanese / English sentence without leaving Study Mode |

The selected Japanese token should be visibly highlighted in a mirrored study line or study overlay. Highlighting the native textbox glyphs directly is desirable but not required for the first release; the original renderer does not expose convenient word-level geometry.

The side card should contain:

- Surface form as it appears in the dialogue.
- Dictionary form.
- Reading.
- Part of speech.
- Context-appropriate English meaning.
- The Japanese sentence or current page.
- The official English line as a reference translation.
- Optional usage note.
- Encounter count and saved/known status.

An optional “Quiz Mode” can hide the reading and meaning until A is pressed. Normal lookup mode should show the concise definition immediately, since forcing a card flip during play would add unnecessary friction.

### 4.4 Visual layout

Recommended desktop layout:

```text
┌──────────────────────────── Game viewport ────────────────────────────┐
│                                                                      │
│                       Original game scene                            │
│                                                                      │
│  ┌──────────── Study sentence / token strip ────────────┐ ┌────────┐ │
│  │ こんな ところで [会う] なんて…                      │ │ 会う   │ │
│  │                                                      │ │ あう   │ │
│  └──────────────────────────────────────────────────────┘ │ to meet│ │
│                                                          │ Verb   │ │
│             Original OoT dialogue box                    │ [Save] │ │
│                                                          └────────┘ │
└──────────────────────────────────────────────────────────────────────┘
```

The card panel should scale with the viewport, respect safe areas, and remain readable at common 16:9 and 4:3 resolutions. Font sizes and panel opacity should be configurable.

## 5. Interaction state model

```text
NoDialogue
    │ textbox opens
    ▼
DialogueJapanese ◄──── L or Z ────► DialogueEnglish
    │
    │ R
    ▼
StudyTokenSelect ◄──────────────► StudyCardOpen
    │ A / B                          │ B
    │
    │ R or B
    └────────────► DialogueJapanese ◄┘
```

Important state rules:

- Language input is only intercepted while dialogue is active.
- Study Mode is only available when Japanese text and token data are available.
- Choice selection is frozen while the study panel has focus.
- A newly opened message resets token selection to the first content word.
- A page change resets selection to the first token on that page.
- Closing a textbox always closes Study Mode.
- Cutscenes must remain paused or otherwise protected from advancing while the user studies.

## 6. Technical design

### 6.1 Proposed components

Add a self-contained enhancement under:

```text
soh/soh/Enhancements/JPAssist/
├── JPAssistManager.h/.cpp
├── JPAssistInput.h/.cpp
├── JPAssistOverlay.h/.cpp
├── DialogueRepository.h/.cpp
├── MessageParser.h/.cpp
├── StudyRepository.h/.cpp
└── JPAssistTypes.h
```

Offline tooling should live outside the game runtime:

```text
scripts/jp_assist/
├── extract_dialogue.py
├── tokenize_dialogue.py
├── validate_corpus.py
├── build_runtime_data.py
└── build_anki_deck.py
```

Generated runtime data should be placed in Shipwright custom assets and packaged into `soh.o2r`. Personal study progress must remain outside the archive.

### 6.2 Runtime responsibilities

`JPAssistManager`

- Owns the interaction state.
- Detects dialogue open, page change, and close events.
- Tracks requested display language.
- Coordinates input, message data, overlay, and persistence.

`DialogueRepository`

- Performs read-only lookup of Japanese and English entries by message ID.
- Returns parsed messages without modifying the active `MessageContext` or `Font`.
- Detects missing or incompatible language tables.
- Selects the correct corpus namespace for the user's game version.

`MessageParser`

- Parses OoT message control codes.
- Produces pages, display runs, choices, button glyphs, and normalized plain text.
- Maintains a mapping from normalized character ranges back to display runs.
- Supports Japanese and English encodings.

`JPAssistInput`

- Uses Ship of Harkinian's controller abstraction.
- Applies configurable bindings.
- Captures inputs only when the feature owns focus.
- Debounces held buttons so one press performs one action.

`JPAssistOverlay`

- Draws the study sentence, highlight, card, notifications, and status.
- Uses ImGui and the included Noto Sans Japanese font.
- Contains no message parsing or dictionary logic.

`StudyRepository`

- Loads token and dictionary metadata.
- Tracks encounters, saved words, and known words.
- Writes progress atomically to a separate JSON file.

### 6.3 Existing Shipwright integration points

Shipwright already exposes:

- `MessageContext.textId` for the active message.
- Japanese and English message table pointers.
- Matching text IDs across the standard language tables.
- Message lookup examples in the Message Viewer.
- ImGui overlay infrastructure.
- Abstract controller mappings.
- CVars and enhancement-menu widgets.
- Noto Sans Japanese in `soh.o2r`.

The Message Viewer's lookup implementation is a useful reference, but JP Assist needs a new read-only lookup API. Reusing its current function directly would mutate the active font buffer and could corrupt or restart live dialogue.

The preferred implementation is an enhancement-level observer or hook. Changes inside `z_message_PAL.c` should be kept to a minimal event notification only if no existing GameInteractor hook can reliably report message and page transitions.

### 6.4 Language switching strategy

This is the highest-risk technical area and should be prototyped first.

The native message system couples text decoding, glyph reveal, choices, sound effects, and conversation progression. Simply changing `gSaveContext.language` and calling `Message_OpenText` may restart the message or disturb choices.

Recommended approach:

1. Keep the game's logical message state unchanged.
2. Parse both language entries into a JP Assist presentation model.
3. During the technical spike, test whether the native display buffer can be safely rebuilt for the alternate language while preserving message state.
4. If safe, use native rendering for both languages.
5. If not safe, leave the logical/native dialogue in Japanese and render English as a precisely positioned replacement overlay while temporarily hiding only the native dialogue text.

The final implementation must not:

- Re-trigger conversation scripts.
- Replay item-give or textbox-open side effects.
- Reset choices.
- Advance a cutscene.
- Change the global menu language.

#### 6.4.1 Milestone 1 spike findings

The technical spike (`soh/soh/Enhancements/JPAssist/`) answered the step 3 question without needing to try the risky path live: `Message_Decode`/`Message_DecodeJPN` (`z_message_PAL.c`, `Message_Decode` ~line 2241) are not safe to call mid-conversation just to redraw the other language. Both unconditionally reset `msgCtx->textDelayTimer`/`textUnskippable`, force `msgMode` to `MSGMODE_TEXT_DISPLAYING` regardless of the caller's actual state, and push a texture-cache-invalidation command for every visible glyph slot as a side effect of decoding. That is exactly the "restart the message or disturb choices" risk called out above, confirmed by reading the source rather than by causing the corruption live. Step 5 (replacement overlay) is therefore the path forward, not step 4.

What was built and live-verified against the recorded test dialogues (multi-page, single-page, and choice):

- `DialogueRepository` — a read-only lookup into the vanilla JPN/NES message tables that never touches `msgCtx`/`font`, unlike `Message_FindMessage`/`Message_FindMessageJPN` and the debug `MessageViewer` path, which write into the live font buffer as a side effect of "finding" a message.
- `MessageParser` — a non-mutating control-code walker that splits a message into pages and flags choice pages for both languages. It only decodes plain text for English; Japanese glyph codes are not translated to displayable text yet (that needs the kanji font/atlas work, which belongs to Milestone 3's corpus pipeline, not this spike).
- `JPAssistManager` — registers on the existing `GameInteractor::OnDialogMessage` and detects dialogue open/page-advance/close by diffing `msgMode`/`textId` across frames. No changes to core `z_message_PAL.c` were needed; both required hooks already existed.
- The L/Z toggle posts the alternate language's current page through `Ship::GameOverlay::TextDrawNotification` (the same frame-safe mechanism `savestates.cpp` uses), not a hand-rolled `ImGui::Begin`/`End` on `GameInteractor::OnPlayDrawEnd` — that was tried first and crashed live (`ImGui::Begin: Assertion g.WithinFrameScope failed`), because that hook doesn't reliably run inside ImGui's frame scope.

Bugs found and fixed during live testing (all in `JPAssistManager.cpp`/`MessageParser.cpp`):

- Page tracking keyed off `msgMode` transitions alone missed `CTRL_TEXTID` jumps to a different message; fixed by keying the "new message" check off `msgCtx->textId` changing.
- `MessageParser` treated `CTRL_TEXTID`'s target-id operand as if it were more of the same message's page content (it isn't — it's a jump, and whatever bytes follow in the segment belong to unrelated data); fixed by treating `CTRL_TEXTID` and `CTRL_EVENT` as terminators, same as `CTRL_END`.
- The initial page-advance heuristic (copied from `tts.cpp`'s `stateTimer == 1` check, which is intentionally a beat early so TTS can queue speech ahead of display) fired before the page was actually visible; fixed by waiting for the real `MSGMODE_TEXT_CONTINUING → MSGMODE_TEXT_DISPLAYING` transition.
- That same transition also fires for a jumped message's own first page, which was getting miscounted as "page 1" instead of page 0; fixed with a per-message `sFirstPageDisplayed` guard.
- `TextDrawNotification` appends an independent timed entry rather than replacing what's showing, so back-to-back toggles or a jump mid-toggle stacked overlapping notifications; fixed by calling `ClearNotifications()` before every post and on dialogue open/close.

**Known remaining limitation:** the overlay can still go one page stale if the player pages through a real multi-page message without pressing the toggle again on each page - confirmed live, screenshots showed the notification catch up a page late. Every *toggle press itself* has shown correct text on every recorded attempt; the gap is specifically in auto-refreshing the display as pages turn without a fresh press, and behaved inconsistently frame-to-frame, consistent with a race between the game-logic thread (where `GameInteractor::OnDialogMessage` runs) and the render thread (where `GameOverlay` actually draws) rather than a logic error in the page-index tracking. This is accepted as a placeholder limitation, not fixed further: `GameOverlay::TextDrawNotification` is a timed-toast queue, not a persistent per-frame display, and is the wrong primitive for "always show the current state." The eventual replacement-overlay implementation should redraw fresh from current state every frame (once a frame-safe hook for that is worked out) rather than post discrete timed events.

### 6.5 Message identity and compatibility

Text IDs alone may not be sufficient across every supported ROM, Master Quest variant, or future Shipwright version. Runtime records should use a composite identity:

```text
game variant + region/version + quest type + text ID + source-text hash
```

If the source hash does not match, JP Assist may still allow language switching but should disable word metadata for that message and report the mismatch in its debug panel.

## 7. Japanese language data

### 7.1 Why preprocessing is required

Japanese text has no reliable whitespace word boundaries. Runtime-only character splitting would produce poor dictionary lookups for conjugated verbs, compounds, names, and fixed expressions.

Dialogue should therefore be tokenized offline and reviewed. Runtime tokenization is not required for the first release.

### 7.2 Corpus pipeline

1. Extract Japanese and English entries from a locally generated `oot.o2r`.
2. Parse and remove or represent message control codes.
3. Divide each entry into pages.
4. Normalize punctuation and player-name placeholders without losing display offsets.
5. Run a Japanese morphological tokenizer.
6. Look up lemmas and candidate senses in a licensed dictionary.
7. Select the intended sense using the English line and sentence context.
8. Apply manual overrides for names, compounds, archaic forms, and Zelda terminology.
9. Validate token coverage and page alignment.
10. Emit a compact runtime data file and Anki source data.

SudachiPy or MeCab with a suitable dictionary are reasonable offline tokenizer candidates. The tokenizer must remain a build-time tool, not a mandatory game dependency.

### 7.3 Suggested runtime schema

```json
{
  "schemaVersion": 1,
  "source": {
    "variant": "N64_NTSC_12",
    "messageId": "0x1001",
    "japaneseHash": "…",
    "englishHash": "…"
  },
  "pages": [
    {
      "japanese": "…",
      "english": "…",
      "tokens": [
        {
          "id": "会う|あう|verb-1",
          "surface": "会う",
          "lemma": "会う",
          "reading": "あう",
          "partOfSpeech": "verb",
          "meaning": "to meet; to encounter",
          "start": 6,
          "length": 2,
          "note": ""
        }
      ]
    }
  ]
}
```

Offsets should be Unicode code-point or normalized-text offsets, never raw UTF-8 byte indexes.

### 7.4 Dictionary licensing

Dictionary data must have a license compatible with redistribution. JMdict is a likely source, subject to its attribution requirements. Any generated distribution should include the required acknowledgements and source information.

Game dialogue and official translations are copyrighted. The repository must not commit extracted full dialogue. Prefer:

- Local corpus generation from the user's legally generated archive.
- Versioned token metadata that does not reproduce complete dialogue.
- A separate review of what an exported public Anki deck may legally contain.

## 8. Anki deck design

### 8.1 Important scope clarification

Vocabulary alone is not enough to “fully understand” the game. Ocarina of Time also uses:

- Grammar and contractions.
- Casual and character-specific speech.
- Archaic or fantasy-flavored language.
- Proper names and Zelda terminology.
- Context that is implied rather than stated.

The deck should begin with vocabulary but leave room for grammar and expression notes.

### 8.2 Deck structure

Recommended hierarchy:

```text
OoT JP Assist
├── Core Vocabulary
├── Story and Dialogue
├── Items and Interface
├── Places and Dungeons
├── Character Speech and Expressions
└── Grammar and Constructions
```

Useful optional subsets:

- Most frequent words first.
- Words needed for 80%, 90%, 95%, and 99% token coverage.
- Child-era and adult-era story order.
- Area or dungeon.
- Encountered words only.
- Saved words only.

### 8.3 Note fields

Each vocabulary note should have:

- Stable note ID.
- Written form.
- Reading.
- Dictionary form.
- Part of speech.
- Context-specific meaning.
- Broader dictionary meaning.
- Japanese example from the current context.
- English reference line.
- Usage or grammar note.
- Message ID and location metadata.
- Frequency and first-appearance order.
- Tags.

Stable IDs should derive from lemma, reading, and selected sense rather than list position. Regenerating the deck must update existing notes instead of creating duplicates.

### 8.4 Card types

Initial card:

- Front: Japanese word, optionally with the sentence.
- Back: reading, meaning, part of speech, and context.

Later optional cards:

- Reading recall.
- Meaning-to-Japanese production.
- Cloze sentence.
- Grammar construction.
- Audio recognition, if a legally distributable source is available.

### 8.5 Export

The offline deck builder should produce:

- `.apkg` for direct Anki import.
- TSV or CSV for inspection and interoperability.
- A coverage report in Markdown or HTML.

`genanki` is suitable as an offline build dependency. AnkiConnect integration can be considered later, but should not be required by the game.

## 9. Persistence

Store JP Assist state separately from OoT save files, for example:

```text
jp_assist_progress.json
```

Suggested fields:

- Schema version.
- Saved and known token IDs.
- Encounter counts.
- Last encounter timestamp.
- Message history.
- Per-profile settings.
- Corpus version.

Writes should be atomic: write a temporary file, flush it, then replace the prior file. A malformed progress file must never prevent the game from starting.

## 10. Settings

Add a JP Assist section to the Enhancements menu:

- Enable JP Assist.
- Default dialogue language.
- Enable L as a Language Toggle alias.
- Enable Z as a Language Toggle alias.
- Enter-study binding (N64 R by default).
- Add-to-study-list binding (C-Right by default).
- Definition reveal mode.
- Show reading.
- Show English reference.
- Card scale and opacity.
- Pause cutscenes in Study Mode.
- Hide particles from token navigation.
- Debug message/token information.

## 11. Recommended feature improvements

### High value

1. **Saved-word export**  
   Export only words the player chose during play. This connects the game experience directly to later Anki study.

2. **Context-specific senses**  
   Show the meaning used in the current line first. Dumping every dictionary sense would overwhelm learners.

3. **Optional furigana**  
   Display readings above or beside the selected word. Full ruby layout can come later; a reading in the card provides most of the initial value.

4. **Dialogue history**  
   Keep a short, searchable history of recently seen lines and saved words. Players often advance a textbox accidentally.

5. **Coverage tiers**  
   Let learners study the smallest vocabulary set that covers a target percentage of the game before playing.

6. **Spoiler-aware decks**  
   A full deck can reveal character, location, and plot vocabulary. Provide story-order or area-based exports.

7. **Mark as known**  
   Allow known words to remain selectable but visually deemphasized, or skip them during navigation.

8. **Compound-first navigation**  
   Prefer meaningful compounds and expressions over mechanically selecting every morphological fragment.

### Later possibilities

- Grammar cards linked to lines where the construction occurs.
- Per-character speech notes.
- Optional English overlay beneath Japanese during normal play.
- Search by kanji, reading, or English meaning.
- Jisho-style kanji details.
- Personal notes attached to a word.
- Frequency comparisons against a general Japanese corpus.
- Optional local text-to-speech for readings.
- AnkiConnect synchronization.

## 12. Accessibility

- Never rely on color alone for the selected token.
- Support keyboard and controller navigation.
- Allow large text and high-contrast panels.
- Do not require rapid or simultaneous button presses.
- Provide a reduced-motion option.
- Preserve Ship of Harkinian's text-to-speech behavior.
- Make button hints reflect the user's configured controller mapping.

## 13. Milestones

### Milestone 0 — Baseline

- Launch the unmodified development build.
- Confirm Japanese and English are both available from the current `oot.o2r`.
- Record a reproducible test dialogue with choices and multiple pages.

### Milestone 1 — Language-switch technical spike

- Detect active message ID and page.
- Read both language entries without changing live message state.
- Switch presentation when either L or Z is pressed.
- Preserve a simple message, a multi-page message, and a choice message.
- Decide between native alternate rendering and replacement overlay rendering.

### Milestone 2 — Study Mode prototype

- Enter and exit Study Mode.
- Pause dialogue progression.
- Render one manually tokenized test message.
- Navigate tokens and display a hard-coded card.
- Confirm gameplay input does not leak through.

#### Milestone 2 spike findings

Built as an extension of the Milestone 1 spike (`JPAssistManager.cpp` plus a new `StudyRepository.h/.cpp`), rather than the separate `JPAssistInput`/`JPAssistOverlay` files section 6.1 eventually calls for - still prototype-scoped, not production structure.

- R enters/exits Study Mode, gated on the native display language being Japanese and the current page not being a choice page (reusing D-pad for token navigation on a choice page would fight the native choice-selection input, so Study Mode simply isn't offered there for this prototype).
- Entering/exiting, and the D-pad token navigation, reuse the existing `GameInteractor::OnDialogMessage` per-frame hook from Milestone 1 - no new hooks needed.
- The card is a hard-coded `StudyRepository` token list (ordinary vocabulary words, not extracted dialogue - see section 7.4), redrawn every frame from current state via `GameOverlay::TextDrawNotification` with a very short duration, rather than only on discrete navigation events. This was a deliberate change from Milestone 1's event-triggered reposting, which went stale under a game-logic/render-thread race; continuous per-frame redraw sidesteps that class of bug rather than trying to catch every triggering event correctly.
- Native dialogue advancement is blocked while Study Mode has focus by clearing `BTN_A`/`BTN_B`/`BTN_CUP` from `play->state.input[0]` before `Message_Update`'s mode switch reads them later in the same frame - the same input-consumption pattern `soh/soh/Enhancements/Items/ArrowCycle.cpp` uses to suppress shield input while cycling arrows, not a new mechanism.
- L/Z continues to toggle the display language without leaving Study Mode, matching the design doc's control table - this worked without any special-casing, since the toggle handler was already independent of Study Mode state.
- Live-tested: entering/exiting via R across several conversations, toggling L/Z repeatedly while Study Mode was active, and dialogue jumps correctly force-exiting it - all without crashing or letting a native advance through.

**Known limitation:** the hard-coded Japanese surface/reading strings render as `?`/tofu in the card, because nothing in this path has loaded a CJK-capable font into ImGui's font atlas - only the native N64 renderer has real kanji textures. This is the same underlying gap as Milestone 1's undecoded Japanese overlay text, not a new problem; a real font/kanji-atlas solution is out of scope until Milestone 3's corpus and font work exists.

### Milestone 3 — Corpus toolchain

- Extract local dialogue.
- Parse message control codes.
- Tokenize Japanese.
- Add review overrides.
- Build and validate runtime data.
- Support the selected ROM version.

#### Milestone 3 spike findings

Built as `scripts/jp_assist/{extract_dialogue,message_codes,tokenize_dialogue,overrides,validate_corpus}.py`, run against the N64 NTSC 1.2 `oot.o2r` directly - no game process involved. `sudachipy`/`sudachidict_core` (tokenizer) and `jamdict`/`jamdict-data` (a packaged JMdict, no XML build step needed) are both readily pip-installable and were verified working; `scripts/jp_assist/requirements.txt` lists them.

- **Archive format, reverse-engineered and verified against real bytes** (no exporter tool ships in this repo to document it): a 64-byte OTR resource header, then `u32 msgCount` followed by that many `{u16 id, u8 textboxType, u8 textboxYPos, u32 stringLength, stringLength raw bytes}` records, all little-endian. Confirmed against `oot.o2r`'s actual `text/jpn_message_data_static/jpn_message_data_static` and `text/nes_message_data_static/ntsc_nes_message_data_static` entries, not just inferred from reading loader code.
- **The Japanese glyph codes are literal Shift-JIS code points**, not a bespoke encoding - confirmed both by a source comment (`soh/src/code/z_kanfont.c`: "The value of `character` is the SHIFT-JIS encoding of the character") and empirically: decoding real message bytes as little-endian u16 code points and Shift-JIS-decoding them produces recognizable real words (e.g. `ポケット`, "pocket"). This means real Unicode Japanese text is recoverable *offline* right now, closing the gap Milestones 1 and 2 both hit at runtime (no CJK font loaded for in-game rendering) - the corpus itself is no longer blocked on that, only in-game *display* still is.
- `message_codes.py` ports the same control-code walk as `MessageParser.cpp`, including the `MESSAGE_TEXTID`/`MESSAGE_EVENT`-as-terminator fix from the Milestone 1 spike (bytes after either belong to a different message, not more of this one) - ported deliberately rather than shared, since there's no existing C++/Python bridge in this codebase to share it through.
- **Real bug caught by `validate_corpus.py` on the first run**: message `0x103E`'s "△の使い方聞く?" prompt (the in-game triangle/C-Up button icon) decoded via standard Shift-JIS to the Greek letter "Η" - a real character that looks enough like ordinary text to pass silently. The game's kanji texture set repurposes a few otherwise-unused Shift-JIS/Windows-31J code points (the IBM-extended Greek-letter range) as custom button-icon glyphs instead of real text. `validate_corpus.py` flags any character outside the expected Japanese Unicode blocks so these get caught rather than silently corrupting the corpus; a follow-up would map the specific button-icon codes (search `soh/assets/xml/*/textures/kanji.xml` for their definitions) to a plain-text placeholder like `[C-Up]` instead of leaving them for Shift-JIS to misinterpret.
- **Sense selection is exactly as unreliable as the design doc predicted** it would be without real cross-language disambiguation: `フン！` ("Sheesh!") picked up JMdict's first sense for フン, which is "feces" - a real dictionary entry, wrong for this context. This is why step 7 (context-based sense selection) and step 8 (manual overrides) exist rather than trusting automatic lookup; `overrides.py` is a working demonstration of the override layer (currently covering the proper nouns in the recorded test dialogues - コキリ, デクの樹, ミド, サリア - since JMdict has no entries for game-specific names), not a claim that sense selection itself is solved.
- Player-name insertion (`MESSAGE_NAME_JPN`/`MESSAGE_NAME`) isn't substituted - the control code is consumed and skipped, leaving a gap in the decoded text (visible in `0x1001`'s "Hi, !"). Not fixed: an accurate fix depends on how names are meant to render in extracted (non-live) text at all, which wasn't otherwise blocking any Milestone 3 acceptance item.
- `build_runtime_data.py` and `build_anki_deck.py` from section 6.1's file list were not created as separate scripts - `tokenize_dialogue.py` emits the final schema-conformant JSON directly, since splitting "tokenize" from "build" would just be passing the same data through. Anki deck export is Milestone 4's job, not this one's.

### Milestone 4 — Study persistence and Anki

- Save words and encounter counts.
- Export saved words and the full vocabulary corpus.
- Generate stable `.apkg` notes.
- Produce coverage and spoiler reports.

#### Milestone 4 spike findings

- `StudyPersistence.h/.cpp` stores progress in `jp_assist_progress.json` via `Ship::Context::GetPathRelativeToAppDirectory` (the same directory `shipofharkinian.json`/`Save/`/`presets/` already use), following existing conventions rather than inventing a new location: safe load modeled on `Presets.cpp`'s try/catch-and-skip-on-corruption pattern, atomic write modeled on `SaveManager.cpp`'s write-to-`.tmp`-then-`std::filesystem::rename` pattern for `.sav` files.
- C-Right in Study Mode toggles the selected token's saved status (saved immediately, since it's an explicit and infrequent action); encounter counts increment once per navigation to a token (not once per frame the card is drawn) and are flushed to disk when Study Mode closes, to avoid a disk write on every D-pad press.
- **Real bug found and fixed via live restart-testing, not just code review**: `StudyPersistence_Load()`'s `json.value(key, default).items()` chained directly in a range-for is undefined behavior - `.value()` returns a temporary `nlohmann::json` by value, and the range-for only extends the lifetime of what it directly binds to (the `.items()` iteration proxy), not the sub-expression the proxy references. The temporary's lifetime ends before the loop body runs. This is exactly the kind of bug that not not show up in code review and behaves inconsistently at runtime: the first observed symptom was a bogus `type must be number, but is null` parse exception; adding diagnostics to narrow it down changed the symptom to a straight segfault on the very next run, with the same on-disk file. Tracked down with a minimal standalone reproduction against the exact file rather than guessing from the exception message, and fixed by binding each `.value()` result to a named local before iterating it. Also explains an earlier observation mid-testing that already-saved words seemed to survive a reload once - UB is exactly this inconsistent; it doesn't corrupt every time.
- `build_anki_deck.py` collapses every token occurrence across however many messages were tokenized into one note per `(lemma, reading, meaning)` identity, with a stable GUID derived from that same identity (design doc 8.3) rather than genanki's default field-hash GUID, so editing an example sentence later doesn't fork the note. Verified directly, not just asserted: regenerated the deck twice from the same input and confirmed the two `.apkg` files contain identical note GUID sets - satisfies the design doc's "regenerating the deck must update existing notes instead of creating duplicates" acceptance criterion.
- `--progress-file` lets the export be restricted to only saved words, or (by omitting it) the full tokenized set - covers "export saved words and the full vocabulary corpus" without two separate scripts.
- The coverage report is deliberately just a frequency listing over whatever messages were tokenized in this session, not a real 80/90/95/99%-coverage claim - that needs the full game corpus tokenized, which hasn't happened (only the recorded test dialogues have). No spoiler-aware/story-order variant was built - out of scope until a real corpus exists to vary by area/story-order in the first place.

### Milestone 5 — Product polish

- Configurable controls and display.
- Missing-data handling.
- Dialogue history.
- Accessibility testing.
- Version compatibility checks.
- Documentation and packaging.

#### Milestone 5 spike findings

- Settings moved from a hardcoded-on spike to a real CVar-backed menu (`Enhancements > JP Assist` in the SoH settings UI): a master `JPAssist.Enabled` toggle plus independently-toggleable `JPAssist.EnableLAlias`/`JPAssist.EnableZAlias` checkboxes, all defaulting to on so existing behavior is unchanged for players who never open the menu. Wiring this in required declaring `SohGui::mSohMenu` as an `extern` *inside* `namespace SohGui` (not just qualified with `SohGui::` at global scope) - the two forms produce different mangled symbols, so the global-scope form linked but never resolved to the real definition. `WidgetPath`/`SECTION_COLUMN_1`, by contrast, are global-namespace types despite living in a `SohGui`-adjacent header, so they must *not* be qualified.
- Dialogue history is a bounded (20-entry), oldest-trimmed `std::vector<HistoryEntry>` recorded whenever a new message opens, persisted alongside the existing saved-token/encounter-count data in `jp_assist_progress.json` under a new `messageHistory` array, and readable in-session via a `jpassist_history` console command. Reused the exact "bind `.value()` to a named variable before iterating" pattern from the Milestone 4 UB fix rather than re-risking the same dangling-reference bug on the new array.
- Stress-tested the extended `StudyPersistence_Load()` against 14 malformed-JSON cases (empty file, truncated JSON, wrong types at every field, non-UTF8 garbage, deeply nested garbage, etc.) via a standalone repro compiled against the exact parsing logic - all handled without crashing, consistent with design doc 14's "a malformed progress file must never prevent the game from starting."
- Accessibility review against section 12: confirmed by code inspection that `tts.cpp`'s dialogue-narration hook reads only `msgCtx` state fields, never button-press bits, so Study Mode's `BTN_A`/`BTN_B`/`BTN_CUP` input-consumption cannot suppress narration; its D-pad reads live in an unrelated pause-menu-narration path. No color-only signaling was introduced (language state is conveyed through textbox content/position, not color alone), no simultaneous-press requirements exist, and button-hint behavior already reflects the configured L/Z alias CVars from the settings menu above.
- Added a lightweight ROM/version compatibility guard (`CheckRomCompatibilityOnce()`, run once on the first real dialogue) that looks up a small set of known test text IDs (Saria's first greeting, Mido's House sign, the Know-It-All Brothers choice) in both language tables and logs a warning naming which table is missing data if any aren't found - a cheap signal that a different ROM/`oot.o2r` than the N64 NTSC 1.2 this spike was built against may not match JP Assist's recorded dialogues. Live-verified in-game: triggering Saria's greeting (text ID `0x1001`) via the Dev Tools Message Viewer produced `"[JPAssist] Compatibility check: all known test dialogues found in both language tables (N64 NTSC 1.2 expected)."`, and the same trigger round-tripped through to `jp_assist_progress.json`'s `messageHistory` array, closing out the one live-verification gap left over from Milestone 4's history work.
- Along the way, hit and worked around a **pre-existing, unrelated SoH bug**: the Dev Tools Message Viewer's `Display Message` handler (`MessageViewer.cpp`) calls `std::stoi` on the Text ID field with no empty-string guard, so clicking the button with an empty field throws `std::invalid_argument` and terminates the process. Not a JP Assist bug (confirmed via `grep` - the only `stoi` call in that file, unrelated to any JPAssist code path) and out of scope for this mod, but worth a heads-up since it's easy to hit while testing.

## 14. Acceptance criteria for the first usable release

- The player can toggle Japanese and English during ordinary dialogue with either L or Z.
- Switching does not restart conversation scripts or alter choices.
- Study Mode can be entered from Japanese dialogue and safely exited.
- Every selectable token in the supported corpus has a reading and concise contextual meaning.
- The selected token is visually unambiguous.
- Saving a word survives a game restart.
- An exported deck imports into Anki without duplicate note IDs.
- Missing corpus data fails gracefully.
- No ROM, complete extracted dialogue table, save, or personal study data is tracked by Git.

## 15. Risks and mitigations

| Risk | Mitigation |
|---|---|
| Language switch corrupts message state | Prototype first; decouple logical message state from presentation |
| JP/EN page breaks do not align | Page-index fallback plus reviewed alignment metadata |
| Tokenization is linguistically poor | Offline tokenizer plus manual override layer |
| Wrong dictionary sense | Use English line and human review; keep alternate senses expandable |
| Randomizer text has no Japanese equivalent | Mark unsupported and retain current display |
| Cutscene advances during study | Explicitly pause or gate message/cutscene advancement |
| Controller conflicts | Contextual input capture and fully configurable bindings |
| Corpus breaks after updates | Composite identity, hashes, schema versions, validation report |
| Deck contains spoilers | Coverage and story/area-based deck variants |
| Copyrighted extracted text enters Git | Local extraction, ignore rules, automated repository checks |

## 16. Open decisions

1. Should C-Right save the selected word, or would another Study Mode input be more comfortable?
2. Should English replace the native text or appear as a temporary overlay?
3. Should cutscenes freeze completely while the study panel is open?
4. Should particles and punctuation be selectable by default?
5. Should definitions appear immediately or use Anki-style reveal by default?
6. Which ROM/version is the initial supported corpus target?
7. Is the Anki deck intended only for personal generation, or eventual public distribution?

## 17. Recommended initial decisions

- Accept both L and Z as interchangeable Language Toggle aliases.
- Use R to enter or exit Study Mode.
- Never require an L+Z chord.
- Keep bindings configurable from the start.
- Freeze dialogue and cutscene progression in Study Mode.
- Show concise definitions immediately; make quiz reveal optional.
- Navigate content words and meaningful expressions by default, with particles available through a setting.
- Generate the corpus locally from the user's N64 NTSC 1.2 archive first.
- Implement saved-word export before live Anki synchronization.
- Treat language switching as the first technical spike because it determines the presentation architecture for the rest of the mod.
