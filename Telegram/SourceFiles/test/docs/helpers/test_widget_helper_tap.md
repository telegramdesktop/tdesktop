# test_widget_helper_tap

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Internal dependency.
Source contract: twidget's `rust/crates/helper/src/tap.rs`, with the
forwarding renderer and the work record in `forward.rs`; no tdesktop
source.

## Use when

Never from an overlay directly. This is the helper's half of the
validated-ops tap ([test_widget_ops](test_widget_ops.md)) for a widget hosted
in the widget helper, where the served host and its renderer run in
`TelegramWidgetHelper` and Telegram has no renderer to wrap. The helper
serves widgets through twidget's Rust serving alone
(`_other/widget_helper_rust.cpp` calls `tw_helper_serve()` of
`twidget_helper_ffi`), whose hosted renderer factory builds each renderer
through the hosted glue (`glue.rs`) inside the tap's `build` callback, so the
glue gets the tap's platform and the tap wraps the renderer the glue
returns. Its Telegram counterpart, `Test::TapHostedWidget()`, asks it to
mirror and records what it sends. An overlay arms the message with
`ArmWidgetOpsTap()` and reads `ReadWidgetOps()`, and asks for the helper's
renderer work with `TimeWidgetHelperWork()`; nothing here is called by name.

## Preconditions and limits

- **Debug helper only.** `_other/widget_helper_rust.cpp` passes `tapped`
  to `tw_helper_serve()` in a Debug build of `TelegramWidgetHelper` alone
  (macOS, Linux and Windows); a Release helper builds its renderers
  untapped, mirrors and times nothing and ends a route at a `Mirror` or
  `MirrorWork` control, as a control the helper does not take. A Telegram
  Release build with the harness starts that Release helper and therefore
  never asks.
- **Rust without `unsafe`.** It is part of the helper's one Rust library,
  in a crate that forbids `unsafe` code; its wire is the Rust mirror of
  `tgw/tgw_hosted_wire.h`.
- **One forwarding.** The renderer wrapper is `ForwardingRenderer` of
  `forward.rs`: `load()`, `send()`, `setTheme()`, `setWidth()` and
  `destroy()` forwarded once each on the caller's thread, the renderer's
  `onMessage` and `onDiag` chained to the wrapper's own, read when invoked
  because the served host assigns them after the factory returned, and
  the glue's renderer destroyed before the observer. The arming, the
  mirror records and the timing are the observer's.
- **Armed per renderer.** Nothing is sent until the platform's first exact
  `Mirror` (`[0xFF][5]`) or `MirrorWork` (`[0xFF][6]`) control, which it
  swallows: the glue never sees one. Telegram sends one of them right after
  constructing the hosted host, before its start; the engine process keeps
  such messages until it built the served host and replays them on the
  glue's thread before that host starts, so the tap is armed before the
  first served-host message. A later control only adds the timing a
  `MirrorWork` asks for.
- **What is mirrored.** Each record is one `Mirrored` platform message
  (`[0xFF][67][way][u32 length][text]`, little endian, exactly that long,
  at most `kMaxMessageBytes`, 32 MiB), sent before the call it records is
  forwarded, except way `w`, sent after it:

| Way | When | Text |
| --- | --- | --- |
| `b` | Once, at the arming | The colour scheme the factory built the renderer with. |
| `t` | At the arming, when a `setTheme()` before it changed the scheme; then at every `setTheme()` | The scheme. |
| `h` | Every served-host message to the renderer but its validated `ops` batches (prefix `{"type":"ops",`), which reach Telegram through the host's `onRenderOps` alone | The message, verbatim. |
| `r` | Every renderer message to the served host | The message, verbatim. |
| `w` | Armed by `MirrorWork` only: right after each outermost renderer call (`send()`, `setTheme()`) and each outermost platform message it chained returned | One `HelperWorkRecord`, 41 bytes (below). |

- **The renderer work (way `w`).** The platform the glue gets forwards
  `send()` and `request()` to the served host's platform unchanged and
  times each `request()`, the glue's wait for Telegram's answer to a backend
  request (a font, a text width, an image, canvas pixels). Each outermost
  operation of the glue thread is one record: `kind`, `started` and
  `finished` on the helper's steady clock (microseconds of
  `std::chrono::steady_clock`, which `_other/widget_helper_rust.cpp` hands
  the Rust serving and Telegram's process reads too), `waited` and `waits`
  (the requests inside it), `seq` of a `FrameRequest`, and `frameBytes`,
  the encoded `Frame` and `FrameDelta` messages it sent. A call made inside
  another (the glue defers nested steps to the outer loop) belongs to the
  outer one, and so does the work of the steps it ran. The kinds:

