import sqlite3
from pathlib import Path


LATEST_SCHEMA_VERSION = 4

SCHEMA = """
PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS metadata (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

INSERT OR IGNORE INTO metadata(key, value) VALUES ('schema_version', '4');

CREATE TABLE IF NOT EXISTS users (
    id TEXT PRIMARY KEY,
    email TEXT NOT NULL UNIQUE COLLATE NOCASE,
    display_name TEXT NOT NULL,
    password_salt BLOB NOT NULL,
    password_hash BLOB NOT NULL,
    created_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS sessions (
    token_hash TEXT PRIMARY KEY,
    user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    created_at TEXT NOT NULL,
    expires_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS pairings (
    id TEXT PRIMARY KEY,
    device_secret_hash TEXT NOT NULL UNIQUE,
    user_code TEXT NOT NULL UNIQUE,
    device_name TEXT NOT NULL,
    adapter_id TEXT NOT NULL,
    game_id TEXT NOT NULL,
    created_at TEXT NOT NULL,
    expires_at TEXT NOT NULL,
    user_id TEXT REFERENCES users(id) ON DELETE CASCADE,
    approved_at TEXT,
    claimed_at TEXT
);

CREATE TABLE IF NOT EXISTS devices (
    id TEXT PRIMARY KEY,
    user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    token_hash TEXT NOT NULL UNIQUE,
    device_name TEXT NOT NULL,
    adapter_id TEXT NOT NULL,
    game_id TEXT NOT NULL,
    created_at TEXT NOT NULL,
    last_seen_at TEXT NOT NULL,
    revoked_at TEXT
);

CREATE TABLE IF NOT EXISTS events (
    device_id TEXT NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
    event_id TEXT NOT NULL,
    user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    event_type TEXT NOT NULL,
    occurred_at TEXT NOT NULL,
    game_id TEXT NOT NULL,
    adapter_id TEXT NOT NULL,
    content_version TEXT NOT NULL,
    word_id TEXT,
    sense_id TEXT,
    message_id TEXT,
    page_index INTEGER,
    location_id TEXT,
    event_count INTEGER NOT NULL,
    received_at TEXT NOT NULL,
    PRIMARY KEY (device_id, event_id)
);

CREATE INDEX IF NOT EXISTS events_user_time_idx ON events(user_id, occurred_at);

CREATE TABLE IF NOT EXISTS word_progress (
    user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    word_id TEXT NOT NULL,
    sense_id TEXT NOT NULL DEFAULT '',
    encounter_count INTEGER NOT NULL DEFAULT 0,
    selection_count INTEGER NOT NULL DEFAULT 0,
    saved INTEGER NOT NULL DEFAULT 0,
    first_seen_at TEXT NOT NULL,
    last_seen_at TEXT NOT NULL,
    saved_changed_at TEXT,
    saved_event_id TEXT,
    PRIMARY KEY (user_id, word_id, sense_id)
);

CREATE TABLE IF NOT EXISTS game_word_progress (
    user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    game_id TEXT NOT NULL,
    word_id TEXT NOT NULL,
    sense_id TEXT NOT NULL DEFAULT '',
    encounter_count INTEGER NOT NULL DEFAULT 0,
    selection_count INTEGER NOT NULL DEFAULT 0,
    first_seen_at TEXT NOT NULL,
    last_seen_at TEXT NOT NULL,
    PRIMARY KEY (user_id, game_id, word_id, sense_id)
);

CREATE TABLE IF NOT EXISTS word_annotations (
    user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    word_id TEXT NOT NULL,
    sense_id TEXT NOT NULL DEFAULT '',
    learning_state TEXT NOT NULL DEFAULT 'new'
        CHECK (learning_state IN ('new', 'learning', 'known', 'ignored')),
    note TEXT NOT NULL DEFAULT '',
    tags_json TEXT NOT NULL DEFAULT '[]',
    updated_at TEXT NOT NULL,
    PRIMARY KEY (user_id, word_id, sense_id)
);

CREATE TABLE IF NOT EXISTS dictionary_entries (
    word_id TEXT NOT NULL,
    sense_id TEXT NOT NULL DEFAULT '',
    written TEXT NOT NULL,
    reading TEXT NOT NULL DEFAULT '',
    part_of_speech TEXT NOT NULL DEFAULT '',
    meaning TEXT NOT NULL DEFAULT '',
    source TEXT NOT NULL DEFAULT '',
    attribution TEXT NOT NULL DEFAULT '',
    updated_at TEXT NOT NULL,
    PRIMARY KEY (word_id, sense_id)
);

CREATE TABLE IF NOT EXISTS review_state (
    user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    word_id TEXT NOT NULL,
    sense_id TEXT NOT NULL DEFAULT '',
    due_at TEXT NOT NULL,
    interval_days REAL NOT NULL DEFAULT 0,
    ease REAL NOT NULL DEFAULT 2.5,
    repetitions INTEGER NOT NULL DEFAULT 0,
    lapses INTEGER NOT NULL DEFAULT 0,
    last_reviewed_at TEXT,
    scheduler_version TEXT NOT NULL DEFAULT 'fsrs-6.3.2',
    algorithm_version TEXT NOT NULL DEFAULT 'FSRS-6',
    parameters_json TEXT NOT NULL DEFAULT '[]',
    desired_retention REAL NOT NULL DEFAULT 0.9,
    card_state INTEGER NOT NULL DEFAULT 1,
    step INTEGER,
    stability REAL,
    difficulty REAL,
    scheduled_days REAL NOT NULL DEFAULT 0,
    elapsed_days REAL NOT NULL DEFAULT 0,
    PRIMARY KEY (user_id, word_id, sense_id)
);

CREATE INDEX IF NOT EXISTS review_state_due_idx ON review_state(user_id, due_at);

CREATE TABLE IF NOT EXISTS reviews (
    id TEXT PRIMARY KEY,
    user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    word_id TEXT NOT NULL,
    sense_id TEXT NOT NULL DEFAULT '',
    rating INTEGER NOT NULL CHECK (rating BETWEEN 1 AND 4),
    reviewed_at TEXT NOT NULL,
    due_at TEXT NOT NULL,
    interval_days REAL NOT NULL,
    ease REAL NOT NULL,
    source TEXT NOT NULL DEFAULT 'web',
    scheduler_version TEXT NOT NULL DEFAULT 'fsrs-6.3.2',
    algorithm_version TEXT NOT NULL DEFAULT 'FSRS-6',
    parameters_json TEXT NOT NULL DEFAULT '[]',
    desired_retention REAL NOT NULL DEFAULT 0.9,
    card_state INTEGER,
    step INTEGER,
    stability REAL,
    difficulty REAL,
    scheduled_days REAL,
    elapsed_days REAL
);

CREATE INDEX IF NOT EXISTS reviews_user_time_idx ON reviews(user_id, reviewed_at);

CREATE TABLE IF NOT EXISTS learning_goals (
    user_id TEXT PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
    daily_new_words INTEGER NOT NULL DEFAULT 10,
    daily_reviews INTEGER NOT NULL DEFAULT 20,
    reminders_enabled INTEGER NOT NULL DEFAULT 0,
    updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS export_history (
    id TEXT PRIMARY KEY,
    user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    export_type TEXT NOT NULL,
    game_id TEXT,
    word_count INTEGER NOT NULL,
    created_at TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS export_history_user_time_idx ON export_history(user_id, created_at);
"""


