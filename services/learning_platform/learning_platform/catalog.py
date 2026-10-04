from __future__ import annotations

import json
from copy import deepcopy
from pathlib import Path
from typing import Any


REQUIRED_CONTENT_REVIEW_CRITERIA = {
    "japanese-usage-and-reading",
    "english-translation-and-sense",
    "original-non-corpus-example",
    "fantasy-tone-without-unrelated-ip-names",
    "spoiler-appropriate-for-chapter",
}


class GameCatalog:
    """Loads the reviewed, repository-owned game learning catalog."""

    def __init__(self, content_root: Path | None = None):
        self.content_root = content_root or Path(__file__).resolve().parent / "content" / "games"
        self._vocabulary = self._load_vocabulary()
        self._card_manifests = self._load_card_manifests()
        self._games = self._load_games()

    def list_games(self) -> list[dict[str, Any]]:
        results = []
        for game in self._games.values():
            summary = self._summary(game)
            vocabulary = self._vocabulary.get(game["id"])
            if vocabulary is not None:
                summary["languageProfile"] = deepcopy(vocabulary["summary"])
            results.append(summary)
        return results

    def get_game(self, game_id: str) -> dict[str, Any] | None:
        game = self._games.get(game_id)
        if game is None:
            return None
        result = deepcopy(game)
        vocabulary = self._vocabulary.get(game_id)
        if vocabulary is not None:
            result["languageProfile"] = deepcopy(vocabulary["summary"])
            result["languageProfile"]["methodology"] = deepcopy(vocabulary["methodology"])
        return result

    def get_chapter_for_export(
        self, game_id: str, chapter_id: str,
    ) -> tuple[dict[str, Any], dict[str, Any]] | None:
        game = self._games.get(game_id)
        manifest = self._card_manifests.get(game_id)
        if game is None or manifest is None:
            return None
        chapter = next((item for item in game["chapters"] if item["id"] == chapter_id), None)
        content = next(
            (item for item in manifest["chapters"] if item["chapterId"] == chapter_id), None
        )
        if chapter is None or content is None:
            return None
        result = deepcopy(chapter)
        result["reviewedCards"] = deepcopy(content.get("cards", []))
        return deepcopy(game), result

    def list_vocabulary(
        self, game_id: str, search: str = "", jlpt_level: str | None = None,
        limit: int = 50, offset: int = 0,
    ) -> dict[str, Any] | None:
        vocabulary = self._vocabulary.get(game_id)
        if vocabulary is None:
            return None
        if jlpt_level is not None and jlpt_level not in {"N5", "N4", "N3", "N2", "N1", "unclassified"}:
            raise ValueError("jlptLevel is invalid")
        if not 1 <= limit <= 250 or offset < 0:
            raise ValueError("limit must be 1-250 and offset cannot be negative")
        needle = search.strip().casefold()
        words = vocabulary["words"]
        if needle:
            words = [word for word in words if any(
                needle in str(word.get(field) or "").casefold()
                for field in ("wordId", "written", "reading", "meaning")
            )]
        if jlpt_level:
            words = [word for word in words if (word["jlptLevel"] or "unclassified") == jlpt_level]
        return {
            "gameId": game_id,
            "total": len(words),
            "limit": limit,
            "offset": offset,
            "words": deepcopy(words[offset:offset + limit]),
        }

    def vocabulary_word_ids(self, game_id: str) -> set[str] | None:
        vocabulary = self._vocabulary.get(game_id)
        if vocabulary is None:
            return None
        return {word["wordId"] for word in vocabulary["words"]}

    def coverage(self, game_id: str, known_word_ids: set[str]) -> dict[str, Any] | None:
        vocabulary = self._vocabulary.get(game_id)
        if vocabulary is None:
            return None
        words = vocabulary["words"]
        known = [word for word in words if word["wordId"] in known_word_ids]
        total_by_level = vocabulary["summary"]["uniqueByLevel"]
        known_by_level = {level: 0 for level in total_by_level}
        for word in known:
            known_by_level[word["jlptLevel"] or "unclassified"] += 1
        levels = {
            level: {
                "known": known_by_level[level],
                "total": total,
                "percent": round(known_by_level[level] * 100 / total, 1) if total else 0.0,
            }
            for level, total in total_by_level.items()
        }
        total = len(words)
        return {
            "gameId": game_id,
            "knownWords": len(known),
            "totalWords": total,
            "percentKnown": round(len(known) * 100 / total, 1) if total else 0.0,
            "knownWordIds": [word["wordId"] for word in known],
            "levels": levels,
        }

    def _load_games(self) -> dict[str, dict[str, Any]]:
        games: dict[str, dict[str, Any]] = {}
        for path in sorted(self.content_root.glob("*.json")):
            if path.name.endswith((".vocabulary.json", ".cards.json")):
                continue
            with path.open(encoding="utf-8") as source:
                game = json.load(source)
            card_manifest = self._card_manifests.get(game["id"])
            self._validate(game, path, card_manifest)
            if card_manifest is not None:
                game["contentReview"] = deepcopy(card_manifest["contentReview"])
            games[game["id"]] = game
        return games

    def _load_vocabulary(self) -> dict[str, dict[str, Any]]:
        manifests: dict[str, dict[str, Any]] = {}
        for path in sorted(self.content_root.glob("*.vocabulary.json")):
            with path.open(encoding="utf-8") as source:
                manifest = json.load(source)
            game_id = manifest.get("gameId")
            words = manifest.get("words")
            if not isinstance(game_id, str) or not game_id or not isinstance(words, list):
                raise ValueError(f"Vocabulary manifest in {path} is invalid")
            word_ids = [word.get("wordId") for word in words]
            if any(not word_id for word_id in word_ids) or len(word_ids) != len(set(word_ids)):
                raise ValueError(f"Vocabulary manifest for {game_id} has invalid word ids")
            manifests[game_id] = manifest
        return manifests

    def _load_card_manifests(self) -> dict[str, dict[str, Any]]:
        manifests: dict[str, dict[str, Any]] = {}
        for path in sorted(self.content_root.glob("*.cards.json")):
            with path.open(encoding="utf-8") as source:
                manifest = json.load(source)
            game_id = manifest.get("gameId")
            chapters = manifest.get("chapters")
            if not isinstance(game_id, str) or not game_id or not isinstance(chapters, list):
                raise ValueError(f"Card manifest in {path} is invalid")
            manifests[game_id] = manifest
        return manifests

    @staticmethod
    def _validate(
        game: dict[str, Any], path: Path, card_manifest: dict[str, Any] | None = None,
    ) -> None:
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
        manifest_chapters = {
            item.get("chapterId"): item
            for item in (card_manifest or {}).get("chapters", [])
        }
        if game.get("cardManifest") and card_manifest is None:
            raise ValueError(f"Catalog game {game_id} references a missing card manifest")
        if card_manifest is not None and set(manifest_chapters) != known:
            raise ValueError(f"Card manifest for {game_id} must contain every chapter")
        if card_manifest is not None:
            review = card_manifest.get("contentReview") or {}
            reviewed_card_count = sum(
                len(item.get("cards", [])) for item in manifest_chapters.values()
            )
            if review.get("status") != "reviewed":
                raise ValueError(f"Card manifest for {game_id} is not reviewed")
            if review.get("criteriaVersion") != 1:
                raise ValueError(
                    f"Card manifest for {game_id} has an unsupported review criteria version"
                )
            if review.get("reviewedCardCount") != reviewed_card_count:
                raise ValueError(f"Card manifest for {game_id} has a stale review card count")
            missing_criteria = REQUIRED_CONTENT_REVIEW_CRITERIA - set(
                review.get("criteria", [])
            )
            if missing_criteria:
                raise ValueError(
                    f"Card manifest for {game_id} has incomplete review criteria"
                )
        for chapter in chapters:
            references = chapter.get("prerequisites", []) + chapter.get("recommendedAfter", [])
            if any(reference not in known for reference in references):
                raise ValueError(f"Catalog game {game_id} has an unknown chapter reference")
            cards = chapter.get("sampleCards", [])
            manifest_chapter = manifest_chapters.get(chapter["id"])
            reviewed_cards = (
                manifest_chapter.get("cards") if manifest_chapter is not None else cards
            )
            if not isinstance(reviewed_cards, list):
                raise ValueError(f"Card manifest chapter {chapter['id']} has invalid cards")
            if chapter.get("deck", {}).get("reviewedCardCount") != len(reviewed_cards):
                raise ValueError(f"Catalog chapter {chapter['id']} reviewed card count is stale")
            card_ids = [card.get("id") for card in reviewed_cards]
            if len(card_ids) != len(set(card_ids)) or any(not value for value in card_ids):
                raise ValueError(f"Catalog chapter {chapter['id']} card ids must be unique")
            reviewed_by_id = {card["id"]: card for card in reviewed_cards}
            for card in cards:
                if reviewed_by_id.get(card.get("id")) != card:
                    raise ValueError(
                        f"Catalog chapter {chapter['id']} preview card is stale"
                    )
            for card in reviewed_cards:
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
