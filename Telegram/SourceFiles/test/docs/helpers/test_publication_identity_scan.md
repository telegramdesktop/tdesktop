# test_publication_identity_scan

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_publication_identity_scan.h](../../test_publication_identity_scan.h).

## Use when

Audit supplied public identities and launch-log coverage. Read the contract below and the source header before use.

## Contract and usage

Separate public-presentation and launch-log coverage audits:
`ReadPublicationIdentity` / `CheckPublicationIdentity` check explicit supplied
public identities; `ReadLaunchLogEnumeration` / `CheckLaunchLogEnumeration`
establish controlled coverage only. Fixed `PublicationIdentityRows` /
`LaunchLogEnumerationRows` contain classifications and counts. Neither proves
fixture secrecy. `AppendPublicationIdentityScanSelfTest` registers seven
synchronous, session-free stages, independently of the eleven strict-secret
stages: six over synthetic log trees, the sixth also calling both actual
wrappers only when this launch's MtpLog has a Send and a Recv header, and a
seventh over actual screenshot markers that applies only when the evidence
root has exactly one `synthetic_marker_collision_handle` directory
component. See the finite grammar and refusals below.

## Claims, coverage and identity grammar

**Public identity and log coverage are separate claims.** The helpers in
`test_publication_identity_scan.h` answer two different questions. A
`LaunchLogEnumerationReading::decided` result establishes which declared
launch log classes were read with controls; it has no `clean` or secret
verdict. A `PublicationIdentityReading` decides whether supported complete
presentations of explicitly supplied public inputs occur in those bytes.
Its `clean` result requires decided coverage, valid supplied scope, completed
bounded parsing, passing matcher controls and zero findings. Neither result
proves that fixture secrets were audited. Only `ReadSecrecy` / `CheckSecrecy`
with usable supplied fixture secrets can support that claim; an empty secret
set remains undecided and not clean.

A late synchronous campaign stage can use the actual wrappers directly:

```cpp
Test::CheckLaunchLogEnumeration();
Test::CheckPublicationIdentity({
    .identity = {
        .firstName = u"Ada"_q,
        .lastName = u"Syntheticriver"_q,
        .username = u"synthetic_public_handle"_q,
        .phone = u"+15551234567"_q,
        .userId = u"7"_q,
    },
});
```

These example identities are synthetic. The helper never discovers an
account identity or reads a credential fixture. Each wrapper plants a fresh
public control through `Note`, `LOG` and `MTP_LOG`, then uses
`SelectThisLaunchLogs`. `CheckPublicationIdentity` takes
`PublicationIdentityScanArgs`, whose `logs` member is
`LaunchLogEnumerationArgs`; both wrappers support its optional
`testLogControl`, `appLogControl`, `debugLogControl`, `mtpLogControl` and
`earlierParts`. The pure readers instead take an actual
`SecrecyLaunchLogs`, a nonempty planted control and optional enumeration
arguments, without planting or printing anything:

```cpp
const auto logs = Test::SelectThisLaunchLogs({});
const auto coverage = Test::ReadLaunchLogEnumeration(logs, plantedControl);
const auto identity = Test::ReadPublicationIdentity(
    suppliedIdentity,
    logs,
    plantedControl);
```

`plantedControl` must already be present in every required class. The four
mandatory ordered rows are `TestLog`, `AppLog`, `DebugLog` and `MtpLog`, even
when a class has no source. Coverage reuses the strict reader's accepted
bytes: TestLog file, supplied main-log text, and quarter-hour DebugLogs parts
whose first line is their own day index. Window parts start at their last
`NEW LOGGING INSTANCE STARTED!!!` banner. No modification time participates.
`MtpLog` requires both Send and Recv headers regardless of source display
name; every required class needs an actual accepted readable source, the
planted control, and any requested named control. Enable DebugLogs for real
campaigns, since absent MTP logging cannot establish coverage.

Only selector metadata marked complete can decide coverage. Invalid or
reversed times, an unspecified/manual selection, the 200-quarter cap,
inconsistent candidate inventories, unreadable sources, absent classes or
controls all refuse by fixed scan/class categories. Exactly 200 quarters is
complete when the next quarter is beyond the end time; 201 quarters is
capped. A missing or foreign window candidate alongside an accepted
controlled part stays visible in counts without claiming its bytes were
examined. Unreadability still refuses. Explicit `EarlierParts` are scanned
whole and require a valid part name, valid matching day and their own
nonempty present per-part control; they are never inferred from other files.

The identity grammar is finite and applies only to supplied descriptors:

- **Profile:** a complete multipart display name with Unicode
  letter/number/mark/underscore boundaries, or supplied `first_name` and
  `last_name` strings in one recognized TL `user` object. An isolated
  one-part name is insufficient; a structured one-part profile also needs a
  supplied username, phone or id matched in that same object. Names use NFC,
  Unicode case folding and collapsed whitespace, with at most 256 UTF-16
  units per supplied component. Objects, siblings, entries and sources do
  not share profile fields.
