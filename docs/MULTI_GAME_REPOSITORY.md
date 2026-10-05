# Multi-game repository migration

JP Assist is moving from a feature developed inside the Shipwright fork to a
platform with independently discoverable game modules.

## Ownership boundaries

The platform owns accounts, canonical lexeme and sense identity, FSRS review,
the catalog API/site, shared content tooling, and the public Study Mod SDK.

A game module owns catalog metadata, reviewed course cards, vocabulary
membership, presentation assets, game-specific pipeline inputs, and runtime
adapters. It does not own account-wide learning state.

A host fork owns only the generic implementation required to load and serve
runtime adapters. Shipwright and libultraship remain separate forks so their
upstream histories can be merged without bringing platform changes into every
conflict.

## Transitional layout

The first migration milestone introduces `games/ocarina-of-time` and
`packages/game-module-contract` in the existing development checkout. The
learning service discovers those modules, and the OoT `.o2r` source now lives
with the OoT module. Existing shared source under `soh/Enhancements/JPAssist`
will be classified and moved in later milestones: portable code goes to the
SDK/core package, while actual host integration remains in Shipwright.

## Extraction milestones

1. Establish and validate the game-module descriptor and content paths.
2. Make the OoT plugin build from its game module without behavior changes.
3. Move the web/API service and shared content tools into the new platform
   repository while preserving their Git history.
4. Extract a standalone Study Mod SDK and make adapters depend only on it.
5. Reduce the Shipwright fork to the generic host bridge and compatibility UI.
6. Add an optional integration workspace that pins the Shipwright fork for
   end-to-end tests without making it a platform build dependency.

During the transition, compatibility loaders may accept the original flat
catalog directory. New content must use the module contract.
