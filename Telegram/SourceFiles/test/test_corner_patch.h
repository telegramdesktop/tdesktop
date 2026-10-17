/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QPoint>
#include <QtCore/QSize>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtGui/QColor>
#include <QtGui/QImage>
#include <QtGui/QRegion>

#include <vector>

namespace Test {

class Runner;

// Rounded-corner parity readings over captured corner patches.
//
// Why it exists. The overlay of
// 2026/10/06/round-rich-message-edge-media-like-ordinary-media hand-wrote
// these readings in its test_scenario.cpp, and in its six runs each naive
// version produced false FAILs, none of them a product fault (that task's
// work/test.md):
// - Run 1: 14 FAIL rows came from containment checks that read the
//   background at patch pixel (0, 0) for every patch. For a top-right or
//   bottom patch that pixel is media, so those rows compared media colours.
//   3 more came from fixed-colour pixel sets ("within 2 of the media
//   colour") compared across media of different colours, where the pixels
//   at the arc's ends flip in or out of the set with the contrast. 5 more
//   came from media colours sampled inside a video or GIF corner-status
//   badge.
// - Run 2 repaired them: the background read at the patch's own corner,
//   coverage maps compared on the max-contrast channel with tolerance 0.03
//   within one media kind and 0.06 across kinds, and containment masks
//   built from two references of different media colours.
// - Run 5: all 8 FAILs came from chrome inside the top-right patch. With a
//   titled link preview HistoryView::Photo draws its enlarge badge there;
//   the reference compare read 742 differing pixels of 1296, all inside the
//   badge, and the containment masks took the badge glyph's 6 white pixels
//   for background. Run 6 decided those rows by excluding the badge's
//   exact shape behind a gate on a twin without it.
// That code died with the overlay; this is its generic part, kept, plus
// the corner-connected background mask the glyph case asked for.
//
// How to capture a patch. A square of about Ui::BubbleRadiusLarge() + 2
// logical px, in device pixels (36 x 36 at 100 % on a DPR 2 screen),
// cropped at the media or bubble corner inside its bounding box: the
// patch's corner-most pixel then lies beyond the arc and its opposite
// corner inside the media. Capture both twins over one base, the same
// background behind them: a compare reads both coverage maps against the
// reference's background. Sample the media colour inside the media, away
// from the arc and from any chrome (a corner-status badge at the top left,
// an enlarge badge at the top right): a coverage map assumes a locally
// uniform media colour near the arc. Every reading indexes the raw device
// pixels of the QImage (QImage::pixelColor) and ignores its
// devicePixelRatio; masks and maps are row-major, index y * width + x.
//
// Refusals. A reading that cannot be made carries a non-empty |refusal|
// that starts with its name, then ": " and what was read, and its
// predicate (read(), accepted(), same(), contained()) is false, so a
// refused reading never stands in for a measured one. A count that was not
// measured is -1 (CornerBackground::pixels, which counts a mask that a
// refused reading leaves empty, is 0). The formatters print every field on
// both verdicts, "none" for a value that was not measured, and the refusal
// last. The names, by reading:
// - ReadCornerBackground: empty-patch, media-missing, media-at-corner,
//   background-reaches-inner-corner;
// - ReadCornerCoverage: empty-patch, background-missing, media-missing,
//   contrast-too-low;
// - GateCornerChrome: region-empty, region-outside-patch, gate-background,
//   gate-coverage, chrome-touches-arc, chrome-touches-background;
// - CompareCornerCoverage: corner-mismatch, size-mismatch, reference,
//   chrome-refused, chrome-mismatch, subject, subject-background-differs;
// - ReadCornerContainment: size-mismatch, second-reference-size-mismatch,
//   reference, chrome-refused, chrome-mismatch, empty-mask.
// gate-background, gate-coverage, reference and subject wrap the refusal
// of the reading they needed ("reference: media-at-corner: ...").

// The coverage tolerances Run 2 used: a twin of the same media kind, and a
// twin of another media kind (photo against map tiles, say). The Run 1
// artifacts measured 0.0034-0.047 between colour twins, while another
// radius, a square corner or a one-pixel arc shift moves pixels by 0.3-1.0.
inline constexpr auto kCoverageSameKind = 0.03;
inline constexpr auto kCoverageCrossKind = 0.06;

// The smallest |media - background| on the max-contrast channel a coverage
// map divides by, and the distance within which a corner-most pixel is
// taken for media.
inline constexpr auto kCoverageMinContrast = 24;

enum class PatchCorner {
	TopLeft,
	TopRight,
	BottomLeft,
	BottomRight,
};

// "TL", "TR", "BL" or "BR".
[[nodiscard]] QString PatchCornerName(PatchCorner corner);

// The pixel of a |size| patch beyond its |corner|: TL (0, 0),
// TR (w - 1, 0), BL (0, h - 1), BR (w - 1, h - 1).
[[nodiscard]] QPoint CornerMostPixel(QSize size, PatchCorner corner);

// The caller's patch: a device-pixel image cropped at |corner|, and the
// media colour sampled inside the media away from the arc and any chrome.
struct CornerPatch {
	QImage image;
	PatchCorner corner = PatchCorner::TopLeft;
	QColor media;
};

// The background beyond the patch's corner. |color| is the colour of the
// corner-most pixel |at|; |mask| holds one byte per pixel, set on the
// 4-connected component of pixels exactly equal (ChannelDelta == 0) to
// |color| that contains |at|, and |pixels| counts it. A pixel of the
// background colour that the media encloses - a white glyph or highlight
// inside the media - is not connected to the corner and is never
// background. Refusals:
// - empty-patch: a null or empty image;
// - media-missing: the patch carries no valid media colour, so a media
//   pixel at the corner could not be told from background;
// - media-at-corner: the corner-most pixel is within kCoverageMinContrast
//   of the media colour - a media pixel (a square corner, a patch cropped
//   outside the bounding box), a corner the media covers, or media too
//   close to the background to tell;
// - background-reaches-inner-corner: the component reaches the opposite
//   corner-most pixel, so the patch holds no media corner.
// A refused reading keeps |at| and |color| once they were read (so a
// media colour read as the background is printed), with an empty |mask|
// and |pixels| 0.
struct CornerBackground {
	PatchCorner corner = PatchCorner::TopLeft;
	QSize size;
	QPoint at = QPoint(-1, -1);
	QColor color;
	QColor media;
	std::vector<uchar> mask;
	int pixels = 0;
	QString refusal;

