# Whole-widget paint replay

This recipe answers what one opaque widget's whole paint costs in optimized
Qt, from recordings of the real owner made inside Debug Telegram. Debug
Telegram links Qt's Debug-configuration libraries, so a paint timed there is
mostly unoptimized Qt raster time (see the "paint-cost bound written as a
ratio of Debug timings" row of `Telegram/SourceFiles/test/docs/diagnostics.md`).
The QR gift campaign (`2026/10/15/choose-a-unique-gift-for-the-qr-box`,
`work/test.md`, Run 2) added a Debug baseline to an optimized replay of a
field; this recipe refuses that arithmetic and measures the whole owner's
own recorded stream instead.

The pieces:

- [test_paint_stream.h](../../../Telegram/SourceFiles/test/docs/helpers/test_paint_stream.md)
  records the owner from the overlay, saves streams with format-preserving
  sidecars and writes `manifest.json`; the replayer compiles the same header
  to load them.
- `.agents/shared/evidence/paint_replay_component/` is a standalone CMake
  project with two executables: `paint_replay`, the replayer, and
  `paint_replay_fixture`, a synthetic opaque owner used by the self-tests in
  place of Telegram.
- [native_exec.py](native-execution.md) gates the replayer before its
  timings are used.
- `paint_replay.py` assesses one replay: gate, build mode, Qt and platform
  contract, stream identities, fidelity, pixel controls and sample
  accounting. Its `account_streams`, `quantile`, `read_input` and
  `stream_name` helpers and `IDENTITY_KEYS` are shared with
  [motion_window.py](motion-window.md), which judges rest, pressed and
  release windows from the same manifest rows.

Nothing here builds or launches Telegram, and no account or network is
needed beyond the campaign's own recording.

## What the recipe measures

Two timed sections, both read with `QElapsedTimer` (monotonic elapsed time):

- `recordMs` (manifest row): the painter session around `QWidget::render()`
  that emits the whole owner into an in-memory `QPicture`, in the recorder's
  Qt build. Recorded in Debug Telegram it runs in Debug Qt, so it
  over-estimates the optimized emission cost.
- `timesNs` (replay row): `QPainter` construction, `QPicture::play()` and
  painter end that raster the whole stream onto an
  `ARGB32_Premultiplied` target of the declared surface in optimized Qt,
  after the declared warmup. Target fills, file loads, decoding, digests and
  encoding are outside the timed section; so are layout, event dispatch,
  other widgets, backing-store flush and the compositor.

The one admitted composition is per stream: `envelopeMs = recordMs +` the
envelope quantile of the same stream's replay. No Debug raster term enters
it: a Debug-Qt replayer is refused (`unverified-qt-build`), a role other
than the four below (such as a "baseline") is refused (`unknown-role`), and
timings on a row that is not a recorded timed stream are refused
(`sample-accounting`). A stream recorded over a region, or by another root,
cannot stand in for the whole owner (`not-whole-owner`, `wrong-paint-root`).

## Recipe

`HOST_ARCH` is what `uname -m` prints in a native shell; `RECORD_DIR` is the
overlay's ignored output directory; `RUN_DIR` is the run's ignored directory;
`WARMUP`, `SAMPLES` and the quantiles `Q...` are the campaign's declared
values. The tools have no defaults for them. Keep each step's command line,
working directory, environment additions, exit code and full log in
`RUN_DIR`.

1. **Record.** In the overlay, follow the
   [helper page](../../../Telegram/SourceFiles/test/docs/helpers/test_paint_stream.md):
   declare a `Test::PaintSurface` with the literal owner type, call
   `RecordPaint` once per sample inside the timed window with the
   `window`, `at`, `pressed`, `moving` and `position` fields (default flags
   for a child owner, `QWidget::DrawWindowBackground` only for a top-level
   window owner), `PaintDirectDifference` on at least one timed sample,
   record the absent, narrow and empty controls, and after the window
   `SavePaintRecording` each recording and `WritePaintManifest` into a
   subdirectory of `Test::EvidenceDir()`. Note every font file the owner's text uses; for
   Telegram that is every file under `Telegram/lib_ui/fonts/`, which it
   loads from its resources at startup.

2. **Build the component** for the host, in Release:

   ```bash
   cmake -S .agents/shared/evidence/paint_replay_component \
     -B RUN_DIR/component-release \
     -DCMAKE_BUILD_TYPE=Release \
     -DCMAKE_OSX_ARCHITECTURES=HOST_ARCH
   cmake --build RUN_DIR/component-release
   ```

   A `CMAKE_PREFIX_PATH` passed with `-D` or exported wins. Without one, the
   project reads `qt_requested` from the checkout's configured
   `out/CMakeCache.txt` and uses `../Libraries/local/Qt-<qt_requested>` and
   `../Libraries/local`, the Apple prefix rule of
   `cmake/external/qt/package.cmake`, so no path or Qt version is written
   into it. `-DCMAKE_BUILD_TYPE=Debug` builds the same project against Qt's
   `_debug` libraries, the Qt of Debug Telegram: the self-tests record with
   that tree's fixture and use its replayer as the refused unverified-build
   control. This is the component, never a Telegram Release build.

3. **Gate** the exact replayer before the replay:

   ```bash
   python3 .agents/shared/evidence/native_exec.py \
     --executable RUN_DIR/component-release/paint_replay \
     --host-arch HOST_ARCH \
     --out RUN_DIR/paint-gate.json
   ```

   A rebuild needs a new gate; the assessor re-reads the file's SHA-256.

4. **Replay** in the logged-in GUI session, in the appearance the recording
   was made in:

   ```bash
   RUN_DIR/component-release/paint_replay \
     --manifest RECORD_DIR/manifest.json \
     --out RUN_DIR/paint-replay.json \
     --warmup WARMUP --samples SAMPLES \
     --font FONT_FILE --font FONT_FILE
   ```

   The replayer reads the manifest first, then starts a `QGuiApplication`
   under the manifest's platform plugin (it sets `QT_QPA_PLATFORM` itself)
   as a background process: no window, no Dock icon, no focus. It loads every
   file before timing anything. Exit 0 means the rows were replayed (row
   errors are judged by the assessor), 1 a fatal error written into the
   result, 2 a usage error.

5. **Assess**:

   ```bash
   python3 .agents/shared/evidence/paint_replay.py \
     --manifest RECORD_DIR/manifest.json \
     --replay RUN_DIR/paint-replay.json \
     --gate RUN_DIR/paint-gate.json \
     --host-arch HOST_ARCH \
     --warmup WARMUP --samples SAMPLES \
     --quantile Q1 --quantile Q2 --envelope-quantile Q2 \
     --out RUN_DIR/paint-assessment.json
   ```

   Exit 0 when every check passes, 1 on a refusal, 2 on a usage or input
   error (an unreadable or non-JSON input, an `--envelope-quantile` that is
   not one of the `--quantile` values, a quantile outside (0, 1], or an
   `--out` naming an input).

## Required controls

Every assessment needs all of them; a missing one is refused.

- **Timed whole-owner streams** (role `timed`): recorded from the declared
  owner (`root.type` and `root.size` equal the surface's) over its whole
  rectangle, each covering every device pixel of the declared surface. A
  device pixel is covered when the stream's replays over the two sentinel
  fills (`Test::PaintSentinel(0)` and `(1)`) agree there, so a translucent
  owner, a region or a container that does not paint the owner falls short.
  `RecordPaint`'s default flags (`QWidget::RenderFlags()`) record what the
  backing store paints for a child: its `paintEvent`, plus an
  `autoFillBackground` fill or styled background, so a translucent child
  is refused. `QWidget::DrawWindowBackground` is for a top-level window
  owner only: Qt then fills the palette's `Window` brush under an owner
  without `Qt::WA_OpaquePaintEvent` or `Qt::WA_NoSystemBackground`, as the
  window paints it on screen. That fill covers the surface, enters the
  stream and its timed cost, and for a child would hide translucency and add
  a paint the screen never runs.
- **Recording fidelity**: `directDifference` on at least one timed row, and
  0 on every row that carries it: the in-memory stream reproduces the owner's
  direct render in the recorder.
- **Absent field** (role `absent`): the owner recorded without the measured
  field, `pairedWith` naming the timed recording of the same state and
  `fieldRect` the field's owner-logical rectangle. Its replay must differ
  from the pair's, and only inside `fieldRect` scaled to device pixels (floor
  of the origin, ceiling of the far edge, at the surface's `dpr`).
- **Narrow region** (role `narrow`): a region smaller than the owner, whose
  coverage must stay below the surface: the coverage reading can fail.
- **Empty** (role `empty`): `EmptyPaintRecording`, which must change no
  pixel of the sentinel target.

Fidelity across builds is tolerance-free: every row's optimized
`replaySha256` must equal the recorder's `referenceSha256` (the same replay
procedure over sentinel 0 in the recorder's process), its fonts must resolve
like the recorder's, and the parsed sidecar lists must have the row's
`counts`. Optimized Qt plays a stream whose sidecar entries are missing as
successful and paints it wrong, so completeness is decided by those counts
and digests, never by `QPicture::play()`.

## Manifest, replay result and assessment

**Manifest** (`WritePaintManifest`, row keys on the helper page): `schema`,
`qt` (`version`, `debug`, `pictureFormat`, `sidecar`, `platform`,
`colorScheme`), `surface` (`rootType`, `size`, `dpr`), `sentinels` and
`streams`. A row carries `name`, `role`, `recordMs`, `root` (`type`, `size`,
`region`, `flags`), `qpic` and `tables` (`file`, `bytes`, `sha256`),
`counts`, `fonts`, `referenceSha256`, `directDifference` when measured and
the caller's fields (`window`, `at`, `pressed`, `moving`, `position`,
`pairedWith`, `fieldRect`).

**Replay result** (`paint_replay --out`): `schema`, `tool`
(`paint_replay`), `startedAt` and `finishedAt` (epoch ms), `executable`,
`native` (`buildArch`, `runningArch`, `translated`), `manifest` (`path`,
`bytes`, `sha256` of the bytes read), `qt` (the replayer's side of the
contract), `fonts` (`file`, `loaded`, `families`), `warmup`, `samples`,
`surface` and `rows`. A row carries `name`, `role`, `qpic` and `tables`
(`bytes`, `sha256` of the files read), then either `error` or
`replaySha256`, `changedPixels` (against a plain sentinel-0 target),
`coveragePixels`, `surfacePixels`, `fonts`, `pair` for an absent row
(`with`, `differingPixels`, `bounds [x, y, w, h]` in device pixels, or
`error: missing-pair`) and `timesNs` for a timed row. A fatal error leaves
`error` and `errorDetail` at the top: `manifest-unreadable`,
`manifest-schema`, `font-load-failed`, `qt-version-mismatch`,
`picture-format-mismatch`, `sidecar-version-mismatch` or
`platform-mismatch`.

**Assessment** (`paint_replay.py`): `schema`, `tool`
(`paint_replay_assess`), `allPassed`, `reasons` (failing codes in check
order), `checks`, `declared`, `qt` (`recorder`, `replayer`), `streams`,
`controls`, `boundary`, `scope`, `limits` and, from the CLI, `inputs`
(`path`, `bytes`, `sha256` of each input). Each `checks` entry is
`{check, pass, reason, message, facts}`; every check runs, and a check with
several problems emits one failing entry per code with the affected rows in
`facts.details`. The checks, in order: `inputs`, `gate`, `replay-build`,
`qt-contract`, `accounting`, `roles`, `streams`, `recording-fidelity`,
`paint-root`, `whole-owner`, `empty-control`, `absent-control`,
`narrow-control`, `timing`. Each `streams` entry is one timed row: `name`,
`window`, `recordMs`, `recorderDebug`, `replayerDebug`, `replay` (`n`,
`minMs`, `medianMs`, `medianRank`, `maxMs`, `quantiles` of `{q, valueMs,
rank, n}`), `envelopeQuantile` and `envelopeMs`.

Stream identity is a bijection by name: every manifest row needs exactly one
replay row, and where both sides carry a `qpic` or `tables` SHA-256 they must
be equal. The replay result must also name the SHA-256 of the manifest file
assessed, so a manifest edited after the replay is refused.

## Refusals

Codes in check order. A replayer row error is reported under its own code.

| Code | Meaning | Action |
|---|---|---|
| `manifest-schema` | The manifest is not a schema-1 paint manifest with a `qt` block, a replayable surface and a list of stream rows. | Pass the `manifest.json` that `WritePaintManifest()` wrote. |
| `replay-schema` | The replay result is not a schema-1 `paint_replay` result with a `rows` list. | Pass the `--out` file of the gated replay. |
| `replay-manifest-mismatch` | The replay result names another manifest SHA-256 than the manifest assessed, or none. | Assess the manifest the replay read, unchanged since. |
| `missing-gate` | No gate verdict was given. | Gate the exact replayer with `native_exec.py --out` before the replay and pass `--gate`. |
| `gate-not-passed` | The verdict is not a `native_exec` schema-1 PASS, or its own execution record contradicts the PASS. | Gate the replayer again. |
| `gate-no-execution` | The verdict carries no execution record with a report. | Gate the replayer again. |
| `gate-nonce` | The verdict's report nonce is not the nonce the gate sent. | Gate the replayer again. |
| `gate-executable-mismatch` | The verdict gated another file than the replay's `executable`, such as the Debug tree's replayer. | Gate the file that ran the replay. |
| `gate-stale-binary` | The replay executable's current SHA-256 differs from the gated one: rebuilt, replaced, or another file. | Gate the current file and replay with it unchanged. |
| `gate-host-arch` | The verdict declared another host architecture than `--host-arch`. | Gate and assess with the architecture the host runs natively. |
| `gate-after-replay` | The gate finished after the replay started, or a timestamp is missing. | Gate first, then replay with the unchanged file. |
| `replay-not-native` | The replay result's `native` block reports another architecture or translation. | Rebuild for the host architecture, gate and replay again. |
| `replay-fatal` | The replayer stopped with a fatal error (`facts.details[].error`). | Act on that code: build against the recording's Qt and header for a version, format or sidecar mismatch; replay in the recorded platform and appearance, or record again, for `platform-mismatch`; pass readable font files for `font-load-failed`; pass the written manifest otherwise. |
| `unverified-qt-build` | The replay's `qt.debug` is missing or not `false`. | Replay with the Release component; a Debug-Qt raster never enters a paint cost. |
| `qt-version-mismatch` | Recorder and replayer report different `qVersion()`. | Build the component against the recording's exact Qt version. |
| `picture-format-mismatch` | The `QPicture` stream format differs (manifest `qt.pictureFormat`, or a `.qpic` header). | Build the component against the recording's exact Qt version. |
| `sidecar-version-mismatch` | The sidecar version differs. | Build the component from the recorder's `test_paint_stream.h`. |
| `platform-mismatch` | Recorder and replayer ran under different platform plugins or color schemes. | Replay under the recorded platform in the recorded appearance, or record again. |
| `duplicate-record` | Two manifest rows carry one stream name. | Give every recording its own name and record again. |
| `duplicate-replay` | Two replay rows carry one stream name. | Replay the manifest once with the unmodified replayer. |
| `missing-replay` | A recorded stream has no replay row. | Replay the complete manifest. |
| `extra-replay` | A replay row has no recorded stream. | Replay exactly the manifest assessed. |
| `mismatched-identity` | A `.qpic` or `.tables` SHA-256 differs between the manifest and the bytes the replayer read. | Replay the files the recorder wrote, unchanged. |
| `unknown-role` | A row's role is not `timed`, `absent`, `narrow` or `empty`, such as a Debug baseline. | Record only the four roles. |
| `invalid-stream-name` | A stream name is not letters, digits, `_`, `.` and `-`. | Fix the recording code and record again. |
| `missing-stream-file` | The stream's `.qpic` is missing. | Keep the run directory as written, or record again. |
| `empty-stream-file` | The stream's `.qpic` is empty. | Keep the run directory as written, or record again. |
| `missing-sidecar` | The stream's `.tables` is missing. | Keep the run directory as written, or record again. |
| `empty-sidecar` | The stream's `.tables` is empty. | Keep the run directory as written, or record again. |
| `stream-load-failed` | `QPicture::load()` refused the `.qpic` header or checksum. | Keep the run directory as written, or record again. |
| `sidecar-unreadable` | The `.tables` does not parse. | Keep the run directory as written, or record again. |
| `sidecar-count-mismatch` | The parsed sidecar lists differ from the row's `counts`: not this stream's sidecar. | Keep each stream with its own sidecar, or record again. |
| `playback-failed` | `QPicture::play()` returned false. | Keep the run directory as written, or record again. |
| `row-error` | A row error this assessor does not know (`facts.details[].error`). | Read the replayer's code and teach the assessor about it. |
| `replay-differs-from-recording` | The optimized replay's digest differs from the recorder's reference replay. | Replay with every font file the recorder used and the complete sidecars; never add a tolerance. |
| `font-not-retained` | The fonts the replay resolves differ from the recorder's. | Pass every font file the recorder used with `--font`. |
| `recording-not-faithful` | A row's `directDifference` is not 0. | Record an owner whose stream reproduces its paint; text laid out differently at replay cannot be measured this way. |
| `recording-fidelity-unmeasured` | No timed row carries `directDifference`. | Call `PaintDirectDifference()` on at least one timed recording. |
| `wrong-paint-root` | A timed, absent or narrow row's `root.type` or `root.size` is not the declared surface's. | Record the declared owner, or declare the owner that was recorded. |
| `missing-timed-stream` | The manifest has no timed row. | Record the timed samples the claim is about. |
| `not-whole-owner` | A timed or absent row does not cover every device pixel of the declared surface, or was recorded over a partial region. | Record the whole opaque owner with `RecordPaint`'s default flags, or `DrawWindowBackground` for a top-level window owner. |
| `missing-empty-control` | No replayed empty control. | Record `EmptyPaintRecording()`. |
| `empty-changes-pixels` | The empty control changed pixels. | Investigate the replayer; nothing may change. |
| `missing-absent-control` | No replayed absent control. | Record the owner without the field, with `pairedWith` and `fieldRect`. |
| `absent-pair-invalid` | `pairedWith` is not a timed stream, or the replay pair is missing, failed or for another stream. | Pair the absent row with the timed recording of the same state. |
| `absent-removes-nothing` | Removing the field changed no pixel against the pair. | Hide the measured field in the absent recording. |
| `absent-outside-field` | The pair differs outside `fieldRect` in device pixels, or no `fieldRect` is declared. | Declare the field's rectangle and change only the field. |
| `missing-narrow-control` | No replayed narrow control. | Record a region smaller than the owner. |
| `narrow-not-narrow` | The narrow control covers the whole surface. | Record it over a region smaller than the owner. |
| `sample-accounting` | The replay's `warmup` or `samples` differ from the declared ones, a timed row lacks exactly the declared samples, or a non-timed row carries timings. | Replay and assess with the same declared values. |
| `missing-record-cost` | A timed row has no `recordMs`. | Save recordings made by `RecordPaint()`. |

## Timing claim and its limits

A passing assessment supports, per timed stream and for this run only: the
recorder took `recordMs` to emit the whole owner's stream in its Qt build,
and the gated optimized replayer's `timesNs` had the reported values; the
quantile of rank `r` among `n` kept replays was `valueMs`; their sum is
`envelopeMs`. Quantiles are empirical nearest-rank order statistics: the
value of rank `ceil(q * n)`, 1-based. Every quantile is reported with its
rank and `n` because the rank decides what it means: with 25 samples the
0.99 quantile is the maximum, with 400 it is order statistic 396.

It does not support:

- a Release Telegram frame time: the replay runs one stream in a standalone
  process, without Telegram's event loop, layout, other widgets or
  backing-store flush;
- a compositor or presentation cadence: nothing is shown on screen;
- a population, tail or confidence bound: the order statistics describe this
  run's kept samples, not other runs, sessions or hosts;
- a cold-cache cost: consecutive replays of one stream reuse warm glyph and
  image caches;
- a claim about any other stream, owner or state than the ones recorded.

`warmup`, `samples` and the quantiles are the campaign's declared inputs; the
assessor checks that the replay used exactly them and invents none.

## Platform contract

Text rasterization depends on the platform plugin's color scheme. Qt's
CoreText engine draws grayscale-smoothed glyphs white-on-black or
black-on-white by `QPlatformTheme::colorScheme()`, so during this recipe's
qualification the same text stream replayed by one optimized binary produced
different pixels under `cocoa` with the Dark scheme than under `offscreen`
(scheme unknown), while `cocoa` with a requested Light scheme equalled
`offscreen`. Therefore:

- the manifest records the recorder's `platform` and `colorScheme`
  (`PaintQtIdentity()`), and Telegram records under `cocoa`;
- the replayer runs under the recorded platform as a background process and
  refuses another color scheme with `platform-mismatch`, for example after
  the session's appearance changed;
- replaying a Telegram recording needs a logged-in GUI session in the
  recorded appearance. No path compares rasters across platforms or
  schemes.

## Qt and private-format contract

- `QPicturePrivate` (`QtGui/private/qpicture_p.h`, the `GuiPrivate` module)
  is private API that may change between Qt versions. Without it the header
  stops with `#error` and the component's
  `find_package(Qt6 REQUIRED COMPONENTS ... GuiPrivate ...)` fails to
  configure; a `DESKTOP_APP_DISABLE_PRIVATE_QT_API` build cannot use it.
- Recorder and replayer must report the same exact `qVersion()`, picture
  format version (`QDataStream::Qt_DefaultCompiledVersion`, also read from
  each `.qpic` header) and sidecar version; anything else is refused.
  `QPicture` itself accepts older stream versions and changed its text
  baselining at Qt 6.11, so mixing versions is refused, not tolerated.
- In-memory streams store only indices into the image, pixmap, brush and pen
  lists; the sidecar keeps images raw (format, size, device pixel ratio,
  color table, color space, scanlines with their recorded row stride, so a
  padded frame such as FFmpeg's 64-byte rows replays from the same layout),
  pixmaps as their images, and brushes and pens with their `QDataStream`
  operators. Qt encodes a texture brush's
  texture as PNG there; the digest oracle would expose a resulting
  difference.
- macOS only: the header, the component CMake (`FATAL_ERROR`) and the gate
  refuse other platforms. Nothing here says how the recipe builds or behaves
  elsewhere.
- Qualified on Qt 6.11.2 on macOS arm64. Another Qt version must pass
  `paint_replay_test.py` before a campaign relies on it.

## Publication safety

Streams and sidecars hold painted content, which in a real session is
account content: the commands, text, images and pens the owner painted.
Keep `RECORD_DIR`, the component builds and the replay results in the
ignored run directory and never promote them. Promote only the assessment
JSON: it carries stream names, digests, counts, pixel counts, timings and
local paths, not painted content.

## Self-tests

```bash
python3 .agents/shared/evidence/paint_replay_test.py -v
```

- `AccountingTests`, `QuantileTests`, `AssessmentTests`, `CliTests` and
  `DocumentationTests` are pure. `AssessmentTests` builds a synthetic
  manifest, replay result and gate (from `native_exec.run_gate` with a
  controlled launcher on a temporary file), shows that it passes, then
  applies one mutation per refusal and asserts the exact set of failing
  codes. `DocumentationTests` checks that the refusal table above lists
  exactly the assessor's codes.
- `ComponentTests` (macOS) configures and builds the component in Release
  and Debug into temporary directories with
  `-DCMAKE_OSX_ARCHITECTURES=<host>`, gates both replayers, records the
  synthetic owner with the Debug-Qt fixture under `cocoa` (the Telegram-like
  recorder) with the repository font
  `Telegram/lib_ui/fonts/OpenSans-Regular.ttf`, and replays and assesses it.
  The owner draws an image whose rows are padded to 64 bytes and whose width
  is not a multiple of 16 pixels (premise: the timed sidecars hold it). The
  positive run must pass with exact digests on every row; then a Debug
  replayer (`unverified-qt-build`), a replay without the font
  (`font-not-retained`, `replay-differs-from-recording`; premise: the font
  is not installed system-wide), a deleted, emptied or swapped sidecar
  (`missing-sidecar`, `empty-sidecar`, `sidecar-count-mismatch`, and with the
  counts swapped too `replay-differs-from-recording`: the silent case), an
  emptied, corrupted or header-only stream (`empty-stream-file`,
  `stream-load-failed`, `playback-failed`), a changed manifest Qt version or
  platform (`replay-fatal`), the container recorded as the owner
  (`wrong-paint-root`, `not-whole-owner`), a translucent child owner
  recorded with the default flags (exactly `not-whole-owner`; premise: its
  coverage stays below the surface), a band-only timed region
  (`not-whole-owner`), a self-paired absent control
  (`absent-removes-nothing`) and missing, foreign and late gates
  (`missing-gate`, `gate-executable-mismatch`, `gate-after-replay`) must be
  refused. Where a mutation sits behind stream accounting, the test rewrites
  the manifest's identity digests to the mutated file so the intended code is
  reached. `test_motion_windows_read_fixture_rows` feeds the same recording
  and replay to `motion_window.py` and requires its rest, pressed and release
  windows to pass. Subprocesses run with `QT_QPA_PLATFORM` removed; an
  exported `CMAKE_PREFIX_PATH` is honoured.
- The component class needs cmake, the Qt prefix (from `out/CMakeCache.txt`
  or `CMAKE_PREFIX_PATH`) and a logged-in GUI session for the `cocoa`
  plugin; without a GUI session it fails rather than switching to
  `offscreen`. A missing cmake fails the class;
  `PAINT_REPLAY_TEST_SKIP_COMPONENT=1` skips only this class.
