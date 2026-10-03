import json
import tempfile
import unittest
from datetime import datetime, timedelta, timezone
from pathlib import Path
from urllib.parse import parse_qs, urlparse

from learning_platform.errors import (
    AuthenticationError,
    ConflictError,
    PairingPendingError,
    ValidationError,
)
from learning_platform.service import LearningPlatform
from learning_platform.security import token_hash


class RecordingMailer:
    def __init__(self):
        self.messages = []

    def send(self, message):
        self.messages.append(message)


class Clock:
    def __init__(self):
        self.value = datetime(2026, 10, 2, 12, 0, tzinfo=timezone.utc)

    def __call__(self):
        return self.value


class LearningPlatformTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="jp-assist-platform-")
        self.clock = Clock()
        self.mailer = RecordingMailer()
        self.platform = LearningPlatform(
            Path(self.temporary.name) / "test.sqlite3", now=self.clock,
            mailer=self.mailer, public_base_url="http://learn.example.test",
        )
        account = self.platform.register_user("player@example.com", "correct horse battery", "Player")
        self.session = account["token"]

    def latest_mail_token(self):
        urls = [part for part in self.mailer.messages[-1].text.split() if part.startswith("http")]
        return next(parse_qs(urlparse(url).fragment)["token"][0] for url in urls if "token=" in url)

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

    def test_email_verification_is_hashed_expiring_and_single_use(self):
        self.assertFalse(self.platform.authenticate_session(self.session)["emailVerified"])
        token = self.latest_mail_token()
        with self.platform.database.connect() as connection:
            stored = connection.execute(
                "SELECT token_hash FROM action_tokens WHERE purpose = 'verify_email'",
            ).fetchone()[0]
        self.assertNotEqual(stored, token)
        verified = self.platform.verify_email(token)
        self.assertTrue(verified["emailVerified"])
        with self.assertRaisesRegex(ValidationError, "invalid or expired"):
            self.platform.verify_email(token)

        second = self.platform.register_user(
            "expires@example.com", "another safe password", "Expires",
        )
        expired_token = self.latest_mail_token()
        self.clock.value += timedelta(hours=25)
        with self.assertRaisesRegex(ValidationError, "invalid or expired"):
            self.platform.verify_email(expired_token)
        self.assertFalse(self.platform.authenticate_session(second["token"])["emailVerified"])
        self.assertTrue(self.platform.resend_email_verification(second["token"])["sent"])
        with self.platform.database.connect() as connection:
            self.assertIsNone(connection.execute(
                "SELECT 1 FROM action_tokens WHERE token_hash = ?", (token_hash(expired_token),),
            ).fetchone())

        third = self.platform.register_user(
            "cooldown@example.com", "another safe password", "Cooldown",
        )
        with self.assertRaisesRegex(ConflictError, "wait one minute"):
            self.platform.resend_email_verification(third["token"])

    def test_password_reset_is_enumeration_resistant_and_revokes_sessions(self):
        unknown = self.platform.request_password_reset("missing@example.com")
        before = len(self.mailer.messages)
        known = self.platform.request_password_reset("player@example.com")
        self.assertEqual(unknown, known)
        self.assertEqual(len(self.mailer.messages), before + 1)
        reset_token = self.latest_mail_token()
        self.platform.reset_password(reset_token, "a replacement safe password")
        with self.assertRaises(AuthenticationError):
            self.platform.authenticate_session(self.session)
        with self.assertRaises(AuthenticationError):
            self.platform.login("player@example.com", "correct horse battery")
        self.session = self.platform.login("player@example.com", "a replacement safe password")["token"]
        with self.assertRaisesRegex(ValidationError, "invalid or expired"):
            self.platform.reset_password(reset_token, "yet another safe password")

    def test_verified_email_change_requires_password_and_revokes_sessions(self):
        self.platform.verify_email(self.latest_mail_token())
        with self.assertRaises(AuthenticationError):
            self.platform.request_email_change(
                self.session, "wrong password", "new-address@example.com",
            )
        result = self.platform.request_email_change(
            self.session, "correct horse battery", "new-address@example.com",
        )
        self.assertTrue(result["sent"])
        self.platform.confirm_email_change(self.latest_mail_token())
        with self.assertRaises(AuthenticationError):
            self.platform.authenticate_session(self.session)
        self.session = self.platform.login("new-address@example.com", "correct horse battery")["token"]
        self.assertTrue(self.platform.authenticate_session(self.session)["emailVerified"])

    def test_notification_preferences_reminder_deduplication_and_unsubscribe(self):
        self.platform.verify_email(self.latest_mail_token())
        device = self.pair_device()
        self.platform.ingest_events(device["deviceToken"], [self.event("reminder-save", "word_saved")])
        preferences = self.platform.update_notification_preferences(
            self.session, True, False, 0, "America/Chicago",
        )
        self.assertTrue(preferences["reviewReminders"])
        first = self.platform.send_due_reminders()
        second = self.platform.send_due_reminders()
        self.assertEqual(first, {"sent": 1, "failed": 0, "skipped": 0})
        self.assertEqual(second, {"sent": 0, "failed": 0, "skipped": 1})
        self.assertEqual(self.mailer.messages[-1].category, "review_reminder")
        self.platform.unsubscribe_review_reminders(self.latest_mail_token())
        self.assertFalse(self.platform.get_notification_preferences(self.session)["reviewReminders"])
        with self.assertRaisesRegex(ValidationError, "invalid or expired"):
            self.platform.unsubscribe_review_reminders(self.latest_mail_token())

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
        self.assertGreater(review["intervalDays"], 0)
        self.assertEqual(review["schedulerVersion"], "fsrs-6.3.2")
        self.assertEqual(review["algorithmVersion"], "FSRS-6")
        self.assertEqual(self.platform.review_queue(self.session), [])
        stats_after = self.platform.get_stats(self.session)
        self.assertEqual(stats_after["reviewedToday"], 1)
        self.assertEqual(stats_after["activityStreakDays"], 1)
        self.assertEqual(self.platform.activity(self.session)["games"][0]["gameId"], "ocarina-of-time")
        export = self.platform.account_export(self.session)
        self.assertEqual(len(export["reviews"]), 1)
        self.assertNotIn("password", str(export).lower())

    def test_fsrs_previews_and_review_state_rebuild_are_deterministic(self):
        device = self.pair_device()
        self.platform.ingest_events(device["deviceToken"], [self.event("evt-save", "word_saved")])
        queue = self.platform.review_queue(self.session)
        self.assertEqual(queue[0]["schedulerVersion"], "fsrs-6.3.2")
        self.assertEqual([item["rating"] for item in queue[0]["ratingPreviews"]], [1, 2, 3, 4])
        self.assertAlmostEqual(queue[0]["ratingPreviews"][2]["intervalDays"], 10 / 1440)
        self.assertEqual(queue[0]["ratingPreviews"][3]["intervalDays"], 8)

        first = self.platform.submit_review(self.session, "武器|ぶき", None, 3)
        self.assertEqual(first["cardState"], 1)
        self.assertAlmostEqual(first["stability"], 2.3065)
        self.clock.value += timedelta(minutes=10)
        second = self.platform.submit_review(self.session, "武器|ぶき", None, 3)
        self.assertEqual(second["cardState"], 2)
        self.assertEqual(second["intervalDays"], 2)

        with self.platform.database.connect() as connection:
            state_before = dict(connection.execute("SELECT * FROM review_state").fetchone())
            reviews_before = [tuple(row) for row in connection.execute("SELECT * FROM reviews ORDER BY reviewed_at, id")]
            connection.execute("DELETE FROM review_state")
            self.assertEqual(self.platform._rebuild_review_states(connection), 1)
            state_after = dict(connection.execute("SELECT * FROM review_state").fetchone())
            reviews_after = [tuple(row) for row in connection.execute("SELECT * FROM reviews ORDER BY reviewed_at, id")]
        self.assertEqual(state_after, state_before)
        self.assertEqual(reviews_after, reviews_before)

        with self.platform.database.connect() as connection:
            connection.execute("UPDATE review_state SET scheduler_version = 'fsrs-future'")
        with self.assertRaisesRegex(RuntimeError, "explicit rebuild-reviews"):
            LearningPlatform(self.platform.database.path, now=self.clock)
        migration = LearningPlatform(
            self.platform.database.path, now=self.clock, allow_scheduler_upgrade=True,
        )
        self.assertEqual(migration.rebuild_review_states(), 1)

    def test_review_day_limits_burying_and_timezone_boundaries(self):
        device = self.pair_device()
        self.platform.ingest_events(device["deviceToken"], [
            self.event("save-a", "word_saved", wordId="森|もり"),
            self.event("save-b", "word_saved", wordId="橋|はし"),
        ])
        goals = self.platform.update_goals(self.session, 1, 0, False, "America/Chicago")
        self.assertEqual(goals["timezone"], "America/Chicago")
        queue = self.platform.review_queue(self.session)
        self.assertEqual(len(queue), 1)
        buried = self.platform.bury_review(self.session, queue[0]["wordId"], None)
        self.assertEqual(buried["buriedUntil"], "2026-10-03T05:00:00.000Z")
        replacement = self.platform.review_queue(self.session)
        self.assertEqual(len(replacement), 1)
        self.assertNotEqual(replacement[0]["wordId"], queue[0]["wordId"])
        self.platform.submit_review(self.session, replacement[0]["wordId"], None, 3)
        self.assertEqual(self.platform.review_queue(self.session), [])

        self.clock.value += timedelta(hours=18)
        # The buried new card returns with the new-day budget, and the short
        # learning step remains available regardless of the review limit.
        self.assertEqual(len(self.platform.review_queue(self.session)), 2)
        with self.assertRaises(ValidationError):
            self.platform.update_goals(self.session, 1, 1, False, "Not/A_Real_Zone")

        self.clock.value = datetime(2026, 11, 1, 12, 0, tzinfo=timezone.utc)
        start, end = self.platform._review_day("America/Chicago")
        self.assertEqual(end - start, timedelta(hours=25))

    def test_review_owner_is_explicit_scoped_and_single_scheduler(self):
        device = self.pair_device()
        self.platform.ingest_events(device["deviceToken"], [self.event("owner-save", "word_saved")])
        default = self.platform.get_review_collection(self.session)
        self.assertEqual(default["reviewOwner"], "jp_assist")
        with self.assertRaises(ConflictError):
            self.platform.update_review_collection(
                self.session, None, "anki", "JP Assist Test", confirmed=False,
            )
        anki_owned = self.platform.update_review_collection(
            self.session, None, "anki", "JP Assist Test", confirmed=True,
        )
        self.assertEqual(anki_owned["reviewOwner"], "anki")
        self.assertEqual(self.platform.review_queue(self.session), [])
        with self.assertRaises(ConflictError):
            self.platform.submit_review(self.session, "武器|ぶき", None, 3)

        game_collection = self.platform.get_review_collection(self.session, "ocarina-of-time")
        self.assertEqual(game_collection["reviewOwner"], "jp_assist")
        synced = self.platform.mark_anki_synced(self.session)
        self.assertIsNotNone(synced["lastAnkiSyncAt"])
        restored = self.platform.update_review_collection(
            self.session, None, "jp_assist", "JP Assist Test", confirmed=True,
        )
        self.assertEqual(restored["reviewOwner"], "jp_assist")
        self.assertEqual(len(self.platform.review_queue(self.session)), 1)

    def test_anki_history_import_is_idempotent_auditable_and_replayable(self):
        device = self.pair_device()
        self.platform.ingest_events(device["deviceToken"], [self.event("anki-save", "word_saved")])
        review = {
            "sourceReviewId": "1790856000123", "sourceCardId": "987654321",
            "wordId": "武器|ぶき", "senseId": None, "rating": 3,
            "reviewedAt": "2026-09-30T12:00:00Z", "intervalDays": 4,
            "previousIntervalDays": 1, "factor": 2450, "durationMs": 3210,
            "reviewType": 1,
        }
        with self.assertRaises(ConflictError):
            self.platform.import_anki_reviews(self.session, [review])
        self.platform.update_review_collection(
            self.session, None, "anki", "JP Assist Test", confirmed=True,
        )
        with self.assertRaisesRegex(ValidationError, "all-games"):
            self.platform.import_anki_reviews(self.session, [review], "ocarina-of-time")
        first = self.platform.import_anki_reviews(self.session, [review])
        retry = self.platform.import_anki_reviews(self.session, [review])
        self.assertEqual((first["accepted"], first["duplicates"]), (1, 0))
        self.assertEqual((retry["accepted"], retry["duplicates"]), (0, 1))
        with self.platform.database.connect() as connection:
            stored = connection.execute(
                "SELECT * FROM reviews WHERE source = 'anki' AND source_review_id = ?",
                (review["sourceReviewId"],),
            ).fetchone()
        self.assertEqual(stored["reviewed_at"], "2026-09-30T12:00:00.000Z")
        self.assertEqual(stored["due_at"], "2026-10-04T12:00:00.000Z")
        self.assertEqual(stored["source_card_id"], "987654321")
        self.assertEqual(stored["review_duration_ms"], 3210)
        self.assertEqual(json.loads(stored["source_metadata_json"])["factor"], 2450)

        self.platform.update_review_collection(
            self.session, None, "jp_assist", "JP Assist Test", confirmed=True,
        )
        with self.platform.database.connect() as connection:
            state = connection.execute(
                "SELECT * FROM review_state WHERE user_id = ? AND word_id = ?",
                (self.platform.authenticate_session(self.session)["id"], "武器|ぶき"),
            ).fetchone()
        self.assertEqual(state["last_reviewed_at"], "2026-09-30T12:00:00.000Z")
        imported = next(item for item in self.platform.account_export(self.session)["reviews"] if item["source"] == "anki")
        self.assertEqual(imported["source_review_id"], review["sourceReviewId"])
        self.assertEqual(imported["sourceMetadata"]["previousIntervalDays"], 1.0)

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

    def test_operational_cleanup_applies_documented_retention_windows(self):
        user_id = self.platform.authenticate_session(self.session)["id"]
        self.platform.start_pairing("Expired PC", "ship-of-harkinian", "ocarina-of-time")
        with self.platform.database.connect() as connection:
            connection.execute(
                "INSERT INTO notification_deliveries VALUES (?, ?, ?, ?, ?)",
                ("old-delivery", user_id, "review_reminder", "2026-10-02", "2026-10-02T12:00:00.000Z"),
            )
        self.clock.value += timedelta(days=400)
        removed = self.platform.cleanup_operational_data()
        self.assertGreaterEqual(removed["sessions"], 1)
        self.assertGreaterEqual(removed["pairings"], 1)
        self.assertGreaterEqual(removed["actionTokens"], 1)
        self.assertEqual(removed["notificationDeliveries"], 1)
        self.assertGreaterEqual(removed["auditEvents"], 1)
        with self.platform.database.connect() as connection:
            for table in (
                "sessions", "pairings", "action_tokens", "notification_deliveries", "audit_events",
            ):
                self.assertEqual(connection.execute(f"SELECT COUNT(*) FROM {table}").fetchone()[0], 0)


if __name__ == "__main__":
    unittest.main()
