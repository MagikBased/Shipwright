# Learning Platform MVP

## Outcome

The MVP gives JP Assist and future recompilation mods one optional account for
cross-game vocabulary progress. Games remain fully usable offline and never
handle a website password. The service receives content-neutral learning
events, presents aggregate statistics, and exposes a saved-word manifest that
the existing local Anki builder can combine with the player's own extracted
corpus.

## First vertical slice

1. A player registers or signs in on the website.
2. A mod requests a device pairing and displays its short user code.
3. The player approves that code on the website.
4. The mod claims a device-scoped bearer token.
5. The mod queues stable, uniquely identified events locally and uploads them
   in batches whenever the service is available.
6. The service deduplicates retries transactionally and updates per-word
   encounter, selection, and saved state.
7. The player views aggregate statistics and downloads a saved-word manifest.
8. A local command combines that manifest with `runtime_data.json` and produces
   the existing stable-GUID `.apkg` deck.

This slice is implemented. Ship of Harkinian has an opt-in account section in
**Enhancements → JP Assist**. Its shared client persists an atomic offline
outbox, pairs without collecting account credentials, performs HTTP work on a
background thread, retries with bounded exponential backoff, and acknowledges
both newly accepted and already-seen event IDs. The game adapter emits stable
message, word, sense, game, adapter, and corpus-version identifiers.
Remote service URLs require HTTPS; cleartext HTTP is accepted only for the
loopback development service. Pairing state and device credentials are bound
to the service URL that issued them, so editing the URL requires an explicit
disconnect/re-pair and cannot silently forward a token to another host.
The complete boundary can be exercised without real player data using
`python scripts/jp_assist/run_mvp_acceptance.py`; it starts an ephemeral HTTP
service and database and validates the resulting Anki package and stable IDs.

## Privacy and copyright boundary

The hosted service accepts identifiers and activity, not extracted dialogue:

- game, adapter, and content-pack versions;
- stable word and sense IDs;
- optional message and location identifiers;
- encounter counts, save state, and timestamps.

Japanese/English sentences remain in the local corpus derived from the
player's own game archive. The server-side API must not add an arbitrary text
payload to learning events without a separate content licensing decision.

## Authentication

- Website passwords are salted with `scrypt`; plaintext passwords are never
  stored.
- Website sessions and device tokens are random bearer credentials represented
  only by SHA-256 hashes in the database.
- Pairing codes expire and can be claimed once.
- A mod receives a revocable device token and never receives the account
  password or website session.
- The desktop client keeps that token in `jp_assist_sync.json`; POSIX builds
  restrict the file to the current user (`0600`). A production follow-up can
  move this secret behind each operating system's credential store without
  changing the pairing or event APIs.

Email verification, password recovery, third-party login, administration, and
rate limiting are deployment requirements after the local MVP proves the
workflow.

The release scaffold now supplies HTTPS termination, explicit host validation,
endpoint-specific in-process rate limits, health/readiness checks, versioned
schema startup, and consistent backup/restore commands. Email verification,
password recovery, external monitoring, and operational backup scheduling
remain deployment-owner requirements before an open public beta. See
`LEARNING_PLATFORM_DEPLOYMENT.md`.

## Event contract

Every upload contains a client-generated `eventId`. Its identity is scoped to
the paired device so replaying an offline batch is safe.

Initial event types:

- `word_encountered`
- `word_selected`
- `word_saved`
- `word_unsaved`
- `dialogue_seen`
- `study_mode_opened`

Encounter and selection counts are additive. Save state uses event time with a
deterministic event-ID tie breaker. Review answers are deliberately absent:
the first MVP tracks play and exports to Anki; a later milestone will introduce
append-only FSRS review logs and make the scheduling owner explicit.

## Portable mod boundary

Adapters map game state to a game-neutral envelope:

```json
{
  "eventId": "019f...",
  "type": "word_encountered",
  "occurredAt": "2026-10-02T18:30:00Z",
  "gameId": "ocarina-of-time",
  "adapterId": "ship-of-harkinian",
  "contentVersion": "n64-ntsc-1.2-v1",
  "wordId": "武器|ぶき",
  "senseId": "weapon",
  "messageId": "0x1034",
  "pageIndex": 2,
  "locationId": "kokiri-forest",
  "count": 1
}
```

The shared client is responsible for durable queueing, batching, exponential
backoff, and acknowledging only accepted or duplicate event IDs. It never
performs network I/O on the game/render thread. `LearningSyncClient` and
`LearningSyncStore` are game-neutral; `LearningSyncRuntime` is the current SoH
adapter. Future recompilation mods can reuse the former two and supply their
own event mapping, storage path, UI, and game/adapter IDs.

Saved-word exports retain only the content-neutral message ID and page index
where the save occurred. The local Anki builder joins those identifiers to the
player's local corpus, so cards use the dialogue context that prompted the save
without uploading either language's text.

## Deferred deliberately

- Bidirectional Anki scheduling or direct AnkiWeb credentials.
- AnkiConnect desktop bridge.
- Website FSRS reviews and review-history import.
- Public content packs containing copyrighted dialogue.
- Social features, leaderboards, or public profiles.

These can build on the event and identity model without changing the first
game adapter.
