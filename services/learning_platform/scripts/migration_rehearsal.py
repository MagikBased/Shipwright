#!/usr/bin/env python3
"""Rehearse schema-8 forward migration, backup rollback, and re-application."""

from __future__ import annotations

import argparse
import json
import sqlite3
import tempfile
import time
from pathlib import Path
from typing import Any

from learning_platform.database import Database, LATEST_SCHEMA_VERSION
from learning_platform.manage import backup_database, restore_database
from learning_platform.service import LearningPlatform


def pair_device(platform: LearningPlatform, session_token: str) -> dict[str, Any]:
    pairing = platform.start_pairing("Migration rehearsal device", "migration-adapter", "migration-game")
    platform.approve_pairing(session_token, pairing["userCode"])
    return platform.claim_pairing(pairing["deviceCode"])


def seed_realistic_database(path: Path, word_count: int) -> dict[str, int]:
    platform = LearningPlatform(path)
    account = platform.register_user(
        "migration@example.test", "synthetic migration password", "Migration Rehearsal",
    )
    device = pair_device(platform, account["token"])
    for start in range(0, word_count, 250):
        events = [
            {
                "eventId": f"migration-{number:07d}",
                "type": "word_encountered",
                "occurredAt": "2026-10-03T12:00:00.000Z",
                "gameId": "migration-game",
                "adapterId": "migration-adapter",
                "contentVersion": "synthetic-migration-v1",
                "wordId": f"migration-word-{number:07d}|reading-{number:07d}",
                "senseId": "synthetic-sense",
                "count": (number % 5) + 1,
            }
            for number in range(start, min(start + 250, word_count))
        ]
        platform.ingest_events(device["deviceToken"], events)
    return logical_counts(path)


def logical_counts(path: Path) -> dict[str, int]:
    uri = path.resolve().as_uri() + "?mode=ro"
    with sqlite3.connect(uri, uri=True) as connection:
        return {
            table: int(connection.execute(f"SELECT COUNT(*) FROM {table}").fetchone()[0])
            for table in ("users", "sessions", "devices", "events", "word_progress", "game_word_progress")
        }


def convert_to_schema_eight(path: Path) -> None:
    with sqlite3.connect(path) as connection:
        connection.execute("PRAGMA foreign_keys = OFF")
        connection.execute("ALTER TABLE sessions RENAME TO sessions_version_nine")
        connection.execute(
            """
            CREATE TABLE sessions (
                token_hash TEXT PRIMARY KEY,
                user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
                created_at TEXT NOT NULL,
                expires_at TEXT NOT NULL
            )
            """
        )
        connection.execute(
            """INSERT INTO sessions(token_hash, user_id, created_at, expires_at)
               SELECT token_hash, user_id, created_at, expires_at FROM sessions_version_nine"""
        )
        connection.execute("DROP TABLE sessions_version_nine")
        connection.execute("UPDATE metadata SET value = '8' WHERE key = 'schema_version'")


def raw_schema(path: Path) -> tuple[int, set[str], str]:
    uri = path.resolve().as_uri() + "?mode=ro"
    with sqlite3.connect(uri, uri=True) as connection:
        version = int(connection.execute(
            "SELECT value FROM metadata WHERE key = 'schema_version'"
        ).fetchone()[0])
        columns = {row[1] for row in connection.execute("PRAGMA table_info(sessions)")}
        integrity = str(connection.execute("PRAGMA quick_check").fetchone()[0])
    return version, columns, integrity


def timed_forward_migration(path: Path) -> float:
    started = time.perf_counter()
    database = Database(path)
    duration = time.perf_counter() - started
    if database.schema_version() != LATEST_SCHEMA_VERSION or database.readiness() != (True, "ready"):
        raise RuntimeError("Forward-migrated database did not become ready")
    return duration


def run(word_count: int) -> dict[str, Any]:
    with tempfile.TemporaryDirectory(prefix="jp-assist-migration-rehearsal-") as temporary:
        root = Path(temporary)
        active = root / "platform.sqlite3"
        rollback_backup = root / "before-migration.sqlite3"

        baseline_counts = seed_realistic_database(active, word_count)
        convert_to_schema_eight(active)
        version, columns, integrity = raw_schema(active)
        if version != 8 or integrity != "ok" or {"id", "last_seen_at", "user_agent"} & columns:
            raise RuntimeError("Could not construct the schema-8 rehearsal fixture")
        backup_database(active, rollback_backup)

        forward_seconds = timed_forward_migration(active)
        migrated_counts = logical_counts(active)
        migrated_version, migrated_columns, migrated_integrity = raw_schema(active)

        rollback_started = time.perf_counter()
        restore_database(active, rollback_backup, confirmed=True, migrate=False)
        rollback_seconds = time.perf_counter() - rollback_started
        rolled_version, rolled_columns, rolled_integrity = raw_schema(active)
        rolled_counts = logical_counts(active)

        reapply_seconds = timed_forward_migration(active)
        final_counts = logical_counts(active)
        final_version, final_columns, final_integrity = raw_schema(active)

        required_columns = {"id", "last_seen_at", "user_agent"}
        violations = []
        if migrated_version != LATEST_SCHEMA_VERSION or migrated_integrity != "ok":
            violations.append("forward migration did not reach a ready current schema")
        if not required_columns.issubset(migrated_columns):
            violations.append("forward migration did not add session metadata columns")
        if rolled_version != 8 or rolled_integrity != "ok" or required_columns & rolled_columns:
            violations.append("rollback did not restore the preserved schema-8 artifact")
        if final_version != LATEST_SCHEMA_VERSION or final_integrity != "ok":
            violations.append("re-applied migration did not reach the current schema")
        if not required_columns.issubset(final_columns):
            violations.append("re-applied migration did not restore session metadata columns")
        if not (baseline_counts == migrated_counts == rolled_counts == final_counts):
            violations.append("logical row counts changed during rehearsal")

        safety_backups = list(root.glob("platform.sqlite3.pre-restore-*"))
        if len(safety_backups) != 1:
            violations.append("rollback did not retain exactly one pre-restore safety backup")

        return {
            "passed": not violations,
            "violations": violations,
            "configuration": {"syntheticWords": word_count, "fromSchema": 8, "toSchema": LATEST_SCHEMA_VERSION},
            "results": {
                "logicalCounts": final_counts,
                "fixtureBytes": rollback_backup.stat().st_size,
                "forwardMigrationMilliseconds": round(forward_seconds * 1000, 3),
                "rollbackMilliseconds": round(rollback_seconds * 1000, 3),
                "reapplyMilliseconds": round(reapply_seconds * 1000, 3),
                "preRestoreSafetyBackups": len(safety_backups),
            },
        }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--words", type=int, default=5000)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.words < 1:
        parser.error("--words must be positive")
    report = run(args.words)
    rendered = json.dumps(report, indent=2, sort_keys=True)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)
    if not report["passed"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
