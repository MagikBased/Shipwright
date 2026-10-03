from __future__ import annotations

import argparse
import json
import os
import sqlite3
import tempfile
from datetime import datetime, timezone
from pathlib import Path

from .database import Database, LATEST_SCHEMA_VERSION
from .config import Settings
from .mailer import mailer_from_settings
from .service import LearningPlatform


def default_database_path() -> Path:
    root = Path(__file__).resolve().parent.parent
    return Path(os.environ.get("JP_ASSIST_PLATFORM_DB", root / "var" / "platform.sqlite3"))


def backup_database(source_path: Path, output_path: Path, overwrite: bool = False) -> None:
    if not source_path.is_file():
        raise SystemExit(f"Database not found: {source_path}")
    if output_path.exists() and not overwrite:
        raise SystemExit(f"Backup already exists: {output_path}")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    source_uri = source_path.resolve().as_uri() + "?mode=ro"
    with sqlite3.connect(source_uri, uri=True) as source, sqlite3.connect(output_path) as destination:
        source.backup(destination)
        destination.commit()
        journal_mode = destination.execute("PRAGMA journal_mode=DELETE").fetchone()[0]
        if str(journal_mode).lower() != "delete":
            raise SystemExit(f"Backup could not enter single-file journal mode: {journal_mode}")
        integrity = destination.execute("PRAGMA quick_check").fetchone()[0]
        if integrity != "ok":
            raise SystemExit(f"Backup integrity check failed: {integrity}")
    try:
        output_path.chmod(0o600)
    except OSError:
        pass


def scheduled_backup(
    source_path: Path,
    output_directory: Path,
    retain: int,
    now: datetime | None = None,
) -> tuple[Path, list[Path]]:
    if retain < 1:
        raise SystemExit("Backup retention must be at least one file")
    timestamp = (now or datetime.now(timezone.utc)).astimezone(timezone.utc)
    output_path = output_directory / f"platform-{timestamp.strftime('%Y%m%dT%H%M%S%fZ')}.sqlite3"
    backup_database(source_path, output_path)
    backups = sorted(output_directory.glob("platform-*.sqlite3"), reverse=True)
    removed = backups[retain:]
    for path in removed:
        path.unlink()
    return output_path, removed


def restore_drill(backup_path: Path) -> tuple[int, str]:
    if not backup_path.is_file():
        raise SystemExit(f"Backup not found: {backup_path}")
    with tempfile.TemporaryDirectory(prefix="jp-assist-restore-drill-") as temporary:
        restored_path = Path(temporary) / "restored.sqlite3"
        restore_database(restored_path, backup_path, confirmed=True)
        database = Database(restored_path)
        ready, detail = database.readiness()
        if not ready:
            raise SystemExit(f"Restore drill failed: {detail}")
        return database.schema_version(), detail


def restore_database(
    database_path: Path, backup_path: Path, confirmed: bool, migrate: bool = True,
) -> None:
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
        if migrate:
            restored = Database(temporary_path)
            ready, detail = restored.readiness()
            if not ready:
                raise SystemExit(f"Restored database is not ready: {detail}")
        else:
            uri = temporary_path.resolve().as_uri() + "?mode=ro"
            with sqlite3.connect(uri, uri=True) as restored:
                integrity = restored.execute("PRAGMA quick_check").fetchone()[0]
                version = restored.execute(
                    "SELECT value FROM metadata WHERE key = 'schema_version'"
                ).fetchone()
            if integrity != "ok" or version is None or not str(version[0]).isdigit():
                raise SystemExit("Preserved-schema restore failed integrity or schema validation")
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


def dictionary_entries_from_runtime(path: Path, source: str, attribution: str) -> list[dict]:
    try:
        runtime = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise SystemExit(f"Could not read runtime corpus: {error}") from error
    messages = runtime.get("messages") if isinstance(runtime, dict) else None
    if not isinstance(messages, dict):
        raise SystemExit("Runtime corpus must contain a messages object")
    entries: dict[tuple[str, str], dict] = {}
    for message in messages.values():
        if not isinstance(message, dict):
            continue
        for page in message.get("pages", []):
            for token in page.get("tokens", []):
                word_id = token.get("id")
                written = token.get("lemma")
                if not word_id or not written:
                    continue
                sense_id = token.get("senseId", "")
                entries[(word_id, sense_id)] = {
                    "wordId": word_id,
                    "senseId": sense_id,
                    "written": written,
                    "reading": token.get("dictionaryReading", token.get("reading", "")),
                    "partOfSpeech": token.get("partOfSpeech", ""),
                    "meaning": token.get("meaning", ""),
                    "source": source,
                    "attribution": attribution,
                }
    return list(entries.values())


