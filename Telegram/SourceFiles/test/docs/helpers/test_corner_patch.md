# test_corner_patch

[Harness guide](../../README.md) · [Helper index](README.md)

Type: Harness API (may include self-tests).

Source contract: [test_corner_patch.h](../../test_corner_patch.h).

## Use when

Rounded-corner comparisons and controls. Read the contract below and the source header before use.

## Contract and usage

Rounded-corner parity readings over device-pixel corner patches, each a
`CornerPatch` (the image, its `PatchCorner`, and a media colour sampled away
from the arc and chrome). `ReadCornerBackground` reads the background at the
patch's own corner-most pixel (`CornerMostPixel`), restricted to the
4-connected component of that exact colour, and refuses a corner-most pixel
within `kCoverageMinContrast` (24) of the media (`media-at-corner`) or a
component that reaches the opposite corner
(`background-reaches-inner-corner`); `ReadCornerCoverage` maps each pixel's
media share on the max-contrast channel, unclamped, and refuses
`contrast-too-low`; `CompareCornerCoverage` compares a subject with a
reference of the same corner, both against the reference's background, at a
caller tolerance (`kCoverageSameKind` 0.03, `kCoverageCrossKind` 0.06),
classifies the subject's corner-most pixel as `background` or `media`, and
prints `maxDelta` with its pixel and both map values, `over/total` and
`excluded` on both verdicts; `ReadCornerContainment` counts the
reference-background pixels that changed in the subject, its mask widened by
an optional second reference's shared zero-coverage pixels connected to the
corner; `GateCornerChrome` accepts a chrome region only when every pixel of it
is full media on a gate patch without the chrome (`chrome-touches-arc`,
`chrome-touches-background` otherwise), and the compare and containment refuse
a chrome that was not accepted (`chrome-refused`) or was gated on another
corner or size (`chrome-mismatch`). Every reading has a named refusal set and
a formatter (`CornerBackgroundText`, `CornerCoverageText`,
`CornerCoverageCompareText`, `CornerContainmentText`, `CornerChromeText`) that
prints `none` for a value it did not measure. Own self-test over synthetic
patches painted with `Images::Round` and `Images::CornersMask` at
`Ui::BubbleRadiusLarge()` and `Ui::BubbleRadiusSmall()`, needing no window,
session or account: a Large and a Small arc (`N/A` when the
`use-small-msg-bubble-radius` option makes the radii equal), a square and a
rounded corner, and an arc shifted by one device pixel told apart; colour
twins equal by coverage with their worst pixel printed; all four corners read
at their own corner-most pixel; `media-at-corner` and `contrast-too-low`
refusals; an enclosed background-coloured glyph kept out of the background and
ignored by containment, which still catches a media pixel beyond the arc; a
chrome region touching the arc refused by its gate, a clear one accepted and
left out of a compare that differs without it; and a second reference widening
the mask by a shared shadow to catch a changed shadow pixel. Nine stages,
eighteen checks, a `corner_patch_self_test` contact sheet.

## Comparing corner patches

Compare a rounded corner of a media or bubble with a twin or a reference
through `test_corner_patch.h`, not through pixels read by hand. Each reading
takes a `CornerPatch`: a device-pixel `QImage` cropped at a named
`PatchCorner` inside the media's bounding box, about
`Ui::BubbleRadiusLarge() + 2` logical px square so that its corner-most
pixel lies beyond the arc and its opposite corner inside the media, and a
media colour sampled inside the media away from the arc and from any chrome;
a coverage map assumes that colour is locally uniform near the arc.
`ReadCornerBackground` reads the background at the patch's own corner-most
pixel (`CornerMostPixel`) and keeps only the 4-connected component of pixels
exactly that colour, so a background-coloured glyph the media encloses is
never background. `ReadCornerCoverage` maps each pixel's media share on the
channel where media and background contrast most. `CompareCornerCoverage`
compares two patches of one corner by those maps, both read against the
reference's background, so capture both twins over one base: twins whose
media colours differ but whose arcs match compare equal, while another
radius, a square corner or a one-pixel arc shift does not, and the worst
pixel, its delta and the count over the tolerance are printed on both
verdicts. Pass `kCoverageSameKind` (0.03) within one media kind and
`kCoverageCrossKind` (0.06) across kinds. `ReadCornerContainment` checks
that every background pixel of the reference stays background in the
subject; a second reference with another media colour widens the mask by the
zero-coverage pixels both share that stay connected to the corner, such as a
shadow. Chrome — a corner-status badge, an enlarge badge — is left out only
through `GateCornerChrome`, gated on a twin or reference that does not draw
it: the gate refuses a region touching an arc or background pixel there, and
the compare and the containment refuse a chrome the gate did not accept
instead of applying it. A reading that cannot be made is refused by name,
its predicate is false and every value it did not measure prints `none`:
`ReadCornerBackground` refuses `empty-patch`, `media-missing`,
`media-at-corner` and `background-reaches-inner-corner`;
`ReadCornerCoverage` `empty-patch`, `background-missing`, `media-missing`
and `contrast-too-low`; `GateCornerChrome` `region-empty`,
`region-outside-patch`, `gate-background`, `gate-coverage`,
`chrome-touches-arc` and `chrome-touches-background`;
`CompareCornerCoverage` `corner-mismatch`, `size-mismatch`, `reference`,
`chrome-refused`, `chrome-mismatch`, `subject` and
`subject-background-differs`; `ReadCornerContainment` `size-mismatch`,
`second-reference-size-mismatch`, `reference`, `chrome-refused`,
`chrome-mismatch` and `empty-mask`. The hand-written readings it replaces
produced false FAILs in Runs 1 and 5 of
`2026/10/06/round-rich-message-edge-media-like-ordinary-media` (the three
corner-patch rows in [failure diagnosis](../diagnostics.md)). Confirm with
`AppendCornerPatchSelfTest`, over synthetic patches rounded with the
product's own `Images::CornersMask` at both bubble radii.
