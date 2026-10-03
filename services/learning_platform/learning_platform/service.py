from __future__ import annotations

import json
import re
import sqlite3
import uuid
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Any, Callable

from .database import Database
from .errors import (
    AuthenticationError,
    ConflictError,
    NotFoundError,
    PairingExpiredError,
    PairingPendingError,
    ValidationError,
)
from .security import hash_password, new_bearer_token, new_user_code, token_hash, verify_password


EVENT_TYPES = {
    "word_encountered",
    "word_selected",
    "word_saved",
    "word_unsaved",
    "dialogue_seen",
    "study_mode_opened",
}
WORD_EVENT_TYPES = {
    "word_encountered",
    "word_selected",
    "word_saved",
    "word_unsaved",
}
EVENT_FIELDS = {
    "eventId",
    "type",
    "occurredAt",
    "gameId",
    "adapterId",
    "contentVersion",
    "wordId",
    "senseId",
    "messageId",
    "pageIndex",
    "locationId",
    "count",
}
IDENTIFIER_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._:/|+-]{0,127}$")
LEARNING_STATES = {"new", "learning", "known", "ignored"}


def utc_now() -> datetime:
    return datetime.now(timezone.utc)


def isoformat(value: datetime) -> str:
    return value.astimezone(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def parse_timestamp(value: str) -> str:
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except (TypeError, ValueError) as exc:
        raise ValidationError("occurredAt must be an ISO-8601 timestamp") from exc
    if parsed.tzinfo is None:
        raise ValidationError("occurredAt must include a timezone")
    return isoformat(parsed)


def require_identifier(name: str, value: Any) -> str:
    if not isinstance(value, str) or not IDENTIFIER_PATTERN.fullmatch(value):
        raise ValidationError(f"{name} is not a valid identifier")
    return value


class LearningPlatform:
    def __init__(self, database_path: str | Path, now: Callable[[], datetime] = utc_now):
        self.database = Database(database_path)
        self.now = now

    def register_user(self, email: str, password: str, display_name: str) -> dict[str, Any]:
        normalized_email = self._validate_email(email)
        clean_name = display_name.strip()
        if not clean_name or len(clean_name) > 80:
            raise ValidationError("displayName must contain between 1 and 80 characters")
        self._validate_password(password)

        user_id = str(uuid.uuid4())
        salt, digest = hash_password(password)
        created_at = isoformat(self.now())
        try:
            with self.database.connect() as connection:
                connection.execute(
                    "INSERT INTO users VALUES (?, ?, ?, ?, ?, ?)",
                    (user_id, normalized_email, clean_name, salt, digest, created_at),
                )
        except sqlite3.IntegrityError as exc:
            raise ConflictError("An account with that email already exists") from exc

        return {"user": self._public_user(user_id, normalized_email, clean_name), "token": self._new_session(user_id)}

    def login(self, email: str, password: str) -> dict[str, Any]:
        normalized_email = self._validate_email(email)
        with self.database.connect() as connection:
            row = connection.execute("SELECT * FROM users WHERE email = ?", (normalized_email,)).fetchone()
        if row is None or not verify_password(password, row["password_salt"], row["password_hash"]):
            raise AuthenticationError("Invalid email or password")
        return {
            "user": self._public_user(row["id"], row["email"], row["display_name"]),
            "token": self._new_session(row["id"]),
        }

    def authenticate_session(self, token: str) -> dict[str, Any]:
        now = isoformat(self.now())
        with self.database.connect() as connection:
            row = connection.execute(
                """
                SELECT users.id, users.email, users.display_name
                FROM sessions JOIN users ON users.id = sessions.user_id
                WHERE sessions.token_hash = ? AND sessions.expires_at > ?
                """,
                (token_hash(token), now),
            ).fetchone()
        if row is None:
            raise AuthenticationError("The session is missing, expired, or invalid")
        return self._public_user(row["id"], row["email"], row["display_name"])

    def logout(self, token: str) -> None:
        with self.database.connect() as connection:
            connection.execute("DELETE FROM sessions WHERE token_hash = ?", (token_hash(token),))

    def start_pairing(self, device_name: str, adapter_id: str, game_id: str) -> dict[str, Any]:
        clean_name = device_name.strip()
        if not clean_name or len(clean_name) > 100:
            raise ValidationError("deviceName must contain between 1 and 100 characters")
        adapter_id = require_identifier("adapterId", adapter_id)
        game_id = require_identifier("gameId", game_id)
        now = self.now()
        expires = now + timedelta(minutes=10)
        device_secret = new_bearer_token()

        for _ in range(5):
            user_code = new_user_code()
            try:
                with self.database.connect() as connection:
                    connection.execute(
                        """
                        INSERT INTO pairings(
                            id, device_secret_hash, user_code, device_name, adapter_id, game_id,
                            created_at, expires_at
                        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?)
                        """,
                        (
                            str(uuid.uuid4()),
                            token_hash(device_secret),
                            user_code,
                            clean_name,
                            adapter_id,
                            game_id,
                            isoformat(now),
                            isoformat(expires),
                        ),
                    )
                return {
                    "deviceCode": device_secret,
                    "userCode": user_code,
                    "verificationPath": "/#pair-device",
                    "expiresIn": 600,
                    "pollInterval": 3,
                }
            except sqlite3.IntegrityError:
                continue
        raise ConflictError("Could not allocate a unique pairing code")

    def approve_pairing(self, session_token: str, user_code: str) -> dict[str, Any]:
        user = self.authenticate_session(session_token)
        normalized_code = user_code.strip().upper()
        now = isoformat(self.now())
        with self.database.connect() as connection:
            row = connection.execute("SELECT * FROM pairings WHERE user_code = ?", (normalized_code,)).fetchone()
            if row is None:
                raise NotFoundError("Pairing code not found")
            if row["expires_at"] <= now:
                raise PairingExpiredError("Pairing code has expired")
            if row["claimed_at"] is not None:
                raise ConflictError("Pairing code has already been claimed")
            if row["user_id"] is not None and row["user_id"] != user["id"]:
                raise ConflictError("Pairing code was approved by another account")
            connection.execute(
                "UPDATE pairings SET user_id = ?, approved_at = ? WHERE id = ?",
                (user["id"], now, row["id"]),
            )
        return {
            "userCode": normalized_code,
            "deviceName": row["device_name"],
            "gameId": row["game_id"],
            "approved": True,
        }

    def claim_pairing(self, device_code: str) -> dict[str, Any]:
        now = isoformat(self.now())
        with self.database.connect() as connection:
            connection.execute("BEGIN IMMEDIATE")
            row = connection.execute(
                "SELECT * FROM pairings WHERE device_secret_hash = ?", (token_hash(device_code),)
            ).fetchone()
            if row is None:
                raise AuthenticationError("Device code is invalid")
            if row["expires_at"] <= now:
                raise PairingExpiredError("Pairing code has expired")
            if row["user_id"] is None:
                raise PairingPendingError("The player has not approved this device yet")
            if row["claimed_at"] is not None:
                raise ConflictError("Pairing code has already been claimed")

            device_id = str(uuid.uuid4())
            device_token = new_bearer_token()
            connection.execute(
                """
                INSERT INTO devices VALUES (?, ?, ?, ?, ?, ?, ?, ?, NULL)
                """,
                (
                    device_id,
                    row["user_id"],
                    token_hash(device_token),
                    row["device_name"],
                    row["adapter_id"],
                    row["game_id"],
                    now,
                    now,
                ),
            )
            connection.execute("UPDATE pairings SET claimed_at = ? WHERE id = ?", (now, row["id"]))
        return {
            "deviceId": device_id,
            "deviceToken": device_token,
            "gameId": row["game_id"],
            "adapterId": row["adapter_id"],
        }

    def ingest_events(self, device_token: str, events: list[dict[str, Any]]) -> dict[str, Any]:
        if not events or len(events) > 250:
            raise ValidationError("An event batch must contain between 1 and 250 events")

        normalized = [self._validate_event(event) for event in events]
        received_at = isoformat(self.now())
        accepted: list[str] = []
        duplicates: list[str] = []

        with self.database.connect() as connection:
            connection.execute("BEGIN IMMEDIATE")
            device = connection.execute(
                """
                SELECT * FROM devices
                WHERE token_hash = ? AND revoked_at IS NULL
                """,
                (token_hash(device_token),),
            ).fetchone()
            if device is None:
                raise AuthenticationError("Device token is invalid or revoked")

            for event in normalized:
                if event["gameId"] != device["game_id"] or event["adapterId"] != device["adapter_id"]:
                    raise ValidationError("Event gameId and adapterId must match the paired device")
                cursor = connection.execute(
                    """
                    INSERT OR IGNORE INTO events(
                        device_id, event_id, user_id, event_type, occurred_at, game_id,
                        adapter_id, content_version, word_id, sense_id, message_id,
                        page_index, location_id, event_count, received_at
                    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                    """,
                    (
                        device["id"],
                        event["eventId"],
                        device["user_id"],
                        event["type"],
                        event["occurredAt"],
                        event["gameId"],
                        event["adapterId"],
                        event["contentVersion"],
                        event.get("wordId"),
                        event.get("senseId", ""),
                        event.get("messageId"),
                        event.get("pageIndex"),
                        event.get("locationId"),
                        event["count"],
                        received_at,
                    ),
                )
                if cursor.rowcount == 0:
                    duplicates.append(event["eventId"])
                    continue
                accepted.append(event["eventId"])
                if event["type"] in WORD_EVENT_TYPES:
                    self._apply_word_event(connection, device["user_id"], event)

            connection.execute("UPDATE devices SET last_seen_at = ? WHERE id = ?", (received_at, device["id"]))

        return {"acceptedEventIds": accepted, "duplicateEventIds": duplicates}

    def list_devices(self, session_token: str) -> list[dict[str, Any]]:
        user = self.authenticate_session(session_token)
        with self.database.connect() as connection:
            rows = connection.execute(
                """
                SELECT id, device_name, adapter_id, game_id, created_at, last_seen_at
                FROM devices
                WHERE user_id = ? AND revoked_at IS NULL
                ORDER BY last_seen_at DESC
                """,
                (user["id"],),
            ).fetchall()
        return [
            {
                "id": row["id"],
                "deviceName": row["device_name"],
                "adapterId": row["adapter_id"],
                "gameId": row["game_id"],
                "createdAt": row["created_at"],
                "lastSeenAt": row["last_seen_at"],
            }
            for row in rows
        ]

    def revoke_device(self, session_token: str, device_id: str) -> None:
        user = self.authenticate_session(session_token)
        with self.database.connect() as connection:
            cursor = connection.execute(
                """
                UPDATE devices SET revoked_at = ?
                WHERE id = ? AND user_id = ? AND revoked_at IS NULL
                """,
                (isoformat(self.now()), device_id, user["id"]),
            )
        if cursor.rowcount == 0:
            raise NotFoundError("Connected device not found")

    def get_stats(self, session_token: str) -> dict[str, Any]:
        user = self.authenticate_session(session_token)
        with self.database.connect() as connection:
            totals = connection.execute(
                """
                SELECT COUNT(*) AS unique_words,
                       COALESCE(SUM(encounter_count), 0) AS encounters,
                       COALESCE(SUM(selection_count), 0) AS selections,
                       COALESCE(SUM(saved), 0) AS saved_words
                FROM word_progress WHERE user_id = ?
                """,
                (user["id"],),
            ).fetchone()
            event_count = connection.execute(
                "SELECT COUNT(*) FROM events WHERE user_id = ?", (user["id"],)
            ).fetchone()[0]
            devices = connection.execute(
                "SELECT COUNT(*) FROM devices WHERE user_id = ? AND revoked_at IS NULL", (user["id"],)
            ).fetchone()[0]
            games = connection.execute(
                "SELECT COUNT(DISTINCT game_id) FROM game_word_progress WHERE user_id = ?", (user["id"],)
            ).fetchone()[0]
        return {
            "uniqueWords": totals["unique_words"],
            "encounters": totals["encounters"],
            "selections": totals["selections"],
            "savedWords": totals["saved_words"],
            "games": games,
            "events": event_count,
            "connectedDevices": devices,
        }

    def list_word_progress(
        self,
        session_token: str,
        saved_only: bool = False,
        search: str = "",
        game_id: str | None = None,
        learning_state: str | None = None,
        sort: str = "frequency",
        limit: int = 250,
        offset: int = 0,
    ) -> list[dict[str, Any]]:
        user = self.authenticate_session(session_token)
        if learning_state is not None and learning_state not in LEARNING_STATES:
            raise ValidationError("learningState is invalid")
        if not 1 <= limit <= 1000 or offset < 0:
            raise ValidationError("limit must be 1-1000 and offset cannot be negative")
        order_by = {
            "frequency": "progress.encounter_count DESC, progress.word_id",
            "recent": "progress.last_seen_at DESC, progress.word_id",
            "alphabetical": "COALESCE(dictionary.written, progress.word_id), progress.word_id",
            "saved": "progress.saved DESC, progress.last_seen_at DESC",
        }.get(sort)
        if order_by is None:
            raise ValidationError("sort is invalid")
        filters = ["progress.user_id = ?"]
        parameters: list[Any] = [user["id"]]
        if saved_only:
            filters.append("progress.saved = 1")
        if search.strip():
            needle = f"%{search.strip()}%"
            filters.append(
                "(progress.word_id LIKE ? OR dictionary.written LIKE ? OR dictionary.reading LIKE ? "
                "OR dictionary.meaning LIKE ? OR annotations.note LIKE ? OR annotations.tags_json LIKE ?)"
            )
            parameters.extend([needle] * 6)
        if game_id:
            game_id = require_identifier("gameId", game_id)
            filters.append(
                "EXISTS (SELECT 1 FROM game_word_progress filter_game WHERE filter_game.user_id = progress.user_id "
                "AND filter_game.word_id = progress.word_id AND filter_game.sense_id = progress.sense_id "
                "AND filter_game.game_id = ?)"
            )
            parameters.append(game_id)
        if learning_state:
            filters.append("COALESCE(annotations.learning_state, 'new') = ?")
            parameters.append(learning_state)
        parameters.extend([limit, offset])
        with self.database.connect() as connection:
            rows = connection.execute(
                f"""
                SELECT progress.word_id, progress.sense_id, progress.encounter_count,
                       progress.selection_count, progress.saved, progress.first_seen_at,
                       progress.last_seen_at, GROUP_CONCAT(DISTINCT games.game_id) AS game_ids,
                       saved_event.game_id AS context_game_id,
                       saved_event.message_id AS context_message_id,
                       saved_event.page_index AS context_page_index,
                       COALESCE(annotations.learning_state, 'new') AS learning_state,
                       COALESCE(annotations.note, '') AS note,
                       COALESCE(annotations.tags_json, '[]') AS tags_json,
                       dictionary.written, dictionary.reading, dictionary.part_of_speech,
                       dictionary.meaning, dictionary.source, dictionary.attribution,
                       review.due_at, review.interval_days, review.repetitions
                FROM word_progress AS progress
                LEFT JOIN game_word_progress AS games
                  ON games.user_id = progress.user_id
                 AND games.word_id = progress.word_id
                 AND games.sense_id = progress.sense_id
                LEFT JOIN events AS saved_event
                  ON saved_event.user_id = progress.user_id
                 AND saved_event.event_id = progress.saved_event_id
                 AND saved_event.event_type = 'word_saved'
                LEFT JOIN word_annotations AS annotations
                  ON annotations.user_id = progress.user_id
                 AND annotations.word_id = progress.word_id
                 AND annotations.sense_id = progress.sense_id
                LEFT JOIN dictionary_entries AS dictionary
                  ON dictionary.word_id = progress.word_id
                 AND dictionary.sense_id = progress.sense_id
                LEFT JOIN review_state AS review
                  ON review.user_id = progress.user_id
                 AND review.word_id = progress.word_id
                 AND review.sense_id = progress.sense_id
                WHERE {' AND '.join(filters)}
                GROUP BY progress.user_id, progress.word_id, progress.sense_id
                ORDER BY {order_by}
                LIMIT ? OFFSET ?
                """,
                parameters,
            ).fetchall()
        return [
            {
                "gameIds": sorted(set((row["game_ids"] or "").split(","))) if row["game_ids"] else [],
                "wordId": row["word_id"],
                "senseId": row["sense_id"] or None,
                "encounterCount": row["encounter_count"],
                "selectionCount": row["selection_count"],
                "saved": bool(row["saved"]),
                "firstSeenAt": row["first_seen_at"],
                "lastSeenAt": row["last_seen_at"],
                "contextGameId": row["context_game_id"] if row["saved"] else None,
                "contextMessageId": row["context_message_id"] if row["saved"] else None,
                "contextPageIndex": row["context_page_index"] if row["saved"] else None,
                "learningState": row["learning_state"],
                "note": row["note"],
                "tags": json.loads(row["tags_json"]),
                "dictionary": {
                    "written": row["written"],
                    "reading": row["reading"],
                    "partOfSpeech": row["part_of_speech"],
                    "meaning": row["meaning"],
                    "source": row["source"],
                    "attribution": row["attribution"],
                } if row["written"] is not None else None,
                "review": {
                    "dueAt": row["due_at"],
                    "intervalDays": row["interval_days"],
                    "repetitions": row["repetitions"],
                } if row["due_at"] is not None else None,
            }
            for row in rows
        ]

    def saved_word_manifest(self, session_token: str, game_id: str | None = None) -> dict[str, Any]:
        user = self.authenticate_session(session_token)
        words = self.list_word_progress(session_token, saved_only=True, game_id=game_id, limit=1000)
        manifest = {
            "schemaVersion": 1,
            "savedTokenIds": sorted({word["wordId"] for word in words}),
            "words": words,
        }
        with self.database.connect() as connection:
            connection.execute(
                "INSERT INTO export_history VALUES (?, ?, 'saved-words', ?, ?, ?)",
                (str(uuid.uuid4()), user["id"], game_id, len(words), isoformat(self.now())),
            )
        return manifest

    def activity(self, session_token: str, days: int = 30) -> dict[str, Any]:
        user = self.authenticate_session(session_token)
        if not 1 <= days <= 365:
            raise ValidationError("days must be between 1 and 365")
        since = isoformat(self.now() - timedelta(days=days - 1))[:10]
        with self.database.connect() as connection:
            daily = connection.execute(
                """
                SELECT substr(occurred_at, 1, 10) AS day,
                       SUM(CASE WHEN event_type = 'word_encountered' THEN event_count ELSE 0 END) AS encounters,
                       SUM(CASE WHEN event_type = 'word_selected' THEN event_count ELSE 0 END) AS selections,
                       SUM(CASE WHEN event_type = 'word_saved' THEN 1 ELSE 0 END) AS saves,
                       COUNT(*) AS events
                FROM events WHERE user_id = ? AND occurred_at >= ?
                GROUP BY day ORDER BY day
                """,
                (user["id"], since),
            ).fetchall()
            games = connection.execute(
                """
                SELECT game_id, COUNT(DISTINCT word_id || char(31) || sense_id) AS unique_words,
                       SUM(encounter_count) AS encounters, SUM(selection_count) AS selections,
                       MAX(last_seen_at) AS last_seen_at
                FROM game_word_progress WHERE user_id = ? GROUP BY game_id ORDER BY last_seen_at DESC
                """,
                (user["id"],),
            ).fetchall()
            reviews = connection.execute(
                """
                SELECT substr(reviewed_at, 1, 10) AS day, COUNT(*) AS reviews
                FROM reviews WHERE user_id = ? AND reviewed_at >= ? GROUP BY day
                """,
                (user["id"], since),
            ).fetchall()
        activity_days = {
            row["day"]: {
                "date": row["day"], "encounters": row["encounters"], "selections": row["selections"],
                "saves": row["saves"], "events": row["events"], "reviews": 0,
            }
            for row in daily
        }
        for row in reviews:
            activity_days.setdefault(
                row["day"],
                {"date": row["day"], "encounters": 0, "selections": 0, "saves": 0, "events": 0, "reviews": 0},
            )["reviews"] = row["reviews"]
        for day_offset in range(days):
            day = (self.now() - timedelta(days=days - day_offset - 1)).date().isoformat()
            activity_days.setdefault(
                day, {"date": day, "encounters": 0, "selections": 0, "saves": 0, "events": 0, "reviews": 0}
            )
        return {
            "days": [activity_days[day] for day in sorted(activity_days)],
            "games": [
                {
                    "gameId": row["game_id"],
                    "uniqueWords": row["unique_words"],
                    "encounters": row["encounters"],
                    "selections": row["selections"],
                    "lastSeenAt": row["last_seen_at"],
                }
                for row in games
            ],
        }

    def update_word_annotation(
        self,
        session_token: str,
        word_id: str,
        sense_id: str | None,
        learning_state: str,
        note: str,
        tags: list[str],
    ) -> dict[str, Any]:
        user = self.authenticate_session(session_token)
        sense_id = sense_id or ""
        if learning_state not in LEARNING_STATES:
            raise ValidationError("learningState is invalid")
        if not isinstance(note, str) or len(note) > 4000:
            raise ValidationError("note cannot exceed 4000 characters")
        if not isinstance(tags, list) or len(tags) > 20:
            raise ValidationError("tags must contain at most 20 values")
        if not isinstance(word_id, str) or not word_id or len(word_id) > 512:
            raise ValidationError("wordId must contain between 1 and 512 characters")
        if any(not isinstance(tag, str) for tag in tags):
            raise ValidationError("tags must contain only strings")
        clean_tags = sorted({tag.strip() for tag in tags if isinstance(tag, str) and tag.strip()})
        if any(len(tag) > 40 for tag in clean_tags):
            raise ValidationError("tags cannot exceed 40 characters")
        now = isoformat(self.now())
        with self.database.connect() as connection:
            exists = connection.execute(
                "SELECT 1 FROM word_progress WHERE user_id = ? AND word_id = ? AND sense_id = ?",
                (user["id"], word_id, sense_id),
            ).fetchone()
            if exists is None:
                raise NotFoundError("Word not found in this account")
            connection.execute(
                """
                INSERT INTO word_annotations VALUES (?, ?, ?, ?, ?, ?, ?)
                ON CONFLICT(user_id, word_id, sense_id) DO UPDATE SET
                    learning_state = excluded.learning_state, note = excluded.note,
                    tags_json = excluded.tags_json, updated_at = excluded.updated_at
                """,
                (user["id"], word_id, sense_id, learning_state, note.strip(), json.dumps(clean_tags), now),
            )
        return {"wordId": word_id, "senseId": sense_id or None, "learningState": learning_state,
                "note": note.strip(), "tags": clean_tags, "updatedAt": now}

    def get_goals(self, session_token: str) -> dict[str, Any]:
        user = self.authenticate_session(session_token)
        with self.database.connect() as connection:
            row = connection.execute("SELECT * FROM learning_goals WHERE user_id = ?", (user["id"],)).fetchone()
        if row is None:
            return {"dailyNewWords": 10, "dailyReviews": 20, "remindersEnabled": False}
        return {"dailyNewWords": row["daily_new_words"], "dailyReviews": row["daily_reviews"],
                "remindersEnabled": bool(row["reminders_enabled"]), "updatedAt": row["updated_at"]}

    def update_goals(
        self, session_token: str, daily_new_words: int, daily_reviews: int, reminders_enabled: bool
    ) -> dict[str, Any]:
        user = self.authenticate_session(session_token)
        if not 0 <= daily_new_words <= 100 or not 0 <= daily_reviews <= 500:
            raise ValidationError("Daily goals are outside their supported range")
        now = isoformat(self.now())
        with self.database.connect() as connection:
            connection.execute(
                """
                INSERT INTO learning_goals VALUES (?, ?, ?, ?, ?)
                ON CONFLICT(user_id) DO UPDATE SET daily_new_words = excluded.daily_new_words,
                    daily_reviews = excluded.daily_reviews, reminders_enabled = excluded.reminders_enabled,
                    updated_at = excluded.updated_at
                """,
                (user["id"], daily_new_words, daily_reviews, int(reminders_enabled), now),
            )
        return self.get_goals(session_token)

    def review_queue(self, session_token: str, limit: int = 20) -> list[dict[str, Any]]:
        user = self.authenticate_session(session_token)
        if not 1 <= limit <= 100:
            raise ValidationError("limit must be between 1 and 100")
        now = isoformat(self.now())
        with self.database.connect() as connection:
            rows = connection.execute(
                """
                SELECT progress.word_id, progress.sense_id, progress.encounter_count,
                       dictionary.written, dictionary.reading, dictionary.meaning, dictionary.part_of_speech,
                       review.due_at, review.interval_days, review.repetitions,
                       COALESCE(annotation.learning_state, 'new') AS learning_state
                FROM word_progress progress
                LEFT JOIN dictionary_entries dictionary ON dictionary.word_id = progress.word_id
                    AND dictionary.sense_id = progress.sense_id
                LEFT JOIN review_state review ON review.user_id = progress.user_id
                    AND review.word_id = progress.word_id AND review.sense_id = progress.sense_id
                LEFT JOIN word_annotations annotation ON annotation.user_id = progress.user_id
                    AND annotation.word_id = progress.word_id AND annotation.sense_id = progress.sense_id
                WHERE progress.user_id = ? AND (progress.saved = 1 OR annotation.learning_state = 'learning')
                    AND COALESCE(annotation.learning_state, 'new') != 'ignored'
                    AND (review.due_at IS NULL OR review.due_at <= ?)
                ORDER BY COALESCE(review.due_at, progress.first_seen_at), progress.encounter_count DESC
                LIMIT ?
                """,
                (user["id"], now, limit),
            ).fetchall()
        return [
            {"wordId": row["word_id"], "senseId": row["sense_id"] or None,
             "written": row["written"] or row["word_id"].split("|")[0],
             "reading": row["reading"] or (row["word_id"].split("|", 1)[1] if "|" in row["word_id"] else ""),
             "meaning": row["meaning"] or "", "partOfSpeech": row["part_of_speech"] or "",
             "encounterCount": row["encounter_count"], "learningState": row["learning_state"],
             "dueAt": row["due_at"], "intervalDays": row["interval_days"] or 0,
             "repetitions": row["repetitions"] or 0}
            for row in rows
        ]

    def submit_review(
        self, session_token: str, word_id: str, sense_id: str | None, rating: int, source: str = "web"
    ) -> dict[str, Any]:
        user = self.authenticate_session(session_token)
        sense_id = sense_id or ""
        if rating not in {1, 2, 3, 4}:
            raise ValidationError("rating must be 1 (again), 2 (hard), 3 (good), or 4 (easy)")
        if source not in {"web", "anki", "import"}:
            raise ValidationError("review source is invalid")
        now_value = self.now()
        now = isoformat(now_value)
        with self.database.connect() as connection:
            progress = connection.execute(
                "SELECT 1 FROM word_progress WHERE user_id = ? AND word_id = ? AND sense_id = ?",
                (user["id"], word_id, sense_id),
            ).fetchone()
            if progress is None:
                raise NotFoundError("Word not found in this account")
            current = connection.execute(
                "SELECT * FROM review_state WHERE user_id = ? AND word_id = ? AND sense_id = ?",
                (user["id"], word_id, sense_id),
            ).fetchone()
            interval = float(current["interval_days"]) if current else 0.0
            ease = float(current["ease"]) if current else 2.5
            repetitions = int(current["repetitions"]) if current else 0
            lapses = int(current["lapses"]) if current else 0
            if rating == 1:
                interval, ease, repetitions, lapses = 0.007, max(1.3, ease - 0.2), 0, lapses + 1
            elif rating == 2:
                interval, ease, repetitions = max(1.0, interval * 1.2), max(1.3, ease - 0.15), repetitions + 1
            elif rating == 3:
                interval, repetitions = max(1.0, interval * ease), repetitions + 1
            else:
                interval, ease, repetitions = max(4.0, interval * ease * 1.3), ease + 0.15, repetitions + 1
            due_at = isoformat(now_value + timedelta(days=interval))
            connection.execute(
                """
                INSERT INTO review_state VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
                ON CONFLICT(user_id, word_id, sense_id) DO UPDATE SET due_at = excluded.due_at,
                    interval_days = excluded.interval_days, ease = excluded.ease,
                    repetitions = excluded.repetitions, lapses = excluded.lapses,
                    last_reviewed_at = excluded.last_reviewed_at
                """,
                (user["id"], word_id, sense_id, due_at, interval, ease, repetitions, lapses, now),
            )
            review_id = str(uuid.uuid4())
            connection.execute(
                "INSERT INTO reviews VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                (review_id, user["id"], word_id, sense_id, rating, now, due_at, interval, ease, source),
            )
        return {"id": review_id, "wordId": word_id, "senseId": sense_id or None, "rating": rating,
                "reviewedAt": now, "dueAt": due_at, "intervalDays": interval, "ease": ease,
                "repetitions": repetitions, "lapses": lapses}

    def list_sessions(self, session_token: str) -> list[dict[str, Any]]:
        user = self.authenticate_session(session_token)
        current_hash = token_hash(session_token)
        with self.database.connect() as connection:
            rows = connection.execute(
                "SELECT token_hash, created_at, expires_at FROM sessions WHERE user_id = ? ORDER BY created_at DESC",
                (user["id"],),
            ).fetchall()
        return [{"id": row["token_hash"][:16], "createdAt": row["created_at"],
                 "expiresAt": row["expires_at"], "current": row["token_hash"] == current_hash} for row in rows]

    def revoke_session(self, session_token: str, session_id: str) -> bool:
        user = self.authenticate_session(session_token)
        with self.database.connect() as connection:
            row = connection.execute(
                "SELECT token_hash FROM sessions WHERE user_id = ? AND substr(token_hash, 1, 16) = ?",
                (user["id"], session_id),
            ).fetchone()
            if row is None:
                raise NotFoundError("Session not found")
            connection.execute("DELETE FROM sessions WHERE token_hash = ?", (row["token_hash"],))
        return row["token_hash"] == token_hash(session_token)

    def change_password(self, session_token: str, current_password: str, new_password: str) -> None:
        user = self.authenticate_session(session_token)
        self._validate_password(new_password)
        with self.database.connect() as connection:
            row = connection.execute("SELECT * FROM users WHERE id = ?", (user["id"],)).fetchone()
            if not verify_password(current_password, row["password_salt"], row["password_hash"]):
                raise AuthenticationError("Current password is incorrect")
            salt, digest = hash_password(new_password)
            connection.execute("UPDATE users SET password_salt = ?, password_hash = ? WHERE id = ?",
                               (salt, digest, user["id"]))
            connection.execute("DELETE FROM sessions WHERE user_id = ? AND token_hash != ?",
                               (user["id"], token_hash(session_token)))

    def delete_account(self, session_token: str, password: str) -> None:
        user = self.authenticate_session(session_token)
        with self.database.connect() as connection:
            row = connection.execute("SELECT * FROM users WHERE id = ?", (user["id"],)).fetchone()
            if not verify_password(password, row["password_salt"], row["password_hash"]):
                raise AuthenticationError("Password is incorrect")
            connection.execute("DELETE FROM users WHERE id = ?", (user["id"],))

    def account_export(self, session_token: str) -> dict[str, Any]:
        user = self.authenticate_session(session_token)
        exported_at = isoformat(self.now())
        with self.database.connect() as connection:
            connection.execute(
                "INSERT INTO export_history VALUES (?, ?, 'account', NULL, 0, ?)",
                (str(uuid.uuid4()), user["id"], exported_at),
            )
            annotations = [dict(row) for row in connection.execute(
                "SELECT word_id, sense_id, learning_state, note, tags_json, updated_at FROM word_annotations WHERE user_id = ?",
                (user["id"],))]
            reviews = [dict(row) for row in connection.execute(
                "SELECT word_id, sense_id, rating, reviewed_at, due_at, interval_days, ease, source FROM reviews WHERE user_id = ? ORDER BY reviewed_at",
                (user["id"],))]
            events = [dict(row) for row in connection.execute(
                """
                SELECT event_id, event_type, occurred_at, game_id, adapter_id, content_version,
                       word_id, sense_id, message_id, page_index, location_id, event_count, received_at
                FROM events WHERE user_id = ? ORDER BY occurred_at, event_id
                """, (user["id"],))]
            pairings = [dict(row) for row in connection.execute(
                """
                SELECT device_name, adapter_id, game_id, created_at, expires_at, approved_at, claimed_at
                FROM pairings WHERE user_id = ? ORDER BY created_at
                """, (user["id"],))]
            exports = [dict(row) for row in connection.execute(
                "SELECT export_type, game_id, word_count, created_at FROM export_history WHERE user_id = ? ORDER BY created_at",
                (user["id"],))]
        for annotation in annotations:
            annotation["tags"] = json.loads(annotation.pop("tags_json"))
        return {"schemaVersion": 1, "exportedAt": exported_at, "user": user,
                "stats": self.get_stats(session_token), "goals": self.get_goals(session_token),
                "words": self.list_word_progress(session_token, limit=1000),
                "annotations": annotations, "reviews": reviews, "events": events,
                "devices": self.list_devices(session_token), "sessions": self.list_sessions(session_token),
                "pairings": pairings, "exportHistory": exports}

    def clear_game_progress(self, session_token: str, game_id: str) -> None:
        user = self.authenticate_session(session_token)
        game_id = require_identifier("gameId", game_id)
        with self.database.connect() as connection:
            connection.execute("BEGIN IMMEDIATE")
            connection.execute("DELETE FROM events WHERE user_id = ? AND game_id = ?", (user["id"], game_id))
            connection.execute("DELETE FROM game_word_progress WHERE user_id = ? AND game_id = ?", (user["id"], game_id))
            connection.execute("DELETE FROM word_progress WHERE user_id = ?", (user["id"],))
            rows = connection.execute(
                """
                SELECT event_id, event_type, occurred_at, game_id, word_id, sense_id, event_count
                FROM events WHERE user_id = ? AND event_type IN ('word_encountered','word_selected','word_saved','word_unsaved')
                ORDER BY occurred_at, event_id
                """, (user["id"],)
            ).fetchall()
            for row in rows:
                self._apply_word_event(connection, user["id"], {
                    "eventId": row["event_id"], "type": row["event_type"], "occurredAt": row["occurred_at"],
                    "gameId": row["game_id"], "wordId": row["word_id"], "senseId": row["sense_id"],
                    "count": row["event_count"],
                })

    def import_dictionary_entries(self, entries: list[dict[str, Any]]) -> int:
        now = isoformat(self.now())
        normalized = []
        for entry in entries:
            word_id = entry.get("wordId")
            written = entry.get("written")
            if (not isinstance(word_id, str) or not word_id or len(word_id) > 512
                    or not isinstance(written, str) or not written or len(written) > 512):
                raise ValidationError("Each dictionary entry requires wordId and written")
            values = [entry.get(key, "") for key in
                      ("senseId", "reading", "partOfSpeech", "meaning", "source", "attribution")]
            if any(not isinstance(value, str) or len(value) > 4000 for value in values):
                raise ValidationError("Dictionary entry values must be strings no longer than 4000 characters")
            normalized.append((word_id, *values[:1], written, *values[1:], now))
        with self.database.connect() as connection:
            connection.executemany(
                """
                INSERT INTO dictionary_entries VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
                ON CONFLICT(word_id, sense_id) DO UPDATE SET written=excluded.written, reading=excluded.reading,
                    part_of_speech=excluded.part_of_speech, meaning=excluded.meaning, source=excluded.source,
                    attribution=excluded.attribution, updated_at=excluded.updated_at
                """, normalized)
        return len(normalized)

    def _new_session(self, user_id: str) -> str:
        token = new_bearer_token()
        now = self.now()
        with self.database.connect() as connection:
            connection.execute(
                "INSERT INTO sessions VALUES (?, ?, ?, ?)",
                (token_hash(token), user_id, isoformat(now), isoformat(now + timedelta(days=30))),
            )
        return token

    def _validate_event(self, event: dict[str, Any]) -> dict[str, Any]:
        if not isinstance(event, dict):
            raise ValidationError("Each event must be an object")
        unexpected = sorted(set(event) - EVENT_FIELDS)
        if unexpected:
            raise ValidationError(f"Unexpected event fields: {', '.join(unexpected)}")
        event_type = event.get("type")
        if event_type not in EVENT_TYPES:
            raise ValidationError(f"Unsupported event type: {event_type}")
        normalized = {
            "eventId": require_identifier("eventId", event.get("eventId")),
            "type": event_type,
            "occurredAt": parse_timestamp(event.get("occurredAt")),
            "gameId": require_identifier("gameId", event.get("gameId")),
            "adapterId": require_identifier("adapterId", event.get("adapterId")),
            "contentVersion": require_identifier("contentVersion", event.get("contentVersion")),
            "count": event.get("count", 1),
        }
        if not isinstance(normalized["count"], int) or not 1 <= normalized["count"] <= 100_000:
            raise ValidationError("count must be an integer between 1 and 100000")

        for field in ("wordId", "senseId", "messageId", "locationId"):
            value = event.get(field)
            if value is not None:
                if not isinstance(value, str) or not value or len(value) > 512:
                    raise ValidationError(f"{field} must contain between 1 and 512 characters")
                normalized[field] = value
        page_index = event.get("pageIndex")
        if page_index is not None:
            if not isinstance(page_index, int) or isinstance(page_index, bool) or not 0 <= page_index <= 10_000:
                raise ValidationError("pageIndex must be an integer between 0 and 10000")
            normalized["pageIndex"] = page_index
        if event_type in WORD_EVENT_TYPES and "wordId" not in normalized:
            raise ValidationError(f"{event_type} requires wordId")
        return normalized

    def _apply_word_event(self, connection: sqlite3.Connection, user_id: str, event: dict[str, Any]) -> None:
        sense_id = event.get("senseId", "")
        connection.execute(
            """
            INSERT OR IGNORE INTO word_progress(
                user_id, word_id, sense_id, first_seen_at, last_seen_at
            ) VALUES (?, ?, ?, ?, ?)
            """,
            (user_id, event["wordId"], sense_id, event["occurredAt"], event["occurredAt"]),
        )
        connection.execute(
            """
            UPDATE word_progress
            SET first_seen_at = MIN(first_seen_at, ?), last_seen_at = MAX(last_seen_at, ?)
            WHERE user_id = ? AND word_id = ? AND sense_id = ?
            """,
            (
                event["occurredAt"],
                event["occurredAt"],
                user_id,
                event["wordId"],
                sense_id,
            ),
        )
        connection.execute(
            """
            INSERT OR IGNORE INTO game_word_progress(
                user_id, game_id, word_id, sense_id, first_seen_at, last_seen_at
            ) VALUES (?, ?, ?, ?, ?, ?)
            """,
            (user_id, event["gameId"], event["wordId"], sense_id, event["occurredAt"], event["occurredAt"]),
        )
        connection.execute(
            """
            UPDATE game_word_progress
            SET first_seen_at = MIN(first_seen_at, ?), last_seen_at = MAX(last_seen_at, ?)
            WHERE user_id = ? AND game_id = ? AND word_id = ? AND sense_id = ?
            """,
            (
                event["occurredAt"],
                event["occurredAt"],
                user_id,
                event["gameId"],
                event["wordId"],
                sense_id,
            ),
        )
        if event["type"] == "word_encountered":
            connection.execute(
                """
                UPDATE word_progress SET encounter_count = encounter_count + ?
                WHERE user_id = ? AND word_id = ? AND sense_id = ?
                """,
                (event["count"], user_id, event["wordId"], sense_id),
            )
            connection.execute(
                """
                UPDATE game_word_progress SET encounter_count = encounter_count + ?
                WHERE user_id = ? AND game_id = ? AND word_id = ? AND sense_id = ?
                """,
                (event["count"], user_id, event["gameId"], event["wordId"], sense_id),
            )
        elif event["type"] == "word_selected":
            connection.execute(
                """
                UPDATE word_progress SET selection_count = selection_count + ?
                WHERE user_id = ? AND word_id = ? AND sense_id = ?
                """,
                (event["count"], user_id, event["wordId"], sense_id),
            )
            connection.execute(
                """
                UPDATE game_word_progress SET selection_count = selection_count + ?
                WHERE user_id = ? AND game_id = ? AND word_id = ? AND sense_id = ?
                """,
                (event["count"], user_id, event["gameId"], event["wordId"], sense_id),
            )
        else:
            saved = 1 if event["type"] == "word_saved" else 0
            connection.execute(
                """
                UPDATE word_progress
                SET saved = ?, saved_changed_at = ?, saved_event_id = ?
                WHERE user_id = ? AND word_id = ? AND sense_id = ?
                  AND (saved_changed_at IS NULL OR saved_changed_at < ?
                       OR (saved_changed_at = ? AND COALESCE(saved_event_id, '') < ?))
                """,
                (
                    saved,
                    event["occurredAt"],
                    event["eventId"],
                    user_id,
                    event["wordId"],
                    sense_id,
                    event["occurredAt"],
                    event["occurredAt"],
                    event["eventId"],
                ),
            )

    @staticmethod
    def _validate_email(email: str) -> str:
        normalized = email.strip().lower()
        if len(normalized) > 254 or normalized.count("@") != 1:
            raise ValidationError("A valid email address is required")
        local, domain = normalized.split("@")
        if not local or "." not in domain or domain.startswith(".") or domain.endswith("."):
            raise ValidationError("A valid email address is required")
        return normalized

    @staticmethod
    def _validate_password(password: str) -> None:
        if not isinstance(password, str) or len(password) < 10 or len(password) > 256:
            raise ValidationError("Password must contain between 10 and 256 characters")

    @staticmethod
    def _public_user(user_id: str, email: str, display_name: str) -> dict[str, str]:
        return {"id": user_id, "email": email, "displayName": display_name}
