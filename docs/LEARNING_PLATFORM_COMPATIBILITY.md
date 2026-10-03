# Learning Platform Compatibility Policy

## Versioned boundaries

The learning platform has three independent compatibility identifiers:

- **HTTP API:** every supported route is under `/v1`. The major path changes
  only when an adapter cannot continue without changing existing semantics.
- **Database schema:** SQLite stores an integer schema version. Release
  `0.3.0-rc.1` uses schema `9`; startup migrates older supported schemas and
  refuses schemas newer than the running application.
- **Adapter content version:** each event carries an adapter-defined opaque
  `contentVersion`. It distinguishes local corpus revisions without exposing
  dialogue and does not control the HTTP or database version.

Ship of Harkinian is the first adapter, not a special server contract. A future
recompiled game uses the same device pairing and event envelope with its own
stable `adapterId`, `gameId`, `contentVersion`, word IDs, sense IDs, message/page
IDs, and location IDs.

## API compatibility

Within `/v1`, a release may add endpoints, optional response fields, optional
request fields with explicit defaults, new documented event types, and new
machine-readable error details. It will not silently reinterpret an existing
field, remove a response field, make an optional request field required, or
reuse an identifier for different content.

Requests use strict schemas and reject unknown fields. Adapters therefore send
only fields defined by the version they target and must ignore unknown response
fields. Device event retries preserve the same `eventId`; the server's
`(device, eventId)` identity makes replay idempotent. A device credential is
scoped to the paired adapter and game.

A breaking API requires `/v2`, a documented coexistence window, adapter test
fixtures for both versions, and a migration path before `/v1` removal. No `/v1`
removal date is set for this release candidate.

## Database compatibility

Migrations are forward-only when the current application starts. They preserve
source answers and events, then rebuild derived projections where required.
Every schema change must include:

1. a migration test from the immediately previous schema;
2. readiness and integrity checks after migration;
3. a pre-upgrade backup and measured forward/rollback rehearsal;
4. release notes identifying the schema transition; and
5. an older-image rollback procedure using `restore --preserve-schema`.

Downgrade migrations are intentionally not run in place. Rollback restores the
pre-upgrade artifact without migration, then starts the matching older image.
The current binary must never serve a preserved older schema, and an older
binary must never serve a newer schema.

## Current matrix

| Site release | HTTP API | SQLite schema | Browser qualification | Adapter status |
| --- | --- | --- | --- | --- |
| `0.3.0-rc.1` | `/v1` | `9` | Chromium and Firefox | Ship of Harkinian adapter; generic event contract ready for additional games |

Chromium owns pixel baselines. Firefox runs the complete functional,
responsive, keyboard, and accessibility suite. WebKit is not release-qualified
on the current Linux Mint host because Playwright's Ubuntu fallback requires
`libavif13`, which is unavailable in that host configuration; no product defect
was observed because the engine could not launch. A supported WebKit runner is
required before claiming Safari/WebKit support.

## Deprecation and support

Each release note records its API and schema versions. The previous release
image and its pre-upgrade backup remain available for at least the operator's
rollback window. Public support lifetimes, user notification channels, and a
hosted deprecation schedule are deferred until infrastructure and operators are
chosen; this does not permit an undocumented breaking change.
