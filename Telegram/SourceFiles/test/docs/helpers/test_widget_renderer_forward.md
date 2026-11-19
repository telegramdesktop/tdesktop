# test_widget_renderer_forward

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Internal dependency.
Source contract: [test_widget_renderer_forward.h](../../test_widget_renderer_forward.h).

## Use when

Never from an overlay directly. It is the forwarding half the two renderer
taps share: `TapWidgetRenderer()`'s wrapping factory in Telegram
([test_widget_ops](test_widget_ops.md)) wraps the product renderer with it,
and the widget helper's Debug tap
([test_widget_helper_tap](test_widget_helper_tap.md)) wraps the glue's
renderer with it. Each tap records through its own `RendererObserver`: the
app tap's generation accounting and scans, the helper tap's arming, mirror
records and work timing. A change to the forwarding, the callback chaining
or the teardown order is made here once, for both comparison arms. The
header also holds the format of the helper work record (`HelperWorkKind`,
`HelperWorkRecord`, `EncodeHelperWork()`, `DecodeHelperWork()`) that the
helper's tap writes and Telegram's tap reads
([The helper work record](#the-helper-work-record)): both targets list this
header, so neither includes a header of the other's.

## Preconditions and limits

- **Qt-free and header-only.** Telegram's harness and `TelegramWidgetHelper`'s
  Debug build compile it the same way; it needs only widget-sandbox's
  `twidget/renderer.h` and the number coding of `tgw/tgw_hosted_wire.h`.
- **What it forwards.** `load()`, `send()`, `setTheme()`, `setWidth()` and
  `destroy()`, each once, on the caller's thread, with the very same string
  object; `calling()` runs before each call (with the message of `send()`
  and the theme of `setTheme()`), `called()` after it returned or threw.
- **What it chains.** It sets the wrapped renderer's `onMessage` and
  `onDiag` at construction; they read the wrapper's own callbacks when
  invoked, because `WidgetHost` (or the served host in the helper) assigns
  those after the factory returned. `received()` sees each renderer -> host
  message first, on whatever thread the renderer calls from; diagnostics are
  forwarded unobserved.
- **Not a scan.** It reads no message; every observation is the observer's.

## The helper work record

One way `w` `Mirrored` record times one outermost renderer operation of a
hosted widget's glue thread on the helper's steady clock, in microseconds
(on macOS the system-wide uptime clock both processes read):
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

The wrapper owns the renderer and the observer. Its destructor destroys the
renderer first, which waits for a callback in flight, and only then the
observer, so no hook runs on a destroyed observer and an observer's
destructor sees the renderer gone (the app tap counts the wrapper destroyed
there). Nothing is written anywhere.

## Verification

`RunWidgetOpsSelfCheck()` ([test_widget_ops](test_widget_ops.md#verification))
drives it through `TapWidgetRenderer()` around a stub: the forwarding
sequence with exact bytes and threads, the callbacks left for the host to
assign, and the stub destroyed before the wrapper is counted destroyed. The
helper's use compiles in the Debug helper; no row has crossed a real helper
yet. Its hosted leg also feeds records through the decoder: one of exactly
an hour is valid, and unordered, negative, over-an-hour, overwaited and
stray-byte records are malformed, and so, by the tap's own check at the
receipt, is one that ends after its receipt. Outside the app a scratch program of 24
boundary cases of `DecodeHelperWork()` passed under ASan and UBSan and at
`-O2`; against the decoder before the hour bound it failed 3 of them and
UBSan reported its signed overflow.

## Related guidance

[Validated-ops tap](test_widget_ops.md);
[the helper's half](test_widget_helper_tap.md).
