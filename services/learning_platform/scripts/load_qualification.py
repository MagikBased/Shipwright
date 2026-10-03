#!/usr/bin/env python3
"""Exercise a large synthetic account and concurrent device uploads over HTTP."""

from __future__ import annotations

import argparse
import json
import math
import os
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from learning_platform.service import LearningPlatform


def percentile(values: list[float], percentile_value: float) -> float:
    ordered = sorted(values)
    return ordered[max(0, math.ceil(len(ordered) * percentile_value) - 1)]


def free_port() -> int:
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


def request_json(
    base_url: str,
    path: str,
    method: str = "GET",
    body: dict[str, Any] | None = None,
    token: str = "",
) -> tuple[dict[str, Any] | list[Any], float]:
    headers = {"User-Agent": "jp-assist-load-qualification/1"}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    payload = None
    if body is not None:
        payload = json.dumps(body, separators=(",", ":")).encode("utf-8")
        headers["Content-Type"] = "application/json"
    started = time.perf_counter()
    response = urllib.request.urlopen(
        urllib.request.Request(base_url + path, data=payload, headers=headers, method=method),
        timeout=30,
    )
    with response:
        parsed = json.loads(response.read())
    return parsed, time.perf_counter() - started


def pair_devices(
    platform: LearningPlatform, session_token: str, device_count: int,
) -> list[dict[str, Any]]:
    devices = []
    for index in range(device_count):
        pairing = platform.start_pairing(
            f"Synthetic load device {index + 1}", "load-adapter", "load-game",
        )
        platform.approve_pairing(session_token, pairing["userCode"])
        devices.append(platform.claim_pairing(pairing["deviceCode"]))
    return devices


def event_batch(device_index: int, start: int, count: int) -> list[dict[str, Any]]:
    occurred_at = "2026-10-03T12:00:00.000Z"
    return [
        {
            "eventId": f"load-{device_index:03d}-{number:07d}",
            "type": "word_encountered",
            "occurredAt": occurred_at,
            "gameId": "load-game",
            "adapterId": "load-adapter",
            "contentVersion": "synthetic-load-v1",
            "wordId": f"synthetic-load-{device_index:03d}-{number:07d}|reading-{number:07d}",
            "senseId": "synthetic-sense",
            "count": 1,
        }
        for number in range(start, start + count)
    ]


def process_peak_rss_bytes(process_id: int) -> int:
    try:
        status = Path(f"/proc/{process_id}/status").read_text(encoding="utf-8")
    except OSError:
        return 0
    for line in status.splitlines():
        if line.startswith("VmHWM:"):
            return int(line.split()[1]) * 1024
    return 0


