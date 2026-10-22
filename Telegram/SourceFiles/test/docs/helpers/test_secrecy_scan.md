# test_secrecy_scan

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_secrecy_scan.h](../../test_secrecy_scan.h).

## Use when

Certify fixture-secret absence from logs. Read the contract below and the source header before use.

## Contract and usage

Prove that no fixture secret reached the logs: `CheckSecrecy` takes phrases,
short secrets and tokens once, selects the test log, the app log
(`Logs::full()`) and this launch's `DebugLogs` parts by identity (the day
index in the first line, never a last-write time), counts only what the client
wrote (plain lines and `Send` dump entries; `Recv` hits are reported with
their schema site), reports every client-written short-secret hit in a plain
line at its `<file>|<head>|<field>` site (the message head and the name of the
field whose whole value the hit is), lets a campaign declare computed fields
(`SecrecyScanArgs::computedFields`, message head and field name) whose
exact-value hits are reported in `SECRECY_COMPUTED` and never decide, carries
the MTProto transport's own integer-only `DEBUG_LOG` lines itself (each
format, its source file and which of its numbers the client computes) so that
a hit that is exactly a computed number's whole integer is reported there too
with no declaration, refuses to certify a zero without a planted nonce, named
controls and passing canaries, and prints counts and public identifiers only
(`SECRECY_SCAN`, `SECRECY_WINDOW`, `SECRECY_CLASS`, `SECRECY_RECEIVED`,
`SECRECY_COMPUTED`). Its `SECRECY: <class>` reports are `DiscriminatingScan`
rows, so they follow the number-format request of `test_log.h` like every
converted helper row, while its `SECRECY_*` rows and its three checks keep
ordinary integers in both modes. Its session-free self-test over synthetic log
trees is `AppendSecrecyScanSelfTest`, which also covers the number format of
overlay telemetry, the shared helpers' numbers in both formats and the carried
transport lines.

## Proving no fixture secret reached logs

**Proving no fixture secret reached the logs.** A funded-wallet campaign proves that no recovery word, 2SV password, custody
record id or secret ref reached the test log, the app log or `DebugLogs` by
calling `Test::CheckSecrecy(args, what)` (`test_secrecy_scan.h`) once, from a
late stage's `then`, instead of grepping by hand. Hand it, in
`SecrecyScanArgs::secrets`:

- `phrases`: `GramReconcileResult::phrases` (the file's words and every
  revealed phrase), plus any rotated phrase the run produced later;
- `shortSecrets`: `*Test::TwoStepPassword()` when the campaign used it;
- `tokens`: `Test::GramCustodySecretTokens(session)` taken at P0 AND again at
  the scan (a restore or rotation mints a new record id), which lists every
  record id, secret ref and `secret/` storage key of the custody store.

Declare computed fields in `SecrecyScanArgs::computedFields` only as described
under **Declared computed fields** below. The MTProto transport's own
integer-only `DEBUG_LOG` lines need no declaration: the scan carries them
(**Plain-line sites**).

A campaign that calls `CheckSecrecy` also calls
`Test::RequestTelemetryNumbers()` (`test_log.h`) unconditionally at its first
stage, before any row the scan may read, never under a condition and never
from code that read a fixture secret. From then on the shared helpers print
the numbers they measure, count or time - the `GEOMETRY:`, `CheckNear`,
post-paint, clocked-frame, probe and scan rows - as `TelemetryNumber`s, whose
digit runs the scan never counts, so no number such a helper printed can
fail the scan by coincidence (the shared-helper row under Failure
diagnosis); the caller text in those rows stays the caller's to format. The
call logs `TELEMETRY_NUMBERS: requested` once, and a row formatted before it
keeps ordinary integers.

The values stay in memory: the helper prints counts, public file names, TL
schema identifiers and the heads and field names of plain-line sites only,
never a secret, a field's value, a length or a whole scanned line.

- **Classes and identity.** It reads five classes: `TestLog`
  (`test_log.txt`), `AppLog` (the main log through `Logs::full()`, the text
  the logger holds), `DebugLog` and `MtpLog` (every `DebugLogs/log_HH_MM.txt`
  / `mtp_HH_MM.txt` part named for a quarter hour in [process start, scan]),
  and `EarlierParts` (only parts named in `earlierParts`). A part is accepted
  only when its first line is its own day index (`yyyyMMdd`); otherwise it is
  `Foreign`, `Missing` or `Unreadable`. A window part is scanned from its last
  `NEW LOGGING INSTANCE STARTED!!!` banner, so an earlier same-day launch's
  lines are not charged to this one. No last-write time takes part anywhere.
