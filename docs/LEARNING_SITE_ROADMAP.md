# JP Assist Learning Site Roadmap

## Target outcome

Deliver a polished learning site that is safe and complete enough to deploy,
while stopping before public hosting. The release candidate must run locally in
the same container, HTTPS, database, migration, and mail topology intended for
production. DNS, public infrastructure, paid services, and opening registration
to the internet are explicitly outside this goal.

The site remains game-neutral. Ship of Harkinian is the first adapter, not a
special case in the account model. Copyrighted Japanese and English game
dialogue remains on the player's computer; the account service receives stable
identifiers, learning events, annotations, and separately licensed dictionary
metadata only.

## Product principles

- The game remains completely usable offline and without an account.
- A mod receives a revocable device credential, never a website password.
- Review answers are append-only; scheduling state is reproducible rather than
  being the only copy of learning history.
- Every destructive account action is explicit, scoped, and testable.
- Anki integration has one declared scheduling owner at a time. The website and
  Anki must never silently reschedule the same card independently.
- Accessibility, responsive behavior, privacy, recovery, and operations are
  release requirements, not post-launch cleanup.

## Current baseline

Completed:

- Account registration, login, logout, and hashed sessions.
- Device-code pairing, revocation, offline event queues, retry deduplication,
  and per-game progress.
- Vocabulary search/filter/sort, notes, tags, learning states, and dictionary
  metadata imports.
- Activity overview, goals, saved-word export, complete account export, and
  direct browser-to-local-Anki AnkiConnect export.
- MVP review queue with append-only review history.
- Password changes, session revocation, per-game clearing, and account deletion.
- Versioned SQLite migrations, health/readiness checks, HTTPS deployment
  scaffold, rate limits, and backup/restore commands.
- Service, API, migration, deployment, and cross-component Anki acceptance tests.

Known intentional limitations:

- The current scheduler is not exact FSRS.
- Reminder preference is stored but notifications are not delivered.
- Email verification and password recovery are absent.
- Browser workflows, responsive layouts, accessibility, and visual regressions
  are not yet covered by automated acceptance tests.
- AnkiConnect currently exports cards but does not reconcile review history.

## Milestone 1 — Account acceptance harness

Build deterministic browser tests around a synthetic account containing
multiple games, homographs, multiple senses, long definitions, missing
definitions, notes, tags, saved/unsaved words, due reviews, and revoked devices.

Deliverables:

- A one-command fixture generator that never uses personal save data or game
  dialogue.
- Playwright coverage for registration/login, navigation, vocabulary filters,
  annotation editing, reviews, goals, pairing, revocation, exports, password
  changes, game clearing, logout, and account deletion.
- An AnkiConnect test double covering successful imports, unavailable Anki,
  malformed responses, duplicate cards, and same-spelling/different-reading
  cards.
- Responsive checks at phone, tablet, 1080p, and ultrawide viewports.
- Accessibility assertions and keyboard-only navigation for every primary flow.
- Failure artifacts: screenshot, DOM snapshot, console log, and API trace.

Exit gate: all critical account journeys run without manual clicks in one
command against a temporary database and clean up after themselves.

## Milestone 2 — Product and visual polish

Turn the functional workspace into a coherent learning product.

Deliverables:

- Consistent navigation, empty/loading/error/offline states, skeletons, and
  non-blocking success feedback.
- Pagination or virtualization for large vocabulary libraries and activity
  histories.
- Word detail views with source attribution, game appearances, encounter
  history, notes, tags, and review status.
- Useful dashboard summaries: due today, recent streak, goal completion,
  learning-state distribution, and per-game trends.
- Accessible confirmation dialogs for destructive actions instead of browser
  `confirm()` calls.
- Reduced-motion support, visible focus, semantic landmarks, sufficient color
  contrast, readable Japanese fonts, and screen-reader labels.
- A small design-token/component layer so future game projects share the same
  account UI without copying pages.

Exit gate: no known severity-1 accessibility failures, no horizontal overflow
at supported widths, and approved visual snapshots for all primary states.

## Milestone 3 — Exact FSRS review system

Replace the temporary interval logic with a pinned, documented FSRS
implementation and deterministic state reconstruction.

Deliverables:

- Store the FSRS algorithm/version, parameters, desired retention, card state,
  stability, difficulty, scheduled days, elapsed days, and last review.
- Migrate existing MVP review history by replaying it; do not discard or invent
  answers.
- Again/Hard/Good/Easy previews, new/learning/relearning/review states, daily
  limits, burying, and timezone-safe day boundaries.
- Parameter defaults plus future per-user optimization without silently
  changing already-scheduled cards.
- Property and replay tests proving identical history produces identical state.
- Document how algorithm upgrades are versioned and rolled back.

Exit gate: published FSRS conformance fixtures pass and database state can be
deleted and rebuilt entirely from the immutable review log.

## Milestone 4 — Anki interoperability

