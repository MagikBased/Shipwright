import tempfile
import unittest
from pathlib import Path

try:
    from fastapi.testclient import TestClient
    from learning_platform.api import create_app
except ImportError:  # Core service tests remain runnable without web dependencies.
    TestClient = None
    create_app = None


@unittest.skipIf(TestClient is None, "FastAPI development dependencies are not installed")
class LearningPlatformApiTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="jp-assist-platform-api-")
        app = create_app(Path(self.temporary.name) / "api.sqlite3")
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
        page = self.client.get("/")
        self.assertEqual(page.status_code, 200)
        self.assertIn("Turn game dialogue", page.text)


if __name__ == "__main__":
    unittest.main()
