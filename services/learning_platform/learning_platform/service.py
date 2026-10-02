from __future__ import annotations

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
    "locationId",
    "count",
}
IDENTIFIER_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._:/|+-]{0,127}$")


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
                        location_id, event_count, received_at
                    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
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

    def list_word_progress(self, session_token: str, saved_only: bool = False) -> list[dict[str, Any]]:
        user = self.authenticate_session(session_token)
        where_saved = "AND progress.saved = 1" if saved_only else ""
        with self.database.connect() as connection:
            rows = connection.execute(
                f"""
                SELECT progress.word_id, progress.sense_id, progress.encounter_count,
                       progress.selection_count, progress.saved, progress.first_seen_at,
                       progress.last_seen_at, GROUP_CONCAT(games.game_id) AS game_ids
                FROM word_progress AS progress
                LEFT JOIN game_word_progress AS games
                  ON games.user_id = progress.user_id
                 AND games.word_id = progress.word_id
                 AND games.sense_id = progress.sense_id
                WHERE progress.user_id = ? {where_saved}
                GROUP BY progress.user_id, progress.word_id, progress.sense_id
                ORDER BY progress.saved DESC, progress.encounter_count DESC, progress.word_id
                """,
                (user["id"],),
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
            }
            for row in rows
        ]

    def saved_word_manifest(self, session_token: str) -> dict[str, Any]:
        words = self.list_word_progress(session_token, saved_only=True)
        return {
            "schemaVersion": 1,
            "savedTokenIds": sorted({word["wordId"] for word in words}),
            "words": words,
        }

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
