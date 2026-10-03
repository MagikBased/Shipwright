# Learning Platform Deployment

This deployment keeps the game-facing API and account website behind automatic
HTTPS while storing the MVP database in a persistent Docker volume. It is
provider-neutral and works on a small Linux host with a public IP.

## Prerequisites

- Docker Engine with the Compose plugin.
- A DNS `A`/`AAAA` record pointing a domain at the host.
- Public TCP ports 80 and 443, plus UDP 443, available to Caddy.
- The repository checked out on the host.

The SQLite MVP intentionally runs one application worker. Scale-up and multiple
application replicas require moving the storage layer to a server database.

## Local production-shaped staging

From the repository root, one command builds and starts a loopback-only stack
with production settings:

```bash
services/learning_platform/scripts/staging.sh up
```

It validates the Compose configuration, waits for readiness, and runs the
production deployment smoke test. The site is available at
`https://localhost:8443`; Mailpit at `http://127.0.0.1:8025`; Prometheus at
`http://127.0.0.1:9090`; and Grafana at `http://127.0.0.1:3000`. Caddy uses a
local certificate, so a browser trust exception may be required. The committed
`.env.staging` values are intentionally local-only and must not be reused for a
public deployment.

```bash
services/learning_platform/scripts/staging.sh status
services/learning_platform/scripts/staging.sh logs app
services/learning_platform/scripts/staging.sh smoke
services/learning_platform/scripts/staging.sh backup
services/learning_platform/scripts/staging.sh restore-drill
services/learning_platform/scripts/staging.sh down
```

`down` stops containers without deleting persistent volumes. The launcher
supports current Docker Compose as well as the legacy rootless Podman stack
shipped by Linux Mint 21; its Podman 3 workaround touches only the Compose
network named for this project.

## Configure and start

From `services/learning_platform`:

```bash
cp .env.example .env
```

Set `JP_ASSIST_DOMAIN` to the public hostname. Put that hostname first in
`JP_ASSIST_ALLOWED_HOSTS` and retain the internal/loopback entries used by the
container health check. Keep `JP_ASSIST_ENV=production` and
`JP_ASSIST_COOKIE_SECURE=1`. Then validate and start the stack:

```bash
docker compose config
docker compose up -d --build
docker compose ps
python scripts/deployment_smoke.py https://learn.example.com
```

Caddy obtains and renews the TLS certificate automatically. Only Caddy is
published on the host; the application is reachable through the private
Compose network. This is why the supplied configuration can safely trust its
forwarded client-address header for rate limiting.

Point the mod's learning-service setting at the same HTTPS origin, without an
API path, for example `https://learn.example.com`.

## Configuration

| Variable | Purpose | Production default/example |
|---|---|---|
| `JP_ASSIST_ENV` | Enables production validation | `production` |
| `JP_ASSIST_PLATFORM_DB` | SQLite path inside the app container | `/data/platform.sqlite3` |
| `JP_ASSIST_COOKIE_SECURE` | Sends website sessions only over HTTPS | `1` |
| `JP_ASSIST_ALLOWED_HOSTS` | Rejects unexpected Host headers | public hostname plus internal health hosts |
| `JP_ASSIST_TRUST_PROXY_HEADERS` | Uses Caddy's forwarded client address | `1` behind bundled Caddy |
| `JP_ASSIST_STRUCTURED_LOGS` | Emits content-neutral JSON request/audit logs | `1` |
| `JP_ASSIST_BACKUP_DIR` | Directory inspected for backup metrics | `/backups` |
| `JP_ASSIST_BACKUP_INTERVAL_SECONDS` | Delay between automatic backup attempts | `86400` |
| `JP_ASSIST_BACKUP_RETAIN_COUNT` | Newest automatic backups kept locally | `14` |
| `JP_ASSIST_BIND_ADDRESS` | Address used for public HTTP/HTTPS bindings | `0.0.0.0` public, `127.0.0.1` local staging |
| `JP_ASSIST_RATE_LIMIT_WINDOW_SECONDS` | Sliding rate-limit window | `60` |
| `JP_ASSIST_AUTH_RATE_LIMIT` | Registrations/login attempts per IP/window | `20` |
| `JP_ASSIST_PAIRING_RATE_LIMIT` | Pairing requests/polls per IP/window | `180` |
| `JP_ASSIST_EVENT_RATE_LIMIT` | Upload batches per device/window | `180` |

