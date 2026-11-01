/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QRect>
#include <QtCore/QString>
#include <QtGui/QColor>
#include <QtGui/QImage>

#include <vector>

namespace Test {

class Runner;

inline constexpr auto kInkDelta = 45;
inline constexpr auto kSameTolerance = 40;
inline constexpr auto kOnLine = 10.;
inline constexpr auto kInkMargin = 8.;
inline constexpr auto kBackgroundSame = 6;
inline constexpr auto kMinInkPixels = 24;
// Chroma is max(channel) - min(channel). 24 keeps a near-neutral fringe
// (the diagnosing run's #888988 has chroma 1) out and a saturated mark in.
// The density floor sits in the measured gap between a fringe smear (~0.26)
// and a coloured glyph (~0.70). Neither retunes a shipped ink threshold.
inline constexpr auto kChromaticFloor = 24;
inline constexpr auto kChromaticDensityFloor = 0.40;
// Not kInkMargin: retuning the attribution gap must not redefine a core.
inline constexpr auto kGlyphCoreTolerance = 8;
// GlyphBackground::Surround geometry, in image pixels: the gap between the
// band and each strip, each strip's height in rows, and the half-width in
// columns of the median that smooths the per-column strip medians.
inline constexpr auto kGlyphSurroundGap = 2;
inline constexpr auto kGlyphSurroundStrip = 8;
inline constexpr auto kGlyphSurroundSmooth = 6;

struct InkCandidate {
	QString name;
	QColor color;
};

// Classified, NoInk, CandidatesCollide and BackgroundCollinear all mean the
// band WAS scanned, so
// every frame-level number on the reading - total, inkPixels,
// backgroundPixels, widestRun and the measured colours - is a real
// measurement. OutsideImage, NoRowsInBand and NotScanned mean no pixel was
// ever looked at, so those numbers are absent rather than measured zeroes.
// Classified claims only that the ink was offered to the candidates, not
// that any pixel was attributed: a scan whose candidates separate but sit
// farther than kOnLine from every ink pixel answers Classified with every
// count 0 and ambiguous == inkPixels. BackgroundCollinear is the other
// zero: a candidate lies within kInkMargin of the segment from the measured
// modal background to another candidate, so that zero is not a count.
enum class InkScanState {
	NotScanned,
	OutsideImage,
	NoRowsInBand,
	CandidatesCollide,
	NoInk,
	Classified,
	BackgroundCollinear,
};

[[nodiscard]] QString InkScanStateName(InkScanState state);

struct DerivedBand {
	bool ok = false;
	QRect fillRegion;
	QRect band;
	std::vector<int> rows;
	int rowsExamined = 0;
	int fillRows = 0;
	QString reason;
};

// One candidate's count read out of a scan. |count| is -1 and |refusal| is
// non-empty on every refusing path - an index the reading does not have, a
// reading that classified nothing, candidates that do not separate - so a
// refused ask can never be read back as a measured zero. A
// background-collinear reading is one of those refusals. |name| and |color|
// are filled whenever |index| names a real candidate, refusal or not, so
// even a refusal says which candidate was asked about, and InkCountDetails
// prints count=none beside that refusal.
struct InkCount {
	int index = -1;
	QString name;
	QColor color;
	int count = -1;
	QString refusal;

	[[nodiscard]] bool read() const {
		return refusal.isEmpty();
	}
};

struct InkScan {
	bool ok = false;
	QRect band;
	QColor background;
	QColor ink;
	QColor classifiedInk;
	int total = 0;
	int inkPixels = 0;
	int backgroundPixels = 0;
	int ambiguous = 0;
	int scannedRows = 0;
	int widestRun = 0;
	int widestRunRow = -1;
	std::vector<InkCandidate> candidates;
	std::vector<int> counts;
	InkScanState state = InkScanState::NotScanned;
	QString reason;

