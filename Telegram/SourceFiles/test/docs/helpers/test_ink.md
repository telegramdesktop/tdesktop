# test_ink

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_ink.h](../../test_ink.h).

## Use when

Painted bands, ink, colour and contrast, glyph cores on flat and gradient grounds, isolated glyph bounds. Read the contract below and the source header before use.

## Contract and usage

### `test_ink.h`

Derived paint bands, colour separation, ink scans, counts, and contrast
reports. `DeriveBand` names the case where the requested fill is the image's
background outside the candidate, distinct from no-rows; `MeasurePaintedInk`
carries that reason in `report`, and can no longer answer with an empty
`report` on any path. A scan that cannot classify a frame refuses by name as
well: `InkScanState` answers `outside-image`, `no-rows-in-band`,
`candidates-collide`, `background-collinear`, `no-ink` or `classified` beside
a `reason` that is never empty on a returned reading, while `ok` keeps its one
meaning — this band was scanned — so only the two states that looked at no
pixel are false and every frame-level number on the other five is a real
measurement. `background-collinear` is a candidate within `kInkMargin` of the
segment from the scan's measured modal background to another candidate: the
reading is not `classified`, and `countAt` refuses every count as `count=none`
instead of returning the zero that candidate can never earn. It loses to
`candidates-collide` and to `no-ink`, which keep their texts. `candidates` and
`counts` are filled before either refusal returns, so the two are always the
same length, and one candidate's count is read through `InkScan::countAt(i)`,
which names the candidate and refuses an index the reading does not have, a
reading that classified nothing and a candidate pair that does not separate,
instead of indexing past the end of a vector the scan never filled;
`InkCountDetails` prints that refusal as `count=none`, never as a zero.
`FormatInkScan` starts with `FormatInkReport`'s unchanged text and then names
the candidate colours, their counts, `CollisionDump`'s collision and all five
thresholds on the **passing** verdict as well as the failing one. Own
self-test over synthetic images.

### `test_ink.h` (`ReadChromaticRaster`)

Where a coloured glyph clusters in a captured band. Given a pen and a fill to
exclude, it keeps pixels whose channel spread clears `kChromaticFloor` and
that match neither excluded colour within `kSameTolerance`, slides a square
window across their x range, and reports the densest window's box, matched
count, total chromatic count, density and the mean colour of the pixels it
matched — never a colour the caller passed in. `no-chromatic` (the band was
scanned and no pixel cleared the floor) and `below-density` (chromatic pixels
exist but no window reaches `kChromaticDensityFloor`) are named refusals;
`readBox`, `readMean` and `readDensity` refuse on both, so an empty box is
never a measurement. A band that misses the image is `raster-outside-band`,
distinct from both. `FormatChromaticRaster` prints the box, both counts, the
density and the measured mean on a found reading and names the refusal on the
others. Own self-test over synthetic images, registered from
`AppendDeriveBandSelfTest`.

### `test_ink.h` (`ReadGlyphCore`)

Which pen painted a text run, read from its solid glyph cores rather than from
nearest-candidate attribution. Every band pixel is compared with a background
from the source passed as the fourth argument and recorded in `source`
(`GlyphBackground`):

- `BandMode`, the default: the band's most frequent exact colour, read
  exactly as before the source existed. Right for text on a flat fill.
- `Surround`: measured outside the band, for text on gradients and patterns,
  where the most frequent exact colour inside the band can be the glyph's
  own. Two strips `kGlyphSurroundStrip` (8) rows tall, `kGlyphSurroundGap`
  (2) rows above and below the band, over the band's columns, give
  per-column per-channel medians, smoothed by a median over
  ±`kGlyphSurroundSmooth` (6) columns and interpolated linearly between the
  two strips' centre rows, so each band pixel has its own background. Choose
  a band whose strips are the same surface as the text's background, with no
  other text, icon or edge inside them; the image must hold
  `kGlyphSurroundGap + kGlyphSurroundStrip` (10) rows above and below it.

Ink is a pixel at least `kInkDelta` from its background. A solid core is an
ink pixel within `kGlyphCoreTolerance` (8) of the per-channel extreme away
from the background - in each channel, the ink value farthest from its own
pixel's background - which is full coverage rather than a fringe or a
partial blend. The reported modal colour is the mode of those cores, with its
share and the core count; a named pen is only a count of solid cores within
the same tolerance and is never copied into the modal. `background` is the
mean of the background used and `backgroundSpread` its largest per-channel
range: 0 in band mode, -1 when none was established. `FormatGlyphCore`
prints `bgSource=band-mode` or `bgSource=surround` after `bg=`, followed by
`bgSpread=<n|none>` for a surround reading; every other token keeps its
text and order.

`no-paint` is a band with no ink. `no-solid-core` is a band that has ink but
no pixel within the tolerance of the extreme, so a fringe colour is not
returned as the pen. `core-no-background` is a surround strip that leaves the
image: no background was established, `ok` is false, `bg=none`, no colour
and no count, and `reason` quotes the band, the image and the strip rows;
there is no fallback to band mode. `readModal` and `solidAt` refuse all
three. `core-outside-band` is a band that misses the image, for either source
and before any surround is measured. Distinct from `background-collinear`,
which is an attribution refusal, and from `ReadChromaticRaster`, which is a
mark-density reading and not a pen reading.

