# JP Assist Learning Platform MVP

This directory contains the content-neutral account and progress service
described in [`docs/LEARNING_PLATFORM_MVP.md`](../../docs/LEARNING_PLATFORM_MVP.md).
It is intentionally separate from the Shipwright build: the game can be
developed and used without running this optional service.

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
MVP is not production-ready until HTTPS termination, email verification,
password recovery, rate limiting, backups, monitoring, and deployment secrets
are configured.

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

## Tests

Core tests use only the Python standard library:

```bash
PYTHONPATH=services/learning_platform \
  python3 -m unittest discover -s services/learning_platform/tests -v
```
