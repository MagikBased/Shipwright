from __future__ import annotations

import os
from pathlib import Path
from typing import Any

from fastapi import FastAPI, Header, Request, Response
from fastapi.responses import FileResponse, JSONResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, StrictInt

from .errors import AuthenticationError, PlatformError
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


def create_app(database_path: str | Path | None = None) -> FastAPI:
    root = Path(__file__).resolve().parent.parent
    database_path = database_path or os.environ.get("JP_ASSIST_PLATFORM_DB", root / "var" / "platform.sqlite3")
    platform = LearningPlatform(database_path)
    app = FastAPI(title="JP Assist Learning Platform", version="0.1.0")
    app.state.platform = platform

    @app.exception_handler(PlatformError)
    async def platform_error_handler(_request: Request, error: PlatformError) -> JSONResponse:
        return JSONResponse(
            status_code=error.status_code,
            content={"error": {"code": error.code, "message": str(error)}},
        )

    @app.get("/healthz")
    def health() -> dict[str, str]:
        return {"status": "ok"}

    @app.post("/v1/auth/register", status_code=201)
    def register(payload: RegisterRequest, response: Response) -> dict[str, Any]:
        result = platform.register_user(payload.email, payload.password, payload.displayName)
        _set_session_cookie(response, result["token"])
        return result

    @app.post("/v1/auth/login")
    def login(payload: LoginRequest, response: Response) -> dict[str, Any]:
        result = platform.login(payload.email, payload.password)
        _set_session_cookie(response, result["token"])
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
        authorization: str | None = Header(default=None),
    ) -> list[dict[str, Any]]:
        return platform.list_word_progress(session_token(request, authorization), savedOnly)

    @app.get("/v1/me/exports/saved-words")
    def saved_words_export(request: Request, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        return platform.saved_word_manifest(session_token(request, authorization))

    web_root = root / "web"
    app.mount("/static", StaticFiles(directory=web_root), name="static")

    @app.get("/", include_in_schema=False)
    def website() -> FileResponse:
        return FileResponse(web_root / "index.html")

    return app


def _set_session_cookie(response: Response, token: str) -> None:
    response.set_cookie(
        "jp_assist_session",
        token,
        max_age=30 * 24 * 60 * 60,
        httponly=True,
        secure=os.environ.get("JP_ASSIST_COOKIE_SECURE") == "1",
        samesite="lax",
        path="/",
    )