	// The only way to read one candidate's count out of this reading.
	// |candidates| and |counts| are filled on every path ScanInk returns
	// through, so the two are always the same length; the count is still
	// proved against |counts| itself rather than against that invariant, so
	// an ask this reading cannot answer - an index outside either vector, or
	// a state that classified nothing - comes back as a named refusal about
	// the frame rather than as a read past the end of a vector the scan
	// never filled.
	[[nodiscard]] InkCount countAt(int index) const;
};

struct InkMeasure {
	DerivedBand derived;
	InkScan scan;
	bool separable = false;
	QString collision;
	float64 contrast = 0.;
	QString report;
};

[[nodiscard]] int ChannelDelta(QColor a, QColor b);

[[nodiscard]] bool Separable(const std::vector<InkCandidate> &candidates);

[[nodiscard]] QString CollisionDump(
	const std::vector<InkCandidate> &candidates);

// Keeps rows of |box| whose modal background is the literal |fill|. When
// |box| is strictly inside the image and that fill is also the image's
// background outside |box|, no band can be separated: |reason| names that
// case and |ok| stays false, distinct from "no row of the recovered box
// has the pill fill as its own background".
[[nodiscard]] DerivedBand DeriveBand(
	const QImage &image,
	QRect box,
	QColor fill);

// Scans |band| clipped to the image, measures the frame-level numbers over
// every scanned row, and attributes each ink pixel to the nearest candidate.
// Only two answers are ok = false, because only they looked at no pixel at
// all: OutsideImage, when |band| does not intersect the image, and
// NoRowsInBand, when an explicit |onlyRows| list has no row inside the band.
// CandidatesCollide - the candidates do not separate, so every ink pixel is
// ambiguous and no count is attributed - NoInk, and BackgroundCollinear -
// one candidate lies within kInkMargin of the segment from the measured
// modal background to another, so a pixel of it can never be attributed -
// all keep ok = true, because the band was scanned and their frame-level
// numbers are measured. BackgroundCollinear is not Classified, and countAt
// refuses it. It loses to CandidatesCollide and to NoInk, which keep their
// texts.
// |candidates| and |counts| are filled before either refusal returns, so the
// two are always the same length and a refused reading still names what it
// was asked to classify against. |reason| is never empty on a returned
// reading, and is u"none"_q only on the Classified verdict.
[[nodiscard]] InkScan ScanInk(
	const QImage &image,
	QRect band,
	std::vector<InkCandidate> candidates,
	const std::vector<int> &onlyRows = {});

[[nodiscard]] float64 InkContrast(QColor a, QColor b);

[[nodiscard]] QString FormatInkReport(
	QColor fill,
	QColor ink,
	float64 contrast,
	int inkPixels);

// InkCountDetails prints one count reading the same way on both verdicts -
// index, name, colour and count - with count=none and the refusal in place
// of a number whenever the ask was refused, so no log line can show a
// refused ask as a measurement.
[[nodiscard]] QString InkCountDetails(const InkCount &reading);

// FormatInkScan starts with FormatInkReport's exact text and then says what
// the reading was classified against: the candidate colours, the counts read
// back through countAt, CollisionDump's collision text and the five
// thresholds the classification used. It prints them on the passing verdict
// as well as on a refusing one, so a palette whose references do not
// separate is readable from the log without re-running a whole campaign.
[[nodiscard]] QString FormatInkScan(const InkScan &scan, QColor fill);

// Both of MeasurePaintedInk's branches assign |report|, so it is non-empty
// on every path: the derivation's own reason when the band cannot be
// derived, and FormatInkScan's reading-level text otherwise, which carries
// a refused scan's reason exactly as it carries a classified one's numbers.
// That refused-scan half is unreachable THROUGH MeasurePaintedInk, because
// DeriveBand always hands ScanInk a non-empty band inside the image and a
// rows list every entry of which lies inside that band; the self-test
// therefore measures it as FormatInkScan over a ScanInk refusal.
[[nodiscard]] InkMeasure MeasurePaintedInk(
	const QImage &image,
	QRect box,
	QColor fill,
	std::vector<InkCandidate> candidates);

// One cluster read out of a chromatic raster. |box|, |color| and |density|
// are filled only when |read()| is true. A refusal leaves them empty and
// carries |refusal|, so a caller cannot treat an empty box as a measurement.
struct ChromaticBox {
	QRect box;
	QString refusal;

	[[nodiscard]] bool read() const {
		return refusal.isEmpty();
	}
};

struct ChromaticMean {
	QColor color;
	QString refusal;

