# JP Assist drop-in plugin migration

## Goal

Package JP Assist as a drop-in `.o2r` mod backed by the smallest reusable
public study-mod host API. The host API must remain useful to other games and
must not expose private Shipwright C++ object layouts.

## Architecture

JP Assist is being split into three layers:

1. **Portable core** — corpus parsing, token/sense identity, selection state,
   persistence, learning synchronization, audio decoding, and layout policy.
2. **Game adapter** — converts a game's dialogue, input, and UI state into the
   portable model. The current implementation is the Shipwright adapter.
3. **Host ABI** — a versioned C interface exposed to `.o2r` modules for events,
   drawing, storage, audio, and settings.

The website, account service, catalog pipeline, and deck tooling remain outside
the game plugin and require no migration.

Shipwright retains two deliberately thin companion surfaces: its settings/
account menu and searchable recent-dialogue window. The latter passively
observes dialogue opens even when the plugin owns Study Mode, so the migration
does not remove existing local history. It does not duplicate the plugin's
account event or participate in study-card state.

## Dependency boundary

| Capability | Current integration | Required plugin API |
| --- | --- | --- |
| Data/save paths | `Ship::Context` | mod data lookup and private writable path |
| Dialogue lifecycle | `GameInteractor` and `PlayState` | open/page/close snapshot events |
| Study controls | raw `Input`/message state | button edges plus selective button/axis consumption |
| Overlay/history | `Ship::GuiWindow` and ImGui | registered immediate overlay draw callback |
| Native highlight | `z_message_PAL.c` insertion | glyph-range highlight provider |
| Pronunciation | mixer call in `OTRGlobals.cpp` | queued clip or audio-mix callback |
| Settings/console | SoH menu and console | optional settings and command registration |
| Scene/exit cleanup | `GameInteractor` | scene, reset, and shutdown events |

## Migration sequence

- [x] Inventory source-integrated dependencies and define the boundary.
- [x] Route portable data and writable paths through an injectable host
  service (`JPAssistHost`).
- [x] Move Study Mode state transitions and semantic input actions behind the
  game-neutral, testable `StudySession` core. Dialogue decoding/page observation
  remains in the Shipwright adapter until the dialogue-event ABI is installed.
- [x] Define the versioned C host ABI and capability negotiation. The v1 table
  initially advertises private writable storage and packaged-resource reads;
  capabilities are added only after external mods can use their
  implementations.
- [x] Implement the minimum ABI in Shipwright: dialogue/page events, semantic
  input with selective consumption, scene/exit lifecycle, native glyph-range
  highlighting, an ImGui-independent overlay draw list, copied PCM playback,
  namespaced typed settings, packaged resources, and private writable storage.
- [x] Replace the active JP Assist overlay, input, lifecycle, and persistence
  usage with calls from the plugin. The plugin now owns corpus lookup,
  `StudySession`, semantic study controls, native highlight selection, private
  saved/known/encounter persistence, pronunciation lookup/decoding/playback,
  durable account-event queuing and pairing/upload, and an initial
  primitive-rendered study card. A runtime-ready diagnostic performs a clean,
  automatic ownership boundary so no source-integrated runtime consumes the
  same input, overlay, or highlight request.
- [x] Build and load a precompiled binary from a real `.o2r` plugin. The
  integration test negotiates every mandatory capability, dispatches events,
  verifies selective input consumption and highlighting, mixes a submitted
  audio clip, invokes an overlay callback, unloads the binary, and confirms
  callback/audio cleanup.
- [x] Package the locally generated corpus as a raw `.o2r` resource when it is
  available. Complete extracted dialogue remains ignored and is never added to
  source control.
- [x] Package optional locally generated audio resources when present. Missing
  audio degrades to a disabled playback action.
- [x] Add user-facing compatibility diagnostics for incompatible hosts,
  missing capabilities/resources, and wrong-game packages.
