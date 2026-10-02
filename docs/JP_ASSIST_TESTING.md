# JP Assist testing

JP Assist includes a developer-only Test Lab for repeatable dialogue checks. It
uses Shipwright's normal entrance transition and message-debug paths; it does
not create or modify a normal save. Start from any loaded scene, then open
`Settings > Enhancements > JP Assist > Open JP Assist Test Lab`.

## Test Lab workflow

1. Select **Start temporary debug session**. The lab initializes Shipwright's
   debug save and marks it as temporary (`fileNum 0xFF`).
2. Pick a scenario and choose **Load scenario** for inspection, **Run smoke
   check** for one automated check, or **Run all smoke checks** for the full
   sequential suite.
3. Exercise L or Z for the language toggle and R for Study Mode manually.
4. Review `jp_assist_smoke_results.json` in the application directory for the
   last result per scenario.

The full suite owns a disposable debug session and returns to File Select when
it finishes. Do not use it as a starting point for normal play; use **Load
scenario** or a normal save for interactive testing afterward.

Scenario definitions live in `scripts/jp_assist/test_scenarios.json`. Entrance,
text, yaw, and console numeric arguments accept decimal or `0x`-prefixed values.
Expected page/token/choice counts deliberately make corpus drift visible.

Progression profiles are isolated to the temporary session:

- `debug_child`
- `post_deku_tree`
- `adult_all_access`
- `endgame`

## One-command smoke run

After building `build-cmake/soh/soh.elf`, run the complete suite without menu
navigation or mouse automation:

```bash
./scripts/jp_assist/run_smoke_suite.sh
```

The game starts in an automated temporary session, writes
`build-cmake/soh/jp_assist_smoke_results.json`, prints `lastSuite`, and exits
with status 0 on success or nonzero on failure. It still requires a graphical
session because Shipwright initializes its normal SDL/OpenGL window. Override
the executable directory with `JPASSIST_APP_DIR` and the 120-second timeout
with `JPASSIST_SMOKE_TIMEOUT` when needed.

## Console commands

```text
jpassist_test_session
jpassist_progress <profile>
jpassist_warp <entrance-id> [profile]
jpassist_message <text-id> [jpn|eng]
jpassist_scenario <scenario-id>
jpassist_smoke <scenario-id>
jpassist_smoke_all
jpassist_status
```

`jpassist_warp` is useful for scene placement without opening dialogue.
`jpassist_message` opens any table message in the current scene. A scenario
combines both operations and can additionally set room, age, time, position,
yaw, progression, and assertions.

Use ordinary textbox messages for automated scenarios. Messages containing
the ocarina control code depend on actor-driven ocarina setup and are unsafe to
inject as standalone textboxes; they require a manual test through their normal
gameplay interaction.

## Unit tests

The standalone suite tests the control-code parser, scenario manifest parser,
and stable vocabulary IDs without starting the game:

```bash
cmake -S . -B build-jpassist-tests -G Ninja -DJPASSIST_BUILD_TESTS=ON
cmake --build build-jpassist-tests --target jpassist_tests
ctest --test-dir build-jpassist-tests --output-on-failure
```

The first configure may download GoogleTest if it is not installed locally.

## Automation boundary

Smoke checks cover scene transition, message dispatch, JP Assist observation,
corpus lookup, expected page/token/choice metadata, and the controller-glyph
persistence regression. Controller bindings, input consumption, text layout,
Japanese glyph rendering, and visual overlap still require an in-game manual
pass. The Test Lab's live counters make L/Z toggles, Study Mode entries,
D-Left/D-Right navigation, and C-Right save toggles directly observable;
D-Up/D-Down scrolls long Study Mode content while the footer remains fixed.