	[[nodiscard]] bool read() const {
		return refusal.isEmpty();
	}
};

struct ChromaticDensity {
	float64 density = 0.;
	QString refusal;

	[[nodiscard]] bool read() const {
		return refusal.isEmpty();
	}
};

// Found means a square window of the chromatic bounding box's height cleared
// kChromaticDensityFloor. NoChromatic and BelowDensity were scanned, so
// |totalChromatic| is a real count (zero for NoChromatic) and ok stays true.
// OutsideBand looked at no pixel. NotScanned is the unread value. The mean
// is the average of the pixels in the densest window, never a colour the
// caller passed as |pen| or |fill|. readBox, readMean and readDensity refuse
// on every state except Found.
enum class ChromaticRasterState {
	NotScanned,
	OutsideBand,
	NoChromatic,
	BelowDensity,
	Found,
};

[[nodiscard]] QString ChromaticRasterStateName(ChromaticRasterState state);

struct ChromaticRaster {
	bool ok = false;
	QRect band;
	int totalChromatic = 0;
	QRect box;
	int matched = 0;
	float64 density = 0.;
	QColor mean;
	ChromaticRasterState state = ChromaticRasterState::NotScanned;
	QString reason;

	[[nodiscard]] bool found() const {
		return state == ChromaticRasterState::Found;
	}
	[[nodiscard]] ChromaticBox readBox() const;
	[[nodiscard]] ChromaticMean readMean() const;
	[[nodiscard]] ChromaticDensity readDensity() const;
};

// Keeps pixels in |band| whose channel spread clears kChromaticFloor and
// that match neither |pen| nor |fill| within kSameTolerance, then reports
// the densest square window. An invalid |pen| or |fill| is not an exclusion.
[[nodiscard]] ChromaticRaster ReadChromaticRaster(
	const QImage &image,
	QRect band,
	QColor pen,
	QColor fill);

// Prints the box, matched count, total chromatic count, density and measured
// mean on a found reading, and names the refusal on every other reading.
// A refused box, density or mean is the text "none", not an empty value.
[[nodiscard]] QString FormatChromaticRaster(const ChromaticRaster &reading);

// Where ReadGlyphCore takes a band's background from. BandMode is the
// band's most frequent exact colour, right for a flat fill. Surround is
// measured from strips outside the band, for text on gradients and
// patterns, where the most frequent exact colour is the glyph's own.
enum class GlyphBackground {
	BandMode,
	Surround,
};

// "band-mode" or "surround", as FormatGlyphCore prints it after bgSource=.
[[nodiscard]] QString GlyphBackgroundName(GlyphBackground source);

// Measured means at least one solid core was read, so the modal colour,
// its count, the core count and every pen's solid count are measurements.
// NoPaint and NoSolidCore were scanned, so |ok| stays true and |strongest|
// is a real distance, but they hand back no colour and no count. OutsideBand
// looked at no pixel. NoBackground (core-no-background) looked at no pixel
// either: a GlyphBackground::Surround strip lies outside the image, so no
// background was established; |ok| is false and it hands back no
// background, colour or count. Unread is the value before ReadGlyphCore. A
// named pen is only something to count against: it is never copied into
// |modal|.
enum class GlyphCoreState {
	Unread,
	OutsideBand,
	NoPaint,
	NoSolidCore,
	NoBackground,
	Measured,
};

[[nodiscard]] QString GlyphCoreStateName(GlyphCoreState state);

// |color| is the modal solid-core colour, |count| is how many cores have
// it, and |cores| is the solid-core count. All three are unset, and
// |refusal| is non-empty, unless the reading measured a core.
struct GlyphCoreModal {
	QColor color;
	int count = -1;
	int cores = -1;
	QString refusal;

	[[nodiscard]] bool read() const {
		return refusal.isEmpty();
	}
};

// One named pen's solid-core count. |count| is -1 and |refusal| is
// non-empty on every refusing path, including an index this reading does
// not have, so a refused ask is never a measured zero. The pen colour is
// not a field: a count is not an echo of the query.
struct GlyphCoreSolid {
	int index = -1;
	int count = -1;
	QString refusal;

