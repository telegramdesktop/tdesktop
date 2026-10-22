# test_ink

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_ink.h](../../test_ink.h).

## Use when

Painted bands, ink, color and contrast measurements. Read the contract below and the source header before use.

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
nearest-candidate attribution. The band's background is its modal colour. Ink
is a pixel at least `kInkDelta` from that background. A solid core is an ink
pixel within `kGlyphCoreTolerance` (8) of the per-channel extreme away from
the background, which is full coverage rather than a fringe or a partial
blend. The reported modal colour is the mode of those cores, with its share
and the core count; a named pen is only a count of solid cores within the same
tolerance and is never copied into the modal. `no-paint` is a band with no
ink. `no-solid-core` is a band that has ink but no pixel within the tolerance
of the extreme, so a fringe colour is not returned as the pen. `readModal` and
`solidAt` refuse both, and `core-outside-band` is a band that misses the
image. Distinct from `background-collinear`, which is an attribution refusal,
and from `ReadChromaticRaster`, which is a mark-density reading and not a pen
reading. Own self-test over synthetic images, registered from
`AppendDeriveBandSelfTest`.

## Related guidance

[Capture coordinate systems](test_capture.md#coordinate-systems); [failure diagnosis](../diagnostics.md).
