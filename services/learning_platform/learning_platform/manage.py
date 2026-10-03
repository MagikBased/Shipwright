from __future__ import annotations

import argparse
import os
import sqlite3
import tempfile
from datetime import datetime, timezone
from pathlib import Path

from .database import Database, LATEST_SCHEMA_VERSION


def default_database_path() -> Path:
    root = Path(__file__).resolve().parent.parent
    return Path(os.environ.get("JP_ASSIST_PLATFORM_DB", root / "var" / "platform.sqlite3"))


def backup_database(source_path: Path, output_path: Path, overwrite: bool = False) -> None:
    if not source_path.is_file():
        raise SystemExit(f"Database not found: {source_path}")
    if output_path.exists() and not overwrite:
        raise SystemExit(f"Backup already exists: {output_path}")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with sqlite3.connect(source_path) as source, sqlite3.connect(output_path) as destination:
        source.backup(destination)
        integrity = destination.execute("PRAGMA quick_check").fetchone()[0]
        if integrity != "ok":
            raise SystemExit(f"Backup integrity check failed: {integrity}")
    try:
        output_path.chmod(0o600)
    except OSError:
        pass


def restore_database(database_path: Path, backup_path: Path, confirmed: bool) -> None:
    if not confirmed:
        raise SystemExit("Restore replaces the active database; rerun with --yes after stopping the service")
    if not backup_path.is_file():
        raise SystemExit(f"Backup not found: {backup_path}")
    database_path.parent.mkdir(parents=True, exist_ok=True)

    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    if database_path.exists():
        safety_backup = database_path.with_name(f"{database_path.name}.pre-restore-{timestamp}")
        backup_database(database_path, safety_backup)
        print(f"Wrote pre-restore backup: {safety_backup}")

    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{database_path.name}.restore-",
        dir=database_path.parent,
    )
    os.close(descriptor)
    temporary_path = Path(temporary_name)
    try:
        temporary_path.unlink()
        backup_database(backup_path, temporary_path)
        restored = Database(temporary_path)
        ready, detail = restored.readiness()
        if not ready:
            raise SystemExit(f"Restored database is not ready: {detail}")
        for suffix in ("-wal", "-shm"):
            stale_path = Path(str(database_path) + suffix)
            if stale_path.exists():
                stale_path.unlink()
        os.replace(temporary_path, database_path)
        try:
            database_path.chmod(0o600)
        except OSError:
            pass
    finally:
        if temporary_path.exists():
            temporary_path.unlink()


def main() -> None:
    parser = argparse.ArgumentParser(description="Manage the JP Assist learning-platform database")
    parser.add_argument("--database", type=Path, default=None, help="Database path (or JP_ASSIST_PLATFORM_DB)")
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("status", help="Check schema version and SQLite integrity")
    subparsers.add_parser("migrate", help="Apply all supported schema migrations")
    backup_parser = subparsers.add_parser("backup", help="Create a consistent online SQLite backup")
    backup_parser.add_argument("output", type=Path)
    backup_parser.add_argument("--force", action="store_true")
    restore_parser = subparsers.add_parser("restore", help="Restore and migrate a backup while the service is stopped")
    restore_parser.add_argument("backup", type=Path)
    restore_parser.add_argument("--yes", action="store_true")
    args = parser.parse_args()

    database_path = args.database or default_database_path()
    if args.command in {"status", "migrate"}:
        database = Database(database_path)
        ready, detail = database.readiness()
        print(
            f"Database: {database_path}\n"
            f"Schema: {database.schema_version()}/{LATEST_SCHEMA_VERSION}\n"
            f"Status: {detail}"
        )
        if not ready:
            raise SystemExit(1)
    elif args.command == "backup":
        Database(database_path)
        backup_database(database_path, args.output, args.force)
        print(f"Wrote backup: {args.output}")
    elif args.command == "restore":
        restore_database(database_path, args.backup, args.yes)
        print(f"Restored database: {database_path}")


if __name__ == "__main__":
    main()
