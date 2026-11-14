# test_paint_stream

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Reusable API.
Source contract: [test_paint_stream.h](../../test_paint_stream.h).

## Use when

A campaign claims what one opaque widget's whole paint costs, for example
"smooth while animating or dragging". Debug Telegram links Debug Qt, so a
paint timed inside it is not a paint cost. This header records the real owner
into an in-memory `QPicture` per sample from the overlay, saves each stream
with format-preserving sidecars and writes a manifest; the shared
[paint replay recipe](../../../../../.agents/shared/evidence/paint-replay.md)
replays the streams in optimized Qt behind the
[native execution gate](../../../../../.agents/shared/evidence/native-execution.md)
and judges them. For a picture of what was painted use
[test_capture](test_capture.md); for frames at chosen animation times use
[test_animation_clock](test_animation_clock.md).

## Preconditions and limits

- An opaque owner whose own `paintEvent` paints the measured surface.
  `RecordPaint`'s default flags, `QWidget::RenderFlags()`, record what the
  backing store paints for a child owner: its `paintEvent`, plus its
  `autoFillBackground` fill or styled background, without children. A
  translucent child therefore leaves device pixels uncovered, and the
  assessor refuses it as `not-whole-owner`. Pass
  `QWidget::DrawWindowBackground` only for a top-level window owner: Qt then
  fills the palette's `Window` brush under an owner that sets neither
  `Qt::WA_OpaquePaintEvent` nor `Qt::WA_NoSystemBackground`, as the window's
  own paint does. That fill is part of the stream and its timed cost, and it
  covers the surface whatever the owner paints. For a child it is a paint
  the screen never runs, so it would hide a translucent owner and inflate
  the cost. Pass `QWidget::DrawChildren` for an owner that paints through
  children.
- Images keep their recorded row stride: an image over an external buffer
  with padded rows, such as an FFmpeg frame with 64-byte rows, replays from
  that layout.
- macOS only, and Qt's private `QtGui/private/qpicture_p.h` (GuiPrivate):
  the header stops with `#error` on another platform or without it, and a
  `DESKTOP_APP_DISABLE_PRIVATE_QT_API` build cannot use it. `QPicturePrivate`
  is private API: the recorder and the replayer must report the same exact
  `qVersion()`, picture format and sidecar version.
- The replayer runs under the recorded platform plugin and color scheme
  (`PaintQtIdentity()` writes both into the manifest) and refuses another one:
  cocoa draws grayscale-smoothed glyphs differently in Dark and Light. A
  session whose appearance changed after recording must be replayed in the
  recorded appearance or recorded again.
- A stream stores text, its font and flags, not glyphs: text items are laid
  out again at replay. The replay needs every font file the owner uses, and an
  owner whose text replays differently from its direct render fails
  `recording-not-faithful`.
- `PaintSurface::rootType` is the literal demangled type the author writes
  down (`PaintTypeName()` returns that form for a live widget); it is never
  read from the recorded widget, so a wrong root is refused as
  `wrong-paint-root` instead of adopted.
  `size` is the owner's logical size and `dpr` the device pixel ratio of every
  replay target.
- Record inside the timed window and save after it: `RecordPaint` times only
  the painter session around `QWidget::render()`, while `SavePaintRecording`
  encodes, writes, reads back, replays and digests. `PaintDirectDifference`
  renders the owner a second time and replays the stream, so call it on a few
  samples whose cadence the claim does not depend on, right after their
  `RecordPaint` and before the owner's state changes.
- A region recording keeps the region's bounding rectangle clipped to the
  owner, at its own position, and clips the picture painter to it.
- Stream names use letters, digits, `_`, `.` and `-`, so a stream file never
  leaves the run directory. Caller fields must not reuse a row key (`name`,
  `role`, `recordMs`, `root`, `qpic`, `tables`, `counts`, `fonts`,
  `referenceSha256`, `directDifference`).
- Never `load()` or `setData()` a recording or a copy sharing it: both detach
  through a private copy that drops `in_memory_only`, the format version and
  the four lists. Copies used for `save()` and `play()` are safe.
- Streams and sidecars hold painted pixels, in a real session account
  content: keep them in the ignored run directory and never promote them;
  promote the assessment JSON only.
- The header establishes no timing claim by itself. `recordMs` measured in
  Debug Telegram is a Debug-Qt CPU cost of emitting the stream (conservative);
  the replay supplies the optimized raster cost of the same stream. Neither is
  a Release Telegram frame time or a compositor cadence.

## Minimal use

An overlay sketch against the real API; `owner` is the exact live widget the
overlay resolved, and the field hiding is the campaign's own owner-specific
step. The same calls are compiled and run by the component fixture
`.agents/shared/evidence/paint_replay_component/fixture.cpp`; no Telegram
translation unit compiles this snippet.

