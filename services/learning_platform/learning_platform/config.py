from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path


def _boolean(name: str, default: bool) -> bool:
    value = os.environ.get(name)
    if value is None:
        return default
    normalized = value.strip().lower()
    if normalized in {"1", "true", "yes", "on"}:
        return True
    if normalized in {"0", "false", "no", "off"}:
        return False
    raise ValueError(f"{name} must be true or false")


def _positive_integer(name: str, default: int) -> int:
    try:
        value = int(os.environ.get(name, str(default)))
    except ValueError as error:
        raise ValueError(f"{name} must be an integer") from error
    if value < 0:
        raise ValueError(f"{name} must be zero or greater")
    return value


@dataclass(frozen=True)
class Settings:
    database_path: str
    production: bool
    cookie_secure: bool
    allowed_hosts: tuple[str, ...]
    trust_proxy_headers: bool
    rate_limit_window_seconds: int
    auth_rate_limit: int
    pairing_rate_limit: int
    event_rate_limit: int
    mail_transport: str = "disabled"
    mailbox_path: str = ""
    smtp_host: str = ""
    smtp_port: int = 1025
    smtp_from: str = "JP Assist <no-reply@jp-assist.invalid>"
    smtp_username: str = ""
    smtp_password: str = ""
    smtp_starttls: bool = False
    public_base_url: str = "http://127.0.0.1:8766"

    @classmethod
    def from_environment(cls, database_path: str | Path | None = None) -> "Settings":
        root = Path(__file__).resolve().parent.parent
        production = os.environ.get("JP_ASSIST_ENV", "development").strip().lower() == "production"
        cookie_secure = _boolean("JP_ASSIST_COOKIE_SECURE", production)
        allowed_hosts = tuple(
            host.strip()
            for host in os.environ.get("JP_ASSIST_ALLOWED_HOSTS", "*").split(",")
            if host.strip()
        ) or ("*",)
        settings = cls(
            database_path=str(
                database_path
                or os.environ.get("JP_ASSIST_PLATFORM_DB", root / "var" / "platform.sqlite3")
            ),
            production=production,
            cookie_secure=cookie_secure,
            allowed_hosts=allowed_hosts,
            trust_proxy_headers=_boolean("JP_ASSIST_TRUST_PROXY_HEADERS", False),
            rate_limit_window_seconds=_positive_integer("JP_ASSIST_RATE_LIMIT_WINDOW_SECONDS", 60),
            auth_rate_limit=_positive_integer("JP_ASSIST_AUTH_RATE_LIMIT", 20),
            pairing_rate_limit=_positive_integer("JP_ASSIST_PAIRING_RATE_LIMIT", 180),
            event_rate_limit=_positive_integer("JP_ASSIST_EVENT_RATE_LIMIT", 180),
            mail_transport=os.environ.get(
                "JP_ASSIST_MAIL_TRANSPORT", "smtp" if production else "file"
            ).strip().lower(),
            mailbox_path=os.environ.get(
                "JP_ASSIST_MAILBOX_PATH", str(root / "var" / "dev-mailbox.jsonl")
            ),
            smtp_host=os.environ.get("JP_ASSIST_SMTP_HOST", "mailpit" if production else ""),
            smtp_port=_positive_integer("JP_ASSIST_SMTP_PORT", 1025),
            smtp_from=os.environ.get(
                "JP_ASSIST_SMTP_FROM", "JP Assist <no-reply@jp-assist.invalid>"
            ),
            smtp_username=os.environ.get("JP_ASSIST_SMTP_USERNAME", ""),
            smtp_password=os.environ.get("JP_ASSIST_SMTP_PASSWORD", ""),
            smtp_starttls=_boolean("JP_ASSIST_SMTP_STARTTLS", False),
            public_base_url=os.environ.get(
                "JP_ASSIST_PUBLIC_BASE_URL",
                f"https://{allowed_hosts[0]}" if production else "http://127.0.0.1:8766",
            ).rstrip("/"),
        )
        settings.validate()
        return settings

    def validate(self) -> None:
        if self.rate_limit_window_seconds == 0:
            raise ValueError("JP_ASSIST_RATE_LIMIT_WINDOW_SECONDS must be greater than zero")
        if self.production and not self.cookie_secure:
            raise ValueError("Production requires JP_ASSIST_COOKIE_SECURE=1")
        if self.production and self.allowed_hosts == ("*",):
            raise ValueError("Production requires an explicit JP_ASSIST_ALLOWED_HOSTS value")
        if self.mail_transport not in {"disabled", "file", "smtp"}:
            raise ValueError("JP_ASSIST_MAIL_TRANSPORT must be disabled, file, or smtp")
        if self.mail_transport == "file" and not self.mailbox_path:
            raise ValueError("File mail transport requires JP_ASSIST_MAILBOX_PATH")
        if self.mail_transport == "smtp" and (not self.smtp_host or self.smtp_port == 0):
            raise ValueError("SMTP mail transport requires JP_ASSIST_SMTP_HOST and a nonzero port")
        if self.production and self.mail_transport != "smtp":
            raise ValueError("Production requires JP_ASSIST_MAIL_TRANSPORT=smtp")
        if not self.public_base_url.startswith(("http://", "https://")):
            raise ValueError("JP_ASSIST_PUBLIC_BASE_URL must be an http(s) URL")
