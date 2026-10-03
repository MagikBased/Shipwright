import os
import sqlite3
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from learning_platform.config import Settings
from learning_platform.database import Database, LATEST_SCHEMA_VERSION
from learning_platform.manage import backup_database, restore_database
from learning_platform.rate_limit import SlidingWindowRateLimiter


class DeploymentTest(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
