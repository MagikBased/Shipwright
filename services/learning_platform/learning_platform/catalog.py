from __future__ import annotations

import json
from copy import deepcopy
from pathlib import Path
from typing import Any


class GameCatalog:
    """Loads the reviewed, repository-owned game learning catalog."""

    def __init__(self, content_root: Path | None = None):
        self.content_root = content_root or Path(__file__).resolve().parent / "content" / "games"
        self._games = self._load_games()

    def list_games(self) -> list[dict[str, Any]]:
        return [self._summary(game) for game in self._games.values()]

    def get_game(self, game_id: str) -> dict[str, Any] | None:
        game = self._games.get(game_id)
        return deepcopy(game) if game is not None else None

    def _load_games(self) -> dict[str, dict[str, Any]]:
        games: dict[str, dict[str, Any]] = {}
        for path in sorted(self.content_root.glob("*.json")):
            with path.open(encoding="utf-8") as source:
                game = json.load(source)
            self._validate(game, path)
            games[game["id"]] = game
        return games

    @staticmethod
    def _validate(game: dict[str, Any], path: Path) -> None:
        game_id = game.get("id")
        chapters = game.get("chapters")
        if not isinstance(game_id, str) or not game_id:
            raise ValueError(f"Catalog game in {path} has no id")
        if not isinstance(chapters, list) or not chapters:
            raise ValueError(f"Catalog game {game_id} has no chapters")
        orders = [chapter.get("order") for chapter in chapters]
        chapter_ids = [chapter.get("id") for chapter in chapters]
        if orders != list(range(1, len(chapters) + 1)):
            raise ValueError(f"Catalog game {game_id} chapters must have contiguous order")
        if len(chapter_ids) != len(set(chapter_ids)) or any(not value for value in chapter_ids):
            raise ValueError(f"Catalog game {game_id} chapter ids must be unique")
        known = set(chapter_ids)
        for chapter in chapters:
            references = chapter.get("prerequisites", []) + chapter.get("recommendedAfter", [])
            if any(reference not in known for reference in references):
                raise ValueError(f"Catalog game {game_id} has an unknown chapter reference")
            cards = chapter.get("sampleCards", [])
            if chapter.get("deck", {}).get("reviewedCardCount") != len(cards):
                raise ValueError(f"Catalog chapter {chapter['id']} reviewed card count is stale")
            card_ids = [card.get("id") for card in cards]
            if len(card_ids) != len(set(card_ids)) or any(not value for value in card_ids):
                raise ValueError(f"Catalog chapter {chapter['id']} card ids must be unique")
            for card in cards:
                evidence = card.get("corpusEvidence", {})
                if not evidence.get("identity") or not evidence.get("messageIds"):
                    raise ValueError(f"Catalog card {card['id']} has no corpus evidence")

    @staticmethod
    def _summary(game: dict[str, Any]) -> dict[str, Any]:
        chapters = game["chapters"]
        return {
            "id": game["id"],
            "title": game["title"],
            "series": game["series"],
            "platform": game["platform"],
            "status": game["status"],
            "summary": game["summary"],
            "chapterCount": len(chapters),
            "reviewedCardCount": sum(chapter["deck"]["reviewedCardCount"] for chapter in chapters),
            "audioStatus": game["audio"]["status"],
        }