	[[nodiscard]] bool read() const {
		return refusal.isEmpty() && (pixels > 0);
	}

	// False on a refused reading and outside the patch.
	[[nodiscard]] bool background(QPoint point) const;
};

[[nodiscard]] CornerBackground ReadCornerBackground(const CornerPatch &patch);

// "corner=<c> size=<w>x<h> at=(<x>,<y>) background=<#rrggbb>
// media=<#rrggbb> pixels=<n>", then " refusal=<refusal>" on a refused
// reading; a value that was not read is "none".
[[nodiscard]] QString CornerBackgroundText(const CornerBackground &reading);

// The media share of each pixel over |background|: on the channel c where
// |media - background| is largest, (pixel[c] - background[c]) / |contrast|
// with |contrast| = media[c] - background[c], sign kept. Unclamped, as Run
// 2 measured it: a pixel beyond both colours reads below 0 or above 1. An
// arc pixel is a blend of media over background, so its colour moves with
// the media colour while its coverage does not - which is what lets twins
// whose media colours differ compare equal. Refusals: empty-patch,
// background-missing, media-missing, and contrast-too-low when |contrast|
// is below kCoverageMinContrast (|channel| and |contrast| keep the
// measured values there, |values| stays empty).
struct CornerCoverage {
	QSize size;
	QColor background;
	QColor media;
	int channel = -1;
	int contrast = 0;
	std::vector<double> values;
	QString refusal;

