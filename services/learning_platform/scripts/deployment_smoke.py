#!/usr/bin/env python3
"""Check a deployed learning platform without creating account data."""

import argparse
import json
import ssl
import urllib.error
import urllib.request


def get(base_url: str, path: str, context: ssl.SSLContext | None = None) -> tuple[int, bytes, object]:
    request = urllib.request.Request(base_url.rstrip("/") + path, headers={"User-Agent": "jp-assist-smoke/1"})
    try:
        response = urllib.request.urlopen(request, timeout=10, context=context)
    except urllib.error.HTTPError as error:
        response = error
    with response:
        return response.status, response.read(), response.headers


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("url", help="Public service URL, such as https://learn.example.com")
    parser.add_argument(
        "--production",
        action="store_true",
        help="also require production-only browser policy and disabled API documentation",
    )
    parser.add_argument(
        "--insecure",
        action="store_true",
        help="accept an untrusted TLS certificate (local staging only)",
    )
    args = parser.parse_args()
    context = ssl._create_unverified_context() if args.insecure else None

    health_status, health_body, _ = get(args.url, "/healthz", context)
    if health_status != 200 or json.loads(health_body) != {"status": "ok"}:
        raise SystemExit(f"Liveness check failed: HTTP {health_status} {health_body!r}")

    ready_status, ready_body, _ = get(args.url, "/readyz", context)
    readiness = json.loads(ready_body)
    if ready_status != 200 or readiness.get("status") != "ready":
        raise SystemExit(f"Readiness check failed: HTTP {ready_status} {readiness}")

    site_status, site_body, site_headers = get(args.url, "/", context)
    content_type = site_headers.get("Content-Type", "")
    if site_status != 200 or b"JP Assist Learning" not in site_body or "text/html" not in content_type:
        raise SystemExit(f"Website check failed: HTTP {site_status} ({content_type})")

    if args.production:
        required_headers = {
            "Content-Security-Policy": "default-src 'self'",
            "Referrer-Policy": "no-referrer",
            "X-Content-Type-Options": "nosniff",
            "X-Frame-Options": "DENY",
            "Cross-Origin-Opener-Policy": "same-origin",
            "Permissions-Policy": "camera=(), microphone=(), geolocation=()",
        }
        for name, expected in required_headers.items():
            actual = site_headers.get(name, "")
            if expected not in actual:
                raise SystemExit(f"Browser policy check failed: {name}={actual!r}")
        for path in ("/docs", "/redoc", "/openapi.json"):
            status, _, _ = get(args.url, path, context)
            if status != 404:
                raise SystemExit(f"Production endpoint check failed: {path} returned HTTP {status}")

    print(
        f"Deployment smoke passed for {args.url.rstrip('/')} "
        f"(schema {readiness.get('schemaVersion')}, "
        f"mode {'production' if args.production else 'standard'})."
    )


if __name__ == "__main__":
    main()