Support two explicit modes rather than allowing competing schedulers:

1. **JP Assist reviews** — JP Assist owns scheduling; Anki receives study cards
   as a portable copy and does not feed scheduling decisions back implicitly.
2. **Anki reviews** — Anki owns scheduling; JP Assist displays imported activity
   but does not place those cards in its own due queue.

Deliverables:

- Stable note/card identity across repeated exports, game filters, homographs,
  and dictionary updates.
- Preflight showing additions, updates, duplicates, skips, and field conflicts.
- Idempotent AnkiConnect model/deck creation and field migration.
- Explicit review-owner setting per collection, with safe mode-switch rules.
- Anki review-history import where supported, preserving source and original
  timestamps in the append-only log.
- Clear recovery instructions for Anki unavailable/CORS/configuration failures.

Exit gate: repeat syncs are idempotent, no same-spelling vocabulary is lost,
and the review owner is visible wherever due dates are shown.

## Milestone 5 — Identity, recovery, and notifications

All flows must work locally using a development mail catcher; selecting a real
email provider is a deployment-time concern.

Deliverables:

- Email verification with expiring, single-use, hashed tokens.
- Password reset with session revocation and enumeration-resistant responses.
- Change-email flow requiring password confirmation and verification.
- Resend limits, token cleanup, audit events, and safe email templates.
- Notification preferences by category and timezone.
- Goal/reminder emails rendered into the local mail catcher, with unsubscribe
  behavior and disabled-by-default delivery.

Exit gate: registration, verification, reset, change-email, and reminder flows
pass browser tests without sending traffic outside the local environment.

## Milestone 6 — Privacy and security hardening

Deliverables:

- CSRF protection for cookie-authenticated mutations and a documented CORS
  policy for local AnkiConnect interaction.
- Content Security Policy and other browser security headers.
- Session metadata sufficient to recognize/revoke access without storing more
  than needed; sensitive values are redacted from logs and exports.
- Rate-limit coverage for recovery, verification, review, export, and destructive
  account endpoints.
- Dependency auditing, pinned production dependencies, secret scanning, and an
  SBOM for the release image.
- Data-retention rules and cleanup jobs for expired sessions, pairing codes,
  recovery tokens, and operational logs.
- Privacy inventory proving dialogue is absent from API contracts, fixtures,
  logs, backups, exports, and dictionary-import transformations.
- Threat-model review of pairing, session theft, CSRF, SSRF, malicious imported
  metadata, AnkiConnect access, and account deletion.

Exit gate: no unresolved high-severity findings and all privacy-boundary tests
pass against the built release image.

## Milestone 7 — Operations-ready local staging

Deliverables:

- One command starts the app, reverse proxy, persistent database, local mail
  catcher, and monitoring dependencies with production-equivalent settings.
- Structured request/audit logs with correlation IDs and no bearer credentials.
- Metrics for health, latency, error rates, queue ingestion, database size,
  backups, and mail failures.
- Automated backup schedule plus restore drill into a clean environment.
- Forward and rollback migration rehearsal using a realistic-sized synthetic
  database.
- Resource/load tests for large accounts and concurrent device batches.
- Operator runbooks for upgrade, rollback, restore, credential rotation,
  incident response, and account-data requests.

Exit gate: a clean machine can create the staging environment, run the complete
acceptance suite, restore a backup, and shut down without orphaned state.

## Milestone 8 — Release candidate

Deliverables:

- Cross-browser pass on current Firefox and Chromium; document any WebKit
  limitation before release.
- Final copy, onboarding, privacy explanation, troubleshooting, and embedded
  help for mod pairing and Anki.
- Versioned API and database compatibility policy for future game adapters.
- Tagged release artifacts, checksums, SBOM, migration notes, and known issues.
- A release checklist signed off for product, accessibility, security, privacy,
  recovery, backup/restore, and adapter compatibility.

Exit gate: the release image and documentation are ready to hand to an operator;
the only absent actions are choosing infrastructure, configuring a real domain
and email provider, and opening the service to users.

## Long-term work after the local release candidate

- Public hosting, domain/DNS, real transactional email, external uptime/error
  monitoring, capacity planning, and a formal incident channel.
- Additional recompilation-game adapters using the shared client/API contract.
- User-controlled cross-game vocabulary identity and game-specific sense
  overrides.
- Optional audio, pitch-accent, handwriting, and richer licensed dictionary
  packs.
- Offline-capable web reviews and native/mobile clients.
- Personalized FSRS parameter optimization once enough review history exists.
- Importers for other SRS tools and open interchange formats.
- Carefully scoped social or sharing features only after a separate privacy and
  moderation design; no leaderboard is assumed by default.

## Definition of done

The goal is complete only when Milestones 1–8 meet their exit gates, the full
suite passes against a freshly built production image, the worktree contains no
private/generated player data, and all remaining tasks are exclusively public
hosting or explicitly listed post-release enhancements.
