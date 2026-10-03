# JP Assist Learning Platform MVP

This directory contains the content-neutral account and progress service
described in [`docs/LEARNING_PLATFORM_MVP.md`](../../docs/LEARNING_PLATFORM_MVP.md).
It is intentionally separate from the Shipwright build: the game can be
developed and used without running this optional service.

The path from the current MVP to a polished, deployment-ready local release is
tracked in
[`docs/LEARNING_SITE_ROADMAP.md`](../../docs/LEARNING_SITE_ROADMAP.md).

## Run locally

From the repository root:

```bash
python3 -m venv venv/learning-platform
. venv/learning-platform/bin/activate
pip install -r services/learning_platform/requirements.txt
PYTHONPATH=services/learning_platform \
  uvicorn learning_platform.main:app --reload --port 8766
```

Open <http://127.0.0.1:8766>. Interactive API documentation is available at
<http://127.0.0.1:8766/docs>. By default, local data is stored under the ignored
`services/learning_platform/var/` directory. Override it with
`JP_ASSIST_PLATFORM_DB=/path/to/platform.sqlite3`.

In Ship of Harkinian, open **Enhancements → JP Assist**, enable **Sync learning
progress**, leave the local service URL as `http://127.0.0.1:8766`, and choose
**Connect learning account**. Copy the displayed address and code, sign in to
the local website, and approve the device. Offline events are retained in the
game's `jp_assist_sync.json` and upload automatically after reconnection.

Set `JP_ASSIST_COOKIE_SECURE=1` behind HTTPS in any non-local deployment. The
included production scaffold supplies HTTPS termination, rate limiting, and
backup tooling. An open public beta still requires email verification,
password recovery, an operational backup schedule, and external monitoring.

## Production deployment scaffold

The included `Dockerfile`, `compose.yaml`, `.env.example`, and Caddy
configuration provide a single-host HTTPS deployment with persistent SQLite
storage, health checks, host validation, and basic endpoint-specific rate
limits. See
[`docs/LEARNING_PLATFORM_DEPLOYMENT.md`](../../docs/LEARNING_PLATFORM_DEPLOYMENT.md)
for configuration, migrations, backup/restore, upgrades, and the deployment
smoke test.

Useful non-container database commands are also available:

```bash
PYTHONPATH=services/learning_platform \
  python3 -m learning_platform.manage status
PYTHONPATH=services/learning_platform \
  python3 -m learning_platform.manage backup /secure/path/platform.sqlite3
```

## Pairing protocol

The mod starts without account credentials:

```http
POST /v1/device-pairings
Content-Type: application/json

{
  "deviceName": "Living room PC",
  "adapterId": "ship-of-harkinian",
  "gameId": "ocarina-of-time"
}
```

It shows the returned `userCode`, polls `/v1/device-pairings/token` using the
opaque `deviceCode`, and stores the one-time returned `deviceToken` in a private
local state file (`0600` on POSIX systems).
The player approves the user code while signed into the website. A pending
claim receives HTTP 428 and should respect the returned polling interval.

Event uploads use that device token:

```http
POST /v1/events/batch
Authorization: Bearer DEVICE_TOKEN
Content-Type: application/json

{"events": [{
  "eventId": "019f-example-1",
  "type": "word_encountered",
  "occurredAt": "2026-10-02T18:30:00Z",
  "gameId": "ocarina-of-time",
  "adapterId": "ship-of-harkinian",
  "contentVersion": "n64-ntsc-1.2-v1",
  "wordId": "武器|ぶき",
  "senseId": "weapon",
  "count": 1
}]}
```

The same `(device, eventId)` may be sent repeatedly. It is counted once and is
reported in `duplicateEventIds` on retries.

## Account features

After signing in, the website provides:

- an overview with activity and per-game totals;
- a searchable vocabulary library with saved/new/learning/known/ignored state,
  personal tags, and notes;
- an MVP spaced-review queue with append-only review history;
- daily new-word and review goals (the reminder preference is stored, but this
  release does not send notifications);
- saved-word and complete-account JSON exports;
- mod-device and website-session revocation, password changes, per-game data
  clearing, and account deletion.

The scheduler is intentionally an MVP scheduler, not an exact FSRS or Anki
scheduling implementation. Its history model is designed so a later scheduler
can replay prior answers instead of discarding them.

## Dictionary enrichment

The service does not upload dialogue or silently redistribute the local game
corpus. An operator can import separately licensed, content-neutral dictionary
metadata. A generic import is a JSON array with `wordId`, `senseId`, `written`,
`reading`, `partOfSpeech`, `meaning`, `source`, and `attribution` fields:

```bash
PYTHONPATH=services/learning_platform \
  python3 -m learning_platform.manage import-dictionary dictionary.json
```

For this repository's locally generated `runtime_data.json`, the specialized
command extracts only token dictionary fields and explicitly drops Japanese
and English dialogue. Supply truthful source and attribution text matching the
dictionary/content licenses used to generate your corpus:

```bash
PYTHONPATH=services/learning_platform \
  python3 -m learning_platform.manage import-runtime-dictionary \
  scripts/jp_assist/out/runtime_data.json \
  --source "Local JP Assist dictionary build" \
  --attribution "See the dictionary licenses used by this deployment"
```

## Anki handoff

Use **Download saved words** on the dashboard. The downloaded JSON is accepted
directly by the existing local builder:

```bash
python scripts/jp_assist/build_anki_deck.py \
  --progress-file ~/Downloads/jp_assist_cloud_progress.json \
  --output-prefix oot_jp_assist_saved \
  --deck-name "OoT JP Assist — Saved Words"
```

The service never needs the extracted dialogue. The local builder joins stable
word IDs with local `runtime_data.json`. Cloud manifests include the message
and page where each word was saved, so the card uses that dialogue instead of
an arbitrary occurrence elsewhere in the game. A custom deck name also gets
its own stable Anki namespace: repeated imports update that named deck without
merging it into the complete `OoT JP Assist` deck.

After downloading the manifest, the normal one-command path automatically
finds the newest download, builds the deck from the local corpus, and validates
the resulting package:

```bash
python scripts/jp_assist/export_saved_deck.py
```

The Export page also offers an optional direct AnkiConnect handoff. Anki and
AnkiConnect must be running on the same computer, and AnkiConnect must allow
the learning site's origin. The browser talks to `127.0.0.1:8765` directly;
the service never receives Anki credentials or review data from that action.
Only cards with server-side dictionary metadata are sent by this direct path.

## Tests

Core tests use only the Python standard library:

```bash
PYTHONPATH=services/learning_platform \
  python3 -m unittest discover -s services/learning_platform/tests -v
```

The cross-component acceptance test starts a temporary localhost server and
database, executes the complete pairing and retry-safe event flow, exports a
manifest, builds the deck twice, and verifies its context and stable note IDs:

```bash
python scripts/jp_assist/run_mvp_acceptance.py
```
