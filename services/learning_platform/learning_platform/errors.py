class PlatformError(Exception):
    """Base class for errors that can be safely mapped to an API response."""

    status_code = 400
    code = "platform_error"


class ValidationError(PlatformError):
    code = "validation_error"


class AuthenticationError(PlatformError):
    status_code = 401
    code = "authentication_failed"


class ConflictError(PlatformError):
    status_code = 409
    code = "conflict"


class NotFoundError(PlatformError):
    status_code = 404
    code = "not_found"


class PairingPendingError(PlatformError):
    status_code = 428
    code = "authorization_pending"


class PairingExpiredError(PlatformError):
    status_code = 410
    code = "pairing_expired"
