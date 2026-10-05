# Game module contract

A game module is the unit that connects one game's reviewed learning content
and runtime adapters to JP Assist. Modules live under `games/<game-id>` while
this repository is being separated from the Shipwright fork. They can later be
published or checked out independently without changing the catalog contract.

Every module has a `module.json` descriptor with:

- a stable, lowercase `id` shared by the catalog and account events;
- paths to the public game, card, and vocabulary manifests;
- a directory containing distributable catalog artwork;
- an optional private/local content pipeline; and
- one or more runtime adapters.

Paths are resolved relative to the module root and may not escape it. The
learning service discovers modules rather than importing game-specific Python
code. Set `JP_ASSIST_GAMES_ROOT` to load modules from another checkout.

Runtime adapters consume the shared Study Mod SDK. Host forks implement that
SDK but do not own catalog content, vocabulary identity, SRS state, or the
website.

The JSON Schema in `game-module.schema.json` documents descriptor version 1.
The service also validates paths at startup so a malformed module fails before
it can appear in the catalog.
