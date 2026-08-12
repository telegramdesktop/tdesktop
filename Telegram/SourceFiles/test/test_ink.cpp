/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_ink.h"

#include "ui/color_contrast.h"

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
	const auto clip = band.intersected(image.rect());
	if (clip.isEmpty()) {
		return result;
	}
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
		return result;
	}
	result.ok = true;
	result.band = clip;
	result.scannedRows = int(rows.size());
	result.candidates = std::move(candidates);
	result.counts.assign(result.candidates.size(), 0);

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
	}
	if (result.scan.ok) {
		result.report = FormatInkReport(
			fill,
			result.scan.ink,
			result.contrast,
			result.scan.inkPixels);
	}
	return result;
}

} // namespace Test

#endif // _DEBUG
