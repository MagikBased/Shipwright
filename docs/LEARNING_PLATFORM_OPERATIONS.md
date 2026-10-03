# Learning Platform Operations Runbook

This runbook covers the single-host, single-worker SQLite release topology. It
is suitable for local production-shaped staging and for an eventual small
deployment. Public hosting still requires named operators, encrypted off-host
storage, external monitoring, a mail provider, and an incident contact.

## Routine checks

From the repository root:

```bash
services/learning_platform/scripts/staging.sh status
services/learning_platform/scripts/staging.sh smoke
services/learning_platform/scripts/staging.sh restore-drill
```

The Grafana **JP Assist Operations** dashboard should show readiness `1`, no
sustained 5xx responses, recent backups, and no unexplained mail failures.
Prometheus and Grafana bind to loopback; `/internal/metrics` must return 404
through Caddy. Use an `X-Request-ID` from a response to correlate request and
account-audit JSON logs without inspecting request content.

Run the full disposable operational qualification before a release:

```bash
services/learning_platform/scripts/qualify_operations.sh
```

It creates no personal data. The reports under the ignored
`services/learning_platform/var/qualification/` directory cover eight
concurrent devices, 8,000 unique words, retry deduplication, account reads,
memory/database limits, and a 5,000-word schema forward/rollback/reapply cycle.

## Upgrade

1. Run the smoke test and restore drill on the current release.
2. Trigger a backup and copy it to encrypted storage outside this Docker or
   Podman installation.
3. Record the application revision, schema version, backup checksum, and image
   digest in the change record.
4. Check out the intended revision and build the pinned release image.
5. Run its unit, browser, operational qualification, image verification, and
   vulnerability gates.
6. Start the stack. Startup performs supported forward migrations.
7. Run the production smoke test and inspect the dashboard and correlated logs.
8. Retain the pre-upgrade image and backup through the rollback window.

## Rollback

Application rollback and database rollback are one operation when an upgrade
changed the schema. Never run an older binary against a newer schema.

1. Stop both writers: `docker compose stop app backup`.
2. Using the current image, restore the recorded pre-upgrade backup without
   migrating it:

   ```bash
   docker compose run --rm app python -m learning_platform.manage \
     restore /backups/PRE_UPGRADE.sqlite3 --yes --preserve-schema
   ```

3. Check out or select the matching previous application image.
4. Start the previous stack and run its deployment smoke test.
5. Preserve the automatically created `platform.sqlite3.pre-restore-*` safety
   copy until the incident is resolved.

`--preserve-schema` still creates a transactionally consistent copy and runs
SQLite integrity/schema validation. It intentionally does not run current
migrations. Use it only with a backup and application revision known to match.

## Restore after data loss

Stop `app` and `backup`, identify the newest off-host artifact whose checksum
matches the backup catalog, and use the normal restore command from the target
release. Normal restore validates, applies supported migrations, and makes a
pre-restore safety copy when an active database exists.

```bash
docker compose run --rm app python -m learning_platform.manage \
  restore /backups/RECOVERY.sqlite3 --yes
docker compose up -d
python services/learning_platform/scripts/deployment_smoke.py \
  --production https://learn.example.com
```

Confirm account counts, newest event time, schema version, mail capture, and a
sample account export. Record recovery-point and recovery-time measurements.

## Backup handling

The local sidecar writes immediately on startup and every 24 hours by default,
retaining 14 files. Local volumes are recovery convenience, not disaster
recovery. A public deployment must copy each successful artifact to encrypted,
versioned off-host storage; restrict decryption access to operators; record a
SHA-256 checksum; define deletion matching the published retention policy; and
perform a restore drill at least monthly and before schema upgrades.

Backups contain account email addresses, annotations, and learning history,
although they contain no raw passwords or bearer credentials. Handle them as
private account data.

## Credential rotation

- **Grafana:** set a new long random `JP_ASSIST_GRAFANA_ADMIN_PASSWORD`, restart
  Grafana, and verify anonymous access remains Viewer-only and loopback-bound.
- **SMTP:** replace username/password in the untracked deployment environment,
  restart `app`, send a test reminder, then revoke the old provider credential.
- **Website access:** users change/reset their password, which revokes other
  website sessions. They can revoke individual sessions from Account settings.
- **Game devices:** users revoke a device from Account settings and pair it
  again. The service stores only credential hashes, so a lost raw token cannot
  be recovered.

There is currently no static application signing key to rotate. Never place
deployment credentials in `.env.staging`, Git, logs, support tickets, or
account exports.

## Incident response

1. Contain exposure: remove public routing or firewall the host while retaining
   loopback/operator access. Do not destroy containers or volumes.
2. Record UTC start time, revision/image digest, health, dashboard snapshots,
   affected request IDs, and recent content-neutral logs.
3. Create an integrity-checked backup and a read-only copy of relevant logs.
4. Determine affected accounts and data classes using audit types and stable
   IDs; do not add dialogue or secrets to the incident record.
5. Revoke affected sessions/devices and rotate Grafana/SMTP credentials as
   applicable.
6. Patch, run all release gates, restore if required, and reopen locally before
   considering public traffic.
7. Document impact, timeline, recovery evidence, notification decision, and
   follow-up controls. Escalation and legal-notification owners must be chosen
   before public hosting.

## Account-data requests

The authenticated website provides a complete account export, per-game clear,
session/device revocation, and account deletion. Prefer those self-service
paths so an operator never needs the password or an impersonation session.

For a support-assisted request, verify identity using the deployment's written
policy, record only the request type and account/audit ID, and direct the user
through the same authenticated action. Do not send database rows or backups by
email. Account deletion uses foreign-key cascades for active storage; expired
backups age out under the documented backup retention policy and must not be
silently restored into production afterward.
