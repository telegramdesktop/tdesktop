/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_ink.h"

#include "test/test_capture.h"
#include "test/test_log.h"
#include "test/test_runner.h"
#include "ui/color_contrast.h"

#include <QtGui/QPainter>

#include <algorithm>
#include <cmath>

namespace Test {
namespace {

[[nodiscard]] QString ColorHex(QColor color) {
	if (!color.isValid()) {
		return u"invalid"_q;
	}
	return u"#%1%2%3"_q
		.arg(color.red(), 2, 16, QChar('0'))
		.arg(color.green(), 2, 16, QChar('0'))
		.arg(color.blue(), 2, 16, QChar('0'));
}

[[nodiscard]] QString CandidateName(const InkCandidate &candidate) {
	return candidate.name.isEmpty()
		? ColorHex(candidate.color)
		: candidate.name;
}

[[nodiscard]] QString ScanReasonText(const InkScan &scan) {
	return scan.reason.isEmpty()
		? u"this reading was never scanned"_q
		: scan.reason;
}

[[nodiscard]] double SegmentDistance(QColor p, QColor a, QColor b) {
	const auto px = double(p.red());
	const auto py = double(p.green());
	const auto pz = double(p.blue());
	const auto ax = double(a.red());
	const auto ay = double(a.green());
	const auto az = double(a.blue());
	const auto dx = double(b.red()) - ax;
	const auto dy = double(b.green()) - ay;
	const auto dz = double(b.blue()) - az;
	const auto len = dx * dx + dy * dy + dz * dz;
	auto t = 0.;
	if (len > 0.) {
		t = ((px - ax) * dx + (py - ay) * dy + (pz - az) * dz) / len;
		t = std::clamp(t, 0., 1.);
	}
	return std::max({
		std::abs(px - (ax + t * dx)),
		std::abs(py - (ay + t * dy)),
		std::abs(pz - (az + t * dz)) });
}

[[nodiscard]] QColor RowMode(const QImage &image, QRect clip, int y) {
	auto counts = std::vector<std::pair<QRgb, int>>();
	for (auto x = clip.left(); x <= clip.right(); ++x) {
		const auto value = image.pixel(x, y);
		auto found = false;
		for (auto &one : counts) {
			if (one.first == value) {
				++one.second;
				found = true;
				break;
			}
		}
		if (!found && (counts.size() < 8192)) {
			counts.push_back({ value, 1 });
		}
	}
	auto best = QRgb(0);
	auto bestCount = -1;
	for (const auto &one : counts) {
		if (one.second > bestCount) {
			bestCount = one.second;
			best = one.first;
		}
	}
	return (bestCount > 0) ? QColor::fromRgb(best) : QColor();
}

[[nodiscard]] QRect PillBand(QRect box) {
	if (box.isEmpty()) {
		return box;
	}
	const auto radius = box.height() / 2;
	const auto inner = box.adjusted(radius, 1, -radius, -1);
	return (inner.width() >= 8) ? inner : box;
}

[[nodiscard]] QColor RegionMode(
		const QImage &image,
		QRect region,
		QRect exclude) {
	auto counts = std::vector<std::pair<QRgb, int>>();
	const auto clip = region.intersected(image.rect());
	for (auto y = clip.top(); y <= clip.bottom(); ++y) {
		for (auto x = clip.left(); x <= clip.right(); ++x) {
			if (exclude.contains(x, y)) {
				continue;
			}
			const auto value = image.pixel(x, y);
			auto found = false;
			for (auto &one : counts) {
				if (one.first == value) {
					++one.second;
					found = true;
					break;
				}
			}
			if (!found && (counts.size() < 8192)) {
				counts.push_back({ value, 1 });
			}
		}
	}
	auto best = QRgb(0);
	auto bestCount = -1;
	for (const auto &one : counts) {
		if (one.second > bestCount) {
			bestCount = one.second;
			best = one.first;
		}
	}
	return (bestCount > 0) ? QColor::fromRgb(best) : QColor();
}

[[nodiscard]] bool SurroundingsAreFill(
		const QImage &image,
		QRect clip,
		QColor fill) {
	const auto ring = clip.adjusted(-1, -1, 1, 1).intersected(image.rect());
	if (ring.isEmpty() || (ring == clip)) {
		return false;
	}
	const auto mode = RegionMode(image, ring, clip);
	return mode.isValid()
		&& (ChannelDelta(mode, fill) <= kBackgroundSame);
}

// Own-colour distance to its own segment is 0, so a candidate closer than
// kInkMargin to another candidate's segment can never be the unique
// attribution. kOnLine is wider and would also refuse pixels that still
// attribute. The background is the scan's modal row colour: a row that is
// mostly ink reports that ink as its own mode and would refuse a palette
// the band still classifies.
struct CollinearPair {
	int swallowed = -1;
	int swallower = -1;
	double distance = 0.;
};

[[nodiscard]] CollinearPair CollinearAgainstBackground(
		const std::vector<InkCandidate> &candidates,
		QColor background) {
	auto result = CollinearPair();
	if (!background.isValid()) {
		return result;
	}
	for (auto i = 0; i != int(candidates.size()); ++i) {
		for (auto j = 0; j != int(candidates.size()); ++j) {
			if (i == j) {
				continue;
			}
			const auto distance = SegmentDistance(
				candidates[i].color,
				background,
				candidates[j].color);
			if (distance >= kInkMargin) {
				continue;
			}
			if ((result.swallowed >= 0) && (distance >= result.distance)) {
				continue;
			}
			result.swallowed = i;
			result.swallower = j;
			result.distance = distance;
		}
	}
	return result;
}

} // namespace

int ChannelDelta(QColor a, QColor b) {
	if (!a.isValid() || !b.isValid()) {
		return -1;
	}
	return std::max({
		std::abs(a.red() - b.red()),
		std::abs(a.green() - b.green()),
		std::abs(a.blue() - b.blue()) });
}

QString InkScanStateName(InkScanState state) {
	switch (state) {
	case InkScanState::NotScanned:
		return u"not-scanned"_q;
	case InkScanState::OutsideImage:
		return u"outside-image"_q;
	case InkScanState::NoRowsInBand:
		return u"no-rows-in-band"_q;
	case InkScanState::CandidatesCollide:
		return u"candidates-collide"_q;
	case InkScanState::NoInk:
		return u"no-ink"_q;
	case InkScanState::Classified:
		return u"classified"_q;
	case InkScanState::BackgroundCollinear:
		return u"background-collinear"_q;
	}
	return u"missing"_q;
}

bool Separable(const std::vector<InkCandidate> &candidates) {
	for (auto i = 0; i != int(candidates.size()); ++i) {
		for (auto j = i + 1; j != int(candidates.size()); ++j) {
			if (ChannelDelta(candidates[i].color, candidates[j].color)
				<= kSameTolerance) {
				return false;
			}
		}
	}
	return true;
}

QString CollisionDump(const std::vector<InkCandidate> &candidates) {
	for (auto i = 0; i != int(candidates.size()); ++i) {
		for (auto j = i + 1; j != int(candidates.size()); ++j) {
			const auto delta = ChannelDelta(
				candidates[i].color,
				candidates[j].color);
			if (delta <= kSameTolerance) {
				return u"%1(%2) %3(%4) delta=%5"_q
					.arg(
						CandidateName(candidates[i]),
						ColorHex(candidates[i].color))
					.arg(
						CandidateName(candidates[j]),
						ColorHex(candidates[j].color))
					.arg(delta);
			}
		}
	}
	return u"none"_q;
}

InkCount InkScan::countAt(int index) const {
	auto result = InkCount();
	result.index = index;
	const auto size = int(candidates.size());
	const auto named = (index >= 0) && (index < size);
	const auto inRange = named && (index < int(counts.size()));
	if (named) {
		result.name = CandidateName(candidates[index]);
		result.color = candidates[index].color;
	}
	if ((state != InkScanState::Classified)
		&& (state != InkScanState::NoInk)) {
		result.refusal = u"index=%1 of %2: this reading (%3) classified "
			u"nothing: %4"_q
			.arg(index)
			.arg(size)
			.arg(InkScanStateName(state), ScanReasonText(*this));
		return result;
	}
	if (!inRange) {
		auto names = QString();
		for (const auto &candidate : candidates) {
			if (!names.isEmpty()) {
				names += u", "_q;
			}
			names += CandidateName(candidate);
		}
		result.refusal = named
			? u"index=%1 names a candidate this reading filled no count "
				u"for: %2 counts for %3 candidates [%4]"_q
				.arg(index)
				.arg(int(counts.size()))
				.arg(size)
				.arg(names)
			: u"index=%1 is outside this reading's %2 candidates [%3]"_q
				.arg(index)
				.arg(size)
				.arg(names);
		return result;
	}
	result.count = counts[index];
	return result;
}

QString InkCountDetails(const InkCount &reading) {
	const auto line = u"index=%1 name=%2 color=%3 count=%4"_q
		.arg(reading.index)
		.arg(
			reading.name.isEmpty() ? u"none"_q : reading.name,
			ColorHex(reading.color),
			reading.read() ? QString::number(reading.count) : u"none"_q);
	return reading.read()
		? line
		: (line + u" refusal=%1"_q.arg(reading.refusal));
}

DerivedBand DeriveBand(
		const QImage &image,
		QRect box,
		QColor fill) {
	// Keep rows whose modal background is the literal fill, take the columns
	// those rows actually span, inset by the pill radius so rounded caps and
	// the surface behind them are excluded, then re-assert the row background
	// inside that narrower band and drop every row that no longer reads as
	// the fill. A hit-box PillBand without this step inverted ink and fill.
	auto result = DerivedBand();
	const auto clip = box.intersected(image.rect());
	if (clip.isEmpty() || !fill.isValid()) {
		result.reason = u"the recovered box is empty or the fill is invalid"_q;
		return result;
	}
	result.rowsExamined = clip.height();
	auto rows = std::vector<int>();
	auto left = clip.right() + 1;
	auto right = clip.left() - 1;
	for (auto y = clip.top(); y <= clip.bottom(); ++y) {
		if (ChannelDelta(RowMode(image, clip, y), fill) > kBackgroundSame) {
			continue;
		}
		auto first = -1;
		auto last = -1;
		for (auto x = clip.left(); x <= clip.right(); ++x) {
			const auto delta = ChannelDelta(image.pixelColor(x, y), fill);
			if (delta <= kBackgroundSame) {
				if (first < 0) {
					first = x;
				}
				last = x;
			}
		}
		if (first < 0) {
			continue;
		}
		rows.push_back(y);
		left = std::min(left, first);
		right = std::max(right, last);
	}
	result.fillRows = int(rows.size());
	if (rows.empty() || (right < left)) {
		result.reason = u"no row of the recovered box has the pill fill "
			u"as its own background"_q;
		return result;
	}
	if (SurroundingsAreFill(image, clip, fill)) {
		result.reason = u"the requested fill is the image's background "
			u"outside the candidate, so no band can be derived"_q;
		return result;
	}
	result.fillRegion = QRect(
		left,
		rows.front(),
		right - left + 1,
		rows.back() - rows.front() + 1);
	result.band = PillBand(result.fillRegion);
	for (const auto y : rows) {
		if ((y < result.band.top()) || (y > result.band.bottom())) {
			continue;
		}
		if (ChannelDelta(RowMode(image, result.band, y), fill)
			<= kBackgroundSame) {
			result.rows.push_back(y);
		}
	}
	if (result.rows.empty()) {
		result.reason = u"no row of the derived band kept the pill fill "
			u"as its own background"_q;
		return result;
	}
	result.reason = u"none"_q;
	result.ok = true;
	return result;
}

InkScan ScanInk(
		const QImage &image,
		QRect band,
		std::vector<InkCandidate> candidates,
		const std::vector<int> &onlyRows) {
	auto result = InkScan();
	result.candidates = std::move(candidates);
	result.counts.assign(result.candidates.size(), 0);
	const auto clip = band.intersected(image.rect());
	if (clip.isEmpty()) {
		result.state = InkScanState::OutsideImage;
		result.reason = u"the requested band [%1] does not intersect the "
			u"image [%2], so no pixel was scanned"_q
			.arg(RectText(band), RectText(image.rect()));
		return result;
	}
	result.band = clip;
	auto rows = std::vector<int>();
	if (onlyRows.empty()) {
		rows.reserve(clip.height());
		for (auto y = clip.top(); y <= clip.bottom(); ++y) {
			rows.push_back(y);
		}
	} else {
		rows.reserve(onlyRows.size());
		for (const auto y : onlyRows) {
			if ((y >= clip.top()) && (y <= clip.bottom())) {
				rows.push_back(y);
			}
		}
	}
	if (rows.empty()) {
		result.state = InkScanState::NoRowsInBand;
		result.reason = u"none of the %1 requested rows falls inside the "
			u"band [%2], so no pixel was scanned"_q
			.arg(int(onlyRows.size()))
			.arg(RectText(clip));
		return result;
	}
	result.ok = true;
	result.scannedRows = int(rows.size());

	auto rowBackground = std::vector<QColor>();
	rowBackground.reserve(rows.size());
	for (const auto y : rows) {
		rowBackground.push_back(RowMode(image, clip, y));
	}
	auto backgroundCounts = std::vector<std::pair<QRgb, int>>();
	for (const auto &one : rowBackground) {
		auto found = false;
		for (auto &entry : backgroundCounts) {
			if (entry.first == one.rgb()) {
				++entry.second;
				found = true;
				break;
			}
		}
		if (!found) {
			backgroundCounts.push_back({ one.rgb(), 1 });
		}
	}
	auto bestBackground = -1;
	for (const auto &entry : backgroundCounts) {
		if (entry.second > bestBackground) {
			bestBackground = entry.second;
			result.background = QColor::fromRgb(entry.first);
		}
	}

	const auto classify = Separable(result.candidates);
	auto inkSum = std::vector<std::pair<QRgb, int>>();
	auto classifiedSum = std::vector<std::pair<QRgb, int>>();
	for (auto index = 0; index != int(rows.size()); ++index) {
		const auto y = rows[index];
		const auto background = rowBackground[index];
		auto run = 0;
		for (auto x = clip.left(); x <= clip.right(); ++x) {
			const auto color = image.pixelColor(x, y);
			const auto delta = ChannelDelta(color, background);
			++result.total;
			if (delta <= kBackgroundSame) {
				++result.backgroundPixels;
			}
			run = (delta >= kInkDelta) ? (run + 1) : 0;
			if (run > result.widestRun) {
				result.widestRun = run;
				result.widestRunRow = y - clip.top();
			}
			if (delta < kInkDelta) {
				continue;
			}
			++result.inkPixels;
			auto found = false;
			for (auto &one : inkSum) {
				if (one.first == color.rgb()) {
					++one.second;
					found = true;
					break;
				}
			}
			if (!found && (inkSum.size() < 8192)) {
				inkSum.push_back({ color.rgb(), 1 });
			}
			if (!classify) {
				++result.ambiguous;
				continue;
			}
			auto best = -1;
			auto bestDistance = 1e9;
			auto secondDistance = 1e9;
			for (auto i = 0; i != int(result.candidates.size()); ++i) {
				const auto distance = SegmentDistance(
					color,
					background,
					result.candidates[i].color);
				if (distance < bestDistance) {
					secondDistance = bestDistance;
					bestDistance = distance;
					best = i;
				} else if (distance < secondDistance) {
					secondDistance = distance;
				}
			}
			const auto lone = (result.candidates.size() < 2);
			if ((best >= 0)
				&& (bestDistance <= kOnLine)
				&& (lone || (secondDistance - bestDistance >= kInkMargin))) {
				++result.counts[best];
				auto seen = false;
				for (auto &one : classifiedSum) {
					if (one.first == color.rgb()) {
						++one.second;
						seen = true;
						break;
					}
				}
				if (!seen && (classifiedSum.size() < 8192)) {
					classifiedSum.push_back({ color.rgb(), 1 });
				}
			} else {
				++result.ambiguous;
			}
		}
	}
	auto bestInk = -1;
	for (const auto &one : inkSum) {
		if (one.second > bestInk) {
			bestInk = one.second;
			result.ink = QColor::fromRgb(one.first);
		}
	}
	auto bestClassified = -1;
	for (const auto &one : classifiedSum) {
		if (one.second > bestClassified) {
			bestClassified = one.second;
			result.classifiedInk = QColor::fromRgb(one.first);
		}
	}
	const auto collinear = CollinearAgainstBackground(
		result.candidates,
		result.background);
	if (!classify) {
		result.state = InkScanState::CandidatesCollide;
		result.reason = u"the candidates do not separate from each other, "
			u"so no pixel is attributed to one: %1 kSameTolerance=%2"_q
			.arg(
				CollisionDump(result.candidates),
				QString::number(kSameTolerance));
	} else if (!result.inkPixels) {
		result.state = InkScanState::NoInk;
		result.reason = u"no pixel of the band [%1] separated from its "
			u"background %2: total=%3 kInkDelta=%4"_q
			.arg(
				RectText(clip),
				ColorHex(result.background),
				QString::number(result.total),
				QString::number(kInkDelta));
	} else if (collinear.swallowed >= 0) {
		const auto &swallowed = result.candidates[collinear.swallowed];
		const auto &swallower = result.candidates[collinear.swallower];
		result.state = InkScanState::BackgroundCollinear;
		result.reason = u"candidate "_q
			+ CandidateName(swallowed)
			+ u"("_q
			+ ColorHex(swallowed.color)
			+ u") lies on the segment from the measured background "_q
			+ ColorHex(result.background)
			+ u" to candidate "_q
			+ CandidateName(swallower)
			+ u"("_q
			+ ColorHex(swallower.color)
			+ u"), so none of its pixels can be attributed: distance="_q
			+ QString::number(collinear.distance)
			+ u" kInkMargin="_q
			+ QString::number(kInkMargin);
	} else {
		result.state = InkScanState::Classified;
		result.reason = u"none"_q;
	}
	return result;
}

float64 InkContrast(QColor a, QColor b) {
	if (!a.isValid() || !b.isValid()) {
		return 0.;
	}
	return Ui::CountContrast(a, b);
}

QString FormatInkReport(
		QColor fill,
		QColor ink,
		float64 contrast,
		int inkPixels) {
	return u"fill=%1 ink=%2 contrast=%3 inkPx=%4"_q
		.arg(ColorHex(fill), ColorHex(ink))
		.arg(contrast)
		.arg(inkPixels);
}

QString FormatInkScan(const InkScan &scan, QColor fill) {
	// Every clause is resolved into its own fragment and the fragments are
	// concatenated, never chained as one long .arg over a single format
	// string: a chained .arg rescans text an earlier .arg already
	// substituted, so a candidate name or a reason carrying its own
	// %<digit> would be eaten by the next call in the chain.
	const auto report = FormatInkReport(
		fill,
		scan.ink,
		InkContrast(scan.ink, scan.background),
		scan.inkPixels);
	const auto measured = u" state=%1 band=%2 bg=%3 classifiedInk=%4"_q
		.arg(
			InkScanStateName(scan.state),
			RectText(scan.band),
			ColorHex(scan.background),
			ColorHex(scan.classifiedInk));
	const auto counted = u" total=%1 ambiguous=%2 rows=%3"_q
		.arg(scan.total)
		.arg(scan.ambiguous)
		.arg(scan.scannedRows);
	auto listed = QString();
	for (auto i = 0; i != int(scan.candidates.size()); ++i) {
		const auto reading = scan.countAt(i);
		if (!listed.isEmpty()) {
			listed += u" "_q;
		}
		listed += u"%1(%2)=%3"_q
			.arg(
				reading.name,
				ColorHex(reading.color),
				reading.read()
					? QString::number(reading.count)
					: u"none"_q);
	}
	const auto against = u" candidates=[%1] collision=%2"_q
		.arg(listed, CollisionDump(scan.candidates));
	const auto thresholds = u" kInkDelta=%1 kOnLine=%2 kInkMargin=%3 "
		u"kSameTolerance=%4 kBackgroundSame=%5"_q
		.arg(kInkDelta)
		.arg(kOnLine)
		.arg(kInkMargin)
		.arg(kSameTolerance)
		.arg(kBackgroundSame);
	return report
		+ measured
		+ counted
		+ against
		+ thresholds
		+ u" reason=%1"_q.arg(ScanReasonText(scan));
}

InkMeasure MeasurePaintedInk(
		const QImage &image,
		QRect box,
		QColor fill,
		std::vector<InkCandidate> candidates) {
	auto result = InkMeasure();
	result.derived = DeriveBand(image, box, fill);
	result.separable = Separable(candidates);
	result.collision = CollisionDump(candidates);
	if (result.derived.ok) {
		result.scan = ScanInk(
			image,
			result.derived.band,
			std::move(candidates),
			result.derived.rows);
		result.contrast = InkContrast(
			result.scan.ink,
			result.scan.background);
		result.report = FormatInkScan(result.scan, fill);
	} else {
		result.report = result.derived.reason;
	}
	return result;
}

namespace {

[[nodiscard]] int ChromaticSpread(QColor color) {
	if (!color.isValid()) {
		return 0;
	}
	const auto high = std::max({ color.red(), color.green(), color.blue() });
	const auto low = std::min({ color.red(), color.green(), color.blue() });
	return high - low;
}

[[nodiscard]] bool ExcludedColor(QColor color, QColor excluded) {
	return excluded.isValid()
		&& (ChannelDelta(color, excluded) <= kSameTolerance);
}

[[nodiscard]] QString ClusterRefusal(const ChromaticRaster &reading) {
	if (reading.state == ChromaticRasterState::Found) {
		return {};
	}
	const auto reason = reading.reason.isEmpty()
		? u"this reading was never scanned"_q
		: reading.reason;
	return u"this reading (%1) found no chromatic cluster: %2"_q
		.arg(ChromaticRasterStateName(reading.state), reason);
}

} // namespace

QString ChromaticRasterStateName(ChromaticRasterState state) {
	switch (state) {
	case ChromaticRasterState::NotScanned:
		return u"not-scanned"_q;
	case ChromaticRasterState::OutsideBand:
		return u"raster-outside-band"_q;
	case ChromaticRasterState::NoChromatic:
		return u"no-chromatic"_q;
	case ChromaticRasterState::BelowDensity:
		return u"below-density"_q;
	case ChromaticRasterState::Found:
		return u"found"_q;
	}
	return u"missing"_q;
}

ChromaticBox ChromaticRaster::readBox() const {
	auto result = ChromaticBox();
	const auto refusal = ClusterRefusal(*this);
	if (!refusal.isEmpty()) {
		result.refusal = refusal;
		return result;
	}
	result.box = box;
	return result;
}

ChromaticMean ChromaticRaster::readMean() const {
	auto result = ChromaticMean();
	const auto refusal = ClusterRefusal(*this);
	if (!refusal.isEmpty()) {
		result.refusal = refusal;
		return result;
	}
	result.color = mean;
	return result;
}

ChromaticDensity ChromaticRaster::readDensity() const {
	auto result = ChromaticDensity();
	const auto refusal = ClusterRefusal(*this);
	if (!refusal.isEmpty()) {
		result.refusal = refusal;
		return result;
	}
	result.density = density;
	return result;
}

ChromaticRaster ReadChromaticRaster(
		const QImage &image,
		QRect band,
		QColor pen,
		QColor fill) {
	auto result = ChromaticRaster();
	const auto clip = band.intersected(image.rect());
	if (clip.isEmpty()) {
		result.state = ChromaticRasterState::OutsideBand;
		result.reason = u"the requested band ["_q
			+ RectText(band)
			+ u"] does not intersect the image ["_q
			+ RectText(image.rect())
			+ u"], so no chromatic pixel was read"_q;
		return result;
	}
	result.ok = true;
	result.band = clip;
	auto points = std::vector<QPoint>();
	for (auto y = clip.top(); y <= clip.bottom(); ++y) {
		for (auto x = clip.left(); x <= clip.right(); ++x) {
			const auto color = image.pixelColor(x, y);
			if (ChromaticSpread(color) < kChromaticFloor) {
				continue;
			}
			if (ExcludedColor(color, pen) || ExcludedColor(color, fill)) {
				continue;
			}
			points.push_back(QPoint(x, y));
		}
	}
	result.totalChromatic = int(points.size());
	if (points.empty()) {
		result.state = ChromaticRasterState::NoChromatic;
		result.reason = u"no pixel of the band ["_q
			+ RectText(clip)
			+ u"] cleared the chromatic floor "_q
			+ QString::number(kChromaticFloor);
		return result;
	}
	auto top = points.front().y();
	auto bottom = top;
	auto left = points.front().x();
	auto right = left;
	for (const auto &point : points) {
		top = std::min(top, point.y());
		bottom = std::max(bottom, point.y());
		left = std::min(left, point.x());
		right = std::max(right, point.x());
	}
	const auto origin = clip.left();
	const auto columns = clip.width();
	auto columnCount = std::vector<int>(columns, 0);
	for (const auto &point : points) {
		++columnCount[point.x() - origin];
	}
	auto prefix = std::vector<int>(columns + 1, 0);
	for (auto i = 0; i != columns; ++i) {
		prefix[i + 1] = prefix[i] + columnCount[i];
	}
	const auto window = std::max(bottom - top + 1, 1);
	auto bestCount = -1;
	auto bestX = left;
	const auto lastStart = std::max(left, right - window + 1);
	for (auto start = left; start <= lastStart; ++start) {
		const auto from = start - origin;
		const auto to = std::min(columns, from + window);
		if ((from < 0) || (to < from)) {
			continue;
		}
		const auto count = prefix[to] - prefix[from];
		if (count > bestCount) {
			bestCount = count;
			bestX = start;
		}
	}
	auto boxLeft = bestX + window;
	auto boxRight = bestX - 1;
	auto boxTop = bottom + 1;
	auto boxBottom = top - 1;
	auto sumR = qint64(0);
	auto sumG = qint64(0);
	auto sumB = qint64(0);
	auto matched = 0;
	for (const auto &point : points) {
		if ((point.x() < bestX) || (point.x() >= bestX + window)) {
			continue;
		}
		boxLeft = std::min(boxLeft, point.x());
		boxRight = std::max(boxRight, point.x());
		boxTop = std::min(boxTop, point.y());
		boxBottom = std::max(boxBottom, point.y());
		const auto color = image.pixelColor(point);
		sumR += color.red();
		sumG += color.green();
		sumB += color.blue();
		++matched;
	}
	if (matched <= 0) {
		result.state = ChromaticRasterState::NoChromatic;
		result.reason = u"no pixel of the band ["_q
			+ RectText(clip)
			+ u"] cleared the chromatic floor "_q
			+ QString::number(kChromaticFloor);
		return result;
	}
	const auto boxWidth = boxRight - boxLeft + 1;
	const auto boxHeight = boxBottom - boxTop + 1;
	const auto area = std::max(boxWidth * boxHeight, 1);
	const auto density = float64(matched) / float64(area);
	if (density >= kChromaticDensityFloor) {
		result.state = ChromaticRasterState::Found;
		result.reason = u"none"_q;
		result.box = QRect(boxLeft, boxTop, boxWidth, boxHeight);
		result.matched = matched;
		result.density = density;
		result.mean = QColor(
			int(sumR / matched),
			int(sumG / matched),
			int(sumB / matched));
	} else {
		result.state = ChromaticRasterState::BelowDensity;
		result.reason = u"the densest chromatic window stays under the "
			"density floor: total="_q
			+ QString::number(result.totalChromatic)
			+ u" floor="_q
			+ QString::number(kChromaticDensityFloor);
	}
	return result;
}

QString FormatChromaticRaster(const ChromaticRaster &reading) {
	const auto box = reading.readBox();
	const auto mean = reading.readMean();
	const auto density = reading.readDensity();
	const auto reason = reading.reason.isEmpty()
		? u"this reading was never scanned"_q
		: reading.reason;
	return u"state="_q
		+ ChromaticRasterStateName(reading.state)
		+ u" band="_q
		+ RectText(reading.band)
		+ u" box="_q
		+ (box.read() ? RectText(box.box) : u"none"_q)
		+ u" matched="_q
		+ (box.read() ? QString::number(reading.matched) : u"none"_q)
		+ u" totalChromatic="_q
		+ (reading.ok
			? QString::number(reading.totalChromatic)
			: u"none"_q)
		+ u" density="_q
		+ (density.read() ? QString::number(density.density) : u"none"_q)
		+ u" mean="_q
		+ (mean.read() ? ColorHex(mean.color) : u"none"_q)
		+ u" reason="_q
		+ reason;
}

namespace {

[[nodiscard]] QString GlyphReason(const GlyphCore &reading) {
	return reading.reason.isEmpty()
		? u"this reading was never scanned"_q
		: reading.reason;
}

} // namespace

QString GlyphCoreStateName(GlyphCoreState state) {
	switch (state) {
	case GlyphCoreState::Unread:
		return u"core-unread"_q;
	case GlyphCoreState::OutsideBand:
		return u"core-outside-band"_q;
	case GlyphCoreState::NoPaint:
		return u"no-paint"_q;
	case GlyphCoreState::NoSolidCore:
		return u"no-solid-core"_q;
	case GlyphCoreState::Measured:
		return u"core-measured"_q;
	}
	return u"missing"_q;
}

GlyphCoreModal GlyphCore::readModal() const {
	auto result = GlyphCoreModal();
	if (state != GlyphCoreState::Measured) {
		result.refusal = u"this reading ("_q
			+ GlyphCoreStateName(state)
			+ u") measured no solid core: "_q
			+ GlyphReason(*this);
		return result;
	}
	result.color = modal;
	result.count = modalCount;
	result.cores = cores;
	return result;
}

GlyphCoreSolid GlyphCore::solidAt(int index) const {
	auto result = GlyphCoreSolid();
	result.index = index;
	if (state != GlyphCoreState::Measured) {
		result.refusal = u"index="_q
			+ QString::number(index)
			+ u" of "_q
			+ QString::number(int(pens.size()))
			+ u": this reading ("_q
			+ GlyphCoreStateName(state)
			+ u") measured no solid core: "_q
			+ GlyphReason(*this);
		return result;
	}
	if ((index < 0) || (index >= int(pens.size()))) {
		result.refusal = u"index="_q
			+ QString::number(index)
			+ u" is outside this reading's "_q
			+ QString::number(int(pens.size()))
			+ u" pens"_q;
		return result;
	}
	if (index >= int(solid.size())) {
		result.refusal = u"index="_q
			+ QString::number(index)
			+ u" names a pen this reading filled no solid count for"_q;
		return result;
	}
	result.count = solid[index];
	return result;
}

GlyphCore ReadGlyphCore(
		const QImage &image,
		QRect band,
		std::vector<InkCandidate> pens) {
	auto result = GlyphCore();
	result.pens = std::move(pens);
	result.solid.assign(result.pens.size(), -1);
	const auto clip = band.intersected(image.rect());
	if (clip.isEmpty()) {
		result.state = GlyphCoreState::OutsideBand;
		result.reason = u"the requested band lies outside the image, so "
			"no glyph pixel was read: band="_q
			+ RectText(band)
			+ u" image="_q
			+ RectText(image.rect());
		return result;
	}
	result.ok = true;
	result.band = clip;
	auto backgroundCounts = std::vector<std::pair<QRgb, int>>();
	for (auto y = clip.top(); y <= clip.bottom(); ++y) {
		for (auto x = clip.left(); x <= clip.right(); ++x) {
			const auto color = image.pixelColor(x, y);
			const auto rgb = qRgb(color.red(), color.green(), color.blue());
			++result.total;
			auto found = false;
			for (auto &one : backgroundCounts) {
				if (one.first == rgb) {
					++one.second;
					found = true;
					break;
				}
			}
			if (!found && (backgroundCounts.size() < 8192)) {
				backgroundCounts.push_back({ rgb, 1 });
			}
		}
	}
	auto bestBackground = -1;
	auto backgroundRgb = QRgb(0);
	for (const auto &one : backgroundCounts) {
		if (one.second > bestBackground) {
			bestBackground = one.second;
			backgroundRgb = one.first;
		}
	}
	result.background = QColor::fromRgb(backgroundRgb);
	auto strongest = 0;
	auto ink = std::vector<QRgb>();
	for (auto y = clip.top(); y <= clip.bottom(); ++y) {
		for (auto x = clip.left(); x <= clip.right(); ++x) {
			const auto color = image.pixelColor(x, y);
			const auto rgb = qRgb(color.red(), color.green(), color.blue());
			const auto delta = ChannelDelta(
				QColor::fromRgb(rgb),
				result.background);
			strongest = std::max(strongest, delta);
			if (delta >= kInkDelta) {
				ink.push_back(rgb);
			}
		}
	}
	result.strongest = strongest;
	result.inkPixels = int(ink.size());
	if (ink.empty()) {
		result.state = GlyphCoreState::NoPaint;
		result.reason = u"the measured background fills the band, so there "
			"is no glyph ink to read a pen from"_q;
		return result;
	}
	const int base[3] = {
		result.background.red(),
		result.background.green(),
		result.background.blue(),
	};
	int extreme[3] = { base[0], base[1], base[2] };
	int bestAbs[3] = { -1, -1, -1 };
	for (const auto rgb : ink) {
		const int value[3] = { qRed(rgb), qGreen(rgb), qBlue(rgb) };
		for (auto channel = 0; channel != 3; ++channel) {
			const auto distance = std::abs(value[channel] - base[channel]);
			if (distance > bestAbs[channel]) {
				bestAbs[channel] = distance;
				extreme[channel] = value[channel];
			}
		}
	}
	const auto extremeColor = QColor(extreme[0], extreme[1], extreme[2]);
	auto cores = std::vector<QRgb>();
	for (const auto rgb : ink) {
		if (ChannelDelta(QColor::fromRgb(rgb), extremeColor)
			<= kGlyphCoreTolerance) {
			cores.push_back(rgb);
		}
	}
	if (cores.empty()) {
		result.state = GlyphCoreState::NoSolidCore;
		result.reason = u"the band has glyph ink but every ink pixel is a "
			"fringe or partial coverage, so no solid core decides the pen"_q;
		return result;
	}
	auto coreCounts = std::vector<std::pair<QRgb, int>>();
	for (const auto rgb : cores) {
		auto found = false;
		for (auto &one : coreCounts) {
			if (one.first == rgb) {
				++one.second;
				found = true;
				break;
			}
		}
		if (!found && (coreCounts.size() < 8192)) {
			coreCounts.push_back({ rgb, 1 });
		}
	}
	auto bestCore = -1;
	auto modalRgb = cores.front();
	for (const auto &one : coreCounts) {
		if (one.second > bestCore) {
			bestCore = one.second;
			modalRgb = one.first;
		}
	}
	result.modal = QColor::fromRgb(modalRgb);
	result.modalCount = bestCore;
	result.cores = int(cores.size());
	result.state = GlyphCoreState::Measured;
	result.reason = u"none"_q;
	for (auto i = 0; i != int(result.pens.size()); ++i) {
		auto count = 0;
		for (const auto rgb : cores) {
			if (ChannelDelta(QColor::fromRgb(rgb), result.pens[i].color)
				<= kGlyphCoreTolerance) {
				++count;
			}
		}
		result.solid[i] = count;
	}
	return result;
}

QString FormatGlyphCore(const GlyphCore &reading) {
	const auto measured = (reading.state == GlyphCoreState::Measured);
	const auto reason = GlyphReason(reading);
	const auto background = reading.background.isValid()
		? ColorHex(reading.background)
		: u"none"_q;
	const auto strongest = (reading.strongest < 0)
		? u"none"_q
		: QString::number(reading.strongest);
	const auto cores = measured
		? QString::number(reading.cores)
		: u"none"_q;
	const auto modal = (measured && reading.modal.isValid())
		? ColorHex(reading.modal)
		: u"none"_q;
	const auto share = measured
		? (QString::number(reading.modalCount)
			+ u"/"_q
			+ QString::number(reading.cores))
		: u"none"_q;
	auto listed = QString();
	for (auto i = 0; i != int(reading.pens.size()); ++i) {
		auto name = reading.pens[i].name;
		if (name.isEmpty()) {
			name = u"pen"_q + QString::number(i);
		}
		const auto solid = reading.solidAt(i);
		if (!listed.isEmpty()) {
			listed += u" "_q;
		}
		listed += name
			+ u"="_q
			+ (solid.read() ? QString::number(solid.count) : u"none"_q);
	}
	auto text = u"state="_q
		+ GlyphCoreStateName(reading.state)
		+ u" band="_q
		+ RectText(reading.band)
		+ u" bg="_q
		+ background
		+ u" kGlyphCoreTolerance="_q
		+ QString::number(kGlyphCoreTolerance)
		+ u" strongest="_q
		+ strongest
		+ u" cores="_q
		+ cores
		+ u" modal="_q
		+ modal
		+ u" share="_q
		+ share;
	if (!listed.isEmpty()) {
		text += u" "_q + listed;
	}
	return text + u" reason="_q + reason;
}

namespace {

void AppendBackgroundCollinearSelfTest(not_null<Runner*> runner) {
	runner->add({
		.name = u"ink scan self-test: a background-collinear candidate "
			"is refused, and a separated control still classifies"_q,
		.run = [] {
			const auto dayFill = QColor(0xf1, 0xf1, 0xf1);
			const auto controlFill = QColor(0x20, 0x40, 0x80);
			const auto sub = QColor(0x99, 0x99, 0x99);
			const auto fg = QColor(0x00, 0x00, 0x00);
			const auto size = QSize(64, 32);
			const auto band = QRect(8, 8, 48, 12);
			const auto candidates = std::vector<InkCandidate>{
				{ u"sub"_q, sub },
				{ u"fg"_q, fg },
			};
			const auto colliding = std::vector<InkCandidate>{
				{ u"mark"_q, QColor(0xff, 0xff, 0xff) },
				{ u"mark-again"_q, QColor(0xf5, 0xf5, 0xf5) },
			};
			auto refusedImage = QImage(
				size,
				QImage::Format_ARGB32_Premultiplied);
			refusedImage.fill(dayFill);
			{
				auto p = QPainter(&refusedImage);
				p.fillRect(QRect(16, 8, 8, 12), sub);
			}
			auto controlImage = QImage(
				size,
				QImage::Format_ARGB32_Premultiplied);
			controlImage.fill(controlFill);
			{
				auto p = QPainter(&controlImage);
				p.fillRect(QRect(16, 8, 6, 12), sub);
				p.fillRect(QRect(32, 8, 4, 12), fg);
			}
			const auto refused = ScanInk(refusedImage, band, candidates);
			const auto control = ScanInk(controlImage, band, candidates);
			const auto collided = ScanInk(refusedImage, band, colliding);
			const auto subCount = refused.countAt(0);
			const auto fgCount = refused.countAt(1);
			const auto controlSub = control.countAt(0);
			const auto controlFg = control.countAt(1);
			const auto refusedText = FormatInkScan(refused, dayFill);
			const auto controlText = FormatInkScan(control, controlFill);
			const auto forbidden = std::vector<QString>{
				u"candidates-collide"_q,
				u"no-ink"_q,
				u"outside-image"_q,
				u"no-rows-in-band"_q,
				u"the recovered box is empty or the fill is invalid"_q,
				u"no row of the recovered box has the pill fill "
					"as its own background"_q,
				u"the requested fill is the image's background "
					"outside the candidate, so no band can be derived"_q,
				u"no row of the derived band kept the pill fill "
					"as its own background"_q,
				u"the candidates do not separate from each other"_q,
				u"no pixel of the band"_q,
				u"does not intersect the image"_q,
			};
			auto distinct = (InkScanStateName(refused.state)
				== u"background-collinear"_q);
			auto quoted = QString();
			for (const auto &one : forbidden) {
				if (refused.reason.contains(one)
					|| (InkScanStateName(refused.state) == one)) {
					distinct = false;
				}
				if (!quoted.isEmpty()) {
					quoted += u"; "_q;
				}
				quoted += one;
			}
			const auto thresholds = std::vector<QString>{
				u"kInkDelta=%1"_q.arg(kInkDelta),
				u"kOnLine=%1"_q.arg(kOnLine),
				u"kInkMargin=%1"_q.arg(kInkMargin),
				u"kSameTolerance=%1"_q.arg(kSameTolerance),
				u"kBackgroundSame=%1"_q.arg(kBackgroundSame),
			};
			auto thresholdsNamed = true;
			for (const auto &one : thresholds) {
				if (!controlText.contains(one)) {
					thresholdsNamed = false;
				}
			}
			Note(u"ink scan self-test: no window, session, chats list, "
				"network, account or wallet - two synthetic images"_q);
			Check(
				refused.ok
					&& (refused.state
						== InkScanState::BackgroundCollinear)
					&& (refused.state != InkScanState::Classified)
					&& (refused.reason != u"none"_q)
					&& refused.reason.contains(u"sub"_q)
					&& refused.reason.contains(u"fg"_q)
					&& refused.reason.contains(ColorHex(sub))
					&& refused.reason.contains(ColorHex(fg))
					&& refused.reason.contains(ColorHex(dayFill))
					&& (refused.ambiguous == refused.inkPixels)
					&& (refused.inkPixels > 0),
				u"a candidate on the segment from the measured background "
				"to another candidate is refused by name, not classified"_q,
				refusedText);
			Check(
				!subCount.read()
					&& (subCount.count == -1)
					&& !fgCount.read()
					&& (fgCount.count == -1)
					&& InkCountDetails(subCount).contains(u"count=none"_q)
					&& InkCountDetails(fgCount).contains(u"count=none"_q)
					&& InkCountDetails(subCount).contains(
						u"background-collinear"_q),
				u"countAt on that reading refuses both candidates with "
				"count=none"_q,
				InkCountDetails(subCount)
					+ u" | "_q
					+ InkCountDetails(fgCount));
			Check(
				control.ok
					&& (control.state == InkScanState::Classified)
					&& (control.reason == u"none"_q)
					&& controlSub.read()
					&& (controlSub.count == 72)
					&& controlFg.read()
					&& (controlFg.count == 48),
				u"the same two candidates against a background that does "
				"not put one on the other's segment still classify"_q,
				controlText
					+ u" | "_q
					+ InkCountDetails(controlSub)
					+ u" | "_q
					+ InkCountDetails(controlFg));
			Check(
				distinct,
				u"the background-collinear name and reason differ from "
				"candidates-collide, no-ink, outside-image, "
				"no-rows-in-band and every DeriveBand reason"_q,
				u"state=%1 reason=%2 forbidden=[%3]"_q
					.arg(
						InkScanStateName(refused.state),
						refused.reason,
						quoted));
			Check(
				refusedText.contains(ColorHex(sub))
					&& refusedText.contains(ColorHex(fg))
					&& refusedText.contains(ColorHex(dayFill))
					&& refusedText.contains(u"sub"_q)
					&& refusedText.contains(u"fg"_q)
					&& controlText.startsWith(u"fill="_q)
					&& controlText.contains(
						u"sub(%1)=72"_q.arg(ColorHex(sub)))
					&& controlText.contains(
						u"fg(%1)=48"_q.arg(ColorHex(fg)))
					&& controlText.contains(u"collision=none"_q)
					&& !controlText.contains(u"background-collinear"_q)
					&& thresholdsNamed,
				u"the refusing format names both colours and the measured "
				"background, and the control format keeps its counts and "
				"thresholds"_q,
				refusedText + u" || "_q + controlText);
			Check(
				collided.ok
					&& (collided.state == InkScanState::CandidatesCollide)
					&& collided.reason.contains(
						u"the candidates do not separate from each other"_q)
					&& (collided.state
						!= InkScanState::BackgroundCollinear),
				u"a pair that already collides under Separable keeps "
				"CandidatesCollide and its existing text"_q,
				FormatInkScan(collided, dayFill));
		},
	});
}

void AppendChromaticRasterSelfTest(not_null<Runner*> runner) {
	runner->add({
		.name = u"chromatic raster self-test: a dense mark, an equal-count "
			"smear, and a flat fill"_q,
		.run = [] {
			const auto pen = QColor(0x11, 0x11, 0x11);
			const auto fill = QColor(0xe8, 0xe4, 0xdc);
			const auto red = QColor(0xff, 0x00, 0x00);
			const auto blue = QColor(0x00, 0x00, 0xff);
			const auto mark = QRect(8, 8, 16, 16);
			const auto band = QRect(0, 0, 96, 40);
			auto denseImage = QImage(
				QSize(96, 40),
				QImage::Format_ARGB32_Premultiplied);
			denseImage.fill(fill);
			auto reds = 0;
			auto blues = 0;
			for (auto y = mark.top(); y <= mark.bottom(); ++y) {
				for (auto x = mark.left(); x <= mark.right(); ++x) {
					const auto color = ((x + y) % 2) ? blue : red;
					denseImage.setPixelColor(x, y, color);
					if (color == red) {
						++reds;
					} else {
						++blues;
					}
				}
			}
			const auto painted = reds + blues;
			const auto expectedMean = QColor(
				int((qint64(red.red()) * reds
					+ qint64(blue.red()) * blues) / painted),
				int((qint64(red.green()) * reds
					+ qint64(blue.green()) * blues) / painted),
				int((qint64(red.blue()) * reds
					+ qint64(blue.blue()) * blues) / painted));
			auto sparseImage = QImage(
				QSize(96, 40),
				QImage::Format_ARGB32_Premultiplied);
			sparseImage.fill(fill);
			auto sparsePainted = 0;
			for (auto y = 8; y < 24; ++y) {
				for (auto x = 8; x < 72; ++x) {
					if ((x % 4) != 0) {
						continue;
					}
					const auto color = ((x + y) % 2) ? blue : red;
					sparseImage.setPixelColor(x, y, color);
					++sparsePainted;
				}
			}
			auto flatImage = QImage(
				QSize(96, 40),
				QImage::Format_ARGB32_Premultiplied);
			flatImage.fill(fill);
			const auto dense = ReadChromaticRaster(
				denseImage,
				band,
				pen,
				fill);
			const auto sparse = ReadChromaticRaster(
				sparseImage,
				band,
				pen,
				fill);
			const auto flat = ReadChromaticRaster(
				flatImage,
				band,
				pen,
				fill);
			const auto outside = ReadChromaticRaster(
				flatImage,
				QRect(200, 200, 8, 8),
				pen,
				fill);
			const auto box = dense.readBox();
			const auto mean = dense.readMean();
			const auto density = dense.readDensity();
			const auto sparseBox = sparse.readBox();
			const auto sparseMean = sparse.readMean();
			const auto sparseDensity = sparse.readDensity();
			const auto flatBox = flat.readBox();
			const auto flatMean = flat.readMean();
			const auto flatDensity = flat.readDensity();
			const auto denseText = FormatChromaticRaster(dense);
			const auto sparseText = FormatChromaticRaster(sparse);
			const auto flatText = FormatChromaticRaster(flat);
			const auto outsideText = FormatChromaticRaster(outside);
			Note(u"chromatic raster self-test: no window, session, chats "
				"list, network, account or wallet - three synthetic "
				"images"_q);
			Check(
				dense.found()
					&& box.read()
					&& (box.box == mark)
					&& (box.box != band)
					&& band.contains(box.box)
					&& density.read()
					&& (density.density >= kChromaticDensityFloor)
					&& mean.read()
					&& (ColorHex(mean.color) == ColorHex(expectedMean))
					&& (ColorHex(mean.color) != ColorHex(pen))
					&& (ColorHex(mean.color) != ColorHex(fill))
					&& (painted == 256),
				u"a dense two-colour mark is found at its own box, with "
				"the mean of its ink and neither colour the check passed "
				"in"_q,
				denseText
					+ u" expectedMean="_q
					+ ColorHex(expectedMean)
					+ u" pen="_q
					+ ColorHex(pen)
					+ u" fill="_q
					+ ColorHex(fill));
			Check(
				(dense.totalChromatic == sparse.totalChromatic)
					&& (dense.totalChromatic == sparsePainted)
					&& (dense.totalChromatic > 0)
					&& (sparse.state == ChromaticRasterState::BelowDensity)
					&& !sparse.found()
					&& !sparseBox.read()
					&& sparseBox.box.isEmpty(),
				u"an equal chromatic count spread below the density floor "
				"is refused and has no found box"_q,
				u"denseTotal=%1 sparseTotal=%2 dense=%3 sparse=%4"_q
					.arg(dense.totalChromatic)
					.arg(sparse.totalChromatic)
					.arg(denseText, sparseText));
			Check(
				flat.ok
					&& (flat.state == ChromaticRasterState::NoChromatic)
					&& (flat.totalChromatic == 0)
					&& !flat.found()
					&& !flatBox.read()
					&& (ChromaticRasterStateName(flat.state)
						!= ChromaticRasterStateName(sparse.state))
					&& (flat.reason != sparse.reason)
					&& flat.reason.contains(u"chromatic floor"_q)
					&& sparse.reason.contains(u"density floor"_q),
				u"a flat fill with no chromatic pixel refuses by the other "
				"name and has no found box"_q,
				flatText + u" || "_q + sparseText);
			Check(
				!sparseBox.read()
					&& !sparseMean.read()
					&& !sparseDensity.read()
					&& !flatBox.read()
					&& !flatMean.read()
					&& !flatDensity.read()
					&& sparseBox.refusal.contains(u"below-density"_q)
					&& flatBox.refusal.contains(u"no-chromatic"_q),
				u"asking a refusing raster for its box, mean or density "
				"returns a refusal rather than a value"_q,
				sparseBox.refusal
					+ u" | "_q
					+ sparseMean.refusal
					+ u" | "_q
					+ flatDensity.refusal);
			Check(
				denseText.contains(RectText(mark))
					&& denseText.contains(u"matched=256"_q)
					&& denseText.contains(u"totalChromatic=256"_q)
					&& denseText.contains(
						u"density="_q
							+ QString::number(density.density)
							+ u" mean="_q)
					&& denseText.contains(ColorHex(expectedMean))
					&& denseText.contains(u"state=found"_q)
					&& !denseText.contains(ColorHex(pen))
					&& !denseText.contains(ColorHex(fill))
					&& sparseText.contains(u"state=below-density"_q)
					&& sparseText.contains(u"box=none"_q)
					&& flatText.contains(u"state=no-chromatic"_q)
					&& flatText.contains(u"box=none"_q)
					&& outsideText.contains(u"state=raster-outside-band"_q)
					&& !outside.ok
					&& (outside.state
						!= ChromaticRasterState::NoChromatic)
					&& (outside.state
						!= ChromaticRasterState::BelowDensity),
				u"the found format carries the box, both counts, the "
				"density and the mean, and each refusal names itself"_q,
				denseText
					+ u" || "_q
					+ sparseText
					+ u" || "_q
					+ flatText
					+ u" || "_q
					+ outsideText);
		},
	});
}

void AppendGlyphCoreSelfTest(not_null<Runner*> runner) {
	runner->add({
		.name = u"glyph-core self-test: neutral and link strokes on both "
			"grounds"_q,
		.run = [] {
			const auto band = QRect(8, 8, 80, 32);
			const auto absent = QColor(0xff, 0x00, 0x00);
			const auto paint = [](
					QColor ground,
					QColor pen,
					QColor fringeA,
					QColor fringeB,
					QColor fringeC) {
				auto image = QImage(
					QSize(96, 48),
					QImage::Format_ARGB32_Premultiplied);
				image.fill(ground);
				for (auto y = 12; y < 28; ++y) {
					for (auto x = 20; x < 36; ++x) {
						image.setPixelColor(x, y, pen);
					}
					image.setPixelColor(17, y, fringeA);
					image.setPixelColor(18, y, fringeB);
					image.setPixelColor(36, y, fringeC);
				}
				return image;
			};
			const auto checkGround = [&](
					const QString &ground,
					QColor groundColor,
					QColor neutral,
					QColor link,
					int neutralStrongest,
					int linkStrongest,
					QColor fringeA,
					QColor fringeB,
					QColor fringeC) {
				const auto scanPens = std::vector<InkCandidate>{
					{ u"neutral"_q, neutral },
					{ u"link"_q, link },
				};
				const auto pens = std::vector<InkCandidate>{
					{ u"neutral"_q, neutral },
					{ u"link"_q, link },
					{ u"absent"_q, absent },
				};
				const auto neutralImage = paint(
					groundColor,
					neutral,
					fringeA,
					fringeB,
					fringeC);
				const auto linkImage = paint(
					groundColor,
					link,
					fringeA,
					fringeB,
					fringeC);
				const auto scan = ScanInk(neutralImage, band, scanPens);
				const auto scanNeutral = scan.countAt(0);
				const auto scanLink = scan.countAt(1);
				const auto neutralCore = ReadGlyphCore(
					neutralImage,
					band,
					pens);
				const auto linkCore = ReadGlyphCore(linkImage, band, pens);
				const auto neutralModal = neutralCore.readModal();
				const auto linkModal = linkCore.readModal();
				const auto neutralOfNeutral = neutralCore.solidAt(0);
				const auto linkOfNeutral = neutralCore.solidAt(1);
				const auto absentOfNeutral = neutralCore.solidAt(2);
				const auto neutralOfLink = linkCore.solidAt(0);
				const auto linkOfLink = linkCore.solidAt(1);
				const auto absentOfLink = linkCore.solidAt(2);
				const auto neutralText = FormatGlyphCore(neutralCore);
				const auto linkText = FormatGlyphCore(linkCore);
				const auto quoted = neutralText
					+ u" || "_q
					+ linkText
					+ u" || scan "_q
					+ InkCountDetails(scanNeutral)
					+ u" | "_q
					+ InkCountDetails(scanLink);
				Check(
					(scan.state == InkScanState::Classified)
						&& scanNeutral.read()
						&& (scanNeutral.count == 256)
						&& scanLink.read()
						&& (scanLink.count == 48)
						&& (neutralCore.state == GlyphCoreState::Measured)
						&& (ColorHex(neutralCore.background)
							== ColorHex(groundColor))
						&& (neutralCore.strongest == neutralStrongest)
						&& (neutralCore.cores == 256)
						&& neutralModal.read()
						&& (ColorHex(neutralModal.color) == ColorHex(neutral))
						&& (neutralModal.count == 256)
						&& (neutralModal.cores == 256)
						&& neutralOfNeutral.read()
						&& (neutralOfNeutral.count == 256)
						&& linkOfNeutral.read()
						&& (linkOfNeutral.count == 0)
						&& absentOfNeutral.read()
						&& (absentOfNeutral.count == 0)
						&& (ColorHex(neutralModal.color) != ColorHex(absent))
						&& neutralText.contains(RectText(band))
						&& neutralText.contains(
							u"bg="_q + ColorHex(groundColor))
						&& neutralText.contains(u"kGlyphCoreTolerance=8"_q)
						&& neutralText.contains(u"cores=256"_q)
						&& neutralText.contains(
							u"modal="_q + ColorHex(neutral))
						&& neutralText.contains(u"share=256/256"_q)
						&& neutralText.contains(u"neutral=256"_q)
						&& neutralText.contains(u"link=0"_q)
						&& neutralText.contains(u"absent=0"_q),
					u"on the "_q
						+ ground
						+ u" ground a neutral stroke is classified with "
						"link fringes, and its solid cores are the "
						"neutral pen"_q,
					quoted);
				Check(
					(linkCore.state == GlyphCoreState::Measured)
						&& (linkCore.strongest == linkStrongest)
						&& (linkCore.cores == 256)
						&& linkModal.read()
						&& (ColorHex(linkModal.color) == ColorHex(link))
						&& (ColorHex(linkModal.color) != ColorHex(absent))
						&& neutralOfLink.read()
						&& (neutralOfLink.count == 0)
						&& linkOfLink.read()
						&& (linkOfLink.count == 256)
						&& absentOfLink.read()
						&& (absentOfLink.count == 0)
						&& linkText.contains(u"kGlyphCoreTolerance=8"_q)
						&& linkText.contains(u"cores=256"_q)
						&& linkText.contains(u"modal="_q + ColorHex(link))
						&& linkText.contains(u"share=256/256"_q)
						&& linkText.contains(u"neutral=0"_q)
						&& linkText.contains(u"link=256"_q)
						&& linkText.contains(
							u"bg="_q + ColorHex(groundColor)),
					u"on the "_q
						+ ground
						+ u" ground the same stroke in the link pen has "
						"that pen as its modal core"_q,
					quoted);
			};
			Note(u"glyph-core self-test: no window, session, chats list, "
				"network, account or wallet - synthetic images on two "
				"grounds"_q);
			checkGround(
				u"day"_q,
				QColor(0xff, 0xff, 0xff),
				QColor(0x99, 0x99, 0x99),
				QColor(0x16, 0x8a, 0xcd),
				102,
				233,
				QColor(0xa1, 0xd0, 0xf5),
				QColor(0xa1, 0xd6, 0xf2),
				QColor(0xa1, 0xdb, 0xf0));
			checkGround(
				u"night"_q,
				QColor(0x17, 0x21, 0x2b),
				QColor(0x70, 0x84, 0x99),
				QColor(0x6a, 0xb3, 0xf3),
				110,
				200,
				QColor(0x38, 0x62, 0x8f),
				QColor(0x34, 0x5b, 0x8b),
				QColor(0x3c, 0x6a, 0x93));
		},
	});
	runner->add({
		.name = u"glyph-core self-test: no ink, no solid core, and refused "
			"reads"_q,
		.run = [] {
			const auto band = QRect(8, 8, 80, 32);
			const auto pens = std::vector<InkCandidate>{
				{ u"neutral"_q, QColor(0x99, 0x99, 0x99) },
			};
			auto flatImage = QImage(
				QSize(96, 48),
				QImage::Format_ARGB32_Premultiplied);
			flatImage.fill(QColor(0xf0, 0xf0, 0xf0));
			const auto flat = ReadGlyphCore(flatImage, band, pens);
			const auto flatModal = flat.readModal();
			const auto flatSolid = flat.solidAt(0);
			const auto flatText = FormatGlyphCore(flat);
			auto fringeImage = QImage(
				QSize(40, 16),
				QImage::Format_ARGB32_Premultiplied);
			fringeImage.fill(Qt::white);
			for (auto x = 0; x < 40; ++x) {
				fringeImage.setPixelColor(x, 0, QColor(80, 255, 255));
				fringeImage.setPixelColor(x, 1, QColor(255, 80, 255));
				fringeImage.setPixelColor(x, 2, QColor(255, 255, 80));
				fringeImage.setPixelColor(x, 3, QColor(210, 220, 230));
			}
			const auto fringeBand = QRect(0, 0, 40, 16);
			const auto fringe = ReadGlyphCore(fringeImage, fringeBand, pens);
			const auto fringeModal = fringe.readModal();
			const auto fringeSolid = fringe.solidAt(0);
			const auto fringeText = FormatGlyphCore(fringe);
			const auto outside = ReadGlyphCore(
				flatImage,
				QRect(200, 200, 8, 8),
				pens);
			const auto outsideModal = outside.readModal();
			const auto outsideText = FormatGlyphCore(outside);
			const auto shipped = std::vector<QString>{
				u"not-scanned"_q,
				u"outside-image"_q,
				u"no-rows-in-band"_q,
				u"candidates-collide"_q,
				u"no-ink"_q,
				u"classified"_q,
				u"background-collinear"_q,
				u"raster-outside-band"_q,
				u"no-chromatic"_q,
				u"below-density"_q,
				u"found"_q,
				u"missing"_q,
			};
			const auto flatName = GlyphCoreStateName(flat.state);
			const auto fringeName = GlyphCoreStateName(fringe.state);
			const auto outsideName = GlyphCoreStateName(outside.state);
			auto distinct = (flatName != fringeName)
				&& (flat.reason != fringe.reason)
				&& (flatName != outsideName)
				&& (fringeName != outsideName);
			auto forbidden = QString();
			for (const auto &one : shipped) {
				if ((flatName == one)
					|| (fringeName == one)
					|| (outsideName == one)
					|| flat.reason.contains(one)
					|| fringe.reason.contains(one)) {
					distinct = false;
				}
				if (!forbidden.isEmpty()) {
					forbidden += u", "_q;
				}
				forbidden += one;
			}
			Note(u"glyph-core self-test: no window, session, chats list, "
				"network, account or wallet - a flat band and a fringe "
				"band"_q);
			Check(
				flat.ok
					&& (flat.state == GlyphCoreState::NoPaint)
					&& !flatModal.read()
					&& !flatModal.color.isValid()
					&& (flatModal.count == -1)
					&& (flatModal.cores == -1)
					&& !flatSolid.read()
					&& (flatSolid.count == -1)
					&& flatSolid.refusal.contains(u"no-paint"_q)
					&& flatModal.refusal.contains(u"no-paint"_q),
				u"asking a flat band for its modal colour or a solid "
				"count refuses, and the count is not a measured zero"_q,
				flatModal.refusal + u" | "_q + flatSolid.refusal);
			Check(
				flatText.contains(u"state=no-paint"_q)
					&& flatText.contains(u"modal=none"_q)
					&& flatText.contains(u"cores=none"_q)
					&& flatText.contains(u"share=none"_q)
					&& flatText.contains(u"neutral=none"_q)
					&& !flatText.contains(u"modal=#"_q),
				u"the no-paint format names the refusal and carries no "
				"modal colour"_q,
				flatText);
			Check(
				fringe.ok
					&& (fringe.state == GlyphCoreState::NoSolidCore)
					&& (fringe.inkPixels > 0)
					&& !fringeModal.read()
					&& !fringeModal.color.isValid()
					&& (fringeModal.count == -1)
					&& !fringeSolid.read()
					&& (fringeSolid.count == -1)
					&& fringeText.contains(u"state=no-solid-core"_q)
					&& fringeText.contains(u"modal=none"_q)
					&& !fringeText.contains(u"modal=#"_q),
				u"a fringe band with no full-coverage core refuses by "
				"name and does not present a fringe colour as the pen"_q,
				fringeText + u" | "_q + fringeModal.refusal);
			Check(
				!outside.ok
					&& (outside.state == GlyphCoreState::OutsideBand)
					&& !outsideModal.read()
					&& (outsideModal.count == -1)
					&& outsideText.contains(u"state=core-outside-band"_q)
					&& (outside.state != GlyphCoreState::NoPaint)
					&& (outside.state != GlyphCoreState::NoSolidCore),
				u"a band that misses the image is not the no-paint "
				"refusal"_q,
				outsideText);
			Check(
				distinct
					&& flat.reason.contains(u"no glyph ink"_q)
					&& fringe.reason.contains(u"no solid core"_q),
				u"the no-paint and no-solid-core names and reasons differ "
				"from each other and from every state the module already "
				"ships"_q,
				u"flat=%1 reason=%2 fringe=%3 reason=%4 outside=%5 "
				"forbidden=[%6]"_q
					.arg(
						flatName,
						flat.reason,
						fringeName,
						fringe.reason,
						outsideName,
						forbidden));
		},
	});
}

} // namespace

void AppendDeriveBandSelfTest(not_null<Runner*> runner) {
	runner->add({
		.name = u"derive-band self-test: underivable fill, separable "
			"control, and no-rows refusal"_q,
		.run = [] {
			const auto fill = QColor(0x29, 0xb0, 0x71);
			const auto surround = QColor(0x17, 0x21, 0x2b);
			const auto size = QSize(64, 32);
			const auto candidate = QRect(8, 4, 48, 24);
			const auto underivableReason = u"the requested fill is the "
				"image's background outside the candidate, so no band "
				"can be derived"_q;
			const auto noRowsReason = u"no row of the recovered box has "
				"the pill fill as its own background"_q;
			auto same = QImage(size, QImage::Format_ARGB32_Premultiplied);
			same.fill(fill);
			const auto underivable = DeriveBand(same, candidate, fill);
			const auto measured = MeasurePaintedInk(
				same,
				candidate,
				fill,
				{ { u"ink"_q, QColor(255, 255, 255) } });
			auto separable = QImage(
				size,
				QImage::Format_ARGB32_Premultiplied);
			separable.fill(surround);
			{
				auto p = QPainter(&separable);
				p.fillRect(candidate, fill);
			}
			const auto derived = DeriveBand(separable, candidate, fill);
			const auto measuredOk = MeasurePaintedInk(
				separable,
				candidate,
				fill,
				{ { u"ink"_q, QColor(255, 255, 255) } });
			auto none = QImage(size, QImage::Format_ARGB32_Premultiplied);
			none.fill(surround);
			const auto noRows = DeriveBand(none, candidate, fill);
			Note(u"derive-band self-test: no window, session, chats list, "
				"network, account or wallet - three synthetic images"_q);
			Check(
				!underivable.ok
					&& (underivable.reason == underivableReason)
					&& (underivable.fillRows > 0)
					&& underivable.rows.empty(),
				u"a candidate whose fill is the surrounding background "
				"is refused with the underivable-band reason"_q,
				u"ok=%1 fillRows=%2 rows=%3 reason=%4"_q
					.arg(underivable.ok ? 1 : 0)
					.arg(underivable.fillRows)
					.arg(int(underivable.rows.size()))
					.arg(underivable.reason));
			Check(
				!measured.derived.ok
					&& measured.report.contains(underivableReason),
				u"MeasurePaintedInk carries that reason into its report "
				"rather than a bare zero"_q,
				u"report=%1 derivedOk=%2"_q
					.arg(measured.report)
					.arg(measured.derived.ok ? 1 : 0));
			Check(
				derived.ok
					&& (derived.reason == u"none"_q)
					&& !derived.rows.empty()
					&& !derived.fillRegion.isEmpty()
					&& !derived.band.isEmpty(),
				u"a genuinely separable band still derives with its rows "
				"and region"_q,
				u"ok=%1 rows=%2 fillRows=%3 fillRegion=%4x%5 band=%6x%7 "
				"reason=%8"_q
					.arg(derived.ok ? 1 : 0)
					.arg(int(derived.rows.size()))
					.arg(derived.fillRows)
					.arg(derived.fillRegion.width())
					.arg(derived.fillRegion.height())
					.arg(derived.band.width())
					.arg(derived.band.height())
					.arg(derived.reason));
			Check(
				measuredOk.derived.ok
					&& measuredOk.report.startsWith(u"fill="_q),
				u"MeasurePaintedInk on a separable band still writes a "
				"FormatInkReport line"_q,
				u"report=%1 derivedOk=%2"_q
					.arg(measuredOk.report)
					.arg(measuredOk.derived.ok ? 1 : 0));
			Check(
				!noRows.ok
					&& (noRows.reason == noRowsReason)
					&& (noRows.reason != underivableReason)
					&& noRows.rows.empty(),
				u"an image whose rows exist but none matches the fill "
				"still produces the existing no-rows refusal"_q,
				u"ok=%1 fillRows=%2 reason=%3"_q
					.arg(noRows.ok ? 1 : 0)
					.arg(noRows.fillRows)
					.arg(noRows.reason));
			Check(
				ChannelDelta(fill, surround) > kBackgroundSame,
				u"the separable control's surround is farther from the "
				"fill than kBackgroundSame"_q,
				u"delta=%1 kBackgroundSame=%2"_q
					.arg(ChannelDelta(fill, surround))
					.arg(kBackgroundSame));
		},
	});
	runner->add({
		.name = u"ink scan self-test: a classifying band, its counts read "
			"back, and an out-of-range ask refused"_q,
		.run = [] {
			const auto background = QColor(0x17, 0x21, 0x2b);
			const auto mark = QColor(0xff, 0xff, 0xff);
			const auto text = QColor(0x29, 0xb0, 0x71);
			const auto size = QSize(64, 32);
			const auto band = QRect(8, 8, 48, 12);
			const auto candidates = std::vector<InkCandidate>{
				{ u"mark"_q, mark },
				{ u"text"_q, text },
			};
			auto painted = QImage(
				size,
				QImage::Format_ARGB32_Premultiplied);
			painted.fill(background);
			{
				auto p = QPainter(&painted);
				p.fillRect(QRect(16, 8, 6, 12), mark);
				p.fillRect(QRect(32, 8, 4, 12), text);
			}
			auto blank = QImage(size, QImage::Format_ARGB32_Premultiplied);
			blank.fill(background);
			const auto scan = ScanInk(painted, band, candidates);
			const auto blankScan = ScanInk(blank, band, candidates);
			const auto lone = ScanInk(
				painted,
				band,
				{ { u"mark"_q, mark } });
			const auto markCount = scan.countAt(0);
			const auto textCount = scan.countAt(1);
			const auto blankCount = blankScan.countAt(0);
			const auto loneCount = lone.countAt(0);
			const auto pastEnd = scan.countAt(2);
			const auto beforeStart = scan.countAt(-1);
			const auto formatted = FormatInkScan(scan, background);
			const auto thresholds = std::vector<QString>{
				u"kInkDelta=%1"_q.arg(kInkDelta),
				u"kOnLine=%1"_q.arg(kOnLine),
				u"kInkMargin=%1"_q.arg(kInkMargin),
				u"kSameTolerance=%1"_q.arg(kSameTolerance),
				u"kBackgroundSame=%1"_q.arg(kBackgroundSame),
			};
			auto thresholdsNamed = true;
			for (const auto &one : thresholds) {
				if (!formatted.contains(one)) {
					thresholdsNamed = false;
				}
			}
			Note(u"ink scan self-test: no window, session, chats list, "
				"network, account or wallet - two synthetic images"_q);
			Check(
				scan.ok
					&& (scan.state == InkScanState::Classified)
					&& (scan.reason == u"none"_q)
					&& (scan.candidates.size() == 2)
					&& (scan.counts.size() == scan.candidates.size()),
				u"a band whose two candidates separate classifies, with "
				"its counts vector as long as its candidates"_q,
				formatted);
			Check(
				markCount.read()
					&& (markCount.name == u"mark"_q)
					&& (markCount.count == 72)
					&& textCount.read()
					&& (textCount.name == u"text"_q)
					&& (textCount.count == 48)
					&& blankCount.read()
					&& (blankCount.count == 0),
				u"each candidate's count reads back through countAt as the "
				"ink the fixture painted, against an image that painted "
				"none"_q,
				InkCountDetails(markCount)
					+ u" | "_q
					+ InkCountDetails(textCount)
					+ u" | control "_q
					+ InkCountDetails(blankCount));
			Check(
				!pastEnd.read()
					&& (pastEnd.count == -1)
					&& pastEnd.refusal.contains(
						u"outside this reading's 2 candidates"_q)
					&& pastEnd.refusal.contains(u"mark, text"_q)
					&& !beforeStart.read()
					&& (beforeStart.count == -1),
				u"an index this reading does not have is refused by name "
				"instead of read past the end of its counts"_q,
				InkCountDetails(pastEnd)
					+ u" | "_q
					+ InkCountDetails(beforeStart));
			Check(
				formatted.startsWith(u"fill="_q)
					&& formatted.contains(
						u"mark(%1)=72"_q.arg(ColorHex(mark)))
					&& formatted.contains(
						u"text(%1)=48"_q.arg(ColorHex(text)))
					&& formatted.contains(u"collision=none"_q)
					&& thresholdsNamed,
				u"the passing reading's own text names the candidate "
				"colours, their counts and every threshold it classified "
				"against"_q,
				formatted);
			Check(
				lone.ok
					&& (lone.state == InkScanState::Classified)
					&& loneCount.read()
					&& (loneCount.count == 72)
					&& (lone.ambiguous == 48),
				u"the collision gate leaves a lone-candidate reading "
				"untouched, which is the shape the only tracked caller "
				"passes"_q,
				FormatInkScan(lone, background)
					+ u" | "_q
					+ InkCountDetails(loneCount));
		},
	});
	runner->add({
		.name = u"ink scan self-test: four named refusals, a collision the "
			"reading names, and a scanned band with no ink"_q,
		.run = [] {
			// The pill control's fill is the same green the band paints as
			// its text ink: that control's derived band is nothing but the
			// fill, so every pixel in it reads as its own background and
			// the reading is a measured NoInk rather than a refusal, which
			// is what keeps the existing stage's fill= report check true.
			const auto surround = QColor(0x17, 0x21, 0x2b);
			const auto fill = QColor(0x29, 0xb0, 0x71);
			const auto mark = QColor(0xff, 0xff, 0xff);
			const auto nearMark = QColor(0xf5, 0xf5, 0xf5);
			const auto size = QSize(64, 32);
			const auto candidate = QRect(8, 4, 48, 24);
			const auto band = QRect(8, 8, 48, 12);
			const auto away = QRect(96, 96, 8, 8);
			const auto candidates = std::vector<InkCandidate>{
				{ u"mark"_q, mark },
				{ u"text"_q, fill },
			};
			const auto colliding = std::vector<InkCandidate>{
				{ u"mark"_q, mark },
				{ u"mark-again"_q, nearMark },
			};
			const auto emptyBoxReason = u"the recovered box is empty or "
				"the fill is invalid"_q;
			const auto noFillRowsReason = u"no row of the recovered box "
				"has the pill fill as its own background"_q;
			const auto underivableReason = u"the requested fill is the "
				"image's background outside the candidate, so no band can "
				"be derived"_q;
			const auto noBandRowsReason = u"no row of the derived band "
				"kept the pill fill as its own background"_q;
			auto painted = QImage(
				size,
				QImage::Format_ARGB32_Premultiplied);
			painted.fill(surround);
			{
				auto p = QPainter(&painted);
				p.fillRect(QRect(16, 8, 6, 12), mark);
				p.fillRect(QRect(32, 8, 4, 12), fill);
			}
			auto blank = QImage(size, QImage::Format_ARGB32_Premultiplied);
			blank.fill(surround);
			auto solid = QImage(size, QImage::Format_ARGB32_Premultiplied);
			solid.fill(surround);
			{
				auto p = QPainter(&solid);
				p.fillRect(candidate, fill);
			}
			const auto outside = ScanInk(painted, away, candidates);
			const auto noRows = ScanInk(
				painted,
				band,
				candidates,
				{ 0, 31 });
			const auto collided = ScanInk(painted, band, colliding);
			const auto noInk = ScanInk(blank, band, candidates);
			const auto outsideCount = outside.countAt(0);
			const auto collidedCount = collided.countAt(0);
			const auto noInkCount = noInk.countAt(0);
			const auto outsideText = FormatInkScan(outside, surround);
			const auto collidedText = FormatInkScan(collided, surround);
			const auto collisionDump = CollisionDump(colliding);
			const auto outsideReason = u"the requested band [%1] does not "
				"intersect the image [%2], so no pixel was scanned"_q
				.arg(RectText(away), RectText(painted.rect()));
			const auto noRowsReason = u"none of the %1 requested rows "
				"falls inside the band [%2], so no pixel was scanned"_q
				.arg(2)
				.arg(RectText(band));
			const auto deriveReasons = std::vector<QString>{
				emptyBoxReason,
				noFillRowsReason,
				underivableReason,
				noBandRowsReason,
			};
			auto distinct = (outside.reason != noRows.reason);
			auto deriveList = QString();
			for (const auto &one : deriveReasons) {
				if ((outside.reason == one) || (noRows.reason == one)) {
					distinct = false;
				}
				if (!deriveList.isEmpty()) {
					deriveList += u"; "_q;
				}
				deriveList += one;
			}
			auto attributed = 0;
			for (const auto count : collided.counts) {
				attributed += count;
			}
			const auto measuredOk = MeasurePaintedInk(
				solid,
				candidate,
				fill,
				candidates);
			const auto measuredCollide = MeasurePaintedInk(
				solid,
				candidate,
				fill,
				colliding);
			const auto measuredRefused = MeasurePaintedInk(
				blank,
				candidate,
				fill,
				candidates);
			const auto refusedCount = measuredRefused.scan.countAt(0);
			const auto thresholds = std::vector<QString>{
				u"kInkDelta=%1"_q.arg(kInkDelta),
				u"kOnLine=%1"_q.arg(kOnLine),
				u"kInkMargin=%1"_q.arg(kInkMargin),
				u"kSameTolerance=%1"_q.arg(kSameTolerance),
				u"kBackgroundSame=%1"_q.arg(kBackgroundSame),
			};
			auto thresholdsNamed = true;
			for (const auto &one : thresholds) {
				if (!measuredOk.report.contains(one)) {
					thresholdsNamed = false;
				}
			}
			Note(u"ink scan self-test: no window, session, chats list, "
				"network, account or wallet - three synthetic images"_q);
			Check(
				!outside.ok
					&& (outside.state == InkScanState::OutsideImage)
					&& (outside.reason == outsideReason)
					&& outside.reason.contains(
						u"does not intersect the image"_q)
					&& (outside.band == QRect()),
				u"a band that does not intersect the image refuses by name "
				"and quotes the band it was asked for"_q,
				outsideText);
			Check(
				!noRows.ok
					&& (noRows.state == InkScanState::NoRowsInBand)
					&& (noRows.reason == noRowsReason)
					&& (noRows.band == band),
				u"an explicit row list with no row inside the band refuses "
				"by name and still carries the clip"_q,
				FormatInkScan(noRows, surround));
			Check(
				distinct,
				u"the two scan refusals differ from each other and from "
				"all four DeriveBand reasons read back here"_q,
				u"outside=%1 noRows=%2 derive=[%3]"_q
					.arg(outside.reason, noRows.reason, deriveList));
			Check(
				(outside.candidates.size() == 2)
					&& (outside.counts.size() == outside.candidates.size())
					&& (outsideCount.name == u"mark"_q)
					&& !outsideCount.read()
					&& (outsideCount.count == -1)
					&& outsideCount.refusal.contains(
						u"classified nothing"_q),
				u"a refused scan still names the candidates it was asked "
				"to classify against and refuses every count"_q,
				outsideText + u" | "_q + InkCountDetails(outsideCount));
			Check(
				collided.ok
					&& (collided.state == InkScanState::CandidatesCollide)
					&& (collided.inkPixels == 120)
					&& (collided.ambiguous == collided.inkPixels)
					&& !attributed
					&& !collided.classifiedInk.isValid()
					&& !collidedCount.read()
					&& (collidedCount.name == u"mark"_q)
					&& collidedText.startsWith(u"fill="_q)
					&& collidedText.contains(collisionDump)
					&& collidedText.contains(
						u"kSameTolerance=%1"_q.arg(kSameTolerance)),
				u"a colliding candidate pair is still a scanned reading "
				"whose text names the collision"_q,
				collidedText + u" | "_q + InkCountDetails(collidedCount));
			Check(
				noInk.ok
					&& (noInk.state == InkScanState::NoInk)
					&& !noInk.inkPixels
					&& noInkCount.read()
					&& (noInkCount.count == 0)
					&& noInk.reason.contains(u"no pixel of the band"_q),
				u"a band that was scanned and held no ink reads back a "
				"real zero rather than a refusal"_q,
				FormatInkScan(noInk, surround)
					+ u" | "_q
					+ InkCountDetails(noInkCount));
			Check(
				!outsideText.isEmpty()
					&& outsideText.startsWith(u"fill="_q)
					&& outsideText.contains(outside.reason),
				u"FormatInkScan over a refused scan reading carries that "
				"reading's own reason and never nothing, which is the text "
				"MeasurePaintedInk's scan branch writes"_q,
				outsideText);
			Check(
				measuredOk.derived.ok
					&& measuredOk.report.startsWith(u"fill="_q)
					&& measuredOk.report.contains(u"state=no-ink"_q)
					&& thresholdsNamed,
				u"a passing MeasurePaintedInk still starts with its "
				"measured fill and now says what it was measured against"_q,
				measuredOk.report);
			Check(
				measuredCollide.derived.ok
					&& measuredCollide.report.startsWith(u"fill="_q)
					&& measuredCollide.report.contains(collisionDump),
				u"MeasurePaintedInk end to end on a colliding candidate "
				"pair names the collision in its report"_q,
				measuredCollide.report);
			Check(
				!measuredRefused.derived.ok
					&& (measuredRefused.scan.state
						== InkScanState::NotScanned)
					&& !measuredRefused.report.isEmpty()
					&& !refusedCount.read()
					&& (refusedCount.count == -1)
					&& refusedCount.refusal.contains(u"not-scanned"_q),
				u"the reading a refused derivation leaves behind names itself "
				"not-scanned and refuses every count instead of reading past "
				"the end of a vector it never filled"_q,
				measuredRefused.report
					+ u" | "_q
					+ InkCountDetails(refusedCount));
		},
	});
	AppendBackgroundCollinearSelfTest(runner);
	AppendChromaticRasterSelfTest(runner);
	AppendGlyphCoreSelfTest(runner);
}

} // namespace Test

#endif // _DEBUG
