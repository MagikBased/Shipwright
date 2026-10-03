import json
import tempfile
import unittest
import uuid
from pathlib import Path

from fastapi.testclient import TestClient

from learning_platform.api import create_app
from learning_platform.config import Settings


class ObservabilityTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="jp-assist-observability-")
        root = Path(self.temporary.name)
        self.backups = root / "backups"
        self.backups.mkdir()
        (self.backups / "platform-test.sqlite3").write_bytes(b"backup")
        settings = Settings(
            database_path=str(root / "platform.sqlite3"),
            production=False,
            cookie_secure=False,
            allowed_hosts=("*",),
            trust_proxy_headers=False,
            rate_limit_window_seconds=60,
            auth_rate_limit=20,
            pairing_rate_limit=180,
            event_rate_limit=180,
            backup_directory=str(self.backups),
            structured_logs=True,
        )
        self.app = create_app(settings=settings)
        self.client = TestClient(self.app)

    def tearDown(self):
        self.client.close()
        self.temporary.cleanup()

    def test_request_and_audit_logs_are_correlated_and_credentials_are_absent(self):
        with self.assertLogs("jp_assist.request", level="INFO") as request_logs:
            with self.assertLogs("jp_assist.audit", level="INFO") as audit_logs:
                response = self.client.post(
                    "/v1/auth/register",
                    headers={"X-Request-ID": "acceptance-request-1"},
                    json={
                        "email": "private@example.test",
                        "password": "correct horse battery",
                        "displayName": "Private Learner",
                    },
                )

        self.assertEqual(response.status_code, 201)
        self.assertEqual(response.headers["X-Request-ID"], "acceptance-request-1")
        request_log = json.loads(request_logs.records[-1].getMessage())
        audit_log = next(
            json.loads(record.getMessage()) for record in audit_logs.records
            if json.loads(record.getMessage())["auditType"] == "account_registered"
        )
        self.assertEqual(request_log["requestId"], "acceptance-request-1")
        self.assertEqual(request_log["route"], "/v1/auth/register")
        self.assertEqual(audit_log["requestId"], "acceptance-request-1")
        self.assertEqual(audit_log["auditType"], "account_registered")
        combined = request_logs.records[-1].getMessage() + audit_logs.records[-1].getMessage()
        for secret in ("private@example.test", "correct horse battery", "jp_assist_session"):
            self.assertNotIn(secret, combined)

        with self.app.state.platform.database.connect() as connection:
            metadata = json.loads(connection.execute(
                "SELECT metadata_json FROM audit_events WHERE event_type = 'account_registered'"
            ).fetchone()[0])
        self.assertEqual(metadata["requestId"], "acceptance-request-1")

    def test_invalid_request_id_is_replaced_and_operational_metrics_are_exported(self):
        with self.assertLogs("jp_assist.request", level="INFO"):
            response = self.client.get("/healthz", headers={"X-Request-ID": "invalid request/id"})
            uuid.UUID(response.headers["X-Request-ID"])
            self.app.state.observability.record_event_batch(2, 1)
            metrics = self.client.get("/internal/metrics")
        self.assertEqual(metrics.status_code, 200)
        self.assertIn("text/plain", metrics.headers["content-type"])
        self.assertIn("version=", metrics.headers["content-type"])
        body = metrics.text
        expected = (
            "jp_assist_http_requests_total",
            "jp_assist_http_request_duration_seconds",
            "jp_assist_events_ingested_total 2.0",
            "jp_assist_events_duplicate_total 1.0",
            "jp_assist_ready 1.0",
            "jp_assist_database_bytes",
            "jp_assist_events_stored 0.0",
            "jp_assist_mail_delivery_failures 0.0",
            "jp_assist_backup_files 1.0",
            "jp_assist_backup_latest_timestamp_seconds",
        )
        for metric in expected:
            self.assertIn(metric, body)


if __name__ == "__main__":
    unittest.main()
