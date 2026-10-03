# Identity, recovery, and notifications

JP Assist keeps account recovery independent from game devices. A paired mod
receives only its revocable device credential and never receives an account
password, email action token, or mail configuration.

## Token lifecycle

Email verification, password reset, verified email changes, and reminder
unsubscribe links use cryptographically random bearer tokens. Only a SHA-256
hash is stored in `action_tokens`; the raw value exists in the outbound email.
Tokens are purpose-bound, expire, and are consumed once. Issuing a replacement
invalidates an older unconsumed token for the same user and purpose. Expired and
old consumed rows are removed during issuance.

- Verification links expire after 24 hours.
- Password-reset and change-email links expire after 1 hour.
- Reminder unsubscribe links expire after 90 days and are replaced by the next
  reminder.
- Authenticated resend/change-email requests have a one-minute cooldown.
- Password-reset requests always return the same response, including for an
  unknown or malformed address.

Completing a password reset or email change revokes every website session.
Changing an email requires the current password, sends the link to the new
address, and leaves the existing address active until confirmation.

Action tokens are deliberately absent from account exports and audit metadata.
The audit log records event categories and timestamps without raw tokens,
passwords, message bodies, or SMTP credentials.

## Local mail delivery

Development defaults to a mode-0600 JSONL mailbox at
`services/learning_platform/var/dev-mailbox.jsonl`. This makes recovery flows
testable without any network delivery. It is not a production mail transport
and contains live action links until the file is removed.

The production-equivalent Compose topology uses the pinned Mailpit container:

- SMTP is internal at `mailpit:1025`.
- The captured inbox is available only on the host at
  `http://127.0.0.1:8025`.
- Mailpit is not routed through Caddy and sends nothing to an external provider.

Copy `.env.example` to `.env`, set the local staging hostname, then start the
stack normally. Registration and recovery messages appear in Mailpit.

## Notifications and scheduled reminders

Review reminders and product updates are separate preferences and default to
off. Review reminders also store an IANA timezone and local delivery hour.
Only verified accounts with JP Assist as the all-games review owner can receive
a due-review reminder. A `(user, category, local date)` uniqueness constraint
prevents duplicate daily messages.

Run one reminder pass with:

```bash
PYTHONPATH=services/learning_platform \
  venv/learning-platform/bin/python -m learning_platform.manage send-reminders
```

The command is safe to run repeatedly. The Compose `maintenance` sidecar runs a
pass immediately and hourly by default, followed by bounded operational-data
cleanup. The database uniqueness constraint keeps overlapping/restarted passes
idempotent. Failed deliveries remove their daily delivery marker so they can be
retried. Users can send a test email from Account → Notifications and every
review email carries a single-use unsubscribe link.

## Acceptance coverage

Service tests prove token hashing, expiry, cleanup, single use, reset response
equivalence, session revocation, reminder deduplication, and unsubscribe.
Playwright drives registration, verification, notification settings, local
mail capture, unsubscribe, verified email change, password reset, sign-in, and
account deletion through the real HTTP service without external traffic.
