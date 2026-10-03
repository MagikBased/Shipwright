import tempfile
import unittest
from pathlib import Path

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

        approved = self.client.post("/v1/device-pairings/approve", json={"userCode": pairing["userCode"]})
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

    def test_account_learning_routes(self):
        self.client.post(
            "/v1/auth/register",
            json={"email": "learner@example.com", "password": "correct horse battery", "displayName": "Learner"},
        )
        pairing = self.client.post(
            "/v1/device-pairings",
            json={"deviceName": "Test PC", "adapterId": "ship-of-harkinian", "gameId": "ocarina-of-time"},
        ).json()
        self.client.post("/v1/device-pairings/approve", json={"userCode": pairing["userCode"]})
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
        )
        self.assertEqual(annotation.status_code, 200)
        self.assertEqual(self.client.get("/v1/me/words?learningState=learning").json()[0]["tags"], ["kokiri"])
        self.assertEqual(self.client.put("/v1/me/goals", json={"dailyNewWords": 5, "dailyReviews": 15}).status_code, 200)
        self.assertEqual(len(self.client.get("/v1/me/reviews/queue").json()), 1)
        self.assertEqual(self.client.post("/v1/me/reviews", json={"wordId": "森|もり", "rating": 3}).status_code, 200)
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
        )
        with TestClient(create_app(settings=settings)) as client:
            self.assertEqual(client.get("/readyz").status_code, 200)
            self.assertEqual(client.get("/docs").status_code, 404)


if __name__ == "__main__":
    unittest.main()
