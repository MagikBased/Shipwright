from __future__ import annotations

import threading
import time
from collections import defaultdict, deque
from collections.abc import Callable


class SlidingWindowRateLimiter:
    def __init__(self, clock: Callable[[], float] = time.monotonic):
        self._clock = clock
        self._lock = threading.Lock()
        self._requests: dict[tuple[str, str], deque[float]] = defaultdict(deque)
        self._checks = 0

    def check(self, bucket: str, identity: str, limit: int, window_seconds: int) -> int | None:
        if limit == 0:
            return None
        now = self._clock()
        cutoff = now - window_seconds
        key = (bucket, identity)
        with self._lock:
            self._checks += 1
            if self._checks % 1024 == 0:
                stale = [
                    stored_key
                    for stored_key, values in self._requests.items()
                    if not values or values[-1] <= cutoff
                ]
                for stored_key in stale:
                    self._requests.pop(stored_key, None)
            requests = self._requests[key]
            while requests and requests[0] <= cutoff:
                requests.popleft()
            if len(requests) >= limit:
                return max(1, int(requests[0] + window_seconds - now) + 1)
            requests.append(now)
        return None
