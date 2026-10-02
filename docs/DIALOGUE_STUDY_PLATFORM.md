# Dialogue Study Platform

JP Assist is the Ocarina of Time implementation of a more general dialogue-learning feature. The reusable design has
three layers so that language data and study behavior do not depend on one game's renderer or message engine.

## Layers

1. **Portable learning core** — display-mode policy, language state, token selection, study records, persistence, and
   export models. This layer must not include game, renderer, or controller types.
2. **Game adapter** — observes the active dialogue and supplies stable message/page identity, input actions, presentation
   capabilities, dialogue geometry, and safe native-text replacement when the host supports it.
3. **Content pack** — versioned bilingual pages and reviewed token metadata keyed by game/version/message identity.

The first portable contract is `DialoguePresentation.h`. It deliberately depends only on the C++ standard library. SoH
implements `PresentationHost`; another recompilation can implement the same interface without inheriting OoT message
internals.

## Display modes

| Mode | Ordinary dialogue | Study Mode |
|---|---|---|
| Native Swap | The adapter replaces the visible native page without restarting dialogue | Either language can be shown |
| Attached Translation | A secondary panel is anchored to the native dialogue UI | Either language can be shown |
| Japanese Only | Native Japanese remains unobstructed; English toggles are ignored outside study | English remains available |

Capabilities are negotiated at runtime. A host that cannot safely replace native text falls back from Native Swap to
Attached Translation and reports that fallback. It must never mutate fragile dialogue state merely to claim support.
Japanese Only assumes the game itself is configured to render Japanese; it does not silently change the game's global
language setting.

## Adapter contract

A game adapter should provide these capabilities where possible:

- `nativeTextReplacement`: replace and restore only the presented page while preserving scripting, choices, timing, and
  page progression.
- `attachedPanel`: render reference text without modifying native message state.
- `dialogueAnchor`: expose the current dialogue rectangle in normalized viewport coordinates so panels follow different
  textbox positions, aspect ratios, and themes.
- `controllerGlyphs`: map semantic actions such as Language Toggle and Save Word to the host's current input glyphs.

The next shared API additions should describe dialogue snapshots and semantic input actions:

```cpp
struct DialogueSnapshot {
    GameMessageId message;
    int pageIndex;
    Language nativeLanguage;
    bool isChoice;
    NormalizedRect dialogueBounds;
};

enum class StudyAction {
    ToggleLanguage,
    EnterOrExitStudy,
    PreviousToken,
    NextToken,
    ScrollUp,
    ScrollDown,
    ToggleSaved,
};
```

`GameMessageId` is adapter-owned and should include the game/version/variant plus a source-text hash where IDs are not
stable. Portable code should not assume an N64 text ID, Shipwright `MessageContext`, ImGui, or any specific controller.

## SoH implementation status

- Attached Translation: implemented through the frame-safe overlay.
- Japanese Only: implemented; English reference remains available inside Study Mode.
- Native Swap: selectable as an adapter preview and capability-falls back to Attached Translation.
- Dialogue anchor and controller glyph capabilities: not implemented yet.

SoH's existing decode functions reset timers and message modes, so calling them to redraw the active conversation is not
a valid native-swap implementation. The SoH adapter needs a presentation-only text surface or a render hook that does not
decode into the live `MessageContext`.

## Implementation roadmap

1. Stabilize the display-mode contract, preference, fallback behavior, and unit tests.
2. Add an SoH dialogue-anchor provider and make the attached panel inherit native textbox placement and styling.
3. Add semantic action/glyph mapping and remove hard-coded controller labels from presentation code.
4. Prototype a non-mutating SoH native-text surface; test choices, page changes, text-ID jumps, shops, and cutscenes.
5. Move the portable contract and persistence models into a small game-neutral library once a second game adapter proves
   which abstractions are genuinely shared.

Waiting for a second adapter before extracting a separate library avoids freezing OoT-specific assumptions into a
supposedly universal API.
