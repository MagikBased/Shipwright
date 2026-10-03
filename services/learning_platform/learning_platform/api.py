from __future__ import annotations

import hashlib
import secrets
from pathlib import Path
from typing import Any

from fastapi import FastAPI, Header, Request, Response
from fastapi.responses import FileResponse, JSONResponse
from fastapi.staticfiles import StaticFiles
from starlette.middleware.trustedhost import TrustedHostMiddleware
from pydantic import BaseModel, Field, StrictInt

from .config import Settings
from .database import LATEST_SCHEMA_VERSION
from .errors import AuthenticationError, PlatformError
from .mailer import Mailer, mailer_from_settings
from .rate_limit import SlidingWindowRateLimiter
from .service import LearningPlatform


class ApiModel(BaseModel):
    class Config:
        extra = "forbid"


class RegisterRequest(ApiModel):
    email: str
    password: str
    displayName: str


class LoginRequest(ApiModel):
    email: str
    password: str


class EmailRequest(ApiModel):
    email: str


class TokenRequest(ApiModel):
    token: str


class PasswordResetRequest(ApiModel):
    token: str
    newPassword: str


class EmailChangeRequest(ApiModel):
    password: str
    newEmail: str


class NotificationPreferencesRequest(ApiModel):
    reviewReminders: bool = False
    productUpdates: bool = False
    reminderHour: StrictInt = 18
    timezone: str = "UTC"


class PairingRequest(ApiModel):
    deviceName: str
    adapterId: str
    gameId: str


class PairingApprovalRequest(ApiModel):
    userCode: str


class PairingClaimRequest(ApiModel):
    deviceCode: str


class EventRequest(ApiModel):
    eventId: str
    type: str
    occurredAt: str
    gameId: str
    adapterId: str
    contentVersion: str
    wordId: str | None = None
    senseId: str | None = None
    messageId: str | None = None
    pageIndex: StrictInt | None = None
    locationId: str | None = None
    count: int = 1


class EventBatchRequest(ApiModel):
    events: list[EventRequest]


class WordAnnotationRequest(ApiModel):
    wordId: str
    senseId: str | None = None
    learningState: str = "new"
    note: str = ""
    tags: list[str] = Field(default_factory=list)


class GoalsRequest(ApiModel):
    dailyNewWords: StrictInt
    dailyReviews: StrictInt
    remindersEnabled: bool = False
    timezone: str = "UTC"


class ReviewRequest(ApiModel):
    wordId: str
    senseId: str | None = None
    rating: StrictInt
    source: str = "web"


class BuryReviewRequest(ApiModel):
    wordId: str
    senseId: str | None = None


class ReviewCollectionRequest(ApiModel):
    gameId: str | None = None
    reviewOwner: str
    ankiDeck: str
    confirmed: bool = False


class AnkiSyncCompleteRequest(ApiModel):
    gameId: str | None = None


class AnkiReviewImportItem(ApiModel):
    sourceReviewId: str
    sourceCardId: str
    wordId: str
    senseId: str | None = None
    rating: StrictInt
    reviewedAt: str
    intervalDays: float = 0
    previousIntervalDays: float = 0
    factor: StrictInt | None = None
    durationMs: StrictInt | None = None
    reviewType: StrictInt | None = None


class AnkiReviewImportRequest(ApiModel):
    gameId: str | None = None
    reviews: list[AnkiReviewImportItem]


class PasswordRequest(ApiModel):
    currentPassword: str
    newPassword: str


class DeleteAccountRequest(ApiModel):
    password: str


class ClearGameRequest(ApiModel):
    gameId: str


def model_dict(model: BaseModel) -> dict[str, Any]:
    # Pydantic 2 renamed dict() to model_dump(); support both so the small MVP
    # is friendly to distributions that still package Pydantic 1.
    if hasattr(model, "model_dump"):
        return model.model_dump(exclude_none=True)
    return model.dict(exclude_none=True)


def bearer_token(authorization: str | None) -> str:
    if not authorization or not authorization.startswith("Bearer "):
        raise AuthenticationError("A bearer token is required")
    token = authorization[7:].strip()
    if not token:
        raise AuthenticationError("A bearer token is required")
    return token


def session_token(request: Request, authorization: str | None) -> str:
    if authorization:
        return bearer_token(authorization)
    token = request.cookies.get("jp_assist_session")
    if not token:
        raise AuthenticationError("A website session is required")
    return token


