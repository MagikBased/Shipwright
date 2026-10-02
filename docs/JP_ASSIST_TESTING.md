# JP Assist testing

JP Assist includes a developer-only Test Lab for repeatable dialogue checks. It
uses Shipwright's normal entrance transition and message-debug paths; it does
not create or modify a normal save. Start from any loaded scene, then open
`Settings > Enhancements > JP Assist > Open JP Assist Test Lab`.

## Test Lab workflow

1. Select **Start temporary debug session**. The lab initializes Shipwright's
   debug save and marks it as temporary (`fileNum 0xFF`).
2. Pick a scenario and choose **Load scenario** for inspection or **Run smoke
   check** for the automated warp/message/corpus assertions.
3. Exercise L or Z for the language toggle and R for Study Mode manually.
4. Review `jp_assist_smoke_results.json` in the application directory for the
   last result per scenario.

Scenario definitions live in `scripts/jp_assist/test_scenarios.json`. Entrance,
text, yaw, and console numeric arguments accept decimal or `0x`-prefixed values.
Expected page/token/choice counts deliberately make corpus drift visible.

Progression profiles are isolated to the temporary session:

- `debug_child`
- `post_deku_tree`
- `adult_all_access`
- `endgame`

## Console commands

```text
jpassist_test_session
jpassist_progress <profile>
jpassist_warp <entrance-id> [profile]
jpassist_message <text-id> [jpn|eng]
jpassist_scenario <scenario-id>
jpassist_smoke <scenario-id>
jpassist_status
```

`jpassist_warp` is useful for scene placement without opening dialogue.
`jpassist_message` opens any table message in the current scene. A scenario
combines both operations and can additionally set room, age, time, position,
yaw, progression, and assertions.

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
corpus lookup, and expected page/token/choice metadata. Controller bindings,
input consumption, text layout, Japanese glyph rendering, and visual overlap
still require an in-game manual pass.