- **Matching.** A phrase counts as any ordered adjacent pair of its words
  (case-insensitive). Words are letter-and-digit runs: whitespace,
  punctuation (commas, quotes, brackets), a dump string's `\n`, `\t`,
  `\\` and `\"` escapes, the dump's type tags (`[STRING]`) and the test
  log's `\uXXXX` escapes all separate them, and a run carries across the
  lines of one file. A phrase one word per line, comma-joined, escaped in a
  dump string or dumped as a one-element-per-line vector therefore still
  counts; a number is neutral, and any other word or an entry header ends
  the run. A short secret counts only when bounded — no letter or
  digit right before or after it; embedded occurrences are reported and never
  count. A token counts on every substring occurrence; a token shorter than 16
  characters is refused as "not a token; hand it as a short secret".
- **Direction.** MTP dump entries are classed by the transport's own
  `Send: ` / `Recv: ` header. Only what the client wrote — plain lines and
  `Send` entries — decides. `Recv` hits are server-sent content (a received
  value that holds a short secret by coincidence, or chat text that happens
  to hold two adjacent BIP-39 words) and are reported with their
  `<top>/<inner>.<field>` schema site in `SECRECY_RECEIVED`, never deciding.
- **Plain-line sites.** Every bounded short-secret hit in a plain line is
  reported at its `<file>|<head>|<field>` site, in its class's
  `SECRECY_CLASS` row (`plainSites`) and in that class's `SECRECY: <class>`
  subjects. The head is the message text after the app log's
  `[yyyy.MM.dd hh:mm:ss] `, a `DebugLogs` entry's `[hh:mm:ss.zzz tid-index] `
  or an `mtp_` entry's `(dc:<dc>) ` (a test-log row has no prefix, so its
  head keeps `NOTE: `), up to the first `name=` field: an ASCII identifier
  at the message start or after a space, outside double quotes. A value runs
  to the next space, less a trailing `;`, `,`, `.` or `:` and one enclosing
  pair of quotes; `<field>` is the name of the field whose whole value the
  hit is, or `-`. The whole site is withheld as `<file>|?|?`, for every
  reason alike, when the line has no field, the hit lies in the head, the
  head is longer than 64 characters or holds anything but ASCII letters,
  digits, spaces and `_.:-/`, or the head (with the field name) holds any
  given secret. No value, length, offset or line is printed.
  One kind of line with no field still gets a site: the scan carries the
  MTProto transport's own `DEBUG_LOG` lines whose every placeholder prints
  one integer (`TransportLines()` in `test_secrecy_scan.cpp`: each format as
  its function in `mtproto/session_private.cpp`, `mtproto/session.cpp` or
  `mtproto/mtp_instance.cpp` writes it, and which of its numbers the client
  computes). A line is carried only in a `DebugLogs/log_HH_MM.txt` part (a
  window part or an accepted earlier part; never `test_log.txt`, the app
  log or an `mtp_` part), and only when it is exactly a
  `[hh:mm:ss.zzz tid-index] ` entry start in ASCII digits with no
  `(dc:<dc>) `, then the format with each placeholder one integer - an
  optional `-` and ASCII digits, nothing else - then `DEBUG_LOG`'s
  ` (<that source file> : <digits>)` and the end of the line. Its site is
  `<file>|<head>|<slot>`: the head is the format's own text before its
  first number, cut at the first character a head may not hold, and
  `<slot>` the table's name of the number whose whole integer, minus
  included, the hit is, or `-` for any other position. A hit before the
  first number, or a head and slot name that would print a given secret,
  keeps the withheld site, and so does every other shape: an added field,
  trailing text, another source file in the suffix, or a sign, decimal or
  digit the product never prints there.