	[[nodiscard]] bool read() const {
		return refusal.isEmpty();
	}
};

struct GlyphCore {
	bool ok = false;
	QRect band;
	GlyphBackground source = GlyphBackground::BandMode;
	QColor background;
	int backgroundSpread = -1;
	int total = 0;
	int inkPixels = 0;
	int cores = 0;
	int strongest = -1;
	QColor modal;
	int modalCount = -1;
	std::vector<InkCandidate> pens;
	std::vector<int> solid;
	GlyphCoreState state = GlyphCoreState::Unread;
	QString reason;

	// Refuse every state except Measured. |solid| is sized to |pens| on
	// every path ReadGlyphCore returns, with -1 until a measured reading
	// fills it, so the accessor does not read off the end of an empty vector.
	[[nodiscard]] GlyphCoreModal readModal() const;
	[[nodiscard]] GlyphCoreSolid solidAt(int index) const;
};

// |source| picks the background every band pixel is compared with.
// BandMode, the default, is the modal colour of the clip, read exactly as
// before the source existed. Surround reads two strips kGlyphSurroundStrip
// rows tall, kGlyphSurroundGap rows above and below |band|, over the clip's
// columns; it takes per-column per-channel medians, smooths them with a
// median over +-kGlyphSurroundSmooth columns and interpolates linearly
// between the two strips' centre rows, so each band pixel has its own
// background. It is for text on gradients and patterns, and the caller
// chooses a band whose strips are the same surface as its background (no
// other text, icon or edge inside them). Ink is a pixel at least kInkDelta
// from its background. A solid core is an ink pixel within
// kGlyphCoreTolerance of the per-channel extreme: in each channel, the ink
// value farthest from its own pixel's background. The modal colour is the
// mode of those cores. |pens| may be empty; each entry is only counted.
// |source| is recorded on the reading; |background| is the mean of the
// background used and |backgroundSpread| its largest per-channel range (0
// in band mode, -1 when no background was established). A band that misses
// the image answers OutsideBand for either source. A Surround strip that
// leaves the image answers NoBackground: ok false, no colour, no count.
// Reference: docs/helpers/test_ink.md.
[[nodiscard]] GlyphCore ReadGlyphCore(
	const QImage &image,
	QRect band,
	std::vector<InkCandidate> pens = {},
	GlyphBackground source = GlyphBackground::BandMode);

// Prints the band, measured background and its source (bgSource=, followed
// by bgSpread= for a surround reading), core tolerance, strongest distance,
// core count, modal colour and share, and each pen's solid count on a
// measured reading. A refused colour or count is the text "none".
[[nodiscard]] QString FormatGlyphCore(const GlyphCore &reading);

// The fixed declared coverage of ReadIsolatedGlyph: a band pixel is glyph
// ink when its projection share onto pen - background is at least this.
inline constexpr auto kIsolatedGlyphCoverage = 0.5;

// Isolated is the only state with bounds. BandOutsideImage, NoPen and a
// NoInk carried from the glyph core built no coverage mask, so |ok| is
// false, |background| and |pen| are invalid, |inkPixels| is -1 and
// |components| is empty. Clipped, Ambiguous and a NoInk coverage mask were
// measured: |ok| is true and every count and component is real. Unread is
// the value before ReadIsolatedGlyph.
enum class IsolatedGlyphState {
	Unread,
	BandOutsideImage,
	NoInk,
	NoPen,
	Clipped,
	Ambiguous,
	Isolated,
};

// "glyph-unread", "glyph-band-outside-image", "glyph-no-ink",
// "glyph-no-pen", "glyph-clipped", "glyph-ambiguous" or "glyph-isolated".
[[nodiscard]] QString IsolatedGlyphStateName(IsolatedGlyphState state);

// |bounds| is filled only when |read()| is true; a refusal leaves it empty
// and names the reading's state and reason.
struct IsolatedGlyphBox {
	QRect bounds;
	QString refusal;

