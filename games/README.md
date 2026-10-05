# Game modules

Each directory is an independently discoverable JP Assist game module. The
module descriptor is the only catalog registration step: the website and API
must not contain a hard-coded list of games.

Use `ocarina-of-time` as the version-one layout reference. Public, reusable
learning content belongs in `catalog/`; distributable presentation assets in
`assets/`; game-specific review inputs in `pipelines/`; and runtime-specific
code in `adapters/<adapter-id>/`.

Extracted dialogue, generated corpora, ROM-derived assets, and other private
local build products must remain ignored and must not be committed to a game
module.