- **Declared computed fields.** The fourth trap: a value the product
  computes and formats into its own diagnostic line can equal a short
  secret, and a count alone cannot tell that from a leak. Declare such a
  field in `SecrecyScanArgs::computedFields` as `{ .head, .field }`, spelled
  exactly as its site prints them, only when the product source shows that
  the field is formatted from a value the product computes, never from
  fixture input; name that formatting site (file and function) in the
  campaign's test design, and decide from the site row and the source,
  never from the matched line. A hit that is exactly that field's value on
  a line with that head is reported in `SECRECY_COMPUTED` and never decides.
  The same value in any other field, in free text or in part of the value,
  a withheld site, and every word run, token and `Send`-entry hit still
  decide; a declaration that matches no printed site changes nothing.
  A carried transport line (**Plain-line sites**) needs no declaration, and
  a campaign declares nothing for it: its numbers belong to the product,
  not to a campaign - a launch with `DebugLogs` on can print them whatever
  the campaign does - a declaration needs a `name=` site such a line does
  not have, and a table tied to its product functions and proven by the
  scan's own self-test keeps the exemption out of a campaign's reach, so
  no harness or overlay row can be exempted this way. Only a slot that the
  product source shows the client computes or keeps itself - a size or
  count, its own clock, its routing state such as a dc id, a request id of
  its own maps, an id it generated - is computed; a number read straight
  from the server's bytes (an acked or received `msg_id`, a server time, a
  server salt) and a number glued to a letter stay deciding, and no
  declaration changes that. A hit that is exactly a computed slot's whole
  integer is reported in `SECRECY_COMPUTED` and never decides; the digits
  under its minus, another slot or position of the line, the suffix's line
  number, and every word run, token and `Send`-entry hit still decide, and
  the bounded and embedded classes are unchanged. The carried lines come
  from
  `2026/10/13/keep-the-secrecy-scan-from-counting-a-coincidental-transport-number`.
  A number an overlay prints into its own telemetry row is never such a
  field: print it with `Test::TelemetryNumber` instead (the overlay
  telemetry row in [failure diagnosis](../diagnostics.md)). Nor is a number a shared helper
  prints into its own row - a `GEOMETRY:` row, a `CheckNear` verdict, a
  post-paint, clocked-frame, probe or scan row: ask with
  `Test::RequestTelemetryNumbers()` at the first stage, or convert the
  helper (the shared-helper row in [failure diagnosis](../diagnostics.md)).
- **Controls and canaries.** A zero is certified only when it can be told
  from absence. The call plants a fresh public nonce through `Note`, `LOG` and
  `MTP_LOG` and requires it in every non-earlier class; a named control
  (`testLogControl`, `appLogControl`, `debugLogControl`, `mtpLogControl`, for
  example the served key hex or `wallet_getState`) must also be hit. A class
  is undecided with no accepted file, an unreadable part, no planted control,
  an absent named control, or an `mtp_` part without headers of both
  directions. In-memory canaries run every given secret through the same
  matcher and dump walk on every call; a failing canary leaves the scan
  undecided.
- **Output.** One `SECRECY: <class>` `DiscriminatingScan` report per class,
  then the rows `SECRECY_SCAN`, `SECRECY_WINDOW` (every window candidate's
  identity), one `SECRECY_CLASS` per class (with its `plainSites`),
  `SECRECY_RECEIVED` and `SECRECY_COMPUTED` (the count of exact-value hits
  on declared fields and carried computed slots, with each one's
  `<class>|<file>|<head>|<field>` site, never the value), then three
  checks: `<what>: every class decided`, `<what>: no client-written fixture
  secret`, and `<what>: the scan's own rows carry no fixture secret` (the bytes
  it appended to `test_log.txt`, re-read). It returns `decided && clean`.
  The `SECRECY: <class>` reports are `DiscriminatingScan` rows and follow
  `Test::RequestTelemetryNumbers()` like any other: after the ask their
  `examined=`, `subject(...)=` and `control(...)=` counts, and a refusal's
  counts, print as `TelemetryNumber`s. The `SECRECY_SCAN`, `SECRECY_WINDOW`,
  `SECRECY_CLASS`, `SECRECY_RECEIVED` and `SECRECY_COMPUTED` rows and the
  three checks keep ordinary integers in both modes: they also print
  identifiers whose digit runs no number format removes (window times,
  `DebugLogs` part names), and judges read their counts as integers. The
  own-rows check re-reads exactly the bytes the call appended, whatever
  their format.

`DebugLogs` must be enabled (`tdata/withdebug` = `1`), or the `DebugLog` and
`MtpLog` classes are undecided — `MTP_LOG` writes nothing without it. The scan
covers logs only: the post-run grep of promoted evidence, `work/` artifacts
and receipts for 64-hex and UUID tokens stays the performer's job. The
matcher's own acceptance is `Test::AppendSecrecyScanSelfTest`, session-free,
over synthetic log trees and synthetic secrets.

## Related guidance

[Account prerequisites](../account-fixtures.md); [reconciliation](test_gram_reconcile.md).

For public identity and controlled log coverage, use the separate
[publication and coverage reference](test_publication_identity_scan.md).
Neither claim substitutes for strict fixture secrecy.
