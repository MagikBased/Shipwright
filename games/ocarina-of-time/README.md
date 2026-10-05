# Ocarina of Time game module

This module contains the public course and the Shipwright adapter for Ocarina
of Time. It is deliberately separate from Shipwright's generic Study Mod host
implementation.

- `catalog/` contains the game, reviewed cards, and transferable vocabulary.
- `assets/` contains catalog presentation artwork.
- `pipelines/` contains non-dialogue alignment and chapter-review metadata.
- `adapters/shipwright/plugin/` builds the drop-in `.o2r` runtime.

The personal corpus builder still reads dialogue from the player's own game
archive and writes its generated output beneath the ignored
`scripts/jp_assist/out/` directory. No extracted dialogue is part of this
module.
