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
3. Smoke checks enter Study Mode with R, navigate with D-Right, exercise saving
   and scrolling, exit with B, and verify vertical choice input is consumed.
   Use **Load scenario** afterward for physical-controller and visual checks.
4. Review `jp_assist_smoke_results.json` in the application directory for the
   last result per scenario.

The full suite owns a disposable debug session and returns to File Select when
it finishes. Do not use it as a starting point for normal play; use **Load
scenario** or a normal save for interactive testing afterward.

Scenario definitions live in
`jp-assist/games/ocarina-of-time/tools/test_scenarios.json`. Entrance,
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
./jp-assist/games/ocarina-of-time/tools/run_smoke_suite.sh
```

The game starts in an automated temporary session, writes
`build-cmake/soh/jp_assist_smoke_results.json`, prints `lastSuite`, and exits
with status 0 on success or nonzero on failure. It still requires a graphical
session because Shipwright initializes its normal SDL/OpenGL window. Override
the executable directory with `JPASSIST_APP_DIR` and the 120-second timeout
with `JPASSIST_SMOKE_TIMEOUT` when needed.

To run the same suite specifically through the packaged `.o2r` plugin, use:

```bash
cmake --build build-cmake --target jpassist_plugin_install soh
./jp-assist/games/ocarina-of-time/tools/run_plugin_smoke_suite.sh
```

The plugin wrapper temporarily copies `jp-assist.o2r` into the application
mods directory and ensures the master feature setting is enabled. The package
automatically takes runtime ownership after successful initialization. A trap
restores the original config and any pre-existing package after success,
failure, interruption, or timeout. Override the package path with
`JPASSIST_PLUGIN_PACKAGE` when testing a separately built artifact.

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
corpus lookup, expected page/token/choice metadata, R-only Study Mode lifecycle,
L/Z definition conceal/reveal, D-Right navigation, C-Right save/restore, D-Up/D-Down
scroll dispatch and input consumption, choice freezing, and the
controller-glyph persistence regression. Unit tests also verify that dialogue
and Study cards remain within 720p, 1080p, and ultrawide work areas at all
supported scales. Physical-controller mapping and ergonomics, visible scroll
movement, Japanese glyph rendering, and visual overlap still require an
in-game manual pass. C-Left known marking is manual because the action is
deliberately one-way and an automated smoke run must not modify the learner's
knowledge state. The controller-glyph case is intentionally
persistence-only and terminal because its special two-page native message is
unsafe to drive as a standalone interactive textbox.

## Manual controller and presentation matrix

Run these after the automated suite is green. Use **Load scenario**, not the
full suite, so the temporary scene remains available for inspection.

| Check | Scenario | Action | Pass condition |
|---|---|---|---|
| Study focus | `mido_house_sign` | R, L, Z, D-Right, D-Left, C-Right, C-Left, R | R opens visible and is the only close action; L/Z toggle the definition; counters, saved state, and known state update |
| Recall-first entry | `mido_house_sign` | With Study Mode closed, press L or Z | Card opens with the definition replaced by native L/Z glyphs and `Reveal`; another L/Z press reveals it |
| Advance while studying | Any multi-page message | Press R, then advance with A and C-Up | Native dialogue advances, Study Mode remains open, the English line updates, and token selection resets to the first token |
| Native highlight | `adult_kakariko` | R, then move through tokens with D-Left/D-Right | Blue backlight follows the complete selected word in the original Japanese textbox without covering its glyphs |
| Long card | `adult_kakariko` | R, then D-Up/D-Down | Long definitions and notes scroll; text stays inside the compact card |
| Choice focus | `know_it_all_choice` | Select the second answer, press R, then move stick and D-pad vertically | Choice index remains unchanged and status says `selection frozen`; after R closes Study Mode, native choice movement resumes |
| History | Any three scenarios | Open **Dialogue History** and search Japanese, English, then `0x103e` | Newest-first entries filter correctly and both languages wrap without clipping |
| Scaling | `adult_kakariko` | Check 720p, 1080p, ultrawide; card scales 0.70/1.00/1.50 | No overlap hides the selected word or definition |
| Lifecycle | Any multi-page message | Advance rapidly near page changes, text-ID jumps, and close | No stale card, input leak, hang, or crash |

Ocarina prompts, shops, and actor-scripted cutscenes must be reached through
normal gameplay rather than injected as standalone messages. Record the game
build, controller mapping, resolution, scenario, and first failed action for
any defect so it can be reproduced without screenshots or mouse automation.
