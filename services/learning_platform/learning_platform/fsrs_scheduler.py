import json
from dataclasses import dataclass
from datetime import datetime
from typing import Any, Iterable

from fsrs import Card, Rating, Scheduler, State


SCHEDULER_VERSION = "fsrs-6.3.2"
ALGORITHM_VERSION = "FSRS-6"
DESIRED_RETENTION = 0.9
_SCHEDULER = Scheduler(desired_retention=DESIRED_RETENTION, enable_fuzzing=False)
PARAMETERS = tuple(float(value) for value in _SCHEDULER.parameters)
PARAMETERS_JSON = json.dumps(PARAMETERS, separators=(",", ":"))


@dataclass(frozen=True)
class ReviewEvent:
    rating: int
    reviewed_at: datetime


def card_from_row(row: Any | None) -> Card:
    if row is None or row["scheduler_version"] != SCHEDULER_VERSION:
        return Card(card_id=0)
    return Card(
        card_id=0,
        state=State(int(row["card_state"])),
        step=row["step"],
        stability=row["stability"],
        difficulty=row["difficulty"],
        due=datetime.fromisoformat(row["due_at"].replace("Z", "+00:00")),
        last_review=(
            datetime.fromisoformat(row["last_reviewed_at"].replace("Z", "+00:00"))
            if row["last_reviewed_at"] else None
        ),
    )


def schedule(card: Card, rating: int, reviewed_at: datetime) -> tuple[Card, dict[str, Any]]:
    previous_review = card.last_review
    reviewed, _ = _SCHEDULER.review_card(card, Rating(rating), reviewed_at)
    scheduled_days = (reviewed.due - reviewed_at).total_seconds() / 86400
    elapsed_days = (
        max(0.0, (reviewed_at - previous_review).total_seconds() / 86400)
        if previous_review else 0.0
    )
    return reviewed, {
        "schedulerVersion": SCHEDULER_VERSION,
        "algorithmVersion": ALGORITHM_VERSION,
        "parametersJson": PARAMETERS_JSON,
        "desiredRetention": DESIRED_RETENTION,
        "cardState": int(reviewed.state),
        "step": reviewed.step,
        "stability": reviewed.stability,
        "difficulty": reviewed.difficulty,
        "scheduledDays": scheduled_days,
        "elapsedDays": elapsed_days,
    }


def replay(events: Iterable[ReviewEvent]) -> tuple[Card, dict[str, Any] | None, int, int]:
    card = Card(card_id=0)
    projection = None
    repetitions = 0
    lapses = 0
    for event in events:
        card, projection = schedule(card, event.rating, event.reviewed_at)
        repetitions += 1
        if event.rating == int(Rating.Again):
            lapses += 1
    return card, projection, repetitions, lapses


def previews(card: Card, reviewed_at: datetime) -> list[dict[str, Any]]:
    result = []
    for rating in Rating:
        candidate = Card.from_dict(card.to_dict())
        candidate, projection = schedule(candidate, int(rating), reviewed_at)
        result.append({
            "rating": int(rating),
            "dueAt": candidate.due,
            "intervalDays": projection["scheduledDays"],
            "cardState": projection["cardState"],
        })
    return result
