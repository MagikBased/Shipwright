#!/usr/bin/env python3
"""Check a deployed learning platform without creating account data."""

import argparse
import json
import urllib.error
import urllib.request


def get(base_url: str, path: str) -> tuple[int, bytes, str]:
    request = urllib.request.Request(base_url.rstrip("/") + path, headers={"User-Agent": "jp-assist-smoke/1"})
    try:
        response = urllib.request.urlopen(request, timeout=10)
    except urllib.error.HTTPError as error:
        response = error
    with response:
        return response.status, response.read(), response.headers.get("Content-Type", "")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("url", help="Public service URL, such as https://learn.example.com")
    args = parser.parse_args()

    health_status, health_body, _ = get(args.url, "/healthz")
    if health_status != 200 or json.loads(health_body) != {"status": "ok"}:
        raise SystemExit(f"Liveness check failed: HTTP {health_status} {health_body!r}")

    ready_status, ready_body, _ = get(args.url, "/readyz")
    readiness = json.loads(ready_body)
    if ready_status != 200 or readiness.get("status") != "ready":
        raise SystemExit(f"Readiness check failed: HTTP {ready_status} {readiness}")

    site_status, site_body, content_type = get(args.url, "/")
    if site_status != 200 or b"JP Assist Learning" not in site_body or "text/html" not in content_type:
        raise SystemExit(f"Website check failed: HTTP {site_status} ({content_type})")

    print(
        f"Deployment smoke passed for {args.url.rstrip('/')} "
        f"(schema {readiness.get('schemaVersion')})."
    )


if __name__ == "__main__":
    main()
