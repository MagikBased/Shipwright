# Cross-game vocabulary identity

## Identity model

JP Assist separates recognition of a written word from mastery of each meaning:

- A **lexeme** is a normalized Japanese lemma plus reading, such as `森|もり`.
- A **sense** is one specific meaning of that lexeme, identified by a stable
  dictionary or reviewed override ID.
- A **game vocabulary membership** records that a particular sense occurs in a
  game, how often it occurs, and its estimated JLPT level.

Inflected forms are reduced to their lemma before identity assignment. Two
spellings with different readings are separate lexemes. Two meanings with the
same spelling and reading share a lexeme but remain separate senses. Homophones
with different spellings likewise remain separate.

For example, `は|は` can contain both the topic-particle sense and the
`feather` sense. Recognizing one makes the written word familiar, but only the
mastered sense receives SRS and dialogue-coverage credit. A user may explicitly
mark the entire lexeme known when they intend to claim every cataloged meaning.

## Storage responsibilities

The repository-owned game manifests remain the reviewed source of truth. At
service startup they are synchronized into:

- `lexemes`: canonical lemma and reading identities;
- `lexical_senses`: meaning, part of speech, and provenance per sense;
- `catalog_games`: synchronized catalog releases; and
- `game_vocabulary`: the many-to-many relation between games and senses.

Account state stays independent of those game memberships:

- `known_words` records an explicit whole-lexeme claim;
- `word_annotations` records `new`, `learning`, `known`, or `ignored` per sense;
- `review_state` and `reviews` retain sense-specific FSRS history; an FSRS card
  that reaches the established review state contributes sense mastery;
- `word_progress` aggregates account-wide encounters; and
- `game_word_progress` preserves where those encounters occurred.

Consequently, learning a sense in one game immediately contributes to every
other catalog game containing that same lexeme and sense. No progress row needs
to be copied between games.

## Catalog measurements

Personal game coverage exposes complementary measurements:

- **Known words**: unique familiar lexemes divided by the game's lexemes.
- **Known meanings**: mastered game senses divided by all game senses.
- **Dialogue familiarity**: occurrences belonging to mastered senses divided by
  all transferable vocabulary occurrences.
- **New words**: game lexemes with no familiar sense or whole-word claim.

The frequency-weighted dialogue estimate is not a comprehension guarantee. It
is a more useful readiness signal than a raw word percentage because frequent
grammar and vocabulary contribute proportionally more than rare words.

Proper names, fictional locations, invented speech endings, interface labels,
and unresolved tokenizer fragments remain outside this graph under the shared
transferable-vocabulary policy.
