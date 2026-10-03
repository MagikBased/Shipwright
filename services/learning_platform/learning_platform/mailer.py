from __future__ import annotations

import json
import smtplib
import threading
from dataclasses import asdict, dataclass
from email.message import EmailMessage
from pathlib import Path
from typing import Protocol


@dataclass(frozen=True)
class OutboundEmail:
    recipient: str
    subject: str
    text: str
    html: str
    category: str


class Mailer(Protocol):
    def send(self, message: OutboundEmail) -> None: ...


class MailDeliveryError(RuntimeError):
    pass


class NullMailer:
    """Explicit no-delivery transport used by service-only tests and tools."""

    def send(self, message: OutboundEmail) -> None:
        del message
        raise MailDeliveryError("Mail delivery is disabled")


class FileMailer:
    """A local JSONL mail catcher for development and browser acceptance."""

    def __init__(self, path: str | Path):
        self.path = Path(path)
        self._lock = threading.Lock()

    def send(self, message: OutboundEmail) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with self._lock:
            with self.path.open("a", encoding="utf-8") as mailbox:
                mailbox.write(json.dumps(asdict(message), ensure_ascii=False) + "\n")
            try:
                self.path.chmod(0o600)
            except OSError:
                pass


class SmtpMailer:
    """SMTP transport intended for the local Mailpit service in staging."""

    def __init__(
        self, host: str, port: int, sender: str, username: str = "", password: str = "",
        starttls: bool = False,
    ):
        self.host = host
        self.port = port
        self.sender = sender
        self.username = username
        self.password = password
        self.starttls = starttls

    def send(self, message: OutboundEmail) -> None:
        email = EmailMessage()
        email["From"] = self.sender
        email["To"] = message.recipient
        email["Subject"] = message.subject
        email["X-JP-Assist-Category"] = message.category
        email.set_content(message.text)
        email.add_alternative(message.html, subtype="html")
        with smtplib.SMTP(self.host, self.port, timeout=10) as smtp:
            if self.starttls:
                smtp.starttls()
            if self.username:
                smtp.login(self.username, self.password)
            smtp.send_message(email)


def mailer_from_settings(settings) -> Mailer:
    if settings.mail_transport == "file":
        return FileMailer(settings.mailbox_path)
    if settings.mail_transport == "smtp":
        return SmtpMailer(
            settings.smtp_host, settings.smtp_port, settings.smtp_from,
            settings.smtp_username, settings.smtp_password, settings.smtp_starttls,
        )
    return NullMailer()
