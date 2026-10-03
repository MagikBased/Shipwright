from __future__ import annotations

import hashlib
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


class ReviewRequest(ApiModel):
    wordId: str
    senseId: str | None = None
    rating: StrictInt
    source: str = "web"


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
) -> FastAPI:
    root = Path(__file__).resolve().parent.parent
    settings = settings or Settings.from_environment(database_path)
    settings.validate()
    platform = LearningPlatform(settings.database_path)
    rate_limiter = rate_limiter or SlidingWindowRateLimiter()
    app = FastAPI(
        title="JP Assist Learning Platform",
        version="0.2.0",
        docs_url=None if settings.production else "/docs",
        redoc_url=None if settings.production else "/redoc",
        openapi_url=None if settings.production else "/openapi.json",
    )
    app.state.platform = platform
    app.state.settings = settings
    if settings.allowed_hosts != ("*",):
        app.add_middleware(TrustedHostMiddleware, allowed_hosts=list(settings.allowed_hosts))

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
                return JSONResponse(
                    status_code=429,
                    headers={"Retry-After": str(retry_after)},
                    content={
                        "error": {
                            "code": "rate_limited",
                            "message": "Too many requests; retry later",
                        }
                    },
                )
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
    def register(payload: RegisterRequest, response: Response) -> dict[str, Any]:
        result = platform.register_user(payload.email, payload.password, payload.displayName)
        _set_session_cookie(response, result["token"], settings.cookie_secure)
        return result

    @app.post("/v1/auth/login")
    def login(payload: LoginRequest, response: Response) -> dict[str, Any]:
        result = platform.login(payload.email, payload.password)
        _set_session_cookie(response, result["token"], settings.cookie_secure)
        return result

    @app.post("/v1/auth/logout", status_code=204)
    def logout(request: Request, response: Response, authorization: str | None = Header(default=None)) -> None:
        token = session_token(request, authorization)
        platform.logout(token)
        response.delete_cookie("jp_assist_session", path="/", samesite="lax")

    @app.get("/v1/me")
    def me(request: Request, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        return platform.authenticate_session(session_token(request, authorization))

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
            payload.dailyReviews, payload.remindersEnabled,
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
            response.delete_cookie("jp_assist_session", path="/", samesite="lax")

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
        response.delete_cookie("jp_assist_session", path="/", samesite="lax")

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


def _request_identity(request: Request, settings: Settings) -> str:
    if settings.trust_proxy_headers:
        forwarded = request.headers.get("x-forwarded-for", "").split(",", 1)[0].strip()
        if forwarded:
            return forwarded
    return request.client.host if request.client is not None else "unknown"


def _rate_limit_rule(request: Request, settings: Settings) -> tuple[str, str, int] | None:
    if request.method != "POST":
        return None
    path = request.url.path
    identity = _request_identity(request, settings)
    if path in {"/v1/auth/register", "/v1/auth/login"}:
        return "auth", identity, settings.auth_rate_limit
    if path.startswith("/v1/device-pairings"):
        return "pairing", identity, settings.pairing_rate_limit
    if path == "/v1/events/batch":
        authorization = request.headers.get("authorization", "")
        if authorization:
            identity = hashlib.sha256(authorization.encode("utf-8")).hexdigest()
        return "events", identity, settings.event_rate_limit
    return None
