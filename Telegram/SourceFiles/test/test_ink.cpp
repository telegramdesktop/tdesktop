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
}

} // namespace Test

#endif // _DEBUG