	[[nodiscard]] bool read() const {
		return refusal.isEmpty() && !values.empty();
	}
};

[[nodiscard]] CornerCoverage ReadCornerCoverage(
	const QImage &image,
	QColor background,
	QColor media);

// "size=<w>x<h> background=<#rrggbb> media=<#rrggbb> channel=<r|g|b>
// contrast=<n> values=<n>", then " refusal=<refusal>"; a value that was
// not read is "none".
[[nodiscard]] QString CornerCoverageText(const CornerCoverage &coverage);

// A caller-supplied chrome region - a badge shape, grown by a device pixel
// - in patch device pixels, gated on |gate|: a twin or reference patch of
// the same corner that does NOT draw the chrome. Every region pixel is
// counted: |backgroundPixels| those in the gate's background mask,
// |arcPixels| the others whose gate coverage is not full media
// (|coverage - 1| > kCoverageSameKind). Only a region of full-media pixels
// is accepted, so leaving it out can hide neither an arc nor a background
// pixel. Refusals: region-empty; region-outside-patch (any part of the
// region outside the gate patch); gate-background: <the gate's background
// refusal>; gate-coverage: <the gate's coverage refusal>;
// chrome-touches-arc when |arcPixels| > 0, else chrome-touches-background
// when |backgroundPixels| > 0. |pixels| is the region's pixel count;
// |arcPixels| and |backgroundPixels| are -1 until counted.
//
// CompareCornerCoverage and ReadCornerContainment take the gated chrome,
// never a bare region, and refuse instead of applying it silently:
// chrome-refused: <its refusal> for a chrome that was not accepted, and
// chrome-mismatch for one gated on another corner or patch size.
struct CornerChrome {
	PatchCorner corner = PatchCorner::TopLeft;
	QSize size;
	QRegion region;
	int pixels = 0;
	int arcPixels = -1;
	int backgroundPixels = -1;
	QString refusal;

	[[nodiscard]] bool accepted() const {
		return refusal.isEmpty()
			&& (pixels > 0)
			&& (arcPixels == 0)
			&& (backgroundPixels == 0);
	}
};

[[nodiscard]] CornerChrome GateCornerChrome(
	const CornerPatch &gate,
	const QRegion &region);

// "corner=<c> size=<w>x<h> region=<x>,<y> <w>x<h> rects=<n> pixels=<n>
// arc=<n> background=<n>", then " refusal=<refusal>"; the region is its
// bounding rect, a count that was not taken is "none".
[[nodiscard]] QString CornerChromeText(const CornerChrome &chrome);

// Two patches of the same corner compared by coverage. Both maps are read
// against the REFERENCE's background - twins are captured over one base -
// each with its own patch's media colour. The subject's corner-most pixel
// is classified first: "background" when it equals the reference
// background exactly, "media" when it is within kCoverageMinContrast of
// the subject's media colour (a square or covered corner: compared, and it
// differs), otherwise refused. Every pixel outside an accepted chrome
// region is compared: |total| counts them, |excluded| the chrome pixels
// left out, |over| those whose |delta| exceeds |tolerance|; |maxDelta| is
// the largest |delta|, at |worst|, with both map values there. All of them
// are printed on both verdicts, so a passing compare still shows its worst
// pixel and delta. same() is no refusal, at least one pixel compared and
// |over| == 0. Pass kCoverageSameKind within one media kind and
// kCoverageCrossKind across kinds. Refusals: corner-mismatch;
// size-mismatch; reference: <the reference's background refusal>;
// chrome-refused / chrome-mismatch; reference: <the reference's coverage
// refusal>; subject: <the subject's coverage refusal>;
// subject-background-differs (the subject's corner-most pixel is neither
// that background nor media: it was captured over another base).
struct CornerCoverageCompare {
	PatchCorner corner = PatchCorner::TopLeft;
	double tolerance = 0.;
	double maxDelta = -1.;
	QPoint worst = QPoint(-1, -1);
	double subjectAtWorst = 0.;
	double referenceAtWorst = 0.;
	int over = -1;
	int total = -1;
	int excluded = -1;
	QString subjectCorner;
	QString colors;
	QString refusal;

