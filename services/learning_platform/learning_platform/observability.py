from __future__ import annotations

import json
import logging
import re
import time
import uuid
from contextvars import ContextVar, Token
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from prometheus_client import CONTENT_TYPE_LATEST, CollectorRegistry, Counter, Gauge, Histogram, generate_latest


REQUEST_ID_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$")
_current_observability: ContextVar[Observability | None] = ContextVar(
    "jp_assist_observability", default=None,
)
_current_request_id: ContextVar[str] = ContextVar("jp_assist_request_id", default="")


def _structured_logger(name: str) -> logging.Logger:
    logger = logging.getLogger(name)
    if not getattr(logger, "_jp_assist_configured", False):
        handler = logging.StreamHandler()
        handler.setFormatter(logging.Formatter("%(message)s"))
        logger.handlers = [handler]
        logger.propagate = False
        logger.setLevel(logging.INFO)
        logger._jp_assist_configured = True  # type: ignore[attr-defined]
    return logger


def _timestamp() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def _emit(logger: logging.Logger, event: str, **fields: Any) -> None:
    logger.info(json.dumps(
        {"timestamp": _timestamp(), "event": event, **fields},
        sort_keys=True,
        separators=(",", ":"),
    ))


class Observability:
    def __init__(self, database_path: str | Path, backup_directory: str | Path, structured_logs: bool):
        self.database_path = Path(database_path)
        self.backup_directory = Path(backup_directory) if backup_directory else None
        self.structured_logs = structured_logs
        self.request_logger = _structured_logger("jp_assist.request")
        self.audit_logger = _structured_logger("jp_assist.audit")
        self.registry = CollectorRegistry(auto_describe=True)
        self.http_requests = Counter(
            "jp_assist_http_requests_total", "Completed HTTP requests",
            ("method", "route", "status"), registry=self.registry,
        )
        self.http_duration = Histogram(
            "jp_assist_http_request_duration_seconds", "HTTP request duration",
            ("method", "route"), registry=self.registry,
        )
        self.http_in_flight = Gauge(
            "jp_assist_http_requests_in_flight", "HTTP requests currently in flight",
            registry=self.registry,
        )
        self.events_ingested = Counter(
            "jp_assist_events_ingested_total", "Device events accepted by the service",
            registry=self.registry,
        )
        self.events_duplicate = Counter(
            "jp_assist_events_duplicate_total", "Duplicate device events ignored by the service",
            registry=self.registry,
        )
        self.audit_events = Counter(
            "jp_assist_audit_events_total", "Account audit events recorded",
            ("type",), registry=self.registry,
        )
        self.ready = Gauge(
            "jp_assist_ready", "Whether the database readiness check passes",
            registry=self.registry,
        )
        self.database_bytes = Gauge(
            "jp_assist_database_bytes", "SQLite database and live sidecar size in bytes",
            registry=self.registry,
        )
        self.events_stored = Gauge(
            "jp_assist_events_stored", "Device events currently retained in SQLite",
            registry=self.registry,
        )
        self.mail_failures = Gauge(
            "jp_assist_mail_delivery_failures", "Retained mail delivery failure audit events",
            registry=self.registry,
        )
        self.backup_files = Gauge(
            "jp_assist_backup_files", "SQLite backup files in the configured backup directory",
            registry=self.registry,
        )
        self.backup_latest_timestamp = Gauge(
            "jp_assist_backup_latest_timestamp_seconds",
            "Modification time of the newest SQLite backup, or zero when none exists",
            registry=self.registry,
        )

    @staticmethod
    def request_id(candidate: str | None) -> str:
        if candidate and REQUEST_ID_PATTERN.fullmatch(candidate):
            return candidate
        return str(uuid.uuid4())

    def bind(self, request_id: str) -> tuple[Token, Token]:
        return _current_observability.set(self), _current_request_id.set(request_id)

    @staticmethod
    def reset(tokens: tuple[Token, Token]) -> None:
        _current_observability.reset(tokens[0])
        _current_request_id.reset(tokens[1])

    def record_request(
        self, request_id: str, method: str, route: str, status: int, duration_seconds: float,
    ) -> None:
        status_text = str(status)
        self.http_requests.labels(method, route, status_text).inc()
        self.http_duration.labels(method, route).observe(duration_seconds)
        if self.structured_logs:
            _emit(
                self.request_logger, "http_request", requestId=request_id, method=method,
                route=route, status=status, durationMs=round(duration_seconds * 1000, 3),
            )

    def record_event_batch(self, accepted: int, duplicates: int) -> None:
        self.events_ingested.inc(accepted)
        self.events_duplicate.inc(duplicates)

    def record_audit(self, event_type: str, metadata: dict[str, Any]) -> None:
        self.audit_events.labels(event_type).inc()
        if self.structured_logs:
            _emit(
                self.audit_logger, "account_audit", requestId=_current_request_id.get(),
                auditType=event_type, metadataKeys=sorted(metadata),
            )

    def render_metrics(self, platform: Any) -> bytes:
        ready, _ = platform.database.readiness()
        self.ready.set(1 if ready else 0)
        self.database_bytes.set(sum(
            path.stat().st_size for path in (
                self.database_path,
                Path(str(self.database_path) + "-wal"),
                Path(str(self.database_path) + "-shm"),
            ) if path.is_file()
        ))
        with platform.database.connect() as connection:
            self.events_stored.set(connection.execute("SELECT COUNT(*) FROM events").fetchone()[0])
            self.mail_failures.set(connection.execute(
                """SELECT COUNT(*) FROM audit_events
                   WHERE event_type LIKE '%delivery_failed'
                      OR event_type = 'review_reminder_failed'"""
            ).fetchone()[0])
        backups = list(self.backup_directory.glob("*.sqlite3")) if self.backup_directory else []
        self.backup_files.set(len(backups))
        self.backup_latest_timestamp.set(max((path.stat().st_mtime for path in backups), default=0))
        return generate_latest(self.registry)


def current_request_id() -> str:
    return _current_request_id.get()


def record_audit(event_type: str, metadata: dict[str, Any]) -> None:
    observability = _current_observability.get()
    if observability is not None:
        observability.record_audit(event_type, metadata)


def monotonic_time() -> float:
    return time.perf_counter()


METRICS_CONTENT_TYPE = CONTENT_TYPE_LATEST
