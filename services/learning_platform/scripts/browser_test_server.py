"""Start an isolated real server seeded for Playwright acceptance tests."""

from __future__ import annotations

import argparse
from pathlib import Path

import uvicorn

from learning_platform.api import create_app
from seed_browser_fixture import seed_fixture


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--database", type=Path, required=True)
    parser.add_argument("--port", type=int, default=18766)
    args = parser.parse_args()
    seed_fixture(args.database, reset=True)
    uvicorn.run(create_app(database_path=args.database), host="127.0.0.1", port=args.port, log_level="warning")


if __name__ == "__main__":
    main()
