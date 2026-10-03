# Anki interoperability

JP Assist supports one scheduling owner for each saved-word collection:

- **JP Assist** owns FSRS due dates. Anki receives a portable study copy and
  does not change the website schedule.
- **Anki** owns due dates. JP Assist hides its due queue for the all-games
  collection, while retaining encounter, annotation, and review-history data.

Changing owner always requires confirmation. The all-games owner controls the
website review queue. A game-filtered owner and deck are independent export
settings for that filtered collection.

## Stable identity and preflight

The `JP Assist Vocabulary` note type stores the content-neutral account
`wordId` and `senseId` in its `Word ID` field. Spelling is never used as the
identity, so homographs and multiple dictionary senses remain separate.

“Check Anki” is read-only. It uses AnkiConnect `modelNames`,
`modelFieldNames`, `findNotes`, and `notesInfo` to report additions, updates,
unchanged notes, duplicate identities, skipped entries, required field
migrations, and blocking conflicts. “Apply sync” then:

1. creates the deck and note type if absent;
2. adds missing non-identity model fields;
3. updates managed fields while preserving custom tags; and
4. adds notes that do not yet exist.

Running preflight again after a successful sync should report only unchanged
notes. A duplicated `Word ID` is blocking because JP Assist cannot safely
choose which Anki note to update.

## Local connection recovery

If the site reports `Failed to fetch`, verify that Anki is open, AnkiConnect is
installed and enabled, and port 8765 is not blocked. Add the local/staging site
origin to AnkiConnect's `webCorsOriginList`; do not expose port 8765 publicly.
Malformed-response errors usually indicate an incompatible plugin or another
service occupying that port. The preflight performs no writes, so it is safe to
fix the connection and retry.

Anki review-history import is intentionally deferred until the history action
is feature-detected: deployed AnkiConnect versions have differed in support for
`getReviewsOfCards`. Import must preserve original timestamps and source IDs in
the append-only log rather than synthesizing FSRS answers.