- **Username/link:** the complete supplied username as a bounded token,
  `@username`, `t.me/username`, `http[s]://t.me/username`,
  `http[s]://telegram.me/username` or `tg://resolve?domain=username`.
  A complete username path component in screenshot metadata counts too.
  `SaveImage` markers are relative to the evidence directory
  ([grammar](test_capture.md#saved-images-and-the-screenshot-marker)), so a
  host directory no longer appears in them, but a username in the caller's
  own image name still counts. Longer handles, partial names and underscore
  continuations do not count; overlapping spellings of one occurrence count
  once. Inputs use ASCII
  letters, digits and underscores, include at least one letter, are at most
  64 characters and match with ASCII case folding.
- **Phone:** the complete international number, canonical or with ASCII
  spaces, balanced nonnested parentheses and hyphens, or full digits at a
  plain `phone` label or recognized `user.phone` field. The descriptor is
  `+` and one to fifteen ASCII digits, starting with a nonzero digit.
  Candidate-local normalization never joins arbitrary numbers across a log
  line. Suffixes, partial numbers and longer digit runs do not count.
- **User id:** an exact canonical positive decimal of at most twenty ASCII
  digits, at plain `user_id=<decimal>` / `user_id: <decimal>`, or the TL
  owner/field pairs `user.id`, `userEmpty.id`, `userFull.id`,
  `peerUser.user_id`, `inputPeerUser.user_id`, `inputUser.user_id`.
  The innermost object owns the field. Tiny positive ids still count there;
  generic `id`, wrong-parent fields, request ids, timing, coordinates,
  counters and transport integers do not count. Caller-defined schemas and
  computed-field exemptions are unavailable.

The TL writer grammar is `{ constructor`, `field: value`, `}` with quoted
strings. It decodes `\\`, `\"`, `\n` and the logger's visible separator
escapes `\u000A`, `\u000B`, `\u000C`, `\u000D`, `\u001C`, `\u001D`,
`\u001E`, `\u0085`, `\u2028`, `\u2029` deliberately, without repeatedly
decoding escaped backslashes. Quotes keep braces and apparent fields inside
strings from changing ownership. Each physical TestLog row is an entry;
visible separators can encode a multiline profile within that row. The
additional parser state is bounded to 32 object frames, 4096 decoded UTF-16
units per string and 256 logical lines per open profile. Relevant
unterminated strings/profiles, unsupported escapes, lost object scope and
limit exhaustion refuse with fixed parser categories, including at entry,
source and final EOF boundaries. Empty/unusable supplied scope or any
nonempty malformed/over-limit descriptor refuses, even if another supplied
family is usable. Every usable family also needs its independent positive
and coincidence-negative in-memory matcher controls.

**Unmatched braces.** A `{` that is not followed, after spaces, by an ASCII
letter, digit or underscore opens no object. Read while no object is open,
it is counted as untyped text, as in webrtc's `Media Info` / `Media Error`
device lists `list: { "<name>" <id>, ... }` and the empty `{  }`. A later
`}` read while still no object is open then ends one untyped opener
instead of refusing. Strings in such a list stay unowned strings, so a
supplied name, username or phone inside a device name still counts.
Opening any named object forfeits every earlier untyped opener, and an
entry, TestLog row, source or final EOF boundary drops them silently; an
unclosed untyped opener alone never refuses. Inside an object an untyped
`{` is ignored as before: it opens no frame and takes no closer, so the
next `}` still ends the innermost object. Every other unmatched closer
still refuses `lost_object_scope`: a bare `}`, an extra closer after a
balanced list or dump (`{ "x" } }`, `Recv: { msgs_ack } }`), a closer
after a named object opened and closed since the untyped opener, and a
closer whose untyped opener was in an earlier entry, TestLog row or
source. An unquoted `user` profile field, a named object left open at a
boundary, and every string, escape and limit refusal are unchanged.

Plain, Send **and Recv** presentations count for identity publication; server
origin is no exemption. Identity has no computed-field exemptions. These
are bounded claims about the supplied identities and supported shapes,
not a general PII detector or an audit of unread credentials.
`LOG_ENUMERATION_*` and `PUBLICATION_IDENTITY_*` rows contain only fixed
class/family/selection/refusal names and counts. They never echo identities,
controls, paths, source names or matched lines. Wrapper nonce setup is a
separate `PUBLICATION_AUDIT_CONTROL` row. Fixed schema words or small counts
may coincide with a supplied tiny name/id; that coincidence is not value
emission. `AppendPublicationIdentityScanSelfTest`
([self-test](test_publication_identity_scan_self_test.md)) independently
exercises coincidences, presentations, enumeration, accepted selection,
claim separation, parser bounds and actual formatter output over synthetic
logs, without an account or network. Its actual wrapper checks need this
launch's MtpLog to hold a Send and a Recv header, which only real MTP
traffic writes. It reads both header counts without planting or printing a
control and notes them in a fixed `PUBLICATION_SELF_WRAPPER_GATE` row.
Without the pair, each wrapper check reports a fixed-reason
`TEST_RESULT: N/A` and neither wrapper runs. With it, both must read
healthy, and any other real-log refusal still fails. Under its
collision-named evidence root it also reads actual `SaveImage` markers:
the relative rows are clean, the same file's historical absolute row and a
deliberately handle-named file each count once.

## Related guidance

[Strict fixture secrecy](test_secrecy_scan.md); [account prerequisites](../account-fixtures.md);
[self-test](test_publication_identity_scan_self_test.md).
