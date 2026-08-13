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
    ("デク", "でく"): {"meaning": "Deku (game-specific name/category)", "note": "Zelda terminology."},
    ("ゴロン", "ごろん"): {"meaning": "Goron (the rock-dwelling people)", "note": "Zelda proper noun."},
    ("ハイラル", "はいらる"): {"meaning": "Hyrule", "note": "Place name."},
    ("ガノンドロフ", "がのんどろふ"): {"meaning": "Ganondorf", "note": "Character name."},
    ("ガノン", "がのん"): {"meaning": "Ganon", "note": "Character name."},
    ("ゾーラ", "ぞーら"): {"meaning": "Zora (the aquatic people)", "note": "Zelda proper noun."},
    ("ゲルド", "げるど"): {"meaning": "Gerudo (the desert people)", "note": "Zelda proper noun."},
    ("ゼルダ", "ぜるだ"): {"meaning": "Zelda", "note": "Character name."},
    ("ハイリア", "はいりあ"): {"meaning": "Hylia; Hylian", "note": "Zelda proper noun/adjective."},
    ("デスマウンテン", "ですまうんてん"): {"meaning": "Death Mountain", "note": "Place name."},
    ("ルト", "ると"): {"meaning": "Ruto", "note": "Character name."},
    ("インパ", "いんぱ"): {"meaning": "Impa", "note": "Character name."},
    ("インゴー", "いんごー"): {"meaning": "Ingo", "note": "Character name."},
    ("カカリコ", "かかりこ"): {"meaning": "Kakariko", "note": "Place name."},
    ("ドドンゴ", "どどんご"): {"meaning": "Dodongo", "note": "Enemy name."},
    ("ダンペイ", "だんぺい"): {"meaning": "Dampé", "note": "Character name."},
    ("エポナ", "えぽな"): {"meaning": "Epona", "note": "Character name."},
    ("ポウ", "ぽう"): {"meaning": "Poe (a ghost enemy)", "note": "Zelda terminology."},
    ("キングゾーラ", "きんぐぞーら"): {"meaning": "King Zora", "note": "Character title/name."},
    ("ロンロン", "ろんろん"): {"meaning": "Lon Lon", "note": "Place/product name."},
    ("ナビィ", "なびぃ"): {"meaning": "Navi", "note": "Character name."},
    ("ダルニア", "だるにあ"): {"meaning": "Darunia", "note": "Character name."},
    ("ナボール", "なぼーる"): {"meaning": "Nabooru", "note": "Character name."},
    ("ダイゴロン", "だいごろん"): {"meaning": "Biggoron", "note": "Character name."},
    ("キータン", "きーたん"): {"meaning": "Keaton", "note": "Zelda proper noun."},
    ("ゴーマ", "ごーま"): {"meaning": "Gohma", "note": "Enemy name."},
    ("コタケ", "こたけ"): {"meaning": "Kotake", "note": "Character name."},
    ("コウメ", "こうめ"): {"meaning": "Koume", "note": "Character name."},
    ("ボムチュウ", "ぼむちゅう"): {"meaning": "Bombchu", "note": "Item name."},
    ("スタルチュラ", "すたるちゅら"): {"meaning": "Skulltula", "note": "Enemy name."},
    ("ヴァルバジア", "ゔぁるばじあ"): {"meaning": "Volvagia", "note": "Enemy name."},
    ("スタルキッド", "すたるきっど"): {"meaning": "Skull Kid", "note": "Character/enemy name."},
    ("ラウル", "らうる"): {"meaning": "Rauru", "note": "Character name."},
    ("コジロー", "こじろー"): {"meaning": "Cojiro", "note": "Character/item name."},
    ("デクババ", "でくばば"): {"meaning": "Deku Baba", "note": "Enemy name."},
    ("デクナッツ", "でくなっつ"): {"meaning": "Deku Scrub", "note": "Enemy/people name."},
    ("キース", "きーす"): {"meaning": "Keese", "note": "Enemy name."},
    ("ファントムガノン", "ふぁんとむがのん"): {"meaning": "Phantom Ganon", "note": "Enemy name."},
    ("スタルフォス", "すたるふぉす"): {"meaning": "Stalfos", "note": "Enemy name."},
    ("ピエール", "ぴえーる"): {"meaning": "Pierre", "note": "Character name."},
    ("だー", "だー"): {"meaning": "dialectal/stylized form of だ (to be)", "note": "Character speech pattern."},
    ("ッ", "っ"): {"meaning": "emphatic final small tsu", "note": "Marks an abrupt or forceful ending."},
    ("ッピ", "っぴ"): {"meaning": "Deku Scrub sentence-ending speech quirk", "note": "Character speech pattern."},
    ("ゴロォ", "ごろぉ"): {"meaning": "stretched Goron sentence ending", "note": "Character speech pattern."},
    ("ゾラ", "ぞら"): {"meaning": "King Zora's sentence-ending speech quirk", "note": "Character speech pattern."},
    ("て", "て"): {"meaning": "te-form connector; and/then; by doing", "note": "Common grammar; exact role depends on context."},
    ("た", "た"): {"meaning": "past/completed-action auxiliary", "note": "Common grammar."},
    ("だ", "だ"): {"meaning": "plain copula; to be", "note": "Common grammar."},
    ("か", "か"): {"meaning": "question or alternative marker", "note": "Common particle."},
    ("と", "と"): {"meaning": "quotation, accompaniment, coordination, or conditional marker", "note": "Common particle; exact role depends on context."},
    ("よ", "よ"): {"meaning": "sentence-ending emphasis/information marker", "note": "Common particle."},
    ("ヨ", "よ"): {"meaning": "sentence-ending emphasis/information marker", "note": "Katakana-styled particle."},
    ("も", "も"): {"meaning": "also; too; even", "note": "Common particle."},
    ("お", "お"): {"meaning": "honorific/polite prefix", "note": "Common prefix."},
    ("この", "この"): {"meaning": "this", "note": "Placed before a noun."},
    ("な", "な"): {"meaning": "sentence-ending/prohibitive particle or attributive copula", "note": "Exact role depends on context."},
    ("ん", "ん"): {"meaning": "explanatory nominalizer or contracted negative ending", "note": "Exact role depends on context."},
    ("ゴロ", "ごろ"): {"meaning": "Goron sentence-ending speech quirk", "note": "Character speech pattern."},
    ("って", "って"): {"meaning": "casual quotation/topic marker", "note": "Common grammar."},
    ("てる", "てる"): {"meaning": "contracted progressive/resultative ending (-ている)", "note": "Common grammar."},
    ("ない", "ない"): {"meaning": "not; nonexistent", "note": "Negative form."},
    ("いい", "いい"): {"meaning": "good; fine; okay", "note": "Common adjective."},
    ("こと", "こと"): {"meaning": "thing; fact; nominalizer", "note": "Common grammar."},
    ("たら", "たら"): {"meaning": "if; when; after", "note": "Conditional grammar."},
    ("なら", "なら"): {"meaning": "if; as for; in the case of", "note": "Conditional grammar."},
    ("いる", "いる"): {"meaning": "to exist (animate); to be; progressive auxiliary", "note": "Exact role depends on context."},
    ("よう", "よう"): {"meaning": "way/manner; seeming; so that/in order to", "note": "Exact role depends on context."},
    ("オレ", "おれ"): {"meaning": "I; me (casual/masculine)", "note": "Pronoun."},
    ("じゃ", "じゃ"): {"meaning": "copula/contraction; or 'well then'", "note": "Exact role depends on context."},
    ("くる", "くる"): {"meaning": "to come", "note": "Common verb; often written 来る."},
    ("おる", "おる"): {"meaning": "to be/exist (humble or dialectal); progressive auxiliary", "note": "Exact role depends on context."},
    ("いう", "いう"): {"meaning": "to say; to be called", "note": "Common verb; often written 言う."},
    ("できる", "できる"): {"meaning": "can; to be able to; to come into existence", "note": "Common verb."},
    ("みる", "みる"): {"meaning": "to see/look; to try doing", "note": "Exact role depends on context; often written 見る."},
    ("つ", "つ"): {"meaning": "counter for objects; one/two/etc.", "note": "Common counting suffix in native Japanese numbers."},
}


def apply_override(lemma: str, reading: str, sense: dict) -> dict:
    override = OVERRIDES.get((lemma, reading))
    if override is None:
        return sense
    merged = dict(sense)
    merged["meaning"] = override["meaning"]
    merged["senseId"] = override.get("senseId", f"override:{lemma}|{reading}")
    if override.get("note"):
        merged["note"] = override["note"]
    merged["source"] = "override"
    return merged