def run(args: argparse.Namespace) -> dict[str, Any]:
    with tempfile.TemporaryDirectory(prefix="jp-assist-load-") as temporary:
        root = Path(temporary)
        database_path = root / "platform.sqlite3"
        platform = LearningPlatform(database_path)
        account = platform.register_user(
            "load@example.test", "synthetic load password", "Synthetic Load Account",
        )
        session_token = account["token"]
        devices = pair_devices(platform, session_token, args.devices)
        dictionary_entries = []
        for device_index in range(args.devices):
            for number in range(args.events_per_device):
                word_id = f"synthetic-load-{device_index:03d}-{number:07d}|reading-{number:07d}"
                meaning = f"content-neutral load-test definition {device_index}-{number}"
                if device_index == 0 and number == 0:
                    meaning = '<script>window.__jpAssistInjected=true</script> pathological load marker'
                dictionary_entries.append({
                    "wordId": word_id,
                    "senseId": "synthetic-sense",
                    "written": f"Synthetic {device_index}-{number}",
                    "reading": f"reading-{number:07d}",
                    "partOfSpeech": "test fixture",
                    "meaning": meaning,
                    "source": "synthetic-load-test",
                    "attribution": "Content-neutral generated qualification data",
                })
        platform.import_dictionary_entries(dictionary_entries)

        port = free_port()
        base_url = f"http://127.0.0.1:{port}"
        service_root = Path(__file__).resolve().parent.parent
        environment = os.environ.copy()
        environment.update({
            "PYTHONPATH": str(service_root),
            "JP_ASSIST_PLATFORM_DB": str(database_path),
            "JP_ASSIST_MAIL_TRANSPORT": "disabled",
        })
        process = subprocess.Popen(
            [sys.executable, "-m", "uvicorn", "learning_platform.main:app", "--host", "127.0.0.1",
             "--port", str(port), "--workers", "1", "--no-access-log"],
            cwd=service_root,
            env=environment,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
        )
        try:
            for _ in range(100):
                try:
                    request_json(base_url, "/readyz")
                    break
                except (OSError, urllib.error.URLError):
                    if process.poll() is not None:
                        raise RuntimeError(f"Server exited early: {process.stderr.read()}")
                    time.sleep(0.05)
            else:
                raise RuntimeError("Server did not become ready")

            batches: list[tuple[int, str, list[dict[str, Any]]]] = []
            retry_items: list[tuple[int, str, list[dict[str, Any]]]] = []
            for device_index, device in enumerate(devices):
                device_batches = []
                for start in range(0, args.events_per_device, args.batch_size):
                    count = min(args.batch_size, args.events_per_device - start)
                    item = (
                        device_index,
                        device["deviceToken"],
                        event_batch(device_index, start, count),
                    )
                    batches.append(item)
                    device_batches.append(item)
                retry_items.append(device_batches[-1])

            ingest_latencies: list[float] = []
            accepted = 0
            failures: list[str] = []

            def upload(item: tuple[int, str, list[dict[str, Any]]]):
                _, token, events = item
                return request_json(base_url, "/v1/events/batch", "POST", {"events": events}, token)

            started = time.perf_counter()
            with ThreadPoolExecutor(max_workers=args.devices) as executor:
                future_items = {executor.submit(upload, item): item for item in batches}
                for future in as_completed(future_items):
                    try:
                        result, duration = future.result()
                        accepted += len(result["acceptedEventIds"])
                        ingest_latencies.append(duration)
                    except Exception as error:  # reported in the qualification artifact
                        failures.append(f"{type(error).__name__}: {error}")
            ingest_wall_seconds = time.perf_counter() - started

            duplicate_count = 0
            retry_latencies = []
            with ThreadPoolExecutor(max_workers=args.devices) as executor:
                retries = [executor.submit(upload, item) for item in retry_items]
                for future in as_completed(retries):
                    result, duration = future.result()
                    duplicate_count += len(result["duplicateEventIds"])
                    retry_latencies.append(duration)

            query_paths = [
                "/v1/me/stats",
                "/v1/me/activity?days=90",
                "/v1/me/words?limit=250&offset=0&sort=frequency",
                f"/v1/me/words?limit=250&offset={max(0, accepted - 250)}&sort=alphabetical",
            ] * args.query_rounds
            query_latencies = []
            with ThreadPoolExecutor(max_workers=min(8, len(query_paths))) as executor:
                queries = [
                    executor.submit(request_json, base_url, path, "GET", None, session_token)
                    for path in query_paths
                ]
                for future in as_completed(queries):
                    _, duration = future.result()
                    query_latencies.append(duration)

            stats, _ = request_json(base_url, "/v1/me/stats", token=session_token)
            database_bytes = sum(
                path.stat().st_size
                for path in (database_path, Path(f"{database_path}-wal"), Path(f"{database_path}-shm"))
                if path.exists()
            )
            report = {
                "configuration": {
                    "devices": args.devices,
                    "eventsPerDevice": args.events_per_device,
                    "batchSize": args.batch_size,
                    "queryRequests": len(query_paths),
                    "dictionaryEntries": len(dictionary_entries),
                },
                "results": {
                    "acceptedEvents": accepted,
                    "uniqueWords": stats["uniqueWords"],
                    "duplicateRetryEvents": duplicate_count,
                    "failures": failures,
                    "ingestWallSeconds": round(ingest_wall_seconds, 3),
                    "ingestRequestsPerSecond": round(len(batches) / ingest_wall_seconds, 3),
                    "ingestP95Milliseconds": round(percentile(ingest_latencies, 0.95) * 1000, 3),
                    "retryP95Milliseconds": round(percentile(retry_latencies, 0.95) * 1000, 3),
                    "queryP95Milliseconds": round(percentile(query_latencies, 0.95) * 1000, 3),
                    "databaseBytes": database_bytes,
                    "serverPeakRssBytes": process_peak_rss_bytes(process.pid),
                },
                "limits": {
                    "maximumIngestP95Milliseconds": args.max_ingest_p95_ms,
                    "maximumQueryP95Milliseconds": args.max_query_p95_ms,
                    "maximumDatabaseBytes": args.max_database_mb * 1024 * 1024,
                    "maximumServerPeakRssBytes": args.max_rss_mb * 1024 * 1024,
                },
            }
            expected = args.devices * args.events_per_device
            violations = []
            results = report["results"]
            if failures:
                violations.append(f"{len(failures)} upload request(s) failed")
            if accepted != expected or results["uniqueWords"] != expected:
                violations.append(f"expected {expected} accepted events/words")
            if duplicate_count != sum(len(item[2]) for item in retry_items):
                violations.append("retry deduplication count did not match")
            if results["ingestP95Milliseconds"] > args.max_ingest_p95_ms:
                violations.append("ingest p95 exceeded limit")
            if results["queryP95Milliseconds"] > args.max_query_p95_ms:
                violations.append("query p95 exceeded limit")
            if database_bytes > report["limits"]["maximumDatabaseBytes"]:
                violations.append("database size exceeded limit")
            if results["serverPeakRssBytes"] > report["limits"]["maximumServerPeakRssBytes"]:
                violations.append("server peak RSS exceeded limit")
            report["passed"] = not violations
            report["violations"] = violations
            return report
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--devices", type=int, default=8)
    parser.add_argument("--events-per-device", type=int, default=1000)
    parser.add_argument("--batch-size", type=int, default=50)
    parser.add_argument("--query-rounds", type=int, default=8)
    parser.add_argument("--max-ingest-p95-ms", type=float, default=2000)
    parser.add_argument("--max-query-p95-ms", type=float, default=1500)
    parser.add_argument("--max-database-mb", type=float, default=256)
    parser.add_argument("--max-rss-mb", type=float, default=512)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if (
        args.devices < 1 or not 1 <= args.batch_size <= 250
        or args.events_per_device < 1 or args.query_rounds < 1
    ):
        parser.error(
            "devices, events, and query-rounds must be positive; "
            "batch-size must be between 1 and 250"
        )
    report = run(args)
    rendered = json.dumps(report, indent=2, sort_keys=True)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)
    if not report["passed"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