class Database:
    def __init__(self, path: str | Path):
        self.path = str(path)
        if self.path != ":memory:":
            Path(self.path).parent.mkdir(parents=True, exist_ok=True)
        with self.connect() as connection:
            existing_version = self._schema_version(connection)
            if existing_version > LATEST_SCHEMA_VERSION:
                raise RuntimeError(
                    f"Database schema {existing_version} is newer than supported schema {LATEST_SCHEMA_VERSION}"
                )
            connection.executescript(SCHEMA)
            event_columns = {row["name"] for row in connection.execute("PRAGMA table_info(events)")}
            if "page_index" not in event_columns:
                connection.execute("ALTER TABLE events ADD COLUMN page_index INTEGER")
            self._migrate_review_schema(connection)
            connection.execute("UPDATE metadata SET value = '4' WHERE key = 'schema_version'")

    @staticmethod
    def _migrate_review_schema(connection: sqlite3.Connection) -> None:
        state_columns = {row["name"] for row in connection.execute("PRAGMA table_info(review_state)")}
        state_additions = {
            "scheduler_version": "TEXT NOT NULL DEFAULT 'mvp-1'",
            "algorithm_version": "TEXT NOT NULL DEFAULT 'MVP'",
            "parameters_json": "TEXT NOT NULL DEFAULT '[]'",
            "desired_retention": "REAL NOT NULL DEFAULT 0.9",
            "card_state": "INTEGER NOT NULL DEFAULT 1",
            "step": "INTEGER",
            "stability": "REAL",
            "difficulty": "REAL",
            "scheduled_days": "REAL NOT NULL DEFAULT 0",
            "elapsed_days": "REAL NOT NULL DEFAULT 0",
        }
        for name, definition in state_additions.items():
            if name not in state_columns:
                connection.execute(f"ALTER TABLE review_state ADD COLUMN {name} {definition}")

        review_columns = {row["name"] for row in connection.execute("PRAGMA table_info(reviews)")}
        review_additions = {
            "scheduler_version": "TEXT NOT NULL DEFAULT 'mvp-1'",
            "algorithm_version": "TEXT NOT NULL DEFAULT 'MVP'",
            "parameters_json": "TEXT NOT NULL DEFAULT '[]'",
            "desired_retention": "REAL NOT NULL DEFAULT 0.9",
            "card_state": "INTEGER",
            "step": "INTEGER",
            "stability": "REAL",
            "difficulty": "REAL",
            "scheduled_days": "REAL",
            "elapsed_days": "REAL",
        }
        for name, definition in review_additions.items():
            if name not in review_columns:
                connection.execute(f"ALTER TABLE reviews ADD COLUMN {name} {definition}")

    @staticmethod
    def _schema_version(connection: sqlite3.Connection) -> int:
        metadata_exists = connection.execute(
            "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'metadata'"
        ).fetchone()
        if metadata_exists is None:
            return 0
        row = connection.execute("SELECT value FROM metadata WHERE key = 'schema_version'").fetchone()
        if row is None:
            return 0
        try:
            return int(row[0])
        except (TypeError, ValueError) as error:
            raise RuntimeError("Database schema version is invalid") from error

    def schema_version(self) -> int:
        with self.connect() as connection:
            return self._schema_version(connection)

    def readiness(self) -> tuple[bool, str]:
        try:
            with self.connect() as connection:
                version = self._schema_version(connection)
                integrity = connection.execute("PRAGMA quick_check(1)").fetchone()[0]
            if version != LATEST_SCHEMA_VERSION:
                return False, f"schema {version}, expected {LATEST_SCHEMA_VERSION}"
            if integrity != "ok":
                return False, f"database integrity check failed: {integrity}"
            return True, "ready"
        except (OSError, sqlite3.Error, RuntimeError) as error:
            return False, str(error)

    def connect(self) -> sqlite3.Connection:
        connection = sqlite3.connect(self.path, timeout=10.0)
        connection.row_factory = sqlite3.Row
        connection.execute("PRAGMA foreign_keys = ON")
        if self.path != ":memory:":
            connection.execute("PRAGMA journal_mode = WAL")
        return connection
