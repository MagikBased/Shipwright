#!/usr/bin/env python3
"""Exercise account pairing, retry-safe sync, manifest export, and Anki generation end to end."""

from __future__ import annotations

import http.cookiejar
import importlib.util
import json
import os
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path

from validate_anki import read_notes


SCRIPTS = Path(__file__).resolve().parent
REPOSITORY = SCRIPTS.parents[1]
SERVICE_ROOT = REPOSITORY / "services" / "learning_platform"


def free_loopback_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.bind(("127.0.0.1", 0))
        return server.getsockname()[1]


def find_server_python() -> str:
    override = os.environ.get("JP_ASSIST_SERVER_PYTHON")
    candidates = [Path(override).expanduser()] if override else []
    if importlib.util.find_spec("fastapi") is not None and importlib.util.find_spec("uvicorn") is not None:
        candidates.append(Path(sys.executable))
    candidates.extend(
        (
            REPOSITORY / "venv" / "learning-platform" / "bin" / "python",
            REPOSITORY / ".venv-learning-platform" / "bin" / "python",
        )
    )
    for candidate in candidates:
        if not candidate.is_file():
            continue
        result = subprocess.run(
            [str(candidate), "-c", "import fastapi, uvicorn"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        if result.returncode == 0:
            return str(candidate)
    raise SystemExit(
        "FastAPI/Uvicorn are not installed. Create the server environment first:\n"
        "  python3 -m venv venv/learning-platform\n"
        "  venv/learning-platform/bin/pip install -r services/learning_platform/requirements.txt"
    )


class JsonClient:
    def __init__(self, base_url: str):
        self.base_url = base_url
        self.opener = urllib.request.build_opener(
            urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar())
        )

    def request(
        self,
        method: str,
        path: str,
        payload: dict | None = None,
        token: str | None = None,
        expected_status: int = 200,
    ) -> dict:
        headers = {"Accept": "application/json"}
        body = None
        if payload is not None:
            body = json.dumps(payload).encode("utf-8")
            headers["Content-Type"] = "application/json"
        if token:
            headers["Authorization"] = f"Bearer {token}"
        request = urllib.request.Request(self.base_url + path, data=body, headers=headers, method=method)
        try:
            response = self.opener.open(request, timeout=5)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            response_body = response.read()
            if response.status != expected_status:
                raise AssertionError(
                    f"{method} {path}: expected HTTP {expected_status}, got {response.status}: "
                    f"{response_body.decode('utf-8', errors='replace')}"
                )
            return json.loads(response_body) if response_body else {}


def wait_for_server(client: JsonClient, process: subprocess.Popen, log_path: Path) -> None:
    deadline = time.monotonic() + 15.0
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"Learning server exited early:\n{log_path.read_text(errors='replace')}")
        try:
            if client.request("GET", "/healthz") == {"status": "ok"}:
                return
        except (OSError, AssertionError):
            time.sleep(0.1)
    raise RuntimeError(f"Learning server did not become ready:\n{log_path.read_text(errors='replace')}")


def write_runtime_corpus(path: Path) -> None:
    def token() -> dict:
        return {
            "surface": "武器",
            "lemma": "武器",
            "reading": "ぶき",
            "dictionaryReading": "ぶき",
            "partOfSpeech": "noun",
            "meaning": "weapon; arms; ordnance",
            "senseId": "weapon",
        }

    corpus = {
        "schemaVersion": 1,
        "messages": {
            "0x1000": {
                "source": {"messageId": "0x1000"},
                "pages": [{
                    "japanese": "最初の武器。",
                    "english": "An unrelated first corpus occurrence.",
                    "tokens": [token()],
                }],
            },
            "0x1034": {
                "source": {"messageId": "0x1034"},
                "pages": [{
                    "japanese": "武器はあったほうがいい。",
                    "english": "You'd better find a weapon!",
                    "tokens": [token()],
                }],
            },
        },
    }
    path.write_text(json.dumps(corpus, ensure_ascii=False), encoding="utf-8")


def run_export(manifest: Path, runtime: Path, out_dir: Path, prefix: str) -> Path:
    subprocess.run(
        [
            sys.executable,
            "-u",
            str(SCRIPTS / "export_saved_deck.py"),
            str(manifest),
            "--runtime-data",
            str(runtime),
            "--out-dir",
            str(out_dir),
            "--output-prefix",
            prefix,
            "--deck-name",
            "JP Assist MVP Acceptance",
        ],
        check=True,
    )
    return out_dir / f"{prefix}.apkg"


