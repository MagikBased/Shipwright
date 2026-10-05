# JP Assist `.o2r` plugin

This directory builds the in-game JP Assist runtime as a drop-in Shipwright
code mod. It owns corpus lookup, study-card state and rendering, saved/known
progress, pronunciation playback, and account synchronization. The website,
catalog, account service, and deck generation remain independent of the game
plugin.

## Build

Configure the repository normally with scripting and the plugin package
enabled (both are on by default for desktop JP Assist builds), then run:

```bash
cmake --build build-cmake --target jpassist_plugin_package
```

The package is written to:

```text
build-cmake/games/ocarina-of-time/adapters/shipwright/plugin/jp-assist.o2r
```

If `scripts/jp_assist/out/runtime_data.json` exists at configure time, it is
included as the plugin corpus. Locally generated audio is included when its
manifest and audio directory are present. These generated resources remain
ignored by Git.

## Install

Copy `jp-assist.o2r` into the `mods` directory next to Shipwright's executable
and game archives. A compatible package initializes automatically; no runtime
toggle is required. The normal **Enhancements → JP Assist** master toggle and
visual/account settings remain the user-facing controls.

Plugin-owned writable state is stored below `mods/jp-assist/`. On first use,
the JP Assist host adapter copies legacy root-level progress and sync files
there only when the destination does not already exist. It also copies any
saved upper-dialogue, lower-dialogue, and no-dialogue card geometry. Because
code mods initialize early, this migration sends a one-shot reload request so
the first plugin session sees the copied state without requiring a restart.

This development package currently requires the Study Mod API and code-mod
loader changes from this repository. Stock Shipwright releases cannot load it
until that reusable host support is accepted upstream.

## Verify

The binary-package integration test loads the actual archive through
libultraship. The gameplay smoke wrapper temporarily installs the package,
runs all recorded scenarios, and restores the previous config/mod state:

```bash
cmake --build build-cmake --target jpassist_plugin_tests soh
ctest --test-dir build-cmake/tests/jp_assist --output-on-failure
./scripts/jp_assist/run_plugin_smoke_suite.sh
```