- [x] Verify the plugin path with native tests and the automated game smoke
  suite. The isolated runner passed all six gameplay scenarios through the
  packaged `.o2r` runtime.
- [x] Make a successfully initialized plugin the sole JP Assist runtime. When
  it is absent, incompatible, or unloaded, Shipwright reports that the plugin
  is unavailable and does not activate a duplicate built-in implementation.
- [x] Remove the guarded source fallback. Shipwright now retains only the
  generic host bridge plus thin settings, migration, history, and Test Lab
  companion code. The reusable ABI/bridge, native renderer hook, code-mod
  loader fix, and JP Assist package are isolated by subsystem for upstream
  review.

## Compatibility policy

The plugin requests capabilities individually. Missing optional capabilities
must degrade predictably: native highlighting falls back to overlay emphasis,
audio controls disappear when playback is unavailable, and account sync can
continue through local persistence. Dialogue observation, input, overlay
rendering, and writable storage are mandatory for the initial usable plugin.

## Host ABI rules

The public interface is declared in `soh/include/mods/study_mod_api.h` and is
resolved through `StudyMod_GetHostApi(minimum_version, maximum_version)`. It is
a C ABI so precompiled plugins do not inherit Shipwright's private C++ layouts.
Tables carry an ABI version and byte size, and every optional facility has a
capability bit. Callers must check all three before using an entry. Version one
provides traversal-safe per-mod writable paths under `mods/<mod-id>/`, mounted
resource reads, dialogue/input/lifecycle subscriptions, native highlight
providers, a host-rendered primitive overlay surface with clipping and native
button glyphs, bounded copied PCM playback, and namespaced integer/float/string
settings. Functions are appended without moving existing fields. Input
callbacks may also return reserved high-bit flags to consume analog axes; older
v1 plugins remain binary-compatible.

## Current runnable milestone

Desktop builds enable libultraship's code-mod support and package
`build-cmake/games/ocarina-of-time/adapters/shipwright/plugin/jp-assist.o2r`
with a platform binary.
Shipwright initializes code-bearing archives when its mod menu mounts them,
preserving dependency ordering and normal `ModExit` teardown. Binary loading
also fixes libultraship's temporary-file bookkeeping, which previously made the
manifest `binaries` path unusable. The automated test loads the actual archive
and reaches both `ModInit` and `ModExit`.

The package is now feature-bearing rather than only a capability probe. It
loads the generated corpus through raw mounted-archive access, retargets the
portable session from dialogue events, implements R and L/Z entry modes,
navigates words, leaves the native dialogue-advance input unconsumed, supplies
the selected token's native highlight range, and renders an initial
three-column card through the public drawing API. The card supports responsive
smart placement, per-location persisted geometry, drag/resize with axis locking
and snapping, independent column scrolling, native control glyphs (including
glyph markers embedded in English dialogue), and compact furigana/word/
definition presentation. A successful plugin initialization publishes a
runtime-ready diagnostic. Failure or unload removes the callbacks cleanly and
leaves the menu in an explicit `plugin not loaded` state; it never activates a
second built-in study runtime.

The packaged-plugin integration test injects a small legal corpus and WAV
fixture and exercises open, navigation, definition toggling, selective input
consumption, highlight changes, saved and known actions, encounter counting,
pronunciation playback, durable account events, immediate account-command
polling, concrete overlay draw calls, lifecycle dispatch, and unload cleanup
through the actual `.o2r` binary.

For a gameplay-level verification, `run_plugin_smoke_suite.sh` temporarily
installs the built package, enables the plugin handoff settings, delegates to
the existing six-scenario automated suite, and restores the prior config and
installed package through a shell trap. This has passed end-to-end after the
source runtime was removed. The resulting Shipwright changes are limited to
the reusable ABI/adapter, the native highlight and textbox-bound hooks, the
code-mod loader reliability fix, and the thin JP Assist companion surfaces
described above.
