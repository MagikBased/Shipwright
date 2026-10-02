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

Email verification, password recovery, third-party login, administration, and
rate limiting are deployment requirements after the local MVP proves the
workflow.

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
  "locationId": "kokiri-forest",
  "count": 1
}
```

The shared client is responsible for durable queueing, batching, exponential
backoff, and acknowledging only accepted or duplicate event IDs. It must never
perform network I/O on the game/render thread.

## Deferred deliberately

- Bidirectional Anki scheduling or direct AnkiWeb credentials.
- AnkiConnect desktop bridge.
- Website FSRS reviews and review-history import.
- Public content packs containing copyrighted dialogue.
- Social features, leaderboards, or public profiles.

These can build on the event and identity model without changing the first
game adapter.
