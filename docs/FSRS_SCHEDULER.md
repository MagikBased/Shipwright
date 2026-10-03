# FSRS scheduler policy

JP Assist pins `py-fsrs==6.3.2`, which implements FSRS-6. The production
configuration uses the package's 21 default parameters, 90% desired retention,
one- and ten-minute learning steps, a ten-minute relearning step, and disabled
fuzzing. These values are serialized with every current card projection and new
review log entry.

The authoritative history is the append-only sequence of rating and UTC review
timestamp pairs. `review_state` is a disposable projection. The conformance
fixture in `services/learning_platform/tests/fixtures/fsrs6_conformance.json`
locks representative learning, graduation, lapse, relearning, and mature-card
results to this package version.

## Version upgrades

1. Pin and test the proposed package version in a branch. Add its version and
   parameter set to the scheduler adapter rather than changing stored rows.
2. Run old and new conformance fixtures and review interval differences.
3. Back up the database. A server will refuse to start when its scheduler does
   not match an existing non-legacy projection.
4. Deliberately run `python3 -m learning_platform.manage rebuild-reviews` with
   the new code. This replays existing ratings and timestamps; it never updates
   or invents review-log entries.
5. Run the acceptance suite and retain the pre-upgrade backup through the
   release window.

To roll back, stop the service, restore the pre-upgrade backup with the prior
release's restore command, and start the prior pinned image. Do not point old
code at a projection produced by a newer scheduler.

## Review-day behavior

Daily new/review limits and “bury today” use the IANA timezone stored with the
learner's goals. Boundaries are computed as local calendar midnights and then
converted to UTC, so 23- and 25-hour daylight-saving days behave correctly.
Learning and relearning steps remain available even after a daily review limit
is reached; otherwise a ten-minute step could be stranded until the next day.