def create_app(
    database_path: str | Path | None = None,
    settings: Settings | None = None,
    rate_limiter: SlidingWindowRateLimiter | None = None,
    mailer: Mailer | None = None,
) -> FastAPI:
    root = Path(__file__).resolve().parent.parent
    settings = settings or Settings.from_environment(database_path)
    settings.validate()
    if mailer is None:
        mailer = mailer_from_settings(settings)
    platform = LearningPlatform(
        settings.database_path, mailer=mailer, public_base_url=settings.public_base_url,
    )
    rate_limiter = rate_limiter or SlidingWindowRateLimiter()
    app = FastAPI(
        title="JP Assist Learning Platform",
        version="0.3.0",
        docs_url=None if settings.production else "/docs",
        redoc_url=None if settings.production else "/redoc",
        openapi_url=None if settings.production else "/openapi.json",
    )
    app.state.platform = platform
    app.state.settings = settings
    if settings.allowed_hosts != ("*",):
        app.add_middleware(TrustedHostMiddleware, allowed_hosts=list(settings.allowed_hosts))

    @app.middleware("http")
    async def browser_security(request: Request, call_next):
        csrf_error = _csrf_error(request)
        response = csrf_error if csrf_error is not None else await call_next(request)
        if request.cookies.get("jp_assist_session") and not request.cookies.get("jp_assist_csrf"):
            _set_csrf_cookie(response, secrets.token_urlsafe(32), settings.cookie_secure)
        _set_security_headers(response)
        return response

    @app.middleware("http")
    async def enforce_rate_limits(request: Request, call_next):
        rule = _rate_limit_rule(request, settings)
        if rule is not None:
            bucket, identity, limit = rule
            retry_after = rate_limiter.check(
                bucket,
                identity,
                limit,
                settings.rate_limit_window_seconds,
            )
            if retry_after is not None:
                response = JSONResponse(
                    status_code=429,
                    headers={"Retry-After": str(retry_after)},
                    content={
                        "error": {
                            "code": "rate_limited",
                            "message": "Too many requests; retry later",
                        }
                    },
                )
                _set_security_headers(response)
                return response
        return await call_next(request)

    @app.exception_handler(PlatformError)
    async def platform_error_handler(_request: Request, error: PlatformError) -> JSONResponse:
        return JSONResponse(
            status_code=error.status_code,
            content={"error": {"code": error.code, "message": str(error)}},
        )

    @app.get("/healthz")
    def health() -> dict[str, str]:
        return {"status": "ok"}

    @app.get("/readyz")
    def readiness(response: Response) -> dict[str, Any]:
        ready, detail = platform.database.readiness()
        if not ready:
            response.status_code = 503
        return {
            "status": "ready" if ready else "unavailable",
            "database": detail,
            "schemaVersion": platform.database.schema_version() if ready else None,
            "expectedSchemaVersion": LATEST_SCHEMA_VERSION,
        }

    @app.post("/v1/auth/register", status_code=201)
    def register(payload: RegisterRequest, request: Request, response: Response) -> dict[str, Any]:
        result = platform.register_user(
            payload.email, payload.password, payload.displayName, _session_label(request),
        )
        _set_session_cookie(response, result["token"], settings.cookie_secure)
        return result

    @app.post("/v1/auth/login")
    def login(payload: LoginRequest, request: Request, response: Response) -> dict[str, Any]:
        result = platform.login(payload.email, payload.password, _session_label(request))
        _set_session_cookie(response, result["token"], settings.cookie_secure)
        return result

    @app.post("/v1/auth/verify-email")
    def verify_email(payload: TokenRequest) -> dict[str, Any]:
        return platform.verify_email(payload.token)

    @app.post("/v1/auth/password-reset/request")
    def request_password_reset(payload: EmailRequest) -> dict[str, str]:
        return platform.request_password_reset(payload.email)

    @app.post("/v1/auth/password-reset/complete", status_code=204)
    def complete_password_reset(payload: PasswordResetRequest, response: Response) -> None:
        platform.reset_password(payload.token, payload.newPassword)
        _clear_session_cookies(response)

    @app.post("/v1/auth/change-email/confirm")
    def confirm_email_change(payload: TokenRequest, response: Response) -> dict[str, Any]:
        user = platform.confirm_email_change(payload.token)
        _clear_session_cookies(response)
        return user

    @app.post("/v1/auth/notifications/unsubscribe", status_code=204)
    def unsubscribe_notifications(payload: TokenRequest) -> None:
        platform.unsubscribe_review_reminders(payload.token)

    @app.post("/v1/auth/logout", status_code=204)
    def logout(request: Request, response: Response, authorization: str | None = Header(default=None)) -> None:
        token = session_token(request, authorization)
        platform.logout(token)
        _clear_session_cookies(response)

    @app.get("/v1/me")
    def me(request: Request, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        return platform.authenticate_session(session_token(request, authorization))

    @app.post("/v1/me/verification-email")
    def resend_verification_email(
        request: Request, authorization: str | None = Header(default=None),
    ) -> dict[str, Any]:
        return platform.resend_email_verification(session_token(request, authorization))

    @app.post("/v1/me/change-email")
    def request_email_change(
        payload: EmailChangeRequest, request: Request,
        authorization: str | None = Header(default=None),
    ) -> dict[str, Any]:
        return platform.request_email_change(
            session_token(request, authorization), payload.password, payload.newEmail,
        )

    @app.get("/v1/me/notification-preferences")
    def notification_preferences(
        request: Request, authorization: str | None = Header(default=None),
    ) -> dict[str, Any]:
        return platform.get_notification_preferences(session_token(request, authorization))

    @app.put("/v1/me/notification-preferences")
    def update_notification_preferences(
        payload: NotificationPreferencesRequest, request: Request,
        authorization: str | None = Header(default=None),
    ) -> dict[str, Any]:
        return platform.update_notification_preferences(
            session_token(request, authorization), payload.reviewReminders,
            payload.productUpdates, payload.reminderHour, payload.timezone,
        )

    @app.post("/v1/me/notification-preferences/test")
    def send_test_review_reminder(
        request: Request, authorization: str | None = Header(default=None),
    ) -> dict[str, bool]:
        return platform.send_test_review_reminder(session_token(request, authorization))

    @app.post("/v1/device-pairings", status_code=201)
    def start_pairing(payload: PairingRequest) -> dict[str, Any]:
        return platform.start_pairing(payload.deviceName, payload.adapterId, payload.gameId)

    @app.post("/v1/device-pairings/approve")
    def approve_pairing(
        payload: PairingApprovalRequest,
        request: Request,
        authorization: str | None = Header(default=None),
    ) -> dict[str, Any]:
        return platform.approve_pairing(session_token(request, authorization), payload.userCode)

    @app.post("/v1/device-pairings/token")
    def claim_pairing(payload: PairingClaimRequest) -> dict[str, Any]:
        return platform.claim_pairing(payload.deviceCode)

    @app.post("/v1/events/batch")
    def ingest_events(payload: EventBatchRequest, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        return platform.ingest_events(
            bearer_token(authorization),
            [model_dict(event) for event in payload.events],
        )

    @app.get("/v1/me/devices")
    def devices(request: Request, authorization: str | None = Header(default=None)) -> list[dict[str, Any]]:
        return platform.list_devices(session_token(request, authorization))

    @app.delete("/v1/me/devices/{device_id}", status_code=204)
    def revoke_device(
        device_id: str,
        request: Request,
        authorization: str | None = Header(default=None),
    ) -> None:
        platform.revoke_device(session_token(request, authorization), device_id)

    @app.get("/v1/me/stats")
    def stats(request: Request, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        return platform.get_stats(session_token(request, authorization))

    @app.get("/v1/me/words")
    def words(
        request: Request,
        savedOnly: bool = False,
        search: str = "",
        gameId: str | None = None,
        learningState: str | None = None,
        sort: str = "frequency",
        limit: int = 250,
        offset: int = 0,
        authorization: str | None = Header(default=None),
    ) -> list[dict[str, Any]]:
        return platform.list_word_progress(
            session_token(request, authorization), savedOnly, search, gameId, learningState, sort, limit, offset
        )

    @app.put("/v1/me/words/annotation")
    def update_word_annotation(
        payload: WordAnnotationRequest,
        request: Request,
        authorization: str | None = Header(default=None),
    ) -> dict[str, Any]:
        return platform.update_word_annotation(
            session_token(request, authorization), payload.wordId, payload.senseId,
            payload.learningState, payload.note, payload.tags,
        )

    @app.get("/v1/me/activity")
    def activity(
        request: Request, days: int = 30, authorization: str | None = Header(default=None)
    ) -> dict[str, Any]:
        return platform.activity(session_token(request, authorization), days)

    @app.get("/v1/me/goals")
    def goals(request: Request, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        return platform.get_goals(session_token(request, authorization))

    @app.put("/v1/me/goals")
    def update_goals(
        payload: GoalsRequest, request: Request, authorization: str | None = Header(default=None)
    ) -> dict[str, Any]:
        return platform.update_goals(
            session_token(request, authorization), payload.dailyNewWords,
            payload.dailyReviews, payload.remindersEnabled, payload.timezone,
        )

    @app.get("/v1/me/reviews/queue")
    def review_queue(
        request: Request, limit: int = 20, authorization: str | None = Header(default=None)
    ) -> list[dict[str, Any]]:
        return platform.review_queue(session_token(request, authorization), limit)

    @app.post("/v1/me/reviews")
    def submit_review(
        payload: ReviewRequest, request: Request, authorization: str | None = Header(default=None)
    ) -> dict[str, Any]:
        return platform.submit_review(
            session_token(request, authorization), payload.wordId, payload.senseId,
            payload.rating, payload.source,
        )

    @app.post("/v1/me/reviews/bury")
    def bury_review(
        payload: BuryReviewRequest, request: Request, authorization: str | None = Header(default=None)
    ) -> dict[str, Any]:
        return platform.bury_review(
            session_token(request, authorization), payload.wordId, payload.senseId,
        )

    @app.get("/v1/me/review-collection")
    def review_collection(
        request: Request, gameId: str | None = None, authorization: str | None = Header(default=None)
    ) -> dict[str, Any]:
        return platform.get_review_collection(session_token(request, authorization), gameId)

    @app.put("/v1/me/review-collection")
    def update_review_collection(
        payload: ReviewCollectionRequest, request: Request,
        authorization: str | None = Header(default=None),
    ) -> dict[str, Any]:
        return platform.update_review_collection(
            session_token(request, authorization), payload.gameId, payload.reviewOwner,
            payload.ankiDeck, payload.confirmed,
        )

    @app.post("/v1/me/review-collection/anki-synced")
    def anki_sync_complete(
        payload: AnkiSyncCompleteRequest, request: Request,
        authorization: str | None = Header(default=None),
    ) -> dict[str, Any]:
        return platform.mark_anki_synced(session_token(request, authorization), payload.gameId)

    @app.post("/v1/me/reviews/import/anki")
    def import_anki_reviews(
        payload: AnkiReviewImportRequest, request: Request,
        authorization: str | None = Header(default=None),
    ) -> dict[str, Any]:
        reviews = [
            {
                "sourceReviewId": item.sourceReviewId, "sourceCardId": item.sourceCardId,
                "wordId": item.wordId, "senseId": item.senseId, "rating": item.rating,
                "reviewedAt": item.reviewedAt, "intervalDays": item.intervalDays,
                "previousIntervalDays": item.previousIntervalDays, "factor": item.factor,
                "durationMs": item.durationMs, "reviewType": item.reviewType,
            }
            for item in payload.reviews
        ]
        return platform.import_anki_reviews(
            session_token(request, authorization), reviews, payload.gameId,
        )

    @app.get("/v1/me/exports/saved-words")
    def saved_words_export(
        request: Request, gameId: str | None = None, authorization: str | None = Header(default=None)
    ) -> dict[str, Any]:
        return platform.saved_word_manifest(session_token(request, authorization), gameId)

    @app.get("/v1/me/exports/account")
    def account_export(request: Request, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        return platform.account_export(session_token(request, authorization))

    @app.get("/v1/me/sessions")
    def sessions(request: Request, authorization: str | None = Header(default=None)) -> list[dict[str, Any]]:
        return platform.list_sessions(session_token(request, authorization))

    @app.delete("/v1/me/sessions/{session_id}", status_code=204)
    def revoke_session(
        session_id: str, request: Request, response: Response,
        authorization: str | None = Header(default=None),
    ) -> None:
        revoked_current = platform.revoke_session(session_token(request, authorization), session_id)
        if revoked_current:
            _clear_session_cookies(response)

    @app.put("/v1/me/password", status_code=204)
    def change_password(
        payload: PasswordRequest, request: Request, authorization: str | None = Header(default=None)
    ) -> None:
        platform.change_password(
            session_token(request, authorization), payload.currentPassword, payload.newPassword
        )

    @app.post("/v1/me/clear-game", status_code=204)
    def clear_game(
        payload: ClearGameRequest, request: Request, authorization: str | None = Header(default=None)
    ) -> None:
        platform.clear_game_progress(session_token(request, authorization), payload.gameId)

    @app.delete("/v1/me", status_code=204)
    def delete_account(
        payload: DeleteAccountRequest, request: Request, response: Response,
        authorization: str | None = Header(default=None),
    ) -> None:
        platform.delete_account(session_token(request, authorization), payload.password)
        _clear_session_cookies(response)

    web_root = root / "web"
    app.mount("/static", StaticFiles(directory=web_root), name="static")

    @app.get("/", include_in_schema=False)
    def website() -> FileResponse:
        return FileResponse(web_root / "index.html")

    return app


def _set_session_cookie(response: Response, token: str, secure: bool) -> None:
    response.set_cookie(
        "jp_assist_session",
        token,
        max_age=30 * 24 * 60 * 60,
        httponly=True,
        secure=secure,
        samesite="lax",
        path="/",
    )
    _set_csrf_cookie(response, secrets.token_urlsafe(32), secure)


def _set_csrf_cookie(response: Response, token: str, secure: bool) -> None:
    response.set_cookie(
        "jp_assist_csrf",
        token,
        max_age=30 * 24 * 60 * 60,
        httponly=False,
        secure=secure,
        samesite="strict",
        path="/",
    )


def _clear_session_cookies(response: Response) -> None:
    response.delete_cookie("jp_assist_session", path="/", samesite="lax")
    response.delete_cookie("jp_assist_csrf", path="/", samesite="strict")


def _csrf_error(request: Request) -> JSONResponse | None:
    if request.method in {"GET", "HEAD", "OPTIONS"}:
        return None
    if not request.cookies.get("jp_assist_session") or request.headers.get("authorization"):
        return None
    path = request.url.path
    requires_session = (
        path == "/v1/auth/logout"
        or path == "/v1/device-pairings/approve"
        or path.startswith("/v1/me")
    )
    if not requires_session:
        return None
    cookie_token = request.cookies.get("jp_assist_csrf", "")
    header_token = request.headers.get("x-csrf-token", "")
    if cookie_token and header_token and secrets.compare_digest(cookie_token, header_token):
        return None
    return JSONResponse(
        status_code=403,
        content={"error": {"code": "csrf_failed", "message": "Refresh the page and try again"}},
    )


def _set_security_headers(response: Response) -> None:
    response.headers["Content-Security-Policy"] = (
        "default-src 'self'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'; "
        "object-src 'none'; img-src 'self' data:; font-src 'self'; style-src 'self'; "
        "script-src 'self'; connect-src 'self' http://127.0.0.1:8765"
    )
    response.headers["Referrer-Policy"] = "no-referrer"
    response.headers["X-Content-Type-Options"] = "nosniff"
    response.headers["X-Frame-Options"] = "DENY"
    response.headers["Permissions-Policy"] = "camera=(), microphone=(), geolocation=()"
    response.headers["Cross-Origin-Opener-Policy"] = "same-origin"


def _request_identity(request: Request, settings: Settings) -> str:
    if settings.trust_proxy_headers:
        forwarded = request.headers.get("x-forwarded-for", "").split(",", 1)[0].strip()
        if forwarded:
            return forwarded
    return request.client.host if request.client is not None else "unknown"


def _session_label(request: Request) -> str:
    return " ".join(request.headers.get("user-agent", "").split())[:160]


def _rate_limit_rule(request: Request, settings: Settings) -> tuple[str, str, int] | None:
    path = request.url.path
    identity = _request_identity(request, settings)
    if path in {
        "/v1/auth/register", "/v1/auth/login", "/v1/auth/password-reset/request",
        "/v1/auth/password-reset/complete", "/v1/auth/verify-email",
        "/v1/auth/change-email/confirm", "/v1/auth/notifications/unsubscribe",
        "/v1/me/verification-email", "/v1/me/change-email",
        "/v1/me/notification-preferences/test",
    }:
        return "auth", identity, settings.auth_rate_limit
    if path.startswith("/v1/device-pairings"):
        return "pairing", identity, settings.pairing_rate_limit
    if path == "/v1/events/batch":
        authorization = request.headers.get("authorization", "")
        if authorization:
            identity = hashlib.sha256(authorization.encode("utf-8")).hexdigest()
        return "events", identity, settings.event_rate_limit
    if path.startswith("/v1/me/reviews"):
        return "reviews", identity, settings.event_rate_limit
    if request.method == "GET" and path.startswith("/v1/me/exports/"):
        return "exports", identity, settings.event_rate_limit
    if request.method in {"POST", "PUT", "PATCH", "DELETE"} and path.startswith("/v1/me"):
        return "account_mutation", identity, settings.auth_rate_limit
    return None
