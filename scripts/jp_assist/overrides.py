"""Manual overrides for lemmas that a general-purpose dictionary (JMdict via
jamdict) doesn't have, or gets wrong for this game: proper nouns, invented
Zelda terminology, and archaic/compound forms (docs/JP_ASSIST_DESIGN.md
section 7.2 step 8). Keyed by (lemma, reading) so the override is specific
to the actual word sense encountered, not just the surface string.

This is a hand-authored starter set covering terms in the recorded test
dialogues (docs/JP_ASSIST_DESIGN.md Milestone 0) - not a complete
Zelda-terminology glossary.
"""

OVERRIDES: dict[tuple[str, str], dict] = {
    ("コキリ", "こきり"): {
        "meaning": "Kokiri (the forest-dwelling child-like people of Kokiri Forest)",
        "note": "Proper noun specific to this game; not in general dictionaries.",
    },
    ("デクの樹", "でくのき"): {
        "meaning": "the Great Deku Tree",
        "note": "Proper noun; 樹 (tree) + の + デク is used here as this specific character's name.",
    },
    ("ミド", "みど"): {
        "meaning": "Mido (character name)",
        "note": "Proper noun.",
    },
    ("サリア", "さりあ"): {
        "meaning": "Saria (character name)",
        "note": "Proper noun.",
    },
}


def apply_override(lemma: str, reading: str, sense: dict) -> dict:
    override = OVERRIDES.get((lemma, reading))
    if override is None:
        return sense
    merged = dict(sense)
    merged["meaning"] = override["meaning"]
    if override.get("note"):
        merged["note"] = override["note"]
    merged["source"] = "override"
    return merged