	[[nodiscard]] bool read() const {
		return refusal.isEmpty();
	}
};

// One 8-connected component of the coverage mask, in image coordinates.
// |interior| is true when its bounds touch no edge of the band.
struct IsolatedGlyphComponent {
	QRect bounds;
	int pixels = 0;
	bool interior = false;
};

struct IsolatedGlyph {
	bool ok = false;
	QRect band;
	QColor background;
	QColor pen;
	int inkPixels = -1;
	int interior = 0;
	QRect bounds;
	std::vector<IsolatedGlyphComponent> components;
	IsolatedGlyphState state = IsolatedGlyphState::Unread;
	QString reason;

	// Refuses every state except Isolated.
	[[nodiscard]] IsolatedGlyphBox readBounds() const;
};

// The observed bounds of one complete connected glyph inside |band|, such
// as a renderer advance cell that may also hold a neighbour's overhang.
// |band| must be non-empty and lie wholly inside the image, else
// BandOutsideImage. The background and pen are those of the band-mode
// ReadGlyphCore of the same band (its modal solid core): a core with no ink
// answers NoInk, one with no solid core NoPen. A band pixel c is ink when
// its projection share ((c - bg) . (pen - bg)) / |pen - bg|^2 is at least
// kIsolatedGlyphCoverage. Components are 8-connected and listed in raster
// order of their first pixel. A component touching any band edge is a
// boundary fragment (a neighbour's overhang or a cut glyph) and is never
// part of the result. Clipped: no interior component is larger than every
// boundary component (none is interior, or a boundary component is at
// least as large as the largest interior one). Otherwise Ambiguous: two or
// more interior components, which is also how a disconnected glyph (i, j,
// ;, %, =, an accented capital) is handled: unsupported and refused, never
// unioned or chosen between. Otherwise Isolated: the one interior
// component, whose half-open raster rectangle is |bounds|. The target
// therefore needs one pixel of clearance from every band edge; touching it,
// the target is a boundary component and refuses Clipped. The reader takes
// no expected height, centre or metric: metric agreement and alignment are
// the caller's assertions. Reference: docs/helpers/test_ink.md.
[[nodiscard]] IsolatedGlyph ReadIsolatedGlyph(const QImage &image, QRect band);

// Prints the state, band, bg, pen, coverage, ink, components, interior and
// bounds, then up to eight component=<rect> pixels=<n> interior=<0|1>
// entries (more=<n> counts the rest) and the reason. A refused colour,
// count or bounds is the text "none".
[[nodiscard]] QString FormatIsolatedGlyph(const IsolatedGlyph &reading);

// AppendDeriveBandSelfTest is the underivable-band refusal measuring
// itself. Three synthetic images, no widget, no window, no session, chats,
// network, account or wallet: the same fill inside and outside the
// candidate (new reason, and MeasurePaintedInk.report carries it), a
// contrasting surround that still derives, and an inside that does not
// match the fill (existing no-rows reason). It emits no deliberate
// failure.
//
// Its later stages are the ink scan measuring itself over the same kind of
// synthetic images: a band that classifies two candidates with its counts
// read back through InkScan::countAt, an out-of-range ask that refuses and
// still lets the stage write the verdict lines after it, the two ok = false
// refusals read back beside all four DeriveBand reasons, a colliding
// candidate pair whose reading names the collision, a scanned band with no
// ink, the not-scanned reading a refused derivation leaves behind, and
// FormatInkScan's candidate colours and thresholds on a passing verdict.
// The background-collinear stage and the chromatic-raster stage register
// here too: a triple the measured background cannot separate, beside one
// that still classifies, and a dense mark beside an equal-count smear and
// a flat fill. The glyph-core stage registers here too.
//
// The glyph-core surround stages register after it: a dark pen over a
// patterned four-colour gradient read with GlyphBackground::Surround and,
// as the revert control, with the band-mode rule; the same glyph over a
// flat fill read alike by both sources; and the core-no-background refusal
// beside the band-mode control and the outside-band precedence.
//
// The isolated-glyph stages register last: a capital beside a neighbour's
// boundary overhang, a clean capital and a coverage fixture, each read
// against the rectangles the stage drew, with the contaminated all-ink union
// as the control the overhang fixture defeats; every named refusal with no
// usable bounds; and two caller expectations whose opposite verdicts leave
// the reading unchanged.
void AppendDeriveBandSelfTest(not_null<Runner*> runner);

} // namespace Test
