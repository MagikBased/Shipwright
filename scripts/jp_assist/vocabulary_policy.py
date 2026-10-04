"""Shared policy for vocabulary suitable for reusable language study."""

from __future__ import annotations


# Character-specific sentence endings are part of the game's characterization,
# not generally useful Japanese vocabulary. They use ordinary-looking kana, so
# they need explicit identities rather than a broad spelling rule.
NON_TRANSFERABLE_IDENTITIES = {
    ("ゴロ", "ごろ", "override:ゴロ|ごろ"),
    ("ゾラ", "ぞら", "override:ゾラ|ぞら"),
    ("コロ", "ころ", "override:コロ|ころ"),
    ("ゴロォ", "ごろぉ", "override:ゴロォ|ごろぉ"),
}


def is_transferable_identity(identity: tuple[str, str, str]) -> bool:
    """Return whether an identity belongs in decks and language statistics."""
    return (
        not identity[2].startswith(("interface:", "proper:", "unresolved:"))
        and identity not in NON_TRANSFERABLE_IDENTITIES
    )
