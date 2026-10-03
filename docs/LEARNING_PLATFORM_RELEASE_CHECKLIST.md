# Learning Platform Release Checklist

Candidate: `0.3.0-rc.1`

API: `/v1`

Database: schema `9`

## Automated evidence

- [x] Service, API, privacy, migration, FSRS, and backup tests pass.
- [x] Chromium functional, accessibility, responsive, and visual acceptance passes.
- [x] Firefox functional, accessibility, and responsive acceptance passes.
- [x] Cross-component mod event, export, Anki package, and stable-ID acceptance passes.
- [x] Load thresholds and concurrent retry deduplication pass.
- [x] Forward migration, preserved-schema rollback, and reapply preserve logical data.
- [x] Production image and production-config deployment smoke pass.
- [x] Disposable staging leaves no containers, networks, or volumes.
- [x] Dependency, secret, SBOM, privacy-boundary, and high-severity image gates pass.

## Product and documentation

- [x] Registration, verification, recovery, pairing, vocabulary, review, export,
  Anki, privacy, and deletion journeys have acceptance coverage.
- [x] Empty/loading/error/offline states and destructive confirmations are present.
- [x] Embedded onboarding, pairing help, Anki troubleshooting, and privacy copy are present.
- [x] API/database/adapter compatibility and rollback policies are versioned.
- [x] Deployment, operations, security/privacy, FSRS, Anki, and identity behavior are documented.
- [x] Known limitations and deferred public-hosting work are explicit.

## Artifact handoff

- [x] Run `services/learning_platform/scripts/build_release_candidate.sh 0.3.0-rc.1`
  from a clean commit.
- [x] Verify `SHA256SUMS` for the source archive, image archive, SBOM,
  vulnerability report, manifest, and release notes.
- [x] Apply local Git tag `jp-assist-site-v0.3.0-rc.1` to that exact commit.
- [x] Record the exact commit, image ID, UTC generation time, and artifact
  checksums in the generated `manifest.json` and `SHA256SUMS` files.

The handoff bundle is generated locally under
`services/learning_platform/var/releases/0.3.0-rc.1/`. These build products are
intentionally ignored; the manifest and checksum file inside the bundle are
the authoritative release-instance record.

## Technical sign-off

| Area | Evidence | Status |
| --- | --- | --- |
| Product | Browser and cross-component journeys | Ready |
| Accessibility | axe, keyboard, responsive Chromium/Firefox suite | Ready |
| Security/privacy | threat model, privacy tests, SBOM, audits, VEX | Ready |
| Recovery | scheduled backup, isolated drill, preserved-schema rollback | Ready |
| Operations | clean staging rehearsal, monitoring, runbooks | Ready |
| Compatibility | `/v1`, schema 9, adapter policy | Ready |

All local release-candidate gates are signed off. Public hosting remains a
separate roadmap phase.
