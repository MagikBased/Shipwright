import tempfile
import unittest
from datetime import datetime, timezone
from pathlib import Path

from learning_platform.errors import (
    AuthenticationError,
    ConflictError,
    PairingPendingError,
    ValidationError,
)
from learning_platform.service import LearningPlatform


class Clock:
    def __init__(self):
        self.value = datetime(2026, 10, 2, 12, 0, tzinfo=timezone.utc)

    def __call__(self):
        return self.value


class LearningPlatformTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="jp-assist-platform-")
        self.clock = Clock()
        self.platform = LearningPlatform(Path(self.temporary.name) / "test.sqlite3", now=self.clock)
        account = self.platform.register_user("player@example.com", "correct horse battery", "Player")
        self.session = account["token"]

    def tearDown(self):
        self.temporary.cleanup()

    def pair_device(self):
        pairing = self.platform.start_pairing("Test PC", "ship-of-harkinian", "ocarina-of-time")
        with self.assertRaises(PairingPendingError):
            self.platform.claim_pairing(pairing["deviceCode"])
        self.platform.approve_pairing(self.session, pairing["userCode"])
        return self.platform.claim_pairing(pairing["deviceCode"])

    def pair_game(self, game_id, adapter_id):
        pairing = self.platform.start_pairing(f"{game_id} PC", adapter_id, game_id)
        self.platform.approve_pairing(self.session, pairing["userCode"])
        return self.platform.claim_pairing(pairing["deviceCode"])

    def event(self, event_id, event_type="word_encountered", occurred_at="2026-10-02T12:00:00Z", **extra):
        result = {
            "eventId": event_id,
            "type": event_type,
            "occurredAt": occurred_at,
            "gameId": "ocarina-of-time",
            "adapterId": "ship-of-harkinian",
            "contentVersion": "n64-ntsc-1.2-v1",
            "wordId": "武器|ぶき",
        }
        result.update(extra)
        return result

    def test_register_login_and_logout(self):
        logged_in = self.platform.login("PLAYER@example.com", "correct horse battery")
        self.assertEqual(logged_in["user"]["displayName"], "Player")
        self.platform.logout(logged_in["token"])
        with self.assertRaises(AuthenticationError):
            self.platform.authenticate_session(logged_in["token"])
        with self.assertRaises(ConflictError):
            self.platform.register_user("player@example.com", "another safe password", "Other")

    def test_pairing_is_one_time_and_account_approved(self):
        pairing = self.platform.start_pairing("Test PC", "ship-of-harkinian", "ocarina-of-time")
        approval = self.platform.approve_pairing(self.session, pairing["userCode"].lower())
        self.assertTrue(approval["approved"])
        device = self.platform.claim_pairing(pairing["deviceCode"])
        self.assertEqual(device["gameId"], "ocarina-of-time")
        with self.assertRaises(ConflictError):
            self.platform.claim_pairing(pairing["deviceCode"])

    def test_retrying_event_batch_does_not_double_count(self):
        device = self.pair_device()
        batch = [
            self.event("evt-encounter", count=3, senseId="weapon"),
            self.event("evt-save", "word_saved", senseId="weapon"),
        ]
        first = self.platform.ingest_events(device["deviceToken"], batch)
        retry = self.platform.ingest_events(device["deviceToken"], batch)

        self.assertEqual(first["acceptedEventIds"], ["evt-encounter", "evt-save"])
        self.assertEqual(retry["duplicateEventIds"], ["evt-encounter", "evt-save"])
        self.assertEqual(self.platform.get_stats(self.session)["encounters"], 3)
        words = self.platform.list_word_progress(self.session)
        self.assertEqual(len(words), 1)
        self.assertTrue(words[0]["saved"])

    def test_save_state_uses_event_time_when_offline_batches_arrive_out_of_order(self):
        device = self.pair_device()
        self.platform.ingest_events(
            device["deviceToken"],
            [self.event("evt-save", "word_saved", "2026-10-02T12:00:00Z")],
        )
        self.platform.ingest_events(
            device["deviceToken"],
            [self.event("evt-old-unsave", "word_unsaved", "2026-10-02T11:00:00Z")],
        )
        self.assertTrue(self.platform.list_word_progress(self.session)[0]["saved"])

        self.platform.ingest_events(
            device["deviceToken"],
            [self.event("evt-new-unsave", "word_unsaved", "2026-10-02T13:00:00Z")],
        )
        self.assertFalse(self.platform.list_word_progress(self.session)[0]["saved"])

    def test_saved_manifest_matches_existing_local_anki_progress_shape(self):
        device = self.pair_device()
        self.platform.ingest_events(
            device["deviceToken"],
            [self.event("evt-save", "word_saved", messageId="0x1234", pageIndex=2)],
        )
        manifest = self.platform.saved_word_manifest(self.session)
        self.assertEqual(manifest["schemaVersion"], 1)
        self.assertEqual(manifest["savedTokenIds"], ["武器|ぶき"])
        self.assertEqual(manifest["words"][0]["contextMessageId"], "0x1234")
        self.assertEqual(manifest["words"][0]["contextPageIndex"], 2)

    def test_same_word_across_games_has_global_and_per_game_progress(self):
        oot = self.pair_device()
        other = self.pair_game("another-game", "another-adapter")
        self.platform.ingest_events(oot["deviceToken"], [self.event("evt-oot", count=2)])
        other_event = {
            **self.event("evt-other-game", count=4),
            "gameId": "another-game",
            "adapterId": "another-adapter",
        }
        self.platform.ingest_events(other["deviceToken"], [other_event])

        stats = self.platform.get_stats(self.session)
        self.assertEqual(stats["uniqueWords"], 1)
        self.assertEqual(stats["encounters"], 6)
        self.assertEqual(stats["games"], 2)
        words = self.platform.list_word_progress(self.session)
        self.assertEqual(words[0]["gameIds"], ["another-game", "ocarina-of-time"])

    def test_revoked_device_can_no_longer_upload(self):
        device = self.pair_device()
        self.assertEqual(len(self.platform.list_devices(self.session)), 1)
        self.platform.revoke_device(self.session, device["deviceId"])
        self.assertEqual(self.platform.list_devices(self.session), [])
        with self.assertRaises(AuthenticationError):
            self.platform.ingest_events(device["deviceToken"], [self.event("evt-revoked")])

    def test_event_payload_rejects_dialogue_text_and_wrong_device_scope(self):
        device = self.pair_device()
        with self.assertRaises(ValidationError):
            self.platform.ingest_events(
                device["deviceToken"],
                [self.event("evt-private", japaneseText="ゲームの台詞")],
            )
        with self.assertRaises(ValidationError):
            self.platform.ingest_events(
                device["deviceToken"],
                [{**self.event("evt-other"), "gameId": "another-game"}],
            )
        self.assertEqual(self.platform.get_stats(self.session)["events"], 0)

    def test_vocabulary_metadata_annotations_filters_and_dictionary(self):
        device = self.pair_device()
        self.platform.ingest_events(device["deviceToken"], [self.event("evt-word", count=4)])
        self.assertEqual(self.platform.import_dictionary_entries([{
            "wordId": "武器|ぶき", "written": "武器", "reading": "ぶき", "partOfSpeech": "noun",
            "meaning": "weapon", "source": "test", "attribution": "Test data",
        }]), 1)
        updated = self.platform.update_word_annotation(
            self.session, "武器|ぶき", None, "learning", "Remember this", ["equipment", "noun"]
        )
        self.assertEqual(updated["learningState"], "learning")
        words = self.platform.list_word_progress(
            self.session, search="weapon", learning_state="learning", sort="alphabetical"
        )
        self.assertEqual(words[0]["dictionary"]["written"], "武器")
        self.assertEqual(words[0]["tags"], ["equipment", "noun"])

    def test_goals_reviews_activity_and_account_export(self):
        device = self.pair_device()
        self.platform.ingest_events(device["deviceToken"], [self.event("evt-save", "word_saved")])
        goals = self.platform.update_goals(self.session, 5, 25, True)
        self.assertEqual(goals["dailyReviews"], 25)
        self.assertEqual(len(self.platform.review_queue(self.session)), 1)
        stats_before = self.platform.get_stats(self.session)
        self.assertEqual(stats_before["dueReviews"], 1)
        self.assertEqual(stats_before["learningStates"], {"new": 1})
        review = self.platform.submit_review(self.session, "武器|ぶき", None, 3)
        self.assertGreaterEqual(review["intervalDays"], 1)
        self.assertEqual(self.platform.review_queue(self.session), [])
        stats_after = self.platform.get_stats(self.session)
        self.assertEqual(stats_after["reviewedToday"], 1)
        self.assertEqual(stats_after["activityStreakDays"], 1)
        self.assertEqual(self.platform.activity(self.session)["games"][0]["gameId"], "ocarina-of-time")
        export = self.platform.account_export(self.session)
        self.assertEqual(len(export["reviews"]), 1)
        self.assertNotIn("password", str(export).lower())

    def test_session_password_and_per_game_clear_controls(self):
        first_device = self.pair_device()
        second_device = self.pair_game("another-game", "another-adapter")
        self.platform.ingest_events(first_device["deviceToken"], [self.event("evt-first", count=2)])
        self.platform.ingest_events(second_device["deviceToken"], [{
            **self.event("evt-second", count=3), "gameId": "another-game", "adapterId": "another-adapter",
        }])
        other_session = self.platform.login("player@example.com", "correct horse battery")["token"]
        self.assertEqual(len(self.platform.list_sessions(self.session)), 2)
        self.platform.change_password(self.session, "correct horse battery", "a safer changed password")
        with self.assertRaises(AuthenticationError):
            self.platform.authenticate_session(other_session)
        self.assertEqual(self.platform.login("player@example.com", "a safer changed password")["user"]["displayName"], "Player")
        self.platform.clear_game_progress(self.session, "ocarina-of-time")
        words = self.platform.list_word_progress(self.session)
        self.assertEqual(words[0]["encounterCount"], 3)
        self.assertEqual(words[0]["gameIds"], ["another-game"])


if __name__ == "__main__":
    unittest.main()