def main() -> None:
    if importlib.util.find_spec("genanki") is None:
        raise SystemExit(
            "genanki is not installed. Run: pip install -r scripts/jp_assist/requirements.txt"
        )
    server_python = find_server_python()
    with tempfile.TemporaryDirectory(prefix="jp-assist-mvp-") as temporary:
        root = Path(temporary)
        port = free_loopback_port()
        base_url = f"http://127.0.0.1:{port}"
        log_path = root / "server.log"
        environment = os.environ.copy()
        environment["PYTHONPATH"] = str(SERVICE_ROOT)
        environment["JP_ASSIST_PLATFORM_DB"] = str(root / "platform.sqlite3")
        environment.pop("JP_ASSIST_COOKIE_SECURE", None)

        with log_path.open("w", encoding="utf-8") as server_log:
            server = subprocess.Popen(
                [
                    server_python,
                    "-m",
                    "uvicorn",
                    "learning_platform.main:app",
                    "--host",
                    "127.0.0.1",
                    "--port",
                    str(port),
                    "--log-level",
                    "warning",
                ],
                cwd=REPOSITORY,
                env=environment,
                stdout=server_log,
                stderr=subprocess.STDOUT,
            )
            client = JsonClient(base_url)
            try:
                wait_for_server(client, server, log_path)
                client.request(
                    "POST",
                    "/v1/auth/register",
                    {"email": "acceptance@example.com", "password": "correct horse battery", "displayName": "MVP"},
                    expected_status=201,
                )
                pairing = client.request(
                    "POST",
                    "/v1/device-pairings",
                    {"deviceName": "Acceptance PC", "adapterId": "ship-of-harkinian", "gameId": "ocarina-of-time"},
                    expected_status=201,
                )
                pending = client.request(
                    "POST", "/v1/device-pairings/token", {"deviceCode": pairing["deviceCode"]}, expected_status=428
                )
                assert pending["error"]["code"] == "authorization_pending"
                client.request("POST", "/v1/device-pairings/approve", {"userCode": pairing["userCode"]})
                device = client.request(
                    "POST", "/v1/device-pairings/token", {"deviceCode": pairing["deviceCode"]}
                )

                events = {
                    "events": [
                        {
                            "eventId": "acceptance-encounter",
                            "type": "word_encountered",
                            "occurredAt": "2026-10-02T12:00:00Z",
                            "gameId": "ocarina-of-time",
                            "adapterId": "ship-of-harkinian",
                            "contentVersion": "acceptance-v1",
                            "wordId": "武器|ぶき",
                            "senseId": "weapon",
                            "messageId": "0x1034",
                            "pageIndex": 0,
                        },
                        {
                            "eventId": "acceptance-save",
                            "type": "word_saved",
                            "occurredAt": "2026-10-02T12:00:01Z",
                            "gameId": "ocarina-of-time",
                            "adapterId": "ship-of-harkinian",
                            "contentVersion": "acceptance-v1",
                            "wordId": "武器|ぶき",
                            "senseId": "weapon",
                            "messageId": "0x1034",
                            "pageIndex": 0,
                        },
                    ]
                }
                first = client.request("POST", "/v1/events/batch", events, device["deviceToken"])
                assert set(first["acceptedEventIds"]) == {"acceptance-encounter", "acceptance-save"}
                retry = client.request("POST", "/v1/events/batch", events, device["deviceToken"])
                assert set(retry["duplicateEventIds"]) == {"acceptance-encounter", "acceptance-save"}

                stats = client.request("GET", "/v1/me/stats")
                assert stats["uniqueWords"] == 1 and stats["savedWords"] == 1 and stats["encounters"] == 1
                manifest_data = client.request("GET", "/v1/me/exports/saved-words")
                assert manifest_data["words"][0]["contextMessageId"] == "0x1034"
                manifest = root / "jp_assist_cloud_progress.json"
                manifest.write_text(json.dumps(manifest_data, ensure_ascii=False), encoding="utf-8")

                runtime = root / "runtime_data.json"
                write_runtime_corpus(runtime)
                out_dir = root / "out"
                first_package = run_export(manifest, runtime, out_dir, "acceptance_first")
                second_package = run_export(manifest, runtime, out_dir, "acceptance_second")
                subprocess.run(
                    [
                        sys.executable,
                        "-u",
                        str(SCRIPTS / "validate_anki.py"),
                        str(first_package),
                        "--compare",
                        str(second_package),
                        "--expected-notes",
                        "1",
                    ],
                    check=True,
                )
                notes = read_notes(first_package)
                fields = next(iter(notes.values()))
                assert fields[5] == "武器はあったほうがいい。"
                assert fields[6] == "You'd better find a weapon!"
            finally:
                server.terminate()
                try:
                    server.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait(timeout=5)

    print("MVP acceptance passed: pairing, retry-safe sync, contextual export, and stable Anki IDs.")


if __name__ == "__main__":
    main()