| Kind | The operation |
| --- | --- |
| `Message` | A served-host message: the renderer's `receive()`, a validated batch applied or a layout query answered, with the style and layout they need. |
| `Theme` | The served host's `setTheme()`. |
| `Tick` | A `FrameRequest` that ticks and does not paint: animations and the build they need. |
| `Paint` | A `FrameRequest` that paints: the settle, style, layout, the display-list build and the `Frame`'s encoding. |
| `Platform` | Any other renderer platform message: input, sizes, selection, claims. |
| `Control` | A glue control: `Open` (the renderer end's construction, theme and first layout) and the press controls. |

  The record's own send comes after `finished`; the mirror records of ways
  `h`, `r` and `t` sent during an operation are inside it (Debug only, a few
  copies of each message). An operation that failed sends no record.
- **Not mirrored.** A message longer than `kMaxMirroredTextBytes` (the
  limit less the record's 7 bytes) is skipped, with no marker. The renderer's
  diagnostics (`onDiag`) are forwarded and not mirrored: they reach Telegram
  through the hosted client's own diagnostics. The glue's local queries
  (the viewport before a paint, the element under the pointer for the
  cursor and the press it follows) and its controls (`Content`, `Loaded`,
  `Stopped`, `Cursor`, `CanvasRefused`, `Clicked`) never pass the wrapper.

## Readings and refusals

It reads nothing and refuses nothing. Telegram's tap consumes a well-formed
record only after it sent the `Mirror` or `MirrorWork` itself. Any other
`Mirrored` message goes to the runtime's platform observer, and every record
of a kept route to the runtime's kept observer; their platform end drops a
well-formed one after its shape check, without a parse or a callback, and
ends the route at a malformed one, as at any malformed control.
`DecodeHelperWork()` takes exactly 41 bytes with a known kind,
`0 <= started <= finished`, at most an hour (`kMaxHelperInterval`) between
them and `0 <= waited <=` that interval
([The helper work record](test_widget_renderer_forward.md#the-helper-work-record)).
This half stamps `finished` on the steady clock before it sends the record,
so Telegram's tap, which reads the same clock at the receipt, counts a
record that ends after its receipt as malformed before it maps the stamps
([Helper renderer work](test_widget_ops.md#helper-renderer-work)).
The record's format is `test_widget_renderer_forward.h` on Telegram's side
and `forward.rs` on the helper's, which encode and decode it alike.

## Lifetime and cleanup

The wrapper owns the glue's renderer and destroys it first. The glue owns
the timed platform; the handler the tap chains into the served host's
platform `onMessage` holds the tap's state and the timed platform weakly, so
a control after the wrapper is gone does nothing, and every other message
still reaches the glue's own handler while the glue lives. All of it runs on
the served host's glue thread, the thread of every renderer call, of the
platform's messages and of the glue's requests. When the widget's engine
ends the served host keeps only its renderer. After a death the glue is
frozen and forwards nothing more; after the end a keep asks for
(`terminated`) the renderer's messages still pass the wrapper while the kept
document lives, and their way `r` and `w` records reach the kept route,
never the tap. Nothing is written anywhere.

## Verification

It is the port of the C++ tap the Debug helper compiled until the helper
became Rust-only (twidget's `rust/PLAN.md`, stages H1, H4 and H5). In stage
H4 the conformance and attack packs ran on Windows (x64 and x86) and Linux,
and the stress pack on Windows x64, with the C++ helper and with the Rust
one, through an overlay that dumped every record by host: ways `b` and `t`
and the `w` kinds and sequences matched, and ways `h` and `r` matched
outside clock-driven widgets, with the same remaining differences as two
runs of the C++ helper.

## Related guidance

[Validated-ops tap](test_widget_ops.md#hosted-widgets);
[renderer work timing](test_widget_host.md#the-helpers-share);
[remote engines](test_widget_fixture.md#remote-engines).
