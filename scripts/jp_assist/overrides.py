"""Manual overrides for lemmas that a general-purpose dictionary (JMdict via
jamdict) doesn't have, or gets wrong for this game: proper nouns, invented
Zelda terminology, and archaic/compound forms (docs/JP_ASSIST_DESIGN.md
section 7.2 step 8). Keyed by (lemma, reading) so the override is specific
to the actual word sense encountered, not just the surface string.

This is a hand-authored review layer covering game terminology plus frequent
stylized speech that general dictionaries do not represent well.
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
    ("ッピ", "っぴ"): {"meaning": "Deku Scrub sentence-ending speech quirk", "note": "Character speech pattern.", "partOfSpeech": "suffix"},
    ("ピー", "ぴー"): {"meaning": "Deku Scrub sentence-ending speech quirk", "note": "Character speech pattern; not the letter P.", "partOfSpeech": "suffix"},
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
    ("ワシ", "わし"): {
        "meaning": "I; me (typically used by an older man)",
        "note": "First-person pronoun; not 鷲 (eagle).",
        "partOfSpeech": "pronoun",
    },
    ("じゃ", "じゃ"): {"meaning": "copula/contraction; or 'well then'", "note": "Exact role depends on context."},
    ("くる", "くる"): {"meaning": "to come", "note": "Common verb; often written 来る."},
    ("おる", "おる"): {"meaning": "to be/exist (humble or dialectal); progressive auxiliary", "note": "Exact role depends on context."},
    ("いう", "いう"): {"meaning": "to say; to be called", "note": "Common verb; often written 言う."},
    ("できる", "できる"): {"meaning": "can; to be able to; to come into existence", "note": "Common verb."},
    ("みる", "みる"): {"meaning": "to see/look; to try doing", "note": "Exact role depends on context; often written 見る."},
    ("つ", "つ"): {"meaning": "counter for objects; one/two/etc.", "note": "Common counting suffix in native Japanese numbers."},
    # Frequent colloquial spellings, character voices, and sound effects.
    # These entries are intentionally exact lemma+reading matches: they improve
    # the deck without changing ordinary words that happen to share a surface.
    ("ボーヤ", "ぼーや"): {"meaning": "boy; kid", "note": "Casual address, usually to Link."},
    ("ぼーや", "ぼーや"): {"meaning": "boy; kid", "note": "Casual address, usually to Link."},
    ("ボーズ", "ぼーず"): {"meaning": "boy; son; kid", "note": "Colloquial form of 坊主."},
    ("でしゅ", "でしゅ"): {"meaning": "childish pronunciation of です (to be)", "note": "Character speech pattern."},
    ("のぉ", "のぉ"): {"meaning": "sentence-ending のう; you see; isn't it", "note": "Old-person/archaic speech."},
    ("どっ", "どっ"): {"meaning": "where (contracted start of どっか/どこ)", "note": "Colloquial contraction."},
    ("ムニャ", "むにゃ"): {"meaning": "mumble; sleepy murmuring", "note": "Sleep sound effect."},
    ("おぉ", "おぉ"): {"meaning": "oh!; ooh!", "note": "Interjection."},
    ("どー", "どー"): {"meaning": "how; in what way (elongated どう)", "note": "Colloquial spelling."},
    ("ッピー", "っぴー"): {"meaning": "Deku Scrub sentence-ending speech quirk", "note": "Character speech pattern.", "partOfSpeech": "suffix"},
    ("ハッハッハ", "はっはっは"): {"meaning": "ha ha ha", "note": "Laughter."},
    ("ハッハッハッハッハ", "はっはっはっはっは"): {"meaning": "ha ha ha ha ha", "note": "Laughter."},
    ("ヒヒッ", "ひひっ"): {"meaning": "hee hee", "note": "Mischievous laughter."},
    ("ヘヘヘ", "へへへ"): {"meaning": "hee hee hee", "note": "Laughter."},
    ("クスス", "くすす"): {"meaning": "tee hee; suppressed giggle", "note": "Laughter."},
    ("フフッ", "ふふっ"): {"meaning": "heh heh; soft laugh", "note": "Laughter."},
    ("ウフ", "うふ"): {"meaning": "tee hee; soft laugh", "note": "Laughter."},
    ("ヒッヒッヒ", "ひっひっひ"): {"meaning": "heh heh heh", "note": "Laughter."},
    ("だ〜", "だー"): {"meaning": "elongated form of だ (to be)", "note": "Stylized speech."},
    ("キョーダイ", "きょーだい"): {"meaning": "brother; sworn brother", "note": "Stylized spelling of 兄弟."},
    ("フォッ", "ふぉっ"): {"meaning": "hoo; ho", "note": "Owl-like laugh/call."},
    ("フォ", "ふぉ"): {"meaning": "hoo; ho", "note": "Owl-like laugh/call."},
    ("ホホォ", "ほほぉ"): {"meaning": "hoo hoo", "note": "Owl call."},
    ("ホホーゥ", "ほほーぅ"): {"meaning": "hoo-hoot", "note": "Owl call."},
    ("幼し", "おさなし"): {"meaning": "young; childish", "note": "Classical/archaic form used in an inscription."},
    ("っと", "っと"): {"meaning": "quotative or emphatic ending", "note": "Colloquial particle; role depends on context."},
    ("そー", "そー"): {"meaning": "so; that way (elongated そう)", "note": "Colloquial spelling."},
    ("でぇ", "でぇ"): {"meaning": "elongated particle/ending", "note": "Stylized speech; exact role depends on context."},
    ("ベイベー", "べいべー"): {"meaning": "baby", "note": "English loanword used as a casual address."},
    ("ベイベ", "べいべ"): {"meaning": "baby", "note": "English loanword used as a casual address."},
    ("ビッグポウ", "びっぐぽう"): {"meaning": "Big Poe", "note": "Enemy/soul name."},
    ("盗賊団", "とうぞくだん"): {"meaning": "band of thieves; thieves' gang"},
    ("おおっ", "おおっ"): {"meaning": "oh!; whoa!", "note": "Interjection."},
    ("タロン", "たろん"): {"meaning": "Talon", "note": "Character name."},
    ("邪竜", "じゃりゅう"): {"meaning": "evil dragon", "note": "Refers to Volvagia."},
    ("ガエル", "がえる"): {"meaning": "frog", "note": "Voiced compound form of カエル."},
    ("的当て", "まとあて"): {"meaning": "target shooting; shooting gallery"},
    ("てめぇ", "てめぇ"): {"meaning": "you (rough/hostile)", "note": "Emphatic colloquial form of てめえ."},
    ("さっさと", "さっさと"): {"meaning": "quickly; promptly; without delay"},
    ("リチャード", "りちゃーど"): {"meaning": "Richard", "note": "Name of the woman's pet dog."},
    ("ローバ", "ろーば"): {"meaning": "old woman; hag", "note": "Stylized spelling of 老婆."},
    ("ケポラ", "けぽら"): {"meaning": "Kaepora", "note": "First half of Kaepora Gaebora's name."},
    ("ゲボラ", "げぼら"): {"meaning": "Gaebora", "note": "Second half of Kaepora Gaebora's name."},
    ("オババ", "おばば"): {"meaning": "granny; old woman", "note": "Familiar character title."},
    ("ボムチュウボウリング", "ぼむちゅうぼうりんぐ"): {"meaning": "Bombchu Bowling", "note": "Minigame name."},
    ("タネブクロ", "たねぶくろ"): {"meaning": "seed bag; bullet bag", "note": "Item name, stylized in katakana."},
    ("オイラ", "おいら"): {"meaning": "I; me (casual, rustic)", "note": "First-person pronoun; singular in this dialogue.", "partOfSpeech": "pronoun"},
    ("c", "c"): {
        "meaning": "C button; C-item control label",
        "note": "Controller interface label rather than Japanese vocabulary.",
        "partOfSpeech": "interface label",
        "senseId": "interface:c-button",
    },
    ("z", "z"): {
        "meaning": "Z button; targeting control label",
        "note": "Controller interface label rather than Japanese vocabulary.",
        "partOfSpeech": "interface label",
        "senseId": "interface:z-button",
    },
    ("パチンコ", "ぱちんこ"): {
        "meaning": "slingshot",
        "note": "The handheld weapon sense, not the mechanical gambling game.",
        "partOfSpeech": "noun",
    },
    ("よい", "よい"): {
        "meaning": "good; fine; all right",
        "note": "良い; not 宵 (evening).",
        "partOfSpeech": "adjective",
    },
    ("かう", "かう"): {
        "meaning": "to buy; to purchase",
        "note": "買う, written in kana in shop choices.",
        "partOfSpeech": "verb",
    },
    ("実", "じつ"): {
        "meaning": "fruit; nut; seed",
        "note": "The plant-fruit sense used in item names; not truth/reality.",
        "partOfSpeech": "noun",
        "dictionaryReading": "み",
        "senseId": "override:実|み",
    },
    ("タネ", "たね"): {
        "meaning": "seed; pit; kernel",
        "note": "種, stylized in katakana; not offspring/issue.",
        "partOfSpeech": "noun",
    },
    ("倒す", "たおす"): {
        "meaning": "to defeat; to knock down; to bring down",
        "note": "The combat/defeat sense used for enemies.",
        "partOfSpeech": "verb",
    },
    ("ボタン", "ぼたん"): {
        "meaning": "button; control button",
        "note": "The interface-control sense, not the peony flower.",
        "partOfSpeech": "noun",
    },
    ("買える", "かえる"): {
        "meaning": "can buy; to be able to purchase",
        "note": "Potential form of 買う, not 帰る (to return home).",
        "partOfSpeech": "verb",
    },
    ("中", "なか"): {
        "meaning": "during; while; in the middle of",
        "note": "The 中 suffix marking an ongoing state or activity.",
        "partOfSpeech": "suffix",
        "dictionaryReading": "ちゅう",
        "senseId": "override:中|ちゅう",
    },
    ("モード", "もーど"): {
        "meaning": "mode; operating state or interface screen",
        "note": "The interface/operating-mode sense.",
        "partOfSpeech": "noun",
    },
    ("つく", "つく"): {
        "meaning": "to become attached; to appear; to catch fire",
        "note": "Covers attachment, displayed marks, and ignition in tutorial text.",
        "partOfSpeech": "verb",
    },
    ("もの", "もの"): {
        "meaning": "thing; that which; nominalizer",
        "note": "The thing or nominalizing sense, not 者 (person).",
        "partOfSpeech": "noun",
    },
    ("まいる", "まいる"): {
        "meaning": "to give up; to be defeated; to be beaten",
        "note": "参った as an admission of defeat, not the humble go/come verb.",
        "partOfSpeech": "verb",
    },
    ("面", "めん"): {
        "meaning": "mask",
        "note": "The wearable mask sense in item dialogue.",
        "partOfSpeech": "noun",
    },
    ("相手", "あいて"): {
        "meaning": "opponent; other party; target",
        "note": "The person or object being targeted or faced.",
        "partOfSpeech": "noun",
    },
    ("ゼ", "ぜ"): {
        "meaning": "sentence-ending emphasis; you know; I tell you",
        "note": "Stylized masculine sentence-ending particle.",
        "partOfSpeech": "particle",
    },
}


_PROPRIETARY_NOTE_MARKERS = (
    "proper noun", "character name", "place name", "enemy name", "item name",
    "zelda", "game-specific", "minigame name", "character/item name",
    "enemy/people name", "enemy/soul name", "place/product name",
)


def apply_override(lemma: str, reading: str, sense: dict) -> dict:
    override = OVERRIDES.get((lemma, reading))
    if override is None:
        return sense
    merged = dict(sense)
    merged["meaning"] = override["meaning"]
    is_proprietary = any(
        marker in override.get("note", "").lower()
        for marker in _PROPRIETARY_NOTE_MARKERS
    )
    default_prefix = "proper" if is_proprietary else "override"
    merged["senseId"] = override.get("senseId", f"{default_prefix}:{lemma}|{reading}")
    if override.get("partOfSpeech"):
        merged["partOfSpeech"] = override["partOfSpeech"]
    if override.get("dictionaryReading"):
        merged["dictionaryReading"] = override["dictionaryReading"]
    if override.get("note"):
        merged["note"] = override["note"]
    merged["source"] = "override"
    return merged
