# JLPT vocabulary profiles

Game catalog profiles count unique `lemma|reading` identities in the Japanese
runtime corpus. They also retain token occurrence counts for future
frequency-weighted views, but the current catalog percentage is based on unique
words so repeated particles do not dominate the chart. The profile uses the
same transferable-vocabulary policy as the chapter decks: proper names,
fictional locations, invented character speech endings, interface labels, and
unresolved tokenizer fragments are omitted rather than presented as learnable
Japanese or placed in a separate glossary.

N5 through N1 classifications come from
[OpenJLPT](https://github.com/evanclan/OpenJLPT), licensed CC BY-SA 4.0. OpenJLPT
derives its vocabulary levels from Jonathan Waller's community lists and checks
entries against JMdict. The modern JLPT does not publish official vocabulary
lists, so these levels are estimates. Words without a reliable match remain
`unclassified`; dictionary-resolved words without a JLPT match remain in the
game and personal coverage denominator.

Regenerate a game manifest after rebuilding the local runtime corpus:

```bash
python3 scripts/jp_assist/build_game_vocabulary.py \
  --jlpt-dir /path/to/OpenJLPT/data/json/vocab \
  --jlpt-version OPENJLPT_COMMIT_OR_RELEASE \
  --output services/learning_platform/learning_platform/content/games/ocarina-of-time.vocabulary.json
```

Known words are account-wide. A saved identity therefore contributes to every
catalog game that contains the same `lemma|reading`, while per-game coverage is
computed at request time from that game's relational vocabulary memberships.
Distinct meanings retain sense-specific mastery and frequency credit; see
[`CROSS_GAME_VOCABULARY.md`](CROSS_GAME_VOCABULARY.md).
