"""Start an isolated real server seeded for Playwright acceptance tests."""

from __future__ import annotations

import argparse
import os
from pathlib import Path

import uvicorn

from learning_platform.api import create_app
from learning_platform.mailer import FileMailer
from seed_browser_fixture import seed_fixture


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--database", type=Path, required=True)
    parser.add_argument("--port", type=int, default=18766)
    parser.add_argument("--mailbox", type=Path, required=True)
    args = parser.parse_args()
    args.mailbox.unlink(missing_ok=True)
    mailer = FileMailer(args.mailbox)
    public_base_url = f"http://127.0.0.1:{args.port}"
    os.environ["JP_ASSIST_PUBLIC_BASE_URL"] = public_base_url
    os.environ["JP_ASSIST_AUTH_RATE_LIMIT"] = "1000"
    seed_fixture(args.database, reset=True, mailer=mailer, public_base_url=public_base_url)
    uvicorn.run(
        create_app(database_path=args.database, mailer=mailer),
        host="127.0.0.1", port=args.port, log_level="warning",
    )


if __name__ == "__main__":
    main()
