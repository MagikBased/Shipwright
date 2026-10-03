"""Create a deterministic, content-neutral account for browser acceptance tests."""

from __future__ import annotations

import argparse
from datetime import datetime, timedelta, timezone
from pathlib import Path

from learning_platform.service import LearningPlatform, isoformat
from learning_platform.mailer import Mailer


EMAIL = "learner@example.test"
PASSWORD = "correct horse battery"
DISPLAY_NAME = "Synthetic Learner"


def _remove_database(path: Path) -> None:
    for candidate in (path, Path(f"{path}-wal"), Path(f"{path}-shm")):
        if candidate.exists():
            candidate.unlink()


def _pair(platform: LearningPlatform, session: str, name: str, adapter: str, game: str) -> dict:
    pairing = platform.start_pairing(name, adapter, game)
    platform.approve_pairing(session, pairing["userCode"])
    return platform.claim_pairing(pairing["deviceCode"])


def seed_fixture(
    database_path: Path, reset: bool = False, mailer: Mailer | None = None,
    public_base_url: str = "http://127.0.0.1:18766",
) -> dict[str, str]:
    database_path = database_path.resolve()
    if reset:
        _remove_database(database_path)
    platform = LearningPlatform(database_path, mailer=mailer, public_base_url=public_base_url)
    account = platform.register_user(EMAIL, PASSWORD, DISPLAY_NAME)
    session = account["token"]
    game_a = _pair(platform, session, "Synthetic desktop", "test-adapter-a", "test-adventure-a")
    game_b = _pair(platform, session, "Synthetic handheld", "test-adapter-b", "test-adventure-b")
    revoked = _pair(platform, session, "Retired test device", "test-adapter-c", "test-adventure-c")
    platform.revoke_device(session, revoked["deviceId"])

    now = datetime.now(timezone.utc)
    vocabulary = [
        ("森|もり", "sense:forest", "森", "もり", "noun", "forest; woods", 14, True, game_a),
        ("生|せい", "sense:life", "生", "せい", "noun", "life; living", 8, True, game_a),
        ("生|なま", "sense:raw", "生", "なま", "noun", "raw; fresh; unprocessed", 6, True, game_b),
        ("冒険|ぼうけん", "sense:adventure", "冒険", "ぼうけん", "noun", "an unusual and exciting experience involving exploration, uncertainty, and discovery", 5, True, game_b),
        ("謎|なぞ", "sense:mystery", "謎", "なぞ", "noun", "mystery; puzzle; riddle", 3, False, game_a),
        ("未収録|みしゅうろく", "sense:missing", "未収録", "みしゅうろく", "", "", 2, True, game_b),
        ("橋|はし", "sense:bridge", "橋", "はし", "noun", "bridge", 4, True, game_a),
        ("橋|はし", "sense:span", "橋", "はし", "noun", "a spanning structure used as a crossing", 2, True, game_b),
        (
            "危険表示|きけんひょうじ", "sense:hostile-metadata",
            '<img src=x onerror="window.__jpAssistInjected=true">', "きけんひょうじ",
            "test fixture", '<script>window.__jpAssistInjected=true</script> unsafe marker',
            1, False, game_a,
        ),
    ]
    vocabulary.extend(
        (
            f"試験語{number:02d}|しけんご{number:02d}", f"sense:fixture-{number:02d}",
            f"試験語{number:02d}", f"しけんご{number:02d}", "noun",
            f"synthetic vocabulary item {number:02d}", (number % 5) + 1, False,
            game_a if number % 2 else game_b,
        )
        for number in range(1, 61)
    )
    dictionary_entries = []
    for index, (word_id, sense_id, written, reading, part, meaning, count, saved, device) in enumerate(vocabulary):
        occurred = isoformat(now - timedelta(days=index + 1))
        game = device["gameId"]
        adapter = device["adapterId"]
        events = [{
            "eventId": f"fixture-encounter-{index}", "type": "word_encountered", "occurredAt": occurred,
            "gameId": game, "adapterId": adapter, "contentVersion": "synthetic-v1",
            "wordId": word_id, "senseId": sense_id, "count": count,
        }, {
            "eventId": f"fixture-select-{index}", "type": "word_selected", "occurredAt": occurred,
            "gameId": game, "adapterId": adapter, "contentVersion": "synthetic-v1",
            "wordId": word_id, "senseId": sense_id, "count": max(1, count // 3),
        }]
        if saved:
            events.append({
                "eventId": f"fixture-save-{index}", "type": "word_saved", "occurredAt": occurred,
                "gameId": game, "adapterId": adapter, "contentVersion": "synthetic-v1",
                "wordId": word_id, "senseId": sense_id, "messageId": f"synthetic-{index}", "pageIndex": 0,
            })
        platform.ingest_events(device["deviceToken"], events)
        if meaning:
            dictionary_entries.append({
                "wordId": word_id, "senseId": sense_id, "written": written, "reading": reading,
                "partOfSpeech": part, "meaning": meaning, "source": "synthetic-test-data",
                "attribution": "Hand-authored test fixture",
            })
    platform.import_dictionary_entries(dictionary_entries)
    platform.update_word_annotation(session, "森|もり", "sense:forest", "learning", "Seen near the beginning", ["nature", "favorite"])
    platform.update_word_annotation(session, "謎|なぞ", "sense:mystery", "known", "", ["puzzle"])
    platform.update_word_annotation(session, "未収録|みしゅうろく", "sense:missing", "ignored", "Missing definition fixture", [])
    platform.update_goals(session, 7, 18, True)
    platform.submit_review(session, "生|せい", "sense:life", 3)
    platform.login(EMAIL, PASSWORD)  # A second website session exercises revocation UI.
    return {"email": EMAIL, "password": PASSWORD, "displayName": DISPLAY_NAME}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--database", type=Path, required=True)
    parser.add_argument("--reset", action="store_true", help="Delete only the explicitly supplied test database first")
    args = parser.parse_args()
    credentials = seed_fixture(args.database, args.reset)
    print(f"Seeded {credentials['email']} in {args.database.resolve()}")


if __name__ == "__main__":
    main()