Own self-test over synthetic images, registered from
`AppendDeriveBandSelfTest`. The glyph-core arm's exact expected numbers on
day and night grounds are band mode's regression control. The surround arms
read a dark `#003a24` pen over a patterned four-colour gradient: through
`Surround` the modal is the pen; through band mode the same band reports
`bg=#003a24` and a wallpaper modal, the revert control that fails the arm.
The same glyph over a flat fill reads alike from both sources, and a band
whose upper strip lies above the image refuses `core-no-background` while
band mode still measures it.

### `test_ink.h` (`ReadIsolatedGlyph`)

The observed bounds of one complete connected glyph inside a caller-supplied
band, such as a renderer advance cell that may also hold a neighbour's
overhang. `ReadIsolatedGlyph(image, band)` takes nothing else: no expected
height, centre or metric, so none can select the component or crop the
bounds. Metric agreement and alignment are the caller's own assertions on
the bounds it reports.

- Background and pen are the band-mode `ReadGlyphCore` of the same band: its
  background and its modal solid core.
- Coverage: a band pixel `c` is ink when its projection share
  `((c - bg) · (pen - bg)) / |pen - bg|²` is at least
  `kIsolatedGlyphCoverage` (0.5), a fixed declared coverage.
- Topology: the ink splits into 8-connected components, listed in raster
  order of their first pixel. A component touching any band edge is a
  boundary fragment - a neighbour's overhang or a cut glyph - and is never
  part of the result. Exactly one interior component, larger than every
  boundary component, is the glyph, and `bounds` is its half-open raster
  rectangle.

| State | Meaning |
| --- | --- |
| `glyph-band-outside-image` | The band is empty or not wholly inside the image; nothing was read. |
| `glyph-no-ink` | The band-mode core found no ink, or no band pixel reaches the coverage. |
| `glyph-no-pen` | The band-mode core read no pen, for example `no-solid-core`. |
| `glyph-clipped` | No interior component, or a boundary component at least as large as the largest interior one: the band cuts the glyph or holds only fragments. |
| `glyph-ambiguous` | Two or more interior components: several glyphs, a disconnected glyph, or a fragment that does not reach the band edge. |
| `glyph-isolated` | The one interior component; `bounds` is set. |

`readBounds()` refuses every state but `glyph-isolated`, naming the state
and the reason, and a refused reading's `bounds` is empty. On
`glyph-band-outside-image`, `glyph-no-pen` and a `glyph-no-ink` carried from
the core no mask was built: `ok` is false, with no colours, `ink=none` and
no components. Disconnected glyphs (i, j, ;, %, =, accented capitals) are
unsupported: they refuse `glyph-ambiguous` and are never unioned or chosen
between, and contamination inside the band refuses the same way instead of
producing a metric. The target needs at least one pixel of clearance from
every band edge; a target that touches one is a boundary component and
refuses `glyph-clipped`. That is how an advance cell is meant to be used: a
neighbour's overhang enters across the cell's edge and is excluded, while
the target sits inside its side bearings. `FormatIsolatedGlyph` prints
`state=<name> band=<rect> bg=<hex|none> pen=<hex|none> coverage=0.50 ink=<n|none> components=<n|none> interior=<n|none> bounds=<rect|none>`,
then up to eight `component=<rect> pixels=<n> interior=<0|1>` entries,
`more=<n>` for the rest, and `reason=<reason>`.

Advance-cell contamination signature: a capital's measured bounds or centre
are off by a few pixels against the renderer's metric, and the cell's raw
ink union reaches its last column, where the following glyph's overhang
lies. In `2026/10/18/align-the-gift-move-confirmations-and-recipient-section`
the night/200 capital M's cell held the following y's descender: its union
read centre 26 against the complete M's 23 (renderer 23.008), and four
repeated readings failed alike. `FormatIsolatedGlyph` lists such fragments
as `interior=0` components at the band edge beside the one interior glyph.

Own self-test over synthetic night-ground images, registered from
`AppendDeriveBandSelfTest` after the surround arms. A 40x40 capital M at
`16,26` in band `10,0 52x88`, beside a neighbour whose arm and foot reach
the band's last column, reads `glyph-isolated bounds=16,26 40x40` with the
two fragments as boundary components, while the contaminated all-ink union
of the same band reads `16,26 46x52` (the control the fixture defeats); the
clean capital reads exact, and at the declared coverage a 60% blend column
joins it and a 40% one does not. A flat band, a band outside the image, a
cut capital, a cut capital beside an interior speck, two capitals, a
disconnected `i`, an interior fragment and ink with no solid core each
refuse by name with `bounds=none`. Two caller expectations of cap height and
centre give opposite verdicts while the reading's text stays identical.

## Related guidance

[Capture coordinate systems](test_capture.md#coordinate-systems); [failure diagnosis](../diagnostics.md).