A rate-limit value of zero disables that category. The limiter is in-memory,
per process, and resets on restart. It is appropriate for the single-worker
SQLite MVP; a multi-replica deployment needs a shared limiter.

Production startup fails closed if secure cookies or explicit allowed hosts
are missing. Device/session bearer credentials are generated randomly and
stored only as SHA-256 hashes, so there is no static application signing secret
to provision in this version.

## Logs and monitoring

Every response includes an `X-Request-ID`. A valid caller-provided ID is
preserved; otherwise the app generates a UUID. Structured request logs contain
only timestamp, request ID, method, normalized route template, status, and
duration. Audit logs contain the request ID, audit type, and metadata key names,
not bodies, query strings, addresses, cookies, bearer tokens, email addresses,
or dialogue.

Prometheus scrapes `/internal/metrics` across the private Compose network.
Caddy deliberately returns 404 for `/internal/*`, so these metrics cannot be
read through the public site. The provisioned **JP Assist Operations** Grafana
dashboard displays request rate, 5xx ratio, p95 latency, readiness, database
size, retained events, backup count, and retained mail-delivery failures. The
monitoring and mail UIs bind to host loopback even in the public-host example.

## Health and migrations

- `/healthz` is a process liveness check.
- `/readyz` checks database reachability, schema compatibility, and SQLite
  integrity. Caddy waits for this check before proxying to a newly started app.

Schema migrations run during application startup and refuse a database created
by a newer service version. They can also be checked explicitly:

```bash
docker compose exec app python -m learning_platform.manage status
docker compose exec app python -m learning_platform.manage migrate
```

## Backup and restore

The Compose `backup` sidecar makes an online backup immediately after startup
and then once per configured interval. It mounts the live data volume read-only,
checks SQLite integrity on every copy, uses single-file journal mode, and keeps
only the configured number of `platform-*.sqlite3` files. Trigger and validate
one manually with:

```bash
services/learning_platform/scripts/staging.sh backup
services/learning_platform/scripts/staging.sh restore-drill
```

The drill restores the newest artifact into an isolated temporary database,
runs supported migrations, checks schema compatibility and SQLite integrity,
then removes the temporary copy. It never replaces the active database.

Create an online, transactionally consistent SQLite backup:

```bash
docker compose exec -T app python -m learning_platform.manage \
  backup /backups/platform-$(date -u +%Y%m%dT%H%M%SZ).sqlite3
```

Copy backups off the host or into provider-managed object storage. A backup
that exists only in the same Docker installation is not disaster recovery.

```bash
docker compose cp app:/backups/platform-20261002T120000Z.sqlite3 .
```

Restoration replaces the active database, so stop the app first. The restore
tool validates and migrates the incoming database and creates an additional
pre-restore safety backup:

```bash
docker compose stop app
docker compose run --rm app python -m learning_platform.manage \
  restore /backups/platform-20261002T120000Z.sqlite3 --yes
docker compose up -d app
python scripts/deployment_smoke.py https://learn.example.com
```

## Upgrade procedure

1. Create and copy off a database backup.
2. Pull or check out the intended application revision.
3. Run `docker compose build --pull app`.
4. Run `docker compose up -d`.
5. Confirm `docker compose ps` and run the deployment smoke test.
6. Inspect `docker compose logs --since=10m app caddy` for errors.

## Remaining public-beta requirements

The deployment scaffold supplies HTTPS, host validation, rate limits, health
checks, migrations, scheduled local backups with restore drills, structured
logs, and local monitoring. Before advertising an open public service, copy
backups to encrypted off-host storage, add external uptime/error monitoring, a
real email provider, an abuse/contact process, and a published privacy policy.
These require deployment-specific infrastructure and organizational choices
and are intentionally not guessed by the repository scaffold.