	[[nodiscard]] bool same() const {
		return refusal.isEmpty() && (total > 0) && (over == 0);
	}
};

[[nodiscard]] CornerCoverageCompare CompareCornerCoverage(
	const CornerPatch &subject,
	const CornerPatch &reference,
	double tolerance,
	const CornerChrome *chrome = nullptr);

// "corner=<c> tolerance=<t> maxDelta=<d> at (<x>,<y>) subject=<v>
// reference=<v> over=<n>/<total> excluded=<n> subjectCorner=<kind>
// background=<#rrggbb> subjectMedia=<#rrggbb> (channel <c> contrast <n>)
// referenceMedia=<#rrggbb> (channel <c> contrast <n>)", coverage values
// with four decimals, then " refusal=<refusal>"; a value that was not
// measured is "none".
[[nodiscard]] QString CornerCoverageCompareText(
	const CornerCoverageCompare &compare);

// Every pixel that is background in |reference| stays background in
// |subject|. The mask is the 4-connected component, from the reference
// background's corner-most pixel, of reference pixels exactly equal to
// that background or - when |secondReference|, the same corner with
// another media colour, is given - exactly equal to the second reference's
// pixel: the zero-coverage pixels the two share, such as background and
// shadow, and never an enclosed pixel both share. |widened| counts the
// mask pixels only the second reference added. An accepted chrome region
// is left out (|excluded|), and |mask| counts what remains. |changed|
// counts mask pixels whose subject colour differs from the reference at
// all, |worstDelta| is the largest ChannelDelta among them, at |worst|,
// and |first| lists the first five, "(x,y) #reference->#subject".
// contained() is no refusal, a non-empty mask and |changed| == 0.
// Refusals: size-mismatch; second-reference-size-mismatch; reference: <the
// reference's background refusal>; chrome-refused / chrome-mismatch;
// empty-mask (the chrome left nothing to judge). The counts are -1 until
// measured.
struct CornerContainment {
	PatchCorner corner = PatchCorner::TopLeft;
	QColor background;
	int mask = -1;
	int widened = -1;
	int excluded = -1;
	int changed = -1;
	int worstDelta = -1;
	QPoint worst = QPoint(-1, -1);
	QStringList first;
	QString refusal;

	[[nodiscard]] bool contained() const {
		return refusal.isEmpty() && (mask > 0) && (changed == 0);
	}
};

[[nodiscard]] CornerContainment ReadCornerContainment(
	const CornerPatch &reference,
	const QImage &subject,
	const QImage &secondReference = QImage(),
	const CornerChrome *chrome = nullptr);

// "corner=<c> background=<#rrggbb> mask=<n> widened=<n> excluded=<n>
// changed=<n> worstDelta=<n> at (<x>,<y>) first=[<pixels>]", then
// " refusal=<refusal>"; a value that was not measured is "none".
[[nodiscard]] QString CornerContainmentText(const CornerContainment &reading);

// AppendCornerPatchSelfTest is the oracle measuring itself over synthetic
// patches it paints: no widget, window, session, chats or account. Each
// media rect of a white canvas is rounded through Images::Round with
// Images::CornersMask at Ui::BubbleRadiusLarge() or
// Ui::BubbleRadiusSmall() - the product's own bubble media mask - and
// drawn at DPR 1, so its arcs are the ones real captures contain. Patches
// are (R_L + 2) logical px in device pixels, cropped at the media corner.
// Nine stages, eighteen Check rows, prefix "corner-patch self-test: ":
// 1. The fixture gate: every patch was painted at that side, and the
//    bottom-right shadow reference holds at least one exact shadow-colour
//    pixel (stage 9 changes one). A contact sheet of the patches is saved
//    as corner_patch_self_test. A failed gate turns every later stage into
//    an N/A row by its text.
// 2. A Large arc and a Small arc are told apart by coverage (N/A when the
//    use-small-msg-bubble-radius option makes the two radii equal).
// 3. A square corner and a rounded one are told apart - the subject's
//    corner is classified media and maxDelta is at least 0.9 - and so is
//    an arc shifted by one device pixel.
// 4. Twins with the same arc and different media colours compare equal at
//    kCoverageSameKind, with their worst pixel and delta printed.
// 5. Each of the four corners reads its background at its own corner-most
//    pixel.
// 6. A patch whose corner-most pixel is media is refused by name
//    (media-at-corner), and a media colour too close to the background is
//    refused (contrast-too-low) rather than measured.
// 7. A background-coloured glyph enclosed by the media is not background;
//    containment ignores it when the subject does not repeat it, and still
//    catches a media pixel beyond the arc.
// 8. A chrome region touching the arc is refused by its gate
//    (chrome-touches-arc), a clear one is accepted, and the accepted chrome
//    is left out of a compare that differs without it.
// 9. A second reference of another media colour widens the containment
//    mask by the shared shadow and catches a changed shadow pixel that the
//    single-reference mask does not hold.
// It emits no deliberate FAIL. Its negative legs are disposable mutations
// of the helper, never a stage that fails on purpose: the background read
// at (0, 0) for every corner fails the rows of the other three corners,
// and the corner-connected restriction dropped from the flood fails the
// glyph rows.
void AppendCornerPatchSelfTest(not_null<Runner*> runner);

} // namespace Test