```cpp
#include "test/test_log.h"
#include "test/test_paint_stream.h"

auto surface = Test::PaintSurface();
surface.rootType = u"Example::Owner"_q; // literal, never PaintTypeName(owner)
surface.size = owner->size();
surface.dpr = owner->devicePixelRatio();

auto recordings = std::vector<Test::PaintRecording>();

// Inside each timed window ("rest", "drag", "glide"), once per sample.
auto recording = Test::RecordPaint(
	owner,
	u"%1_%2"_q.arg(window).arg(index),
	u"timed"_q,
	QJsonObject{
		{ u"window"_q, window },
		{ u"at"_q, double(crl::now()) },
		{ u"pressed"_q, pressed },
		{ u"moving"_q, moving },
		{ u"position"_q, QJsonArray{ position.x(), position.y() } },
	});
if (window == u"rest"_q && !index) {
	recording.directDifference = Test::PaintDirectDifference(
		owner,
		recording,
		surface);
}
recordings.push_back(std::move(recording));

// After the window, with the owner back in the state of "rest_0".
hideMeasuredField(owner);
recordings.push_back(Test::RecordPaint(
	owner,
	u"absent_0"_q,
	u"absent"_q,
	QJsonObject{
		{ u"pairedWith"_q, u"rest_0"_q },
		{ u"fieldRect"_q, QJsonArray{ 16, 16, 64, 64 } },
	}));
showMeasuredField(owner);
recordings.push_back(Test::RecordPaint(
	owner,
	u"narrow_0"_q,
	u"narrow"_q,
	QJsonObject(),
	QRegion(0, 0, 24, owner->height())));
recordings.push_back(Test::EmptyPaintRecording(u"empty_0"_q));

const auto dir = QDir(Test::EvidenceDir()).filePath(u"paint"_q);
auto rows = QJsonArray();
for (const auto &recording : recordings) {
	auto error = QString();
	rows.push_back(
		Test::SavePaintRecording(dir, recording, surface, &error));
	Test::Check(error.isEmpty(), u"saved "_q + recording.name, error);
}
auto error = QString();
Test::Check(
	Test::WritePaintManifest(dir, surface, rows, &error),
	u"paint manifest"_q,
	error);
```

Roles are `timed` (whole-owner samples the timing and the window judgement
read), `absent` (the paired state without the measured field, with its
owner-logical `fieldRect`), `narrow` (a region smaller than the owner, which
must not pass as whole) and `empty`. Any other role, such as a "baseline", is
refused by the assessor.

## Readings and refusals

`SavePaintRecording` returns the manifest row: `name`, `role`, `recordMs`,
`root {type, size [w, h], region [x, y, w, h], flags}`, `qpic` and `tables`
`{file, bytes, sha256}`, `counts {images, pixmaps, brushes, pens}`, `fonts`
(sorted `{family, resolved}`), `referenceSha256` (the recorder-side replay
over sentinel 0), `directDifference` when measured, then the caller's fields.
On failure it returns an empty object and sets `*error` to `<code>: <detail>`
with code `invalid-stream-name`, `reserved-field-key`,
`not-in-memory-picture`, `cannot-create-directory`, `invalid-surface`,
`stream-save-failed`, `sidecar-write-failed`, `write-failed`,
`write-verify-failed` or `reference-playback-failed`.
`WritePaintManifest` writes `<dir>/manifest.json` with `schema`, `qt`
(`version`, `debug`, `pictureFormat`, `sidecar`, `platform`, `colorScheme`),
`surface`, `sentinels` and `streams`, and returns false with
`write-failed: <path>`.

A saved row proves only that its files were written and read back
byte-identical. `directDifference == 0` proves that the in-memory stream
reproduces the owner's direct render in the recorder's process. Fidelity
across builds is decided by the replay: its digest must equal
`referenceSha256`, its fonts must resolve like the recorder's, and the
parsed sidecar lists must have the row's `counts`. The list counts and digests decide completeness:
optimized Qt plays a stream whose sidecar entries are missing as `true` and
paints it wrong, while only Debug Qt asserts.

`LoadPaintStream` returns `error` as one of, in decision order:
`invalid-stream-name`, `missing-stream-file`, `empty-stream-file`,
`missing-sidecar`, `empty-sidecar`, `picture-format-mismatch`,
`stream-load-failed`, `sidecar-unreadable`, `sidecar-version-mismatch`,
`sidecar-count-mismatch`; `identity` always carries `{bytes, sha256}` of the
files it read. The replayer adds `playback-failed` and `missing-pair` per
row and its own fatal errors; the assessor's codes and the actions for them
are in the recipe. Every refusal leaves the measurement undecided, never
passed.

## Lifetime and cleanup

A `PaintRecording` owns its picture, whose lists keep shared copies of the
images, pixmaps, brushes and pens the owner painted until the recording is
destroyed. The header keeps no global state, installs nothing and writes only
under the directory the caller passes, so there is nothing to release in
`Runner::onFinish`.

## Verification

`python3 .agents/shared/evidence/paint_replay_test.py -v` is the self-test.
Its component class builds the standalone component in Release and Debug Qt,
records the synthetic opaque owner with the Debug-Qt fixture under cocoa,
gates and runs the optimized replayer and assesses the result. The owner
draws an image whose rows are padded to 64 bytes, so the positive digests
cover a non-default stride. It catches a deleted, emptied or swapped sidecar,
an emptied, corrupted or header-only stream, a container recorded as the
owner, a translucent child owner recorded with the default flags, a
band-only timed region, a self-paired absent control, a replay without the
font, a changed Qt version or platform and a Debug replayer. It needs macOS,
cmake, the repository font `Telegram/lib_ui/fonts/OpenSans-Regular.ttf` and
a logged-in GUI session for the cocoa plugin. No Telegram translation unit includes the header; the
component builds compile it with Telegram's `QT_NO_KEYWORDS`,
`QT_NO_CAST_FROM_BYTEARRAY` and `QT_DEPRECATED_WARNINGS_SINCE` definitions and
`-Wall -Wextra`, so an in-target compile is not verified.

## Related guidance

- [Paint replay recipe](../../../../../.agents/shared/evidence/paint-replay.md):
  component build, gate, replay, assessment, refusals and timing limits.
- [Motion-window assessment](../../../../../.agents/shared/evidence/motion-window.md):
  judging rest, pressed and release windows from the same manifest rows.
- [test_animation_clock](test_animation_clock.md): clocked input whose
  `actionStarted` / `actionFinished` bracket the window's origin actions.
- [test_probe](test_probe.md): handler-time telemetry for exact time origins.
