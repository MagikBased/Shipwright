import os
import sqlite3
import tempfile
import unittest
from datetime import datetime, timezone
from pathlib import Path
from unittest.mock import patch

from learning_platform.config import Settings
from learning_platform.database import Database, LATEST_SCHEMA_VERSION
from learning_platform.manage import (
    backup_database,
    dictionary_entries_from_runtime,
    restore_database,
    restore_drill,
    scheduled_backup,
)
from learning_platform.rate_limit import SlidingWindowRateLimiter


class DeploymentTest(unittest.TestCase):
    def test_container_defaults_to_writable_data_volume(self):
        dockerfile = (Path(__file__).parent.parent / "Dockerfile").read_text(encoding="utf-8")
        self.assertIn("JP_ASSIST_PLATFORM_DB=/data/platform.sqlite3", dockerfile)

    def test_large_account_join_indexes_are_installed(self):
        with tempfile.TemporaryDirectory(prefix="jp-assist-indexes-") as temporary:
            database = Database(Path(temporary) / "indexes.sqlite3")
            with database.connect() as connection:
                event_indexes = {
                    row["name"] for row in connection.execute("PRAGMA index_list(events)")
                }
                game_indexes = {
                    row["name"] for row in connection.execute("PRAGMA index_list(game_word_progress)")
                }
            self.assertIn("events_user_event_type_idx", event_indexes)
            self.assertIn("game_word_progress_user_word_idx", game_indexes)

    def test_rate_limit_allows_requests_after_window_expires(self):
        clock = [100.0]
        limiter = SlidingWindowRateLimiter(clock=lambda: clock[0])
        self.assertIsNone(limiter.check("auth", "client", 1, 60))
        self.assertEqual(limiter.check("auth", "client", 1, 60), 61)
        clock[0] = 160.0
        self.assertIsNone(limiter.check("auth", "client", 1, 60))

    def test_production_requires_secure_cookie_and_explicit_host(self):
        environment = {
            "JP_ASSIST_ENV": "production",
            "JP_ASSIST_COOKIE_SECURE": "0",
            "JP_ASSIST_ALLOWED_HOSTS": "learn.example.com",
        }
        with patch.dict(os.environ, environment, clear=True):
            with self.assertRaisesRegex(ValueError, "COOKIE_SECURE"):
                Settings.from_environment(":memory:")

        environment["JP_ASSIST_COOKIE_SECURE"] = "1"
        environment["JP_ASSIST_ALLOWED_HOSTS"] = "*"
        with patch.dict(os.environ, environment, clear=True):
            with self.assertRaisesRegex(ValueError, "ALLOWED_HOSTS"):
                Settings.from_environment(":memory:")

    def test_newer_database_schema_is_rejected(self):
        with tempfile.TemporaryDirectory(prefix="jp-assist-schema-") as temporary:
            path = Path(temporary) / "future.sqlite3"
            with sqlite3.connect(path) as connection:
                connection.execute("CREATE TABLE metadata(key TEXT PRIMARY KEY, value TEXT NOT NULL)")
                connection.execute("INSERT INTO metadata VALUES ('schema_version', '999')")
            with self.assertRaisesRegex(RuntimeError, "newer than supported"):
                Database(path)

    def test_version_one_database_is_migrated(self):
        with tempfile.TemporaryDirectory(prefix="jp-assist-migration-") as temporary:
            path = Path(temporary) / "old.sqlite3"
            database = Database(path)
            with database.connect() as connection:
                connection.execute("ALTER TABLE events DROP COLUMN page_index")
                connection.execute("UPDATE metadata SET value = '1' WHERE key = 'schema_version'")

            migrated = Database(path)
            self.assertEqual(migrated.schema_version(), LATEST_SCHEMA_VERSION)
            with migrated.connect() as connection:
                columns = {row["name"] for row in connection.execute("PRAGMA table_info(events)")}
            self.assertIn("page_index", columns)
            with migrated.connect() as connection:
                tables = {row[0] for row in connection.execute("SELECT name FROM sqlite_master WHERE type='table'")}
            self.assertTrue({"word_annotations", "review_state", "reviews", "learning_goals",
                             "review_collections"}.issubset(tables))

    def test_legacy_review_schema_gains_versioned_fsrs_projection_columns(self):
        with tempfile.TemporaryDirectory(prefix="jp-assist-review-migration-") as temporary:
            path = Path(temporary) / "version-three.sqlite3"
            with sqlite3.connect(path) as connection:
                connection.executescript("""
                    CREATE TABLE metadata(key TEXT PRIMARY KEY, value TEXT NOT NULL);
                    INSERT INTO metadata VALUES ('schema_version', '3');
                    CREATE TABLE review_state (
                        user_id TEXT NOT NULL, word_id TEXT NOT NULL, sense_id TEXT NOT NULL DEFAULT '',
                        due_at TEXT NOT NULL, interval_days REAL NOT NULL DEFAULT 0,
                        ease REAL NOT NULL DEFAULT 2.5, repetitions INTEGER NOT NULL DEFAULT 0,
                        lapses INTEGER NOT NULL DEFAULT 0, last_reviewed_at TEXT,
                        PRIMARY KEY (user_id, word_id, sense_id)
                    );
                    CREATE TABLE reviews (
                        id TEXT PRIMARY KEY, user_id TEXT NOT NULL, word_id TEXT NOT NULL,
                        sense_id TEXT NOT NULL DEFAULT '', rating INTEGER NOT NULL,
                        reviewed_at TEXT NOT NULL, due_at TEXT NOT NULL,
                        interval_days REAL NOT NULL, ease REAL NOT NULL,
                        source TEXT NOT NULL DEFAULT 'web'
                    );
                """)
            migrated = Database(path)
            self.assertEqual(migrated.schema_version(), LATEST_SCHEMA_VERSION)
            expected = {"scheduler_version", "algorithm_version", "parameters_json", "desired_retention",
                        "card_state", "step", "stability", "difficulty", "scheduled_days", "elapsed_days"}
            with migrated.connect() as connection:
                state_columns = {row["name"] for row in connection.execute("PRAGMA table_info(review_state)")}
                review_columns = {row["name"] for row in connection.execute("PRAGMA table_info(reviews)")}
            self.assertTrue(expected.issubset(state_columns))
            self.assertTrue(expected.issubset(review_columns))

    def test_version_four_goals_gain_timezone_and_bury_table(self):
        with tempfile.TemporaryDirectory(prefix="jp-assist-day-migration-") as temporary:
            path = Path(temporary) / "version-four.sqlite3"
            database = Database(path)
            with database.connect() as connection:
                connection.execute("UPDATE metadata SET value = '4' WHERE key = 'schema_version'")
                connection.execute("ALTER TABLE learning_goals DROP COLUMN timezone")
                connection.execute("DROP TABLE buried_cards")
            migrated = Database(path)
            with migrated.connect() as connection:
                goal_columns = {row["name"] for row in connection.execute("PRAGMA table_info(learning_goals)")}
                buried = connection.execute(
                    "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'buried_cards'"
                ).fetchone()
            self.assertEqual(migrated.schema_version(), LATEST_SCHEMA_VERSION)
            self.assertIn("timezone", goal_columns)
            self.assertIsNotNone(buried)

    def test_version_five_gains_review_collection_ownership(self):
        with tempfile.TemporaryDirectory(prefix="jp-assist-owner-migration-") as temporary:
            path = Path(temporary) / "version-five.sqlite3"
            database = Database(path)
            with database.connect() as connection:
                connection.execute("UPDATE metadata SET value = '5' WHERE key = 'schema_version'")
                connection.execute("DROP TABLE review_collections")
            migrated = Database(path)
            with migrated.connect() as connection:
                columns = {row["name"] for row in connection.execute("PRAGMA table_info(review_collections)")}
            self.assertEqual(migrated.schema_version(), LATEST_SCHEMA_VERSION)
            self.assertTrue({"collection_id", "review_owner", "anki_deck", "last_anki_sync_at"}.issubset(columns))

    def test_version_six_gains_anki_review_source_identity(self):
        with tempfile.TemporaryDirectory(prefix="jp-assist-anki-history-migration-") as temporary:
            path = Path(temporary) / "version-six.sqlite3"
            database = Database(path)
            with database.connect() as connection:
                connection.execute("UPDATE metadata SET value = '6' WHERE key = 'schema_version'")
                connection.execute("DROP INDEX reviews_source_identity_idx")
                for column in ("source_review_id", "source_card_id", "review_duration_ms", "source_metadata_json"):
                    connection.execute(f"ALTER TABLE reviews DROP COLUMN {column}")
            migrated = Database(path)
            with migrated.connect() as connection:
                columns = {row["name"] for row in connection.execute("PRAGMA table_info(reviews)")}
                indexes = {row["name"] for row in connection.execute("PRAGMA index_list(reviews)")}
            self.assertEqual(migrated.schema_version(), LATEST_SCHEMA_VERSION)
            self.assertTrue({"source_review_id", "source_card_id", "review_duration_ms", "source_metadata_json"}.issubset(columns))
            self.assertIn("reviews_source_identity_idx", indexes)

    def test_version_seven_gains_identity_tokens_notifications_and_audit(self):
        with tempfile.TemporaryDirectory(prefix="jp-assist-identity-migration-") as temporary:
            path = Path(temporary) / "version-seven.sqlite3"
            database = Database(path)
            with database.connect() as connection:
                connection.execute("UPDATE metadata SET value = '7' WHERE key = 'schema_version'")
                for table in (
                    "notification_deliveries", "notification_preferences", "action_tokens", "audit_events",
                ):
                    connection.execute(f"DROP TABLE {table}")
                connection.execute("ALTER TABLE users DROP COLUMN email_verified_at")
            migrated = Database(path)
            with migrated.connect() as connection:
                user_columns = {row["name"] for row in connection.execute("PRAGMA table_info(users)")}
                tables = {row[0] for row in connection.execute("SELECT name FROM sqlite_master WHERE type='table'")}
            self.assertEqual(migrated.schema_version(), LATEST_SCHEMA_VERSION)
            self.assertIn("email_verified_at", user_columns)
            self.assertTrue({"action_tokens", "notification_preferences", "notification_deliveries", "audit_events"}.issubset(tables))

    def test_version_eight_sessions_gain_private_metadata_and_public_ids(self):
        with tempfile.TemporaryDirectory(prefix="jp-assist-session-migration-") as temporary:
            path = Path(temporary) / "version-eight.sqlite3"
            database = Database(path)
            with database.connect() as connection:
                connection.execute("DROP TABLE sessions")
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
                    "INSERT INTO users VALUES (?, ?, ?, ?, ?, ?, ?)",
                    ("user", "session@example.test", "Session", b"salt", b"hash", "2026-01-01T00:00:00.000Z", None),
                )
                connection.execute(
                    "INSERT INTO sessions VALUES (?, ?, ?, ?)",
                    ("secret-hash", "user", "2026-01-01T00:00:00.000Z", "2026-02-01T00:00:00.000Z"),
                )
                connection.execute("UPDATE metadata SET value = '8' WHERE key = 'schema_version'")
            migrated = Database(path)
            with migrated.connect() as connection:
                columns = {row["name"] for row in connection.execute("PRAGMA table_info(sessions)")}
                row = connection.execute("SELECT * FROM sessions").fetchone()
                indexes = {item["name"] for item in connection.execute("PRAGMA index_list(sessions)")}
            self.assertEqual(migrated.schema_version(), LATEST_SCHEMA_VERSION)
            self.assertTrue({"id", "last_seen_at", "user_agent"}.issubset(columns))
            self.assertEqual(len(row["id"]), 32)
            self.assertEqual(row["last_seen_at"], row["created_at"])
            self.assertEqual(row["user_agent"], "")
            self.assertIn("sessions_public_id_idx", indexes)

    def test_backup_and_restore_create_ready_database_and_safety_copy(self):
        with tempfile.TemporaryDirectory(prefix="jp-assist-backup-") as temporary:
            root = Path(temporary)
            active = root / "active.sqlite3"
            backup = root / "backup.sqlite3"
            database = Database(active)
            with database.connect() as connection:
                connection.execute("INSERT INTO metadata VALUES ('sentinel', 'before')")
            backup_database(active, backup)
            with database.connect() as connection:
                connection.execute("UPDATE metadata SET value = 'after' WHERE key = 'sentinel'")

            restore_database(active, backup, confirmed=True)
            restored = Database(active)
            self.assertEqual(restored.schema_version(), LATEST_SCHEMA_VERSION)
            self.assertEqual(restored.readiness(), (True, "ready"))
            with restored.connect() as connection:
                self.assertEqual(
                    connection.execute("SELECT value FROM metadata WHERE key = 'sentinel'").fetchone()[0],
                    "before",
                )
            self.assertEqual(len(list(root.glob("active.sqlite3.pre-restore-*"))), 1)

    def test_scheduled_backup_retention_and_isolated_restore_drill(self):
        with tempfile.TemporaryDirectory(prefix="jp-assist-scheduled-backup-") as temporary:
            root = Path(temporary)
            active = root / "active.sqlite3"
            backups = root / "backups"
            database = Database(active)
            with database.connect() as connection:
                connection.execute("INSERT INTO metadata VALUES ('sentinel', 'restorable')")

            written = []
            for second in range(4):
                output, _ = scheduled_backup(
                    active,
                    backups,
                    retain=2,
                    now=datetime(2026, 10, 3, 12, 0, second, tzinfo=timezone.utc),
                )
                written.append(output)

            remaining = sorted(backups.glob("platform-*.sqlite3"))
            self.assertEqual(remaining, written[-2:])
            self.assertFalse(written[0].exists())
            self.assertEqual(restore_drill(written[-1]), (LATEST_SCHEMA_VERSION, "ready"))

    def test_preserved_schema_restore_supports_application_rollback(self):
        with tempfile.TemporaryDirectory(prefix="jp-assist-preserved-restore-") as temporary:
            root = Path(temporary)
            active = root / "active.sqlite3"
            backup = root / "version-eight.sqlite3"
            database = Database(active)
            with database.connect() as connection:
                connection.execute("UPDATE metadata SET value = '8' WHERE key = 'schema_version'")
            backup_database(active, backup)
            Database(active)

            restore_database(active, backup, confirmed=True, migrate=False)
            with sqlite3.connect(active) as connection:
                version = connection.execute(
                    "SELECT value FROM metadata WHERE key = 'schema_version'"
                ).fetchone()[0]
                integrity = connection.execute("PRAGMA quick_check").fetchone()[0]
            self.assertEqual(version, "8")
            self.assertEqual(integrity, "ok")

    def test_runtime_dictionary_import_excludes_dialogue(self):
        with tempfile.TemporaryDirectory(prefix="jp-assist-dictionary-") as temporary:
            path = Path(temporary) / "runtime.json"
            path.write_text(
                '{"messages":{"0x1":{"pages":[{"japanese":"秘密の台詞","english":"private line",'
                '"tokens":[{"id":"森|もり","senseId":"sense:1","lemma":"森",'
                '"dictionaryReading":"もり","partOfSpeech":"noun","meaning":"forest"}]}]}}}',
                encoding="utf-8",
            )
            entries = dictionary_entries_from_runtime(path, "test", "test attribution")
            self.assertEqual(entries[0]["meaning"], "forest")
            self.assertNotIn("japanese", entries[0])
            self.assertNotIn("english", entries[0])
            self.assertNotIn("private line", str(entries))


if __name__ == "__main__":
    unittest.main()