def main() -> None:
    parser = argparse.ArgumentParser(description="Manage the JP Assist learning-platform database")
    parser.add_argument("--database", type=Path, default=None, help="Database path (or JP_ASSIST_PLATFORM_DB)")
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("status", help="Check schema version and SQLite integrity")
    subparsers.add_parser("migrate", help="Apply all supported schema migrations")
    subparsers.add_parser("rebuild-reviews", help="Rebuild derived FSRS card state from the review log")
    subparsers.add_parser("send-reminders", help="Send due-review reminders through the configured local mail transport")
    subparsers.add_parser("cleanup", help="Remove expired credentials and bounded operational history")
    backup_parser = subparsers.add_parser("backup", help="Create a consistent online SQLite backup")
    backup_parser.add_argument("output", type=Path)
    backup_parser.add_argument("--force", action="store_true")
    scheduled_backup_parser = subparsers.add_parser(
        "backup-scheduled", help="Create a timestamped backup and enforce count retention"
    )
    scheduled_backup_parser.add_argument(
        "--directory", type=Path,
        default=Path(os.environ.get("JP_ASSIST_BACKUP_DIR", "/backups")),
    )
    scheduled_backup_parser.add_argument(
        "--retain", type=int,
        default=int(os.environ.get("JP_ASSIST_BACKUP_RETAIN_COUNT", "14")),
    )
    restore_drill_parser = subparsers.add_parser(
        "restore-drill", help="Restore the newest backup into an isolated temporary database"
    )
    restore_drill_parser.add_argument("backup", type=Path, nargs="?")
    restore_drill_parser.add_argument(
        "--directory", type=Path,
        default=Path(os.environ.get("JP_ASSIST_BACKUP_DIR", "/backups")),
    )
    restore_parser = subparsers.add_parser("restore", help="Restore and migrate a backup while the service is stopped")
    restore_parser.add_argument("backup", type=Path)
    restore_parser.add_argument("--yes", action="store_true")
    restore_parser.add_argument(
        "--preserve-schema", action="store_true",
        help="restore without migrating for rollback with the matching older application",
    )
    dictionary_parser = subparsers.add_parser(
        "import-dictionary", help="Import licensed dictionary metadata from a JSON array"
    )
    dictionary_parser.add_argument("input", type=Path)
    runtime_dictionary_parser = subparsers.add_parser(
        "import-runtime-dictionary", help="Import only dictionary fields from a local runtime corpus"
    )
    runtime_dictionary_parser.add_argument("input", type=Path)
    runtime_dictionary_parser.add_argument("--source", required=True)
    runtime_dictionary_parser.add_argument("--attribution", required=True)
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
    elif args.command == "rebuild-reviews":
        count = LearningPlatform(database_path, allow_scheduler_upgrade=True).rebuild_review_states()
        print(f"Rebuilt {count} FSRS card states in {database_path}")
    elif args.command == "send-reminders":
        settings = Settings.from_environment(database_path)
        result = LearningPlatform(
            database_path, mailer=mailer_from_settings(settings),
            public_base_url=settings.public_base_url,
        ).send_due_reminders()
        print(
            f"Reminder delivery: {result['sent']} sent, {result['failed']} failed, "
            f"{result['skipped']} skipped"
        )
    elif args.command == "cleanup":
        result = LearningPlatform(database_path).cleanup_operational_data()
        print("Operational cleanup: " + ", ".join(
            f"{count} {category}" for category, count in result.items()
        ))
    elif args.command == "backup":
        backup_database(database_path, args.output, args.force)
        print(f"Wrote backup: {args.output}")
    elif args.command == "backup-scheduled":
        output, removed = scheduled_backup(database_path, args.directory, args.retain)
        print(f"Wrote scheduled backup: {output}")
        print(f"Pruned {len(removed)} expired backup(s); retaining at most {args.retain}")
    elif args.command == "restore-drill":
        backup = args.backup
        if backup is None:
            backups = sorted(args.directory.glob("platform-*.sqlite3"), reverse=True)
            if not backups:
                raise SystemExit(f"No scheduled backups found in {args.directory}")
            backup = backups[0]
        schema_version, detail = restore_drill(backup)
        print(f"Restore drill passed: {backup} (schema {schema_version}, {detail})")
    elif args.command == "restore":
        restore_database(database_path, args.backup, args.yes, migrate=not args.preserve_schema)
        print(f"Restored database: {database_path}")
    elif args.command == "import-dictionary":
        try:
            entries = json.loads(args.input.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            raise SystemExit(f"Could not read dictionary JSON: {error}") from error
        if not isinstance(entries, list):
            raise SystemExit("Dictionary JSON must contain an array of entries")
        count = LearningPlatform(database_path).import_dictionary_entries(entries)
        print(f"Imported {count} dictionary entries into {database_path}")
    elif args.command == "import-runtime-dictionary":
        entries = dictionary_entries_from_runtime(args.input, args.source, args.attribution)
        count = LearningPlatform(database_path).import_dictionary_entries(entries)
        print(f"Imported {count} content-neutral dictionary entries into {database_path}")


if __name__ == "__main__":
    main()
