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

Operational telemetry follows the same boundary. Structured request logs use
normalized route templates and omit query strings, bodies, headers, addresses,
cookies, and tokens. Audit logs emit only audit types and metadata key names;
their request IDs permit correlation without duplicating account content.
Prometheus labels are bounded to method, route template, status, and audit type.
The metrics endpoint is reachable only on the private Compose network and Caddy
blocks the entire `/internal/` prefix from the public origin.

## Threat model

The local release-candidate review is complete. Items marked deferred below
require a hosting decision or multi-instance architecture and are gates for a
public deployment, not for the single-host local candidate.

| Threat | Current control | Residual or deferred risk |
| --- | --- | --- |
| Stolen website session | Hashed fixed-lifetime 30-day token, `HttpOnly` cookie, independent public session ID, privacy-minimal browser label, revocation, password/reset invalidation | Sessions are deliberately non-sliding and are not transparently rotated, avoiding concurrent-request races; public deployment should reassess duration against its risk profile |
| Cross-site request forgery | Double-submit token on every cookie-authenticated mutation; SameSite cookies; negative API tests | Re-run the route inventory whenever a mutation is added |
| Pairing-code guessing | Short expiry, one-time claim, rate limit, explicit account approval | A shared limiter is required before horizontally scaling beyond one app instance |
| Device-token theft | Hashed storage, per-device scope, revocation; the reference adapter stores its local state mode `0600` on POSIX | Platform-specific credential vaults remain a future adapter enhancement |
| Malicious event payload | Strict schemas, identifier/type/count/length bounds and unknown-field rejection, exercised by privacy and validation tests | Extend the adversarial corpus when the event contract grows |
| Malicious dictionary metadata | Explicit field import, contextual output escaping, pathological-string browser acceptance, and an 8,000-entry load corpus | Re-run corpus qualification for materially larger bundled dictionaries |
| AnkiConnect abuse | Browser connects only to loopback; explicit user action; no server proxy; setup documents an exact-origin `webCorsOriginList` | The user controls the local AnkiConnect add-on and its permissions |
| Recovery-link leakage | Hashed, purpose-bound, expiring, single-use tokens in URL fragments; no-referrer policy | Validate the selected provider's log/redaction behavior before public hosting |
| Destructive account action | Reauthentication or explicit confirmation, CSRF defense, audit events, browser acceptance for cancellation and completion | No known local-candidate gap; repeat review when adding destructive actions |
| Backup disclosure | Local mode-`0600` SQLite backups, bounded retention, and no raw bearer values in the database | Select encrypted off-host storage and a key-management policy before public hosting |

## Supply-chain evidence

The production Dockerfile pins the Python base image by immutable multi-platform
digest and pins every direct application dependency to an exact version. The
browser-test dependency tree is locked by `package-lock.json`.

The learning-platform workflow performs the following release checks:

- `pip-audit` 2.10.1 against the production requirements and a CycloneDX JSON
  component report;
- `npm audit` with high severity as the failure threshold;
- Gitleaks 8.30.1 against all Git history, downloaded with its published SHA-256
  checksum;
- a production image build; and
- an SPDX JSON SBOM of the built image using a commit-pinned Anchore action;
- the complete service/privacy suite and production runtime checks against that
  built image; and
- Grype 0.119.0 against the image SBOM, failing on unresolved high or critical
  findings and retaining its JSON report.

`.gitleaksignore` contains only exact finding fingerprints for reviewed
pre-existing upstream Ship of Harkinian constants and StormLib test-key
material. It does not suppress a path, rule, or future finding. Security reports
and SBOMs are CI artifacts rather than committed generated files.

Before a release, rerun the audits against the final commit, review the complete
image SBOM, and record any accepted vulnerability with owner, rationale, and an
expiry date. A clean automated result is evidence for—not a replacement for—the
final threat-model review.

### Release audit record — 2026-10-03

- `pip-audit` reported no known vulnerability in the pinned application
  requirements, and `npm audit` reported no browser-test dependency finding.
- Gitleaks scanned all repository history with no unreviewed finding after
  applying the fingerprint-specific upstream allowlist.
- The final Alpine 3.23 image inventory contains 58 packages. The Grype result
  has no unresolved high or critical finding; its remaining report contains
  nine medium and one negligible finding.
- CVE-2026-85091 in Alpine zlib is marked `not_affected` in
  `services/learning_platform/security/openvex.json`. Exploitation requires the
  non-blocking `gzFile` write APIs; JP Assist neither calls nor exposes those
  APIs. The maintainer-owned exception expires on 2026-11-02 and must be
  revisited sooner if zlib or native compression code changes.
- Privacy-contract tests and the production runtime smoke test pass inside the
  built image. Production docs are disabled and all required browser security
  headers are present.

This document describes the local release candidate boundary. Internet hosting
still requires a shared rate limiter where applicable, external monitoring, and
infrastructure-specific backup and incident policies.
