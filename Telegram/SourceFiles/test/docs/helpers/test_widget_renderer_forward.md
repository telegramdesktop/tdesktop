# test_widget_renderer_forward

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Internal dependency.
Source contract: [test_widget_renderer_forward.h](../../test_widget_renderer_forward.h).

## Use when

Never from an overlay directly. It holds the format of the helper work
record (`HelperWorkKind`, `HelperWorkRecord`, `EncodeHelperWork()`,
`DecodeHelperWork()`) that the widget helper's Debug renderer tap
([test_widget_helper_tap](test_widget_helper_tap.md)) writes and Telegram's
hosted tap ([test_widget_ops](test_widget_ops.md)) reads
([The helper work record](#the-helper-work-record)). The tap itself, with
the renderer forwarding this header once held for the C++ helper, is
twidget's Rust serving (`rust/crates/helper`: `tap.rs`, `forward.rs`),
which encodes the record alike.

## Preconditions and limits

- **Qt-free and header-only.** Telegram's harness compiles it; it needs only
  the number coding of `tgw/tgw_hosted_wire.h`.

## The helper work record

One way `w` `Mirrored` record times one outermost renderer operation of a
hosted widget's glue thread on the helper's steady clock, in microseconds
(on macOS the system-wide uptime clock and on Windows
`QueryPerformanceCounter`, each read by both processes):
`EncodeHelperWork()` writes exactly `kHelperWorkRecordBytes` (41): the kind,
`started`, `finished`, `waited` and `seq` as 8 little-endian bytes each,
then `waits` and `frameBytes` as 4 each. `DecodeHelperWork()` keeps a record
only when it has exactly those bytes, a known kind, `0 <= started <=
finished`, at most `kMaxHelperInterval` (an hour) between them and `0 <=
waited <=` that interval, and returns nullopt otherwise. Each check comes
before the arithmetic it guards, so no stamp of any value overflows in the
decode, and a kept record's `finished - started` is exact. An hour is far
past any renderer operation and far below what the sum of a full room of
such intervals could overflow. `waits` and `frameBytes` stay unsigned
32-bit values. Mapping the stamps onto Telegram's clock and summing them
are the readers' own checks
([Helper renderer work](test_widget_ops.md#helper-renderer-work)).

## Lifetime and cleanup

It holds nothing: the encode and the decode are pure functions. Nothing is
written anywhere.

## Verification

`RunWidgetOpsSelfCheck()`'s hosted leg
([test_widget_ops](test_widget_ops.md#verification)) feeds records through
the decoder: one of exactly an hour is valid, and unordered, negative,
over-an-hour, overwaited and
stray-byte records are malformed, and so, by the tap's own check at the
receipt, is one that ends after its receipt. Outside the app a scratch program of 24
boundary cases of `DecodeHelperWork()` passed under ASan and UBSan and at
`-O2`; against the decoder before the hour bound it failed 3 of them and
UBSan reported its signed overflow.

## Related guidance

[Validated-ops tap](test_widget_ops.md);
[the helper's half](test_widget_helper_tap.md).
