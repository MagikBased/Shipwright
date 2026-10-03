# Anki interoperability

JP Assist supports one scheduling owner for each saved-word collection:

- **JP Assist** owns FSRS due dates. Anki receives a portable study copy and
  does not change the website schedule.
- **Anki** owns due dates. JP Assist hides its due queue for the all-games
  collection, while retaining encounter, annotation, and imported review-history
  data.

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

## Review-history import and scheduler handoff

“Import review history” is enabled only for the all-games collection while
Anki owns it. Game filters remain deck/export scopes; restricting history
import to the owner of the global website queue prevents two schedulers from
owning the same card. The browser matches each Anki card to the stable `Word ID`, then
feature-detects AnkiConnect's `getReviewsOfCards` action. It imports ratings
1–4 and preserves the Anki revlog ID, card ID, original timestamp, intervals,
factor, duration, and review type. Manual/reschedule entries without a study
rating are counted as skipped instead of being converted into invented answers.

The service stores each source revlog ID only once. Repeating the import is
safe and reports those rows as already present. Imported entries are included
in account exports and activity, but do not create JP Assist due dates while
Anki owns scheduling. If the all-games owner is later changed back to JP
Assist, its derived FSRS state is deleted and deterministically replayed from
the complete append-only answer log, including imported Anki answers.

AnkiConnect documents the review-history response under
[`getReviewsOfCards`](https://git.foosoft.net/alex/anki-connect/src/branch/master/README.md).
Because deployed plugin versions differ, JP Assist reports an update-and-retry
message when that action is unavailable rather than falling back to fabricated
history.

## Local connection recovery

If the site reports that it could not reach AnkiConnect, verify that Anki is
open, AnkiConnect is installed and enabled, and port 8765 is not blocked. Add
the local/staging site origin to AnkiConnect's `webCorsOriginList`; do not expose
port 8765 publicly. Transport and invalid-JSON errors are normalized so these
instructions remain consistent across Chromium and Firefox. The preflight
performs no writes, so it is safe to fix the connection and retry. If
review-history import alone is unsupported, update AnkiConnect; card
synchronization remains available independently.
