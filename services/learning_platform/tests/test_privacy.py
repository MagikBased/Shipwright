import json
import tempfile
import unittest
from pathlib import Path

from learning_platform.api import EventRequest
from learning_platform.service import EVENT_FIELDS, LearningPlatform


class RecordingMailer:
    def __init__(self):
        self.messages = []

    def send(self, message):
        self.messages.append(message)


class PrivacyContractTest(unittest.TestCase):
    def test_event_contract_cannot_accept_dialogue_or_arbitrary_text(self):
        model_fields = EventRequest.model_fields if hasattr(EventRequest, "model_fields") else EventRequest.__fields__
        self.assertEqual(set(model_fields), EVENT_FIELDS)
        self.assertTrue({
            "japanese", "english", "japaneseText", "englishText", "dialogueText",
            "screenshot", "audio", "saveData",
        }.isdisjoint(EVENT_FIELDS))

    def test_database_stores_hashes_instead_of_raw_credentials(self):
        with tempfile.TemporaryDirectory(prefix="jp-assist-privacy-schema-") as temporary:
            platform = LearningPlatform(Path(temporary) / "privacy.sqlite3")
            with platform.database.connect() as connection:
                tables = {
                    table: {row["name"] for row in connection.execute(f"PRAGMA table_info({table})")}
                    for table in ("sessions", "devices", "pairings", "action_tokens")
                }
            self.assertIn("token_hash", tables["sessions"])
            self.assertIn("token_hash", tables["devices"])
            self.assertIn("device_secret_hash", tables["pairings"])
            self.assertIn("token_hash", tables["action_tokens"])
            for columns in tables.values():
                self.assertTrue({"token", "device_token", "device_secret"}.isdisjoint(columns))

    def test_account_export_excludes_raw_credentials_and_email_links(self):
        with tempfile.TemporaryDirectory(prefix="jp-assist-privacy-export-") as temporary:
            mailer = RecordingMailer()
            platform = LearningPlatform(Path(temporary) / "privacy.sqlite3", mailer=mailer)
            account = platform.register_user(
                "privacy@example.test", "correct horse battery", "Privacy",
            )
            exported = json.dumps(platform.account_export(account["token"]), sort_keys=True)
            self.assertNotIn(account["token"], exported)
            self.assertNotIn("correct horse battery", exported)
            self.assertNotIn("token_hash", exported)
            self.assertNotIn("password_hash", exported)
            self.assertNotIn("password_salt", exported)
            for part in mailer.messages[-1].text.split():
                if "token=" in part:
                    self.assertNotIn(part, exported)


if __name__ == "__main__":
    unittest.main()
