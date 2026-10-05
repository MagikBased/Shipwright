import io
import sqlite3
import tempfile
import unittest
import zipfile
from pathlib import Path

from learning_platform.anki_export import stable_note_guid
from learning_platform.database import LATEST_SCHEMA_VERSION

try:
    from fastapi.testclient import TestClient
    from learning_platform.api import create_app
    from learning_platform.config import Settings
    from learning_platform.rate_limit import SlidingWindowRateLimiter
except ImportError:  # Core service tests remain runnable without web dependencies.
    TestClient = None
    create_app = None
    Settings = None
    SlidingWindowRateLimiter = None


@unittest.skipIf(TestClient is None, "FastAPI development dependencies are not installed")
class LearningPlatformApiTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="jp-assist-platform-api-")
        settings = Settings(
            database_path=str(Path(self.temporary.name) / "api.sqlite3"),
            production=False,
            cookie_secure=False,
            allowed_hosts=("*",),
            trust_proxy_headers=False,
            rate_limit_window_seconds=60,
            auth_rate_limit=20,
            pairing_rate_limit=180,
            event_rate_limit=180,
        )
        app = create_app(settings=settings)
        self.client = TestClient(app)

    def tearDown(self):
        self.client.close()
        self.temporary.cleanup()

    def csrf_headers(self):
        return {"X-CSRF-Token": self.client.cookies["jp_assist_csrf"]}

    def test_complete_browser_pairing_and_mod_event_flow(self):
        response = self.client.post(
            "/v1/auth/register",
            json={"email": "player@example.com", "password": "correct horse battery", "displayName": "Player"},
        )
        self.assertEqual(response.status_code, 201)
        self.assertIn("jp_assist_session", self.client.cookies)

        pairing = self.client.post(
            "/v1/device-pairings",
            json={"deviceName": "Test PC", "adapterId": "ship-of-harkinian", "gameId": "ocarina-of-time"},
        ).json()
        pending = self.client.post("/v1/device-pairings/token", json={"deviceCode": pairing["deviceCode"]})
        self.assertEqual(pending.status_code, 428)
        self.assertEqual(pending.json()["error"]["code"], "authorization_pending")

        approved = self.client.post(
            "/v1/device-pairings/approve", json={"userCode": pairing["userCode"]},
            headers=self.csrf_headers(),
        )
        self.assertEqual(approved.status_code, 200)
        device = self.client.post("/v1/device-pairings/token", json={"deviceCode": pairing["deviceCode"]}).json()

        event = {
            "eventId": "evt-api-1",
            "type": "word_saved",
            "occurredAt": "2026-10-02T12:00:00Z",
            "gameId": "ocarina-of-time",
            "adapterId": "ship-of-harkinian",
            "contentVersion": "n64-ntsc-1.2-v1",
            "wordId": "武器|ぶき",
            "senseId": "weapon",
            "messageId": "0x1034",
            "pageIndex": 2,
        }
        ingested = self.client.post(
            "/v1/events/batch",
            json={"events": [event]},
            headers={"Authorization": f"Bearer {device['deviceToken']}"},
        )
        self.assertEqual(ingested.status_code, 200)
        self.assertEqual(ingested.json()["acceptedEventIds"], ["evt-api-1"])

        stats = self.client.get("/v1/me/stats").json()
        self.assertEqual(stats["savedWords"], 1)
        sessions = self.client.get("/v1/me/sessions").json()
        self.assertEqual(sessions[0]["label"], "testclient")
        self.assertNotIn("token", sessions[0])
        self.assertEqual(len(sessions[0]["id"]), 36)
        manifest = self.client.get("/v1/me/exports/saved-words").json()
        self.assertEqual(manifest["savedTokenIds"], ["武器|ぶき"])
        self.assertEqual(manifest["words"][0]["contextMessageId"], "0x1034")
        self.assertEqual(manifest["words"][0]["contextPageIndex"], 2)

    def test_api_rejects_uncontracted_dialogue_text(self):
        response = self.client.post(
            "/v1/events/batch",
            json={
                "events": [{
                    "eventId": "evt-api-private",
                    "type": "dialogue_seen",
                    "occurredAt": "2026-10-02T12:00:00Z",
                    "gameId": "ocarina-of-time",
                    "adapterId": "ship-of-harkinian",
                    "contentVersion": "n64-ntsc-1.2-v1",
                    "japaneseText": "ゲームの台詞",
                }]
            },
            headers={"Authorization": "Bearer invalid"},
        )
        self.assertEqual(response.status_code, 422)

    def test_website_and_health_are_served(self):
        self.assertEqual(self.client.get("/healthz").json(), {"status": "ok"})
        readiness = self.client.get("/readyz")
        self.assertEqual(readiness.status_code, 200)
        self.assertEqual(readiness.json()["status"], "ready")
        self.assertEqual(readiness.json()["schemaVersion"], LATEST_SCHEMA_VERSION)
        page = self.client.get("/")
        self.assertEqual(page.status_code, 200)
        self.assertIn("Learn from every adventure", page.text)
        self.assertIn("default-src 'self'", page.headers["Content-Security-Policy"])
        self.assertEqual(page.headers["Referrer-Policy"], "no-referrer")
        self.assertEqual(page.headers["X-Content-Type-Options"], "nosniff")

        catalog_page = self.client.get("/catalog")
        self.assertEqual(catalog_page.status_code, 200)
        self.assertIn("Game catalog", catalog_page.text)
        self.assertIn('id="catalog-tab" href="/catalog"', page.text)

    def test_public_catalog_exposes_ordered_oot_chapters_and_ready_decks(self):
        listing = self.client.get("/v1/catalog/games")
        self.assertEqual(listing.status_code, 200)
        self.assertEqual(listing.json()["games"][0]["id"], "ocarina-of-time")
        self.assertTrue(
            listing.json()["games"][0]["artwork"]["heroImage"].endswith(
                "/ocarina-of-time/hero.svg"
            )
        )
        artwork = self.client.get(listing.json()["games"][0]["artwork"]["heroImage"])
        self.assertEqual(artwork.status_code, 200)
        self.assertIn("image/svg+xml", artwork.headers["content-type"])
        self.assertEqual(listing.json()["games"][0]["chapterCount"], 11)
        self.assertEqual(listing.json()["games"][0]["languageProfile"]["uniqueWords"], 3762)

        response = self.client.get("/v1/catalog/games/ocarina-of-time")
        self.assertEqual(response.status_code, 200)
        game = response.json()
        self.assertEqual([chapter["order"] for chapter in game["chapters"]], list(range(1, 12)))
        self.assertTrue(all(chapter["deck"]["status"] == "ready" for chapter in game["chapters"]))
        self.assertTrue(all(chapter["deck"]["downloadAvailable"] for chapter in game["chapters"]))
        self.assertEqual(game["contentReview"]["status"], "reviewed")
        self.assertEqual(game["contentReview"]["reviewedCardCount"], 759)
        cards = [card for chapter in game["chapters"] for card in chapter["sampleCards"]]
        self.assertLessEqual(
            len(cards),
            sum(chapter["deck"]["reviewedCardCount"] for chapter in game["chapters"]),
        )
        self.assertTrue(all(len(chapter["sampleCards"]) >= 5 for chapter in game["chapters"]))
        self.assertGreater(
            game["chapters"][0]["deck"]["reviewedCardCount"],
            len(game["chapters"][0]["sampleCards"]),
        )
        self.assertTrue(all(card["corpusEvidence"]["messageIds"] for card in cards))
        self.assertTrue(all(card["wordAudio"] is None for card in cards))
        self.assertTrue(all(card["sentenceAudio"] is None for card in cards))
        self.assertEqual(game["languageProfile"]["uniqueWords"], 3762)
        self.assertGreater(game["languageProfile"]["uniqueByLevel"]["N5"], 0)
        vocabulary = self.client.get(
            "/v1/catalog/games/ocarina-of-time/vocabulary?search=森&jlptLevel=N4"
        ).json()
        self.assertGreater(vocabulary["total"], 0)
        self.assertEqual(vocabulary["words"][0]["written"], "森")
        secondary_sense = self.client.get(
            "/v1/catalog/games/ocarina-of-time/vocabulary?search=feather"
        ).json()
        self.assertTrue(any(
            sense["meaning"].startswith("feather")
            for word in secondary_sense["words"]
            for sense in word["senses"]
        ))
        self.assertEqual(self.client.get("/v1/catalog/games/not-a-game").status_code, 404)

        for chapter in game["chapters"]:
            deck = self.client.get(
                f"/v1/catalog/games/ocarina-of-time/chapters/{chapter['id']}/deck"
            )
            self.assertEqual(deck.status_code, 200)
            self.assertEqual(deck.headers["content-type"], "application/octet-stream")
            self.assertIn(".apkg", deck.headers["content-disposition"])
            with zipfile.ZipFile(io.BytesIO(deck.content)) as package:
                database_name = next(
                    name for name in ("collection.anki2", "collection.anki21")
                    if name in package.namelist()
                )
                database_path = Path(self.temporary.name) / database_name
                database_path.write_bytes(package.read(database_name))
            database = sqlite3.connect(database_path)
            try:
                note_count = database.execute("SELECT COUNT(*) FROM notes").fetchone()[0]
                exported_notes = {
                    fields.split("\x1f")[9]: guid
                    for guid, fields in database.execute("SELECT guid, flds FROM notes")
                }
            finally:
                database.close()
            self.assertEqual(note_count, chapter["deck"]["reviewedCardCount"])
            for card_id, guid in exported_notes.items():
                self.assertEqual(
                    guid,
                    stable_note_guid("ocarina-of-time", chapter["id"], card_id),
                )
        self.assertEqual(
            self.client.get(
                "/v1/catalog/games/ocarina-of-time/chapters/not-a-chapter/deck"
            ).status_code,
            404,
        )

    def test_catalog_known_words_are_global_and_drive_game_coverage(self):
        self.client.post(
            "/v1/auth/register",
            json={"email": "coverage@example.com", "password": "correct horse battery", "displayName": "Coverage"},
        )
        initial = self.client.get("/v1/me/catalog/games/ocarina-of-time/coverage").json()
        self.assertEqual(initial["knownWords"], 0)
        updated = self.client.put(
            "/v1/me/catalog/games/ocarina-of-time/known-word",
            json={"wordId": "森|もり", "known": True}, headers=self.csrf_headers(),
        )
        self.assertEqual(updated.status_code, 200)
        self.assertEqual(updated.json()["coverage"]["knownWords"], 1)
        self.assertIn("森|もり", updated.json()["coverage"]["knownWordIds"])
        self.assertGreaterEqual(updated.json()["coverage"]["knownSenses"], 1)
        self.assertGreater(updated.json()["coverage"]["dialogueFamiliarityPercent"], 0)
        self.assertEqual(
            updated.json()["coverage"]["newWords"],
            updated.json()["coverage"]["totalWords"] - 1,
        )
        all_games = self.client.get("/v1/me/catalog/games").json()["games"]
        self.assertEqual(all_games[0]["knownWords"], 1)
        archive = self.client.get("/v1/me/exports/account").json()
        self.assertEqual(archive["knownWords"], ["森|もり"])
        removed = self.client.put(
            "/v1/me/catalog/games/ocarina-of-time/known-word",
            json={"wordId": "森|もり", "known": False}, headers=self.csrf_headers(),
        )
        self.assertEqual(removed.json()["coverage"]["knownWords"], 0)
        self.assertEqual(self.client.put(
            "/v1/me/catalog/games/ocarina-of-time/known-word",
            json={"wordId": "not-in-game", "known": True}, headers=self.csrf_headers(),
        ).status_code, 404)

    def test_chapter_cards_can_be_studied_with_the_shared_fsrs_queue(self):
        game_id = "ocarina-of-time"
        chapter_id = "11-hero-of-time"
        cards = self.client.get(
            f"/v1/catalog/games/{game_id}/chapters/{chapter_id}/cards?limit=2"
        )
        self.assertEqual(cards.status_code, 200)
        self.assertEqual(cards.json()["total"], 5)
        self.assertEqual(len(cards.json()["cards"]), 2)
        self.assertTrue(cards.json()["cards"][0]["sentenceJapanese"])
        self.assertIn("wordId", cards.json()["cards"][0])
        self.assertIn("senseId", cards.json()["cards"][0])

        self.client.post(
            "/v1/auth/register",
            json={
                "email": "course@example.com", "password": "correct horse battery",
                "displayName": "Course Learner",
            },
        )
        before = self.client.get(f"/v1/me/courses/{game_id}/chapters/{chapter_id}")
        self.assertFalse(before.json()["active"])
        self.assertEqual(before.json()["totalCards"], 5)

        enrolled = self.client.put(
            f"/v1/me/courses/{game_id}/chapters/{chapter_id}",
            json={"active": True}, headers=self.csrf_headers(),
        )
        self.assertEqual(enrolled.status_code, 200)
        self.assertTrue(enrolled.json()["active"])
        self.assertEqual(enrolled.json()["newCards"], 5)
        self.assertEqual(enrolled.json()["dueCards"], 5)
        account_export = self.client.get("/v1/me/exports/account").json()
        self.assertEqual(account_export["schemaVersion"], 6)
        self.assertEqual(account_export["courseEnrollments"][0]["chapter_id"], chapter_id)
        self.assertEqual(self.client.get("/v1/me/stats").json()["dueReviews"], 5)
        queue = self.client.get(
            f"/v1/me/reviews/queue?gameId={game_id}&chapterId={chapter_id}&limit=10"
        ).json()
        self.assertEqual(len(queue), 5)
        self.assertEqual(queue[0]["courseCard"]["chapterId"], chapter_id)
        self.assertTrue(queue[0]["courseCard"]["sentenceJapanese"])

        reviewed = self.client.post(
            "/v1/me/reviews",
            json={
                "wordId": queue[0]["wordId"], "senseId": queue[0]["senseId"], "rating": 1,
            },
            headers=self.csrf_headers(),
        )
        self.assertEqual(reviewed.status_code, 200)
        self.assertEqual(len(reviewed.json()["ratingPreviews"]), 4)
        retry_key = (queue[0]["wordId"], queue[0]["senseId"])
        self.client.put(
            "/v1/me/goals", json={"dailyNewWords": 1, "dailyReviews": 10},
            headers=self.csrf_headers(),
        )
        retry_queue = self.client.get(
            f"/v1/me/reviews/queue?gameId={game_id}&chapterId={chapter_id}&limit=1"
        ).json()
        self.assertEqual(len(retry_queue), 1)
        self.assertIn(retry_key, {(card["wordId"], card["senseId"]) for card in retry_queue})
        progress = self.client.get(
            f"/v1/me/courses/{game_id}/chapters/{chapter_id}"
        ).json()
        self.assertEqual(progress["reviewedCards"], 1)
        self.assertEqual(progress["clearedToday"], 0)
        self.assertEqual(progress["learningToday"], 1)
        self.assertEqual(progress["attemptsToday"], 1)
        self.assertEqual(progress["dueCards"], 5)

        passed = self.client.post(
            "/v1/me/reviews",
            json={"wordId": retry_key[0], "senseId": retry_key[1], "rating": 3},
            headers=self.csrf_headers(),
        )
        self.assertEqual(passed.status_code, 200)
        retry_queue = self.client.get(
            f"/v1/me/reviews/queue?gameId={game_id}&chapterId={chapter_id}&limit=10"
        ).json()
        self.assertNotIn(retry_key, {(card["wordId"], card["senseId"]) for card in retry_queue})
        progress = self.client.get(
            f"/v1/me/courses/{game_id}/chapters/{chapter_id}"
        ).json()
        self.assertEqual(progress["clearedToday"], 1)
        self.assertEqual(progress["learningToday"], 0)
        self.assertEqual(progress["attemptsToday"], 2)
        self.assertEqual(progress["dueCards"], 4)

        paused = self.client.put(
            f"/v1/me/courses/{game_id}/chapters/{chapter_id}",
            json={"active": False}, headers=self.csrf_headers(),
        )
        self.assertFalse(paused.json()["active"])
        self.assertEqual(self.client.get("/v1/me/stats").json()["dueReviews"], 0)
        self.assertEqual(self.client.get(
            f"/v1/me/reviews/queue?gameId={game_id}&chapterId={chapter_id}"
        ).json(), [])

    def test_cookie_mutations_require_matching_csrf_token(self):
        self.client.post(
            "/v1/auth/register",
            json={"email": "csrf@example.com", "password": "correct horse battery", "displayName": "CSRF"},
        )
        rejected = self.client.put(
            "/v1/me/goals", json={"dailyNewWords": 5, "dailyReviews": 10},
        )
        self.assertEqual(rejected.status_code, 403)
        self.assertEqual(rejected.json()["error"]["code"], "csrf_failed")
        rejected = self.client.put(
            "/v1/me/goals", json={"dailyNewWords": 5, "dailyReviews": 10},
            headers={"X-CSRF-Token": "wrong"},
        )
        self.assertEqual(rejected.status_code, 403)
        accepted = self.client.put(
            "/v1/me/goals", json={"dailyNewWords": 5, "dailyReviews": 10},
            headers=self.csrf_headers(),
        )
        self.assertEqual(accepted.status_code, 200)

        bearer_request = self.client.post(
            "/v1/events/batch", json={"events": []},
            headers={"Authorization": "Bearer invalid"},
        )
        self.assertNotEqual(bearer_request.status_code, 403)

    def test_account_learning_routes(self):
        self.client.post(
            "/v1/auth/register",
            json={"email": "learner@example.com", "password": "correct horse battery", "displayName": "Learner"},
        )
        pairing = self.client.post(
            "/v1/device-pairings",
            json={"deviceName": "Test PC", "adapterId": "ship-of-harkinian", "gameId": "ocarina-of-time"},
        ).json()
        self.client.post(
            "/v1/device-pairings/approve", json={"userCode": pairing["userCode"]},
            headers=self.csrf_headers(),
        )
        device = self.client.post("/v1/device-pairings/token", json={"deviceCode": pairing["deviceCode"]}).json()
        self.client.post(
            "/v1/events/batch",
            json={"events": [{"eventId": "saved", "type": "word_saved", "occurredAt": "2026-10-02T12:00:00Z",
                              "gameId": "ocarina-of-time", "adapterId": "ship-of-harkinian",
                              "contentVersion": "v1", "wordId": "森|もり"}]},
            headers={"Authorization": f"Bearer {device['deviceToken']}"},
        )
        annotation = self.client.put(
            "/v1/me/words/annotation",
            json={"wordId": "森|もり", "learningState": "learning", "note": "forest", "tags": ["kokiri"]},
            headers=self.csrf_headers(),
        )
        self.assertEqual(annotation.status_code, 200)
        self.assertEqual(self.client.get("/v1/me/words?learningState=learning").json()[0]["tags"], ["kokiri"])
        self.assertEqual(self.client.put(
            "/v1/me/goals", json={"dailyNewWords": 5, "dailyReviews": 15},
            headers=self.csrf_headers(),
        ).status_code, 200)
        self.assertEqual(len(self.client.get("/v1/me/reviews/queue").json()), 1)
        self.assertEqual(self.client.post(
            "/v1/me/reviews", json={"wordId": "森|もり", "rating": 3},
            headers=self.csrf_headers(),
        ).status_code, 200)
        archive = self.client.get("/v1/me/exports/account").json()
        self.assertEqual(archive["annotations"][0]["note"], "forest")
        self.assertEqual(len(archive["reviews"]), 1)

    def test_auth_rate_limit_returns_retry_after(self):
        database_path = Path(self.temporary.name) / "limited.sqlite3"
        settings = Settings(
            database_path=str(database_path),
            production=False,
            cookie_secure=False,
            allowed_hosts=("*",),
            trust_proxy_headers=False,
            rate_limit_window_seconds=60,
            auth_rate_limit=1,
            pairing_rate_limit=0,
            event_rate_limit=0,
        )
        clock_value = [100.0]
        limiter = SlidingWindowRateLimiter(clock=lambda: clock_value[0])
        with TestClient(create_app(settings=settings, rate_limiter=limiter)) as client:
            first = client.post(
                "/v1/auth/login",
                json={"email": "missing@example.com", "password": "correct horse battery"},
            )
            self.assertEqual(first.status_code, 401)
            second = client.post(
                "/v1/auth/login",
                json={"email": "missing@example.com", "password": "correct horse battery"},
            )
            self.assertEqual(second.status_code, 429)
            self.assertEqual(second.json()["error"]["code"], "rate_limited")
            self.assertIn("Retry-After", second.headers)

    def test_rate_limits_cover_exports_and_account_mutations(self):
        settings = Settings(
            database_path=str(Path(self.temporary.name) / "expanded-limits.sqlite3"),
            production=False,
            cookie_secure=False,
            allowed_hosts=("*",),
            trust_proxy_headers=False,
            rate_limit_window_seconds=60,
            auth_rate_limit=1,
            pairing_rate_limit=1,
            event_rate_limit=1,
        )
        with TestClient(create_app(settings=settings)) as client:
            client.post(
                "/v1/auth/register",
                json={"email": "limits@example.com", "password": "correct horse battery", "displayName": "Limits"},
            )
            self.assertEqual(client.get("/v1/me/exports/account").status_code, 200)
            self.assertEqual(client.get("/v1/me/exports/account").status_code, 429)
            csrf = {"X-CSRF-Token": client.cookies["jp_assist_csrf"]}
            self.assertEqual(client.put(
                "/v1/me/goals", json={"dailyNewWords": 4, "dailyReviews": 8}, headers=csrf,
            ).status_code, 200)
            self.assertEqual(client.put(
                "/v1/me/goals", json={"dailyNewWords": 5, "dailyReviews": 10}, headers=csrf,
            ).status_code, 429)

    def test_production_disables_interactive_api_docs(self):
        settings = Settings(
            database_path=str(Path(self.temporary.name) / "production.sqlite3"),
            production=True,
            cookie_secure=True,
            allowed_hosts=("testserver", "127.0.0.1"),
            trust_proxy_headers=True,
            rate_limit_window_seconds=60,
            auth_rate_limit=20,
            pairing_rate_limit=180,
            event_rate_limit=180,
            mail_transport="smtp",
            smtp_host="mailpit",
        )
        with TestClient(create_app(settings=settings)) as client:
            self.assertEqual(client.get("/readyz").status_code, 200)
            self.assertEqual(client.get("/docs").status_code, 404)


if __name__ == "__main__":
    unittest.main()
