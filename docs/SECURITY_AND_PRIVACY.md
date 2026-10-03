# Security and privacy boundary

JP Assist separates the browser account, paired game adapters, and local Anki.
The service treats every boundary as untrusted even when the staging stack runs
on one machine.

## Browser account

Website sessions use random bearer values in `HttpOnly`, `SameSite=Lax`
cookies. Only their hashes are stored. Cookie-authenticated mutations require a
matching `jp_assist_csrf` cookie and `X-CSRF-Token` header. A request carrying an
explicit `Authorization` header does not use this browser-CSRF mechanism; this
keeps the device API independent from browser cookies.

The account session list uses a separate random public session ID, a normalized
browser user-agent label capped at 160 characters, and created/last-seen/expiry
times. It never exposes a full or partial token hash and deliberately does not
store client IP addresses.

Responses set a Content Security Policy that allows scripts, styles, images,
fonts, forms, and frames only where the site needs them. The sole extra network
destination is `http://127.0.0.1:8765`, the local AnkiConnect endpoint. The
service does not enable cross-origin API access. AnkiConnect itself must opt in
to the site's origin using its local CORS configuration; JP Assist never proxies
arbitrary URLs.

Additional response policy includes `no-referrer`, MIME sniffing protection,
frame denial, same-origin opener isolation, and disabled camera, microphone, and
geolocation capabilities. Recovery tokens remain in URL fragments, which are
not included in normal HTTP requests or referrer headers.

## Rate limits and credentials

Authentication, recovery, verification, sensitive account mutations, device
pairing, mod event uploads, reviews, and exports have bounded in-process rate
buckets. Public deployment still requires the reverse proxy's limits and a
shared limiter if multiple application instances are introduced.

Paired mods receive a random, revocable device token and never a website
password or cookie. Session, device, pairing, and action-token secrets are
stored only as hashes. Account exports omit credential hashes, action tokens,
password material, and mail credentials.

## Retention

Run the idempotent cleanup pass regularly:

```bash
PYTHONPATH=services/learning_platform \
  venv/learning-platform/bin/python -m learning_platform.manage cleanup
```

It removes expired website sessions and pairing codes, expired action tokens,
consumed action tokens after 7 days, reminder delivery markers after 120 days,
and account audit events after 365 days. It does not remove vocabulary,
annotations, encounters, or review history. Account deletion removes all data
owned by that account through foreign-key cascades. File-mailbox and Mailpit
retention are separate operator responsibilities because captured messages can
contain live action links.

## Privacy inventory

Allowed server-side learning data includes stable game/adapter/content IDs,
message and page IDs, location IDs, word and sense IDs, counts, timestamps,
user-authored notes and tags, review history, and separately licensed
dictionary metadata.

The following data is intentionally absent from API event contracts, synthetic
fixtures, dictionary transformations, account telemetry, and server logs:

- Japanese or English dialogue text;
- screenshots, audio, save files, and controller input streams;
- raw session, device, pairing, recovery, or verification tokens;
- Anki collection contents other than the explicit card/review fields selected
  by the user for synchronization.

The event API rejects unknown fields, including attempted dialogue payloads.
The runtime dictionary importer copies dictionary fields only and does not copy
message text.

## Threat model

| Threat | Current control | Remaining release work |
| --- | --- | --- |
| Stolen website session | Hashed 30-day token, `HttpOnly` cookie, independent public session ID, privacy-minimal browser label, revocation, password/reset invalidation | Verify rotation policy in the final audit |
| Cross-site request forgery | Double-submit token on cookie-authenticated mutations; SameSite cookies | Continue negative tests as routes are added |
| Pairing-code guessing | Short expiry, one-time claim, rate limit, explicit account approval | Load-test shared limiter topology |
| Device-token theft | Hashed storage, per-device scope, revocation | Document adapter-side secret storage |
| Malicious event payload | Strict schemas, identifier and count bounds, unknown-field rejection | Expand fuzz/property coverage |
| Malicious dictionary metadata | Explicit field import, output escaping in the UI | Add corpus-size and pathological-string tests |
| AnkiConnect abuse | Browser connects only to loopback; explicit user action; no server proxy | Document least-privilege AnkiConnect origin setup |
| Recovery-link leakage | Hashed, purpose-bound, expiring, single-use tokens in URL fragments; no-referrer policy | Validate real-provider log/redaction behavior before hosting |
| Destructive account action | Reauthentication or explicit confirmation, CSRF defense, audit events | Complete release threat-model review |
| Backup disclosure | Local mode-0600 SQLite backups and no raw bearer values in the database | Add encryption/storage policy for chosen infrastructure |

This document describes the local release candidate boundary. Internet hosting
still requires secret scanning, dependency/image auditing, an SBOM, a shared
rate limiter where applicable, external monitoring, and infrastructure-specific
backup and incident policies.
