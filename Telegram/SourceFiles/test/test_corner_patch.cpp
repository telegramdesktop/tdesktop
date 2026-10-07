/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_corner_patch.h"

#include "test/test_capture.h"
#include "test/test_ink.h"
#include "test/test_log.h"
#include "test/test_runner.h"
#include "ui/chat/chat_style_radius.h"
#include "ui/image/image_prepare.h"
#include "ui/style/style_core_scale.h"
#include "ui/rect_part.h"

#include <QtGui/QPainter>

#include <algorithm>
#include <array>
#include <cmath>

namespace Test {
namespace {

constexpr auto kFirstChanged = 5;

[[nodiscard]] QString ColorHex(QColor color) {
	if (!color.isValid()) {
		return u"invalid"_q;
	}
	return u"#%1%2%3"_q
		.arg(color.red(), 2, 16, QChar('0'))
		.arg(color.green(), 2, 16, QChar('0'))
		.arg(color.blue(), 2, 16, QChar('0'));
}

[[nodiscard]] QString ColorOrNone(QColor color) {
	return color.isValid() ? ColorHex(color) : u"none"_q;
}

[[nodiscard]] QString PointText(QPoint point) {
	return u"(%1,%2)"_q.arg(point.x()).arg(point.y());
}

[[nodiscard]] QString SizeText(QSize size) {
	return u"%1x%2"_q.arg(size.width()).arg(size.height());
}

[[nodiscard]] QString BoundsText(QRect rect) {
	return u"%1,%2 %3x%4"_q
		.arg(rect.x())
		.arg(rect.y())
		.arg(rect.width())
		.arg(rect.height());
}

[[nodiscard]] QString CountText(int count) {
	return (count < 0) ? u"none"_q : QString::number(count);
}

[[nodiscard]] QString ValueText(double value) {
	return QString::number(value, 'f', 4);
}

[[nodiscard]] QString ChannelName(int channel) {
	switch (channel) {
	case 0: return u"r"_q;
	case 1: return u"g"_q;
	case 2: return u"b"_q;
	}
	return u"none"_q;
}

[[nodiscard]] int ChannelValue(QColor color, int channel) {
	switch (channel) {
	case 0: return color.red();
	case 1: return color.green();
	case 2: return color.blue();
	}
	Unexpected("Channel in Test::ChannelValue.");
}

[[nodiscard]] QString MediaText(QColor media, const CornerCoverage &map) {
	const auto color = ColorOrNone(media);
	return (map.channel < 0)
		? color
		: (color
			+ u" (channel %1 contrast %2)"_q
				.arg(ChannelName(map.channel))
				.arg(map.contrast));
}

[[nodiscard]] int Area(QSize size) {
	return std::max(size.width(), 0) * std::max(size.height(), 0);
}

[[nodiscard]] bool Inside(QSize size, QPoint point) {
	return (point.x() >= 0)
		&& (point.y() >= 0)
		&& (point.x() < size.width())
		&& (point.y() < size.height());
}

[[nodiscard]] int Index(QSize size, QPoint point) {
	return point.y() * size.width() + point.x();
}

[[nodiscard]] PatchCorner Opposite(PatchCorner corner) {
	switch (corner) {
	case PatchCorner::TopLeft: return PatchCorner::BottomRight;
	case PatchCorner::TopRight: return PatchCorner::BottomLeft;
	case PatchCorner::BottomLeft: return PatchCorner::TopRight;
	case PatchCorner::BottomRight: return PatchCorner::TopLeft;
	}
	Unexpected("Corner in Test::Opposite.");
}

void ForEachRegionPixel(const QRegion &region, Fn<void(QPoint)> callback) {
	for (const auto &rect : region) {
		for (auto y = rect.top(); y <= rect.bottom(); ++y) {
			for (auto x = rect.left(); x <= rect.right(); ++x) {
				callback(QPoint(x, y));
			}
		}
	}
}

[[nodiscard]] int RegionPixels(const QRegion &region) {
	auto result = 0;
	for (const auto &rect : region) {
		result += rect.width() * rect.height();
	}
	return result;
}

// The 4-connected component of |start| over the pixels |inside| accepts,
// one byte per pixel of |size|, row-major. Connectivity is the whole point:
// a pixel |inside| accepts that the component never reaches - a glyph of
// the background colour enclosed by media - stays unset. No pixel is set
// when |start| is outside the patch or not accepted.
[[nodiscard]] std::vector<uchar> FloodCornerComponent(
		QSize size,
		QPoint start,
		Fn<bool(int x, int y)> inside) {
	auto result = std::vector<uchar>(Area(size), uchar(0));
	if (!Inside(size, start) || !inside(start.x(), start.y())) {
		return result;
	}
	result[Index(size, start)] = 1;
	auto pending = std::vector<QPoint>{ start };
	while (!pending.empty()) {
		const auto point = pending.back();
		pending.pop_back();
		const auto neighbours = std::array{
			QPoint(point.x() - 1, point.y()),
			QPoint(point.x() + 1, point.y()),
			QPoint(point.x(), point.y() - 1),
			QPoint(point.x(), point.y() + 1),
		};
		for (const auto &next : neighbours) {
			if (!Inside(size, next)) {
				continue;
			}
			auto &marked = result[Index(size, next)];
			if (!marked && inside(next.x(), next.y())) {
				marked = 1;
				pending.push_back(next);
			}
		}
	}
	return result;
}

// Shared by the compare and the containment: a chrome is applied only once
// its own gate accepted it, on the same corner and patch size.
[[nodiscard]] QString ChromeRefusal(
		const CornerChrome *chrome,
		PatchCorner corner,
		QSize size) {
	if (!chrome) {
		return QString();
	}
	if (!chrome->accepted()) {
		return u"chrome-refused: "_q
			+ (chrome->refusal.isEmpty()
				? u"the region was never accepted by GateCornerChrome"_q
				: chrome->refusal);
	}
	if (chrome->corner != corner || chrome->size != size) {
		return u"chrome-mismatch: the chrome was gated on a %1 %2 patch, "
			"this reading is of a %3 %4 patch"_q
			.arg(PatchCornerName(chrome->corner))
			.arg(SizeText(chrome->size))
			.arg(PatchCornerName(corner))
			.arg(SizeText(size));
	}
	return QString();
}

// One byte per pixel of |size|, set inside the chrome region; all unset
// without a chrome. Call it only after ChromeRefusal() returned nothing.
[[nodiscard]] std::vector<uchar> ChromeMask(
		const CornerChrome *chrome,
		QSize size) {
	auto result = std::vector<uchar>(Area(size), uchar(0));
	if (!chrome) {
		return result;
	}
	ForEachRegionPixel(chrome->region, [&](QPoint point) {
		if (Inside(size, point)) {
			result[Index(size, point)] = 1;
		}
	});
	return result;
}

} // namespace

QString PatchCornerName(PatchCorner corner) {
	switch (corner) {
	case PatchCorner::TopLeft: return u"TL"_q;
	case PatchCorner::TopRight: return u"TR"_q;
	case PatchCorner::BottomLeft: return u"BL"_q;
	case PatchCorner::BottomRight: return u"BR"_q;
	}
	Unexpected("Corner in Test::PatchCornerName.");
}

QPoint CornerMostPixel(QSize size, PatchCorner corner) {
	const auto right = size.width() - 1;
	const auto bottom = size.height() - 1;
	switch (corner) {
	case PatchCorner::TopLeft: return QPoint(0, 0);
	case PatchCorner::TopRight: return QPoint(right, 0);
	case PatchCorner::BottomLeft: return QPoint(0, bottom);
	case PatchCorner::BottomRight: return QPoint(right, bottom);
	}
	Unexpected("Corner in Test::CornerMostPixel.");
}

bool CornerBackground::background(QPoint point) const {
	return read()
		&& Inside(size, point)
		&& (int(mask.size()) == Area(size))
		&& (mask[Index(size, point)] != 0);
}

CornerBackground ReadCornerBackground(const CornerPatch &patch) {
	const auto &image = patch.image;
	const auto size = image.size();
	auto result = CornerBackground{
		.corner = patch.corner,
		.size = size,
		.media = patch.media,
	};
	if (image.isNull() || size.isEmpty()) {
		result.refusal = u"empty-patch: the %1 patch image is null or "
			"empty (%2)"_q
			.arg(PatchCornerName(patch.corner))
			.arg(SizeText(size));
		return result;
	}
	const auto at = CornerMostPixel(size, patch.corner);
	const auto color = image.pixelColor(at);
	result.at = at;
	result.color = color;
	if (!patch.media.isValid()) {
		result.refusal = u"media-missing: the %1 patch carries no valid "
			"media colour, so its corner-most pixel %2 (%3) could not be "
			"told from a media pixel"_q
			.arg(PatchCornerName(patch.corner))
			.arg(PointText(at))
			.arg(ColorHex(color));
		return result;
	}
	const auto toMedia = ChannelDelta(color, patch.media);
	if (toMedia < kCoverageMinContrast) {
		result.refusal = u"media-at-corner: the corner-most pixel %1 of "
			"the %2 patch is %3, within %4 of the media colour %5 (delta "
			"%6): a media pixel, a corner the media covers, or media too "
			"close to the background to tell"_q
			.arg(PointText(at))
			.arg(PatchCornerName(patch.corner))
			.arg(ColorHex(color))
			.arg(kCoverageMinContrast)
			.arg(ColorHex(patch.media))
			.arg(toMedia);
		return result;
	}
	const auto pure = [&](int x, int y) {
		return (ChannelDelta(image.pixelColor(x, y), color) == 0);
	};
	auto mask = FloodCornerComponent(size, at, pure);
	const auto pixels = int(std::count(begin(mask), end(mask), uchar(1)));
	const auto inner = CornerMostPixel(size, Opposite(patch.corner));
	if (mask[Index(size, inner)]) {
		result.refusal = u"background-reaches-inner-corner: the %1-pixel "
			"component of %2 from %3 reaches the opposite corner %4, so the "
			"%5 patch holds no media corner"_q
			.arg(pixels)
			.arg(ColorHex(color))
			.arg(PointText(at))
			.arg(PointText(inner))
			.arg(PatchCornerName(patch.corner));
		return result;
	}
	result.mask = std::move(mask);
	result.pixels = pixels;
	return result;
}

QString CornerBackgroundText(const CornerBackground &reading) {
	auto text = u"corner="_q
		+ PatchCornerName(reading.corner)
		+ u" size="_q
		+ SizeText(reading.size)
		+ u" at="_q
		+ (Inside(reading.size, reading.at)
			? PointText(reading.at)
			: u"none"_q)
		+ u" background="_q
		+ ColorOrNone(reading.color)
		+ u" media="_q
		+ ColorOrNone(reading.media)
		+ u" pixels="_q
		+ (reading.read() ? QString::number(reading.pixels) : u"none"_q);
	if (!reading.refusal.isEmpty()) {
		text += u" refusal="_q + reading.refusal;
	}
	return text;
}

CornerCoverage ReadCornerCoverage(
		const QImage &image,
		QColor background,
		QColor media) {
	const auto size = image.size();
	auto result = CornerCoverage{
		.size = size,
		.background = background,
		.media = media,
	};
	if (image.isNull() || size.isEmpty()) {
		result.refusal = u"empty-patch: the image is null or empty (%1)"_q
			.arg(SizeText(size));
		return result;
	}
	if (!background.isValid()) {
		result.refusal = u"background-missing: no valid background colour "
			"to read the media share over"_q;
		return result;
	}
	if (!media.isValid()) {
		result.refusal = u"media-missing: no valid media colour to read "
			"the share of"_q;
		return result;
	}
	for (auto channel = 0; channel != 3; ++channel) {
		const auto contrast = ChannelValue(media, channel)
			- ChannelValue(background, channel);
		if (result.channel < 0
			|| std::abs(contrast) > std::abs(result.contrast)) {
			result.channel = channel;
			result.contrast = contrast;
		}
	}
	if (std::abs(result.contrast) < kCoverageMinContrast) {
		result.refusal = u"contrast-too-low: media %1 over background %2 "
			"contrasts by %3 at most (channel %4), below %5"_q
			.arg(ColorHex(media))
			.arg(ColorHex(background))
			.arg(std::abs(result.contrast))
			.arg(ChannelName(result.channel))
			.arg(kCoverageMinContrast);
		return result;
	}
	const auto base = ChannelValue(background, result.channel);
	const auto contrast = double(result.contrast);
	result.values.reserve(Area(size));
	for (auto y = 0; y != size.height(); ++y) {
		for (auto x = 0; x != size.width(); ++x) {
			const auto value = ChannelValue(
				image.pixelColor(x, y),
				result.channel);
			result.values.push_back((value - base) / contrast);
		}
	}
	return result;
}

QString CornerCoverageText(const CornerCoverage &coverage) {
	const auto measured = (coverage.channel >= 0);
	auto text = u"size="_q
		+ SizeText(coverage.size)
		+ u" background="_q
		+ ColorOrNone(coverage.background)
		+ u" media="_q
		+ ColorOrNone(coverage.media)
		+ u" channel="_q
		+ ChannelName(coverage.channel)
		+ u" contrast="_q
		+ (measured ? QString::number(coverage.contrast) : u"none"_q)
		+ u" values="_q
		+ (coverage.read()
			? QString::number(int(coverage.values.size()))
			: u"none"_q);
	if (!coverage.refusal.isEmpty()) {
		text += u" refusal="_q + coverage.refusal;
	}
	return text;
}

CornerChrome GateCornerChrome(
		const CornerPatch &gate,
		const QRegion &region) {
	const auto size = gate.image.size();
	auto result = CornerChrome{
		.corner = gate.corner,
		.size = size,
		.region = region,
		.pixels = RegionPixels(region),
	};
	if (region.isEmpty()) {
		result.refusal = u"region-empty: no chrome region was given"_q;
		return result;
	}
	const auto outside = region.subtracted(QRegion(QRect(QPoint(), size)));
	if (!outside.isEmpty()) {
		result.refusal = u"region-outside-patch: %1 of the %2 pixels of "
			"the region %3 lie outside the %4 %5 gate patch"_q
			.arg(RegionPixels(outside))
			.arg(result.pixels)
			.arg(BoundsText(region.boundingRect()))
			.arg(PatchCornerName(gate.corner))
			.arg(SizeText(size));
		return result;
	}
	const auto background = ReadCornerBackground(gate);
	if (!background.read()) {
		result.refusal = u"gate-background: "_q + background.refusal;
		return result;
	}
	const auto coverage = ReadCornerCoverage(
		gate.image,
		background.color,
		gate.media);
	if (!coverage.read()) {
		result.refusal = u"gate-coverage: "_q + coverage.refusal;
		return result;
	}
	auto arcPixels = 0;
	auto backgroundPixels = 0;
	auto firstArc = QPoint(-1, -1);
	auto firstArcValue = 0.;
	auto firstBackground = QPoint(-1, -1);
	ForEachRegionPixel(region, [&](QPoint point) {
		if (background.background(point)) {
			if (!backgroundPixels) {
				firstBackground = point;
			}
			++backgroundPixels;
			return;
		}
		const auto value = coverage.values[Index(size, point)];
		if (std::abs(value - 1.) <= kCoverageSameKind) {
			return;
		}
		if (!arcPixels) {
			firstArc = point;
			firstArcValue = value;
		}
		++arcPixels;
	});
	result.arcPixels = arcPixels;
	result.backgroundPixels = backgroundPixels;
	if (arcPixels > 0) {
		result.refusal = u"chrome-touches-arc: %1 of the %2 region pixels "
			"are not full media on the gate patch (|coverage - 1| > %3), "
			"the first at %4 with coverage %5; %6 are background"_q
			.arg(arcPixels)
			.arg(result.pixels)
			.arg(ValueText(kCoverageSameKind))
			.arg(PointText(firstArc))
			.arg(ValueText(firstArcValue))
			.arg(backgroundPixels);
	} else if (backgroundPixels > 0) {
		result.refusal = u"chrome-touches-background: %1 of the %2 region "
			"pixels are in the gate's background mask, the first at %3"_q
			.arg(backgroundPixels)
			.arg(result.pixels)
			.arg(PointText(firstBackground));
	}
	return result;
}

QString CornerChromeText(const CornerChrome &chrome) {
	auto text = u"corner="_q
		+ PatchCornerName(chrome.corner)
		+ u" size="_q
		+ SizeText(chrome.size)
		+ u" region="_q
		+ (chrome.region.isEmpty()
			? u"none"_q
			: BoundsText(chrome.region.boundingRect()))
		+ u" rects="_q
		+ QString::number(chrome.region.rectCount())
		+ u" pixels="_q
		+ QString::number(chrome.pixels)
		+ u" arc="_q
		+ CountText(chrome.arcPixels)
		+ u" background="_q
		+ CountText(chrome.backgroundPixels);
	if (!chrome.refusal.isEmpty()) {
		text += u" refusal="_q + chrome.refusal;
	}
	return text;
}

CornerCoverageCompare CompareCornerCoverage(
		const CornerPatch &subject,
		const CornerPatch &reference,
		double tolerance,
		const CornerChrome *chrome) {
	const auto size = reference.image.size();
	auto result = CornerCoverageCompare{
		.corner = reference.corner,
		.tolerance = tolerance,
	};
	auto background = CornerBackground();
	auto referenceMap = CornerCoverage();
	auto subjectMap = CornerCoverage();
	const auto finish = [&](QString refusal) {
		result.colors = u"background="_q
			+ ColorOrNone(background.color)
			+ u" subjectMedia="_q
			+ MediaText(subject.media, subjectMap)
			+ u" referenceMedia="_q
			+ MediaText(reference.media, referenceMap);
		result.refusal = std::move(refusal);
		return result;
	};
	if (subject.corner != reference.corner) {
		return finish(u"corner-mismatch: the subject is a %1 patch, the "
			"reference a %2 patch"_q
			.arg(PatchCornerName(subject.corner))
			.arg(PatchCornerName(reference.corner)));
	}
	if (subject.image.size() != size) {
		return finish(u"size-mismatch: the subject is %1, the reference "
			"%2"_q
			.arg(SizeText(subject.image.size()))
			.arg(SizeText(size)));
	}
	background = ReadCornerBackground(reference);
	if (!background.read()) {
		return finish(u"reference: "_q + background.refusal);
	}
	const auto chromeRefusal = ChromeRefusal(chrome, reference.corner, size);
	if (!chromeRefusal.isEmpty()) {
		return finish(chromeRefusal);
	}
	referenceMap = ReadCornerCoverage(
		reference.image,
		background.color,
		reference.media);
	if (!referenceMap.read()) {
		return finish(u"reference: "_q + referenceMap.refusal);
	}
	subjectMap = ReadCornerCoverage(
		subject.image,
		background.color,
		subject.media);
	if (!subjectMap.read()) {
		return finish(u"subject: "_q + subjectMap.refusal);
	}
	const auto corner = subject.image.pixelColor(background.at);
	const auto toMedia = ChannelDelta(corner, subject.media);
	if (ChannelDelta(corner, background.color) == 0) {
		result.subjectCorner = u"background"_q;
	} else if (toMedia < kCoverageMinContrast) {
		result.subjectCorner = u"media"_q;
	} else {
		return finish(u"subject-background-differs: the subject's "
			"corner-most pixel %1 is %2, neither the reference background "
			"%3 nor within %4 of the subject media %5 (delta %6), so the "
			"twins were not captured over one base"_q
			.arg(PointText(background.at))
			.arg(ColorHex(corner))
			.arg(ColorHex(background.color))
			.arg(kCoverageMinContrast)
			.arg(ColorHex(subject.media))
			.arg(toMedia));
	}
	const auto excluded = ChromeMask(chrome, size);
	result.over = 0;
	result.total = 0;
	result.excluded = 0;
	for (auto y = 0; y != size.height(); ++y) {
		for (auto x = 0; x != size.width(); ++x) {
			const auto point = QPoint(x, y);
			const auto index = Index(size, point);
			if (excluded[index]) {
				++result.excluded;
				continue;
			}
			const auto subjectValue = subjectMap.values[index];
			const auto referenceValue = referenceMap.values[index];
			const auto delta = std::abs(subjectValue - referenceValue);
			++result.total;
			if (delta > tolerance) {
				++result.over;
			}
			if (delta > result.maxDelta) {
				result.maxDelta = delta;
				result.worst = point;
				result.subjectAtWorst = subjectValue;
				result.referenceAtWorst = referenceValue;
			}
		}
	}
	return finish(QString());
}

QString CornerCoverageCompareText(const CornerCoverageCompare &compare) {
	const auto measured = (compare.maxDelta >= 0.);
	auto text = u"corner="_q
		+ PatchCornerName(compare.corner)
		+ u" tolerance="_q
		+ ValueText(compare.tolerance)
		+ u" maxDelta="_q
		+ (measured ? ValueText(compare.maxDelta) : u"none"_q)
		+ u" at "_q
		+ (measured ? PointText(compare.worst) : u"none"_q)
		+ u" subject="_q
		+ (measured ? ValueText(compare.subjectAtWorst) : u"none"_q)
		+ u" reference="_q
		+ (measured ? ValueText(compare.referenceAtWorst) : u"none"_q)
		+ u" over="_q
		+ CountText(compare.over)
		+ u"/"_q
		+ CountText(compare.total)
		+ u" excluded="_q
		+ CountText(compare.excluded)
		+ u" subjectCorner="_q
		+ (compare.subjectCorner.isEmpty()
			? u"none"_q
			: compare.subjectCorner);
	if (!compare.colors.isEmpty()) {
		text += u" "_q + compare.colors;
	}
	if (!compare.refusal.isEmpty()) {
		text += u" refusal="_q + compare.refusal;
	}
	return text;
}

CornerContainment ReadCornerContainment(
		const CornerPatch &reference,
		const QImage &subject,
		const QImage &secondReference,
		const CornerChrome *chrome) {
	const auto &image = reference.image;
	const auto size = image.size();
	const auto second = !secondReference.isNull();
	auto result = CornerContainment{ .corner = reference.corner };
	if (subject.size() != size) {
		result.refusal = u"size-mismatch: the subject is %1, the %2 "
			"reference %3"_q
			.arg(SizeText(subject.size()))
			.arg(PatchCornerName(reference.corner))
			.arg(SizeText(size));
		return result;
	}
	if (second && secondReference.size() != size) {
		result.refusal = u"second-reference-size-mismatch: the second "
			"reference is %1, the reference %2"_q
			.arg(SizeText(secondReference.size()))
			.arg(SizeText(size));
		return result;
	}
	const auto background = ReadCornerBackground(reference);
	result.background = background.color;
	if (!background.read()) {
		result.refusal = u"reference: "_q + background.refusal;
		return result;
	}
	const auto chromeRefusal = ChromeRefusal(chrome, reference.corner, size);
	if (!chromeRefusal.isEmpty()) {
		result.refusal = chromeRefusal;
		return result;
	}
	const auto color = background.color;
	const auto zeroCoverage = [&](int x, int y) {
		const auto value = image.pixelColor(x, y);
		return (ChannelDelta(value, color) == 0)
			|| (second
				&& (ChannelDelta(value, secondReference.pixelColor(x, y))
					== 0));
	};
	const auto mask = FloodCornerComponent(size, background.at, zeroCoverage);
	const auto excluded = ChromeMask(chrome, size);
	result.mask = 0;
	result.widened = 0;
	result.excluded = 0;
	result.changed = 0;
	result.worstDelta = 0;
	for (auto y = 0; y != size.height(); ++y) {
		for (auto x = 0; x != size.width(); ++x) {
			const auto point = QPoint(x, y);
			const auto index = Index(size, point);
			if (!mask[index]) {
				continue;
			}
			if (excluded[index]) {
				++result.excluded;
				continue;
			}
			++result.mask;
			const auto was = image.pixelColor(point);
			if (ChannelDelta(was, color) != 0) {
				++result.widened;
			}
			const auto now = subject.pixelColor(point);
			const auto delta = ChannelDelta(now, was);
			if (delta <= 0) {
				continue;
			}
			++result.changed;
			if (delta > result.worstDelta) {
				result.worstDelta = delta;
				result.worst = point;
			}
			if (result.first.size() < kFirstChanged) {
				result.first.push_back(u"%1 %2->%3"_q
					.arg(PointText(point))
					.arg(ColorHex(was))
					.arg(ColorHex(now)));
			}
		}
	}
	if (result.mask == 0) {
		result.refusal = u"empty-mask: the accepted chrome left none of the "
			"%1 mask pixels to judge"_q
			.arg(result.excluded);
	}
	return result;
}

QString CornerContainmentText(const CornerContainment &reading) {
	auto text = u"corner="_q
		+ PatchCornerName(reading.corner)
		+ u" background="_q
		+ ColorOrNone(reading.background)
		+ u" mask="_q
		+ CountText(reading.mask)
		+ u" widened="_q
		+ CountText(reading.widened)
		+ u" excluded="_q
		+ CountText(reading.excluded)
		+ u" changed="_q
		+ CountText(reading.changed)
		+ u" worstDelta="_q
		+ CountText(reading.worstDelta)
		+ u" at "_q
		+ ((reading.worst.x() < 0) ? u"none"_q : PointText(reading.worst))
		+ u" first=["_q
		+ reading.first.join(u", "_q)
		+ u"]"_q;
	if (!reading.refusal.isEmpty()) {
		text += u" refusal="_q + reading.refusal;
	}
	return text;
}

namespace {

// The source overlay's patch side: R_L + 2 logical px.
constexpr auto kPatchMargin = 2;
constexpr auto kSheetZoom = 8;

constexpr auto kCorners = std::array{
	PatchCorner::TopLeft,
	PatchCorner::TopRight,
	PatchCorner::BottomLeft,
	PatchCorner::BottomRight,
};

// One synthetic media on a background canvas, all in device pixels at
// DPR 1. The media image - |fill|, with |glyph| (media-local) filled with
// |glyphColor| - is rounded at |rounded| by Images::Round with the
// product's Images::CornersMask(|radius|) and drawn with SourceOver at
// |media|, over an optional |shadow| of the same shape drawn |shadowShift|
// lower.
struct MediaCanvas {
	QSize canvas;
	QRect media;
	QColor background;
	QColor fill;
	int radius = 0;
	RectParts rounded = RectPart::AllCorners;
	QRect glyph;
	QColor glyphColor;
	QColor shadow;
	QPoint shadowShift;
};

// |rl| and |rs| are logical px, |side| and |media| device px.
struct Geometry {
	int dpr = 0;
	int rl = 0;
	int rs = 0;
	int side = 0;
	QRect media;
	QSize canvas;
};

// Every patch is |side| x |side|. |large| holds the four corners of one
// Large media in PatchCorner order; the regions are in top-right patch
// coordinates; |shadowPixel| is the first pixel of the shadow colour in
// both shadow references, which |shadowSubject| sets to the background.
struct State {
	Geometry geometry;
	std::array<CornerPatch, 4> large;
	CornerPatch largeOtherMedia;
	CornerPatch smallArc;
	CornerPatch squareCorner;
	CornerPatch shiftedArc;
	CornerPatch glyph;
	QRect glyphRect;
	QImage beyondArc;
	CornerPatch shadowA;
	CornerPatch shadowB;
	QImage shadowSubject;
	QPoint shadowPixel = QPoint(-1, -1);
	int shadowPixels = 0;
	QRegion touching;
	QRegion clear;
	QPoint chromeGlyph = QPoint(-1, -1);
	CornerPatch chromeSubject;
	QString fixtureGate;
};

[[nodiscard]] QColor BackgroundColor() {
	return QColor(0xff, 0xff, 0xff);
}

[[nodiscard]] QColor MediaColorA() {
	return QColor(0xe0, 0x18, 0x9c);
}

[[nodiscard]] QColor MediaColorB() {
	return QColor(0x27, 0x92, 0x27);
}

[[nodiscard]] QColor LowContrastColor() {
	return QColor(0xf4, 0xf4, 0xf4);
}

[[nodiscard]] QColor ShadowColor() {
	return QColor(0xc8, 0xc8, 0xc8);
}

[[nodiscard]] QColor BadgeColor() {
	return QColor(0x30, 0x30, 0x30);
}

[[nodiscard]] bool IsLeft(PatchCorner corner) {
	return (corner == PatchCorner::TopLeft)
		|| (corner == PatchCorner::BottomLeft);
}

[[nodiscard]] bool IsTop(PatchCorner corner) {
	return (corner == PatchCorner::TopLeft)
		|| (corner == PatchCorner::TopRight);
}

[[nodiscard]] const CornerPatch &LargePatch(
		const State &state,
		PatchCorner corner) {
	return state.large[int(corner)];
}

[[nodiscard]] Geometry ComputeGeometry() {
	const auto dpr = style::DevicePixelRatio();
	const auto rl = Ui::BubbleRadiusLarge();
	const auto side = (rl + style::ConvertScale(kPatchMargin)) * dpr;
	return {
		.dpr = dpr,
		.rl = rl,
		.rs = Ui::BubbleRadiusSmall(),
		.side = side,
		.media = QRect(side, side, 3 * side, 3 * side),
		.canvas = QSize(5 * side, 5 * side),
	};
}

[[nodiscard]] QImage PaintMediaCanvas(const MediaCanvas &spec) {
	const auto masks = Images::CornersMask(spec.radius);
	const auto rounded = [&](QColor fill, QRect glyph, QColor glyphColor) {
		auto image = QImage(
			spec.media.size(),
			QImage::Format_ARGB32_Premultiplied);
		image.fill(fill);
		if (!glyph.isEmpty()) {
			auto painter = QPainter(&image);
			painter.fillRect(glyph, glyphColor);
		}
		return Images::Round(std::move(image), masks, spec.rounded);
	};
	auto result = QImage(spec.canvas, QImage::Format_ARGB32_Premultiplied);
	result.fill(spec.background);
	auto painter = QPainter(&result);
	if (spec.shadow.isValid()) {
		painter.drawImage(
			spec.media.topLeft() + spec.shadowShift,
			rounded(spec.shadow, QRect(), QColor()));
	}
	painter.drawImage(
		spec.media.topLeft(),
		rounded(spec.fill, spec.glyph, spec.glyphColor));
	painter.end();
	return result;
}

// The |side| square at the |corner| of |media|, inside its bounding box.
[[nodiscard]] QImage CropCorner(
		const QImage &canvas,
		QRect media,
		PatchCorner corner,
		int side) {
	const auto left = IsLeft(corner)
		? media.x()
		: (media.x() + media.width() - side);
	const auto top = IsTop(corner)
		? media.y()
		: (media.y() + media.height() - side);
	return Crop(canvas, QRect(left, top, side, side));
}

// |region| is designed in top-left patch coordinates.
[[nodiscard]] QRegion MirrorRegion(
		const QRegion &region,
		QSize size,
		PatchCorner corner) {
	auto result = QRegion();
	for (const auto &rect : region) {
		const auto left = IsLeft(corner)
			? rect.x()
			: (size.width() - rect.x() - rect.width());
		const auto top = IsTop(corner)
			? rect.y()
			: (size.height() - rect.y() - rect.height());
		result += QRect(QPoint(left, top), rect.size());
	}
	return result;
}

// The pixels exactly the shadow colour in both shadow references are the
// shared zero-coverage pixels stage 9 needs; the first in row order is the
// one its subject changes.
void FindSharedShadow(not_null<State*> state) {
	const auto &a = state->shadowA.image;
	const auto &b = state->shadowB.image;
	state->shadowPixels = 0;
	state->shadowPixel = QPoint(-1, -1);
	if (a.isNull() || a.size() != b.size()) {
		return;
	}
	const auto shadow = ShadowColor();
	for (auto y = 0; y != a.height(); ++y) {
		for (auto x = 0; x != a.width(); ++x) {
			if (ChannelDelta(a.pixelColor(x, y), shadow) != 0
				|| ChannelDelta(b.pixelColor(x, y), shadow) != 0) {
				continue;
			}
			if (!state->shadowPixels) {
				state->shadowPixel = QPoint(x, y);
			}
			++state->shadowPixels;
		}
	}
}

void PaintRegions(not_null<State*> state) {
	const auto &geometry = state->geometry;
	const auto dpr = geometry.dpr;
	const auto side = geometry.side;
	const auto size = QSize(side, side);
	const auto radius = geometry.rl * dpr;
	const auto diagonal = int(std::round(
		radius * (1. - 1. / std::sqrt(2.))));
	const auto band = QRect(
		radius / 2,
		radius,
		side - radius / 2,
		side - radius);
	const auto inner = QRect(
		radius - 3 * dpr,
		radius - 3 * dpr,
		2 * dpr,
		2 * dpr);
	state->touching = MirrorRegion(
		QRegion(QRect(diagonal - 1, diagonal - 1, 3, 3)),
		size,
		PatchCorner::TopRight);
	state->clear = MirrorRegion(
		QRegion(band) + QRegion(inner),
		size,
		PatchCorner::TopRight);
	state->chromeGlyph = MirrorRegion(
		QRegion(band),
		size,
		PatchCorner::TopRight).boundingRect().center();

	auto chrome = LargePatch(*state, PatchCorner::TopRight).image;
	if (!chrome.isNull()) {
		auto painter = QPainter(&chrome);
		for (const auto &rect : state->clear) {
			painter.fillRect(rect, BadgeColor());
		}
		painter.end();
		chrome.setPixelColor(state->chromeGlyph, BackgroundColor());
	}
	state->chromeSubject = CornerPatch{
		.image = chrome,
		.corner = PatchCorner::TopRight,
		.media = MediaColorA(),
	};
}

void PaintPatches(not_null<State*> state) {
	state->geometry = ComputeGeometry();
	const auto &geometry = state->geometry;
	const auto dpr = geometry.dpr;
	const auto side = geometry.side;
	const auto patch = [&](
			const QImage &canvas,
			PatchCorner corner,
			QColor media) {
		return CornerPatch{
			.image = CropCorner(canvas, geometry.media, corner, side),
			.corner = corner,
			.media = media,
		};
	};
	const auto base = MediaCanvas{
		.canvas = geometry.canvas,
		.media = geometry.media,
		.background = BackgroundColor(),
		.fill = MediaColorA(),
		.radius = geometry.rl,
	};

	const auto large = PaintMediaCanvas(base);
	for (const auto corner : kCorners) {
		state->large[int(corner)] = patch(large, corner, MediaColorA());
	}

	auto otherMedia = base;
	otherMedia.fill = MediaColorB();
	state->largeOtherMedia = patch(
		PaintMediaCanvas(otherMedia),
		PatchCorner::BottomRight,
		MediaColorB());

	auto smallArc = base;
	smallArc.radius = geometry.rs;
	state->smallArc = patch(
		PaintMediaCanvas(smallArc),
		PatchCorner::TopLeft,
		MediaColorA());

	auto squareCorner = base;
	squareCorner.rounded = RectPart::TopRight
		| RectPart::BottomLeft
		| RectPart::BottomRight;
	state->squareCorner = patch(
		PaintMediaCanvas(squareCorner),
		PatchCorner::TopLeft,
		MediaColorA());

	// Cropped at the unshifted media corner, so only the arc moves.
	auto shiftedArc = base;
	shiftedArc.media = geometry.media.translated(1, 0);
	state->shiftedArc = patch(
		PaintMediaCanvas(shiftedArc),
		PatchCorner::TopLeft,
		MediaColorA());

	// The top-left patch starts at the media's top-left, so media-local
	// and patch coordinates agree.
	auto glyph = base;
	glyph.glyph = QRect(side - 3 * dpr, side - 3 * dpr, 2 * dpr, 2 * dpr);
	glyph.glyphColor = BackgroundColor();
	state->glyph = patch(
		PaintMediaCanvas(glyph),
		PatchCorner::TopLeft,
		MediaColorA());
	state->glyphRect = glyph.glyph;

	const auto &topLeft = LargePatch(*state, PatchCorner::TopLeft).image;
	state->beyondArc = topLeft;
	if (!topLeft.isNull()) {
		state->beyondArc.setPixelColor(
			CornerMostPixel(topLeft.size(), PatchCorner::TopLeft),
			MediaColorA());
	}

	auto shadowA = base;
	shadowA.shadow = ShadowColor();
	shadowA.shadowShift = QPoint(0, 2 * dpr);
	state->shadowA = patch(
		PaintMediaCanvas(shadowA),
		PatchCorner::BottomRight,
		MediaColorA());
	auto shadowB = shadowA;
	shadowB.fill = MediaColorB();
	state->shadowB = patch(
		PaintMediaCanvas(shadowB),
		PatchCorner::BottomRight,
		MediaColorB());
	FindSharedShadow(state);
	state->shadowSubject = state->shadowA.image;
	if (state->shadowPixels > 0) {
		state->shadowSubject.setPixelColor(
			state->shadowPixel,
			BackgroundColor());
	}

	PaintRegions(state);
}

[[nodiscard]] std::vector<std::pair<QString, QImage>> PatchImages(
		const State &state) {
	auto result = std::vector<std::pair<QString, QImage>>();
	for (const auto corner : kCorners) {
		result.emplace_back(
			u"large-"_q + PatchCornerName(corner),
			LargePatch(state, corner).image);
	}
	result.emplace_back(u"large-mediaB-BR"_q, state.largeOtherMedia.image);
	result.emplace_back(u"small-TL"_q, state.smallArc.image);
	result.emplace_back(u"square-TL"_q, state.squareCorner.image);
	result.emplace_back(u"shifted-TL"_q, state.shiftedArc.image);
	result.emplace_back(u"glyph-TL"_q, state.glyph.image);
	result.emplace_back(u"beyond-arc-TL"_q, state.beyondArc);
	result.emplace_back(u"shadow-mediaA-BR"_q, state.shadowA.image);
	result.emplace_back(u"shadow-mediaB-BR"_q, state.shadowB.image);
	result.emplace_back(u"shadow-subject-BR"_q, state.shadowSubject);
	result.emplace_back(u"chrome-subject-TR"_q, state.chromeSubject.image);
	return result;
}

void PrepareFixture(not_null<State*> state) {
	PaintPatches(state);
	const auto &geometry = state->geometry;
	const auto expected = QSize(geometry.side, geometry.side);
	const auto images = PatchImages(*state);
	auto wrong = QStringList();
	auto zoomed = std::vector<QImage>();
	for (const auto &[name, image] : images) {
		if (image.isNull() || image.size() != expected) {
			wrong.push_back(name + u"="_q + SizeText(image.size()));
		}
		zoomed.push_back(Zoom(image, kSheetZoom));
	}
	const auto ok = wrong.isEmpty() && (state->shadowPixels > 0);
	const auto details = QStringList{
		u"R_L=%1 R_S=%2 (logical px) dpr=%3 side=%4 (device px)"_q
			.arg(geometry.rl)
			.arg(geometry.rs)
			.arg(geometry.dpr)
			.arg(geometry.side),
		u"background=%1 mediaA=%2 mediaB=%3 shadow=%4 badge=%5"_q
			.arg(ColorHex(BackgroundColor()))
			.arg(ColorHex(MediaColorA()))
			.arg(ColorHex(MediaColorB()))
			.arg(ColorHex(ShadowColor()))
			.arg(ColorHex(BadgeColor())),
		u"shadowPixels=%1 shadowPixel=%2 patches=%3 wrong=[%4]"_q
			.arg(state->shadowPixels)
			.arg(PointText(state->shadowPixel))
			.arg(int(images.size()))
			.arg(wrong.join(u", "_q)),
	}.join(u" "_q);
	Check(
		ok,
		u"corner-patch self-test: fixture gate: every synthetic corner "
		"patch was painted with Images::CornersMask at the bubble radii"_q,
		details);
	if (!ok) {
		state->fixtureGate = u"the fixture gate failed: "_q + details;
	}
	SaveImage(ContactSheet(zoomed), u"corner_patch_self_test"_q);
}

[[nodiscard]] QString ArcRadiiSkipReason(const State &state) {
	if (!state.fixtureGate.isEmpty()) {
		return state.fixtureGate;
	}
	const auto &geometry = state.geometry;
	return (geometry.rl == geometry.rs)
		? u"the use-small-msg-bubble-radius option makes "
			"BubbleRadiusLarge() equal BubbleRadiusSmall() (%1 px), so "
			"no Small arc differs from the Large one"_q.arg(geometry.rl)
		: QString();
}

void CheckArcRadii(const State &state) {
	const auto compare = CompareCornerCoverage(
		state.smallArc,
		LargePatch(state, PatchCorner::TopLeft),
		kCoverageSameKind);
	Check(
		compare.refusal.isEmpty() && !compare.same(),
		u"corner-patch self-test: a Large arc and a Small arc are told "
		"apart by coverage"_q,
		u"subject=small-TL reference=large-TL "_q
			+ CornerCoverageCompareText(compare));
}

void CheckSquareAndShift(const State &state) {
	const auto &reference = LargePatch(state, PatchCorner::TopLeft);
	const auto square = CompareCornerCoverage(
		state.squareCorner,
		reference,
		kCoverageSameKind);
	Check(
		!square.same()
			&& (square.subjectCorner == u"media"_q)
			&& (square.maxDelta >= 0.9),
		u"corner-patch self-test: a square corner and a rounded corner "
		"are told apart by coverage"_q,
		u"subject=square-TL reference=large-TL "_q
			+ CornerCoverageCompareText(square));
	const auto shifted = CompareCornerCoverage(
		state.shiftedArc,
		reference,
		kCoverageSameKind);
	Check(
		shifted.refusal.isEmpty() && !shifted.same(),
		u"corner-patch self-test: an arc shifted by one device pixel is "
		"told apart from the original"_q,
		u"subject=shifted-TL reference=large-TL "_q
			+ CornerCoverageCompareText(shifted));
}

void CheckColorTwins(const State &state) {
	const auto compare = CompareCornerCoverage(
		state.largeOtherMedia,
		LargePatch(state, PatchCorner::BottomRight),
		kCoverageSameKind);
	Check(
		compare.same(),
		u"corner-patch self-test: twins with the same arc and different "
		"media colours compare equal by coverage"_q,
		u"subject=large-mediaB-BR reference=large-BR "_q
			+ CornerCoverageCompareText(compare));
}

void CheckCornerBackgrounds(const State &state) {
	for (const auto corner : kCorners) {
		const auto &patch = LargePatch(state, corner);
		const auto expected = CornerMostPixel(patch.image.size(), corner);
		const auto reading = ReadCornerBackground(patch);
		Check(
			reading.read()
				&& (reading.at == expected)
				&& (ChannelDelta(reading.color, BackgroundColor()) == 0)
				&& (reading.pixels > 0),
			u"corner-patch self-test: "_q
				+ PatchCornerName(corner)
				+ u" patch reads its background at its own corner-most "
				"pixel"_q,
			CornerBackgroundText(reading)
				+ u" expectedAt="_q
				+ PointText(expected)
				+ u" expectedBackground="_q
				+ ColorHex(BackgroundColor()));
	}
}

void CheckRefusals(const State &state) {
	const auto square = ReadCornerBackground(state.squareCorner);
	Check(
		square.refusal.startsWith(u"media-at-corner"_q),
		u"corner-patch self-test: a patch whose corner-most pixel is "
		"media is refused by name"_q,
		u"patch=square-TL "_q + CornerBackgroundText(square));
	const auto coverage = ReadCornerCoverage(
		LargePatch(state, PatchCorner::TopLeft).image,
		BackgroundColor(),
		LowContrastColor());
	Check(
		coverage.refusal.startsWith(u"contrast-too-low"_q)
			&& coverage.values.empty(),
		u"corner-patch self-test: a media colour too close to the "
		"background is refused, not measured"_q,
		u"patch=large-TL "_q + CornerCoverageText(coverage));
}

void CheckEnclosedGlyph(const State &state) {
	const auto &plainPatch = LargePatch(state, PatchCorner::TopLeft);
	const auto glyph = ReadCornerBackground(state.glyph);
	const auto plain = ReadCornerBackground(plainPatch);
	auto coloured = 0;
	auto inMask = 0;
	ForEachRegionPixel(QRegion(state.glyphRect), [&](QPoint point) {
		const auto color = state.glyph.image.pixelColor(point);
		if (ChannelDelta(color, BackgroundColor()) == 0) {
			++coloured;
		}
		if (glyph.background(point)) {
			++inMask;
		}
	});
	const auto area = RegionPixels(QRegion(state.glyphRect));
	Check(
		glyph.read()
			&& plain.read()
			&& (area > 0)
			&& (coloured == area)
			&& (inMask == 0)
			&& (glyph.pixels == plain.pixels),
		u"corner-patch self-test: a white glyph inside the media is not "
		"counted as background"_q,
		u"glyph{"_q
			+ CornerBackgroundText(glyph)
			+ u"} plain{"_q
			+ CornerBackgroundText(plain)
			+ u"} glyphRect=%1 glyphPixels=%2 backgroundColoured=%3 "
			"inMask=%4"_q
				.arg(BoundsText(state.glyphRect))
				.arg(area)
				.arg(coloured)
				.arg(inMask));
	const auto ignored = ReadCornerContainment(
		state.glyph,
		plainPatch.image);
	Check(
		ignored.contained(),
		u"corner-patch self-test: containment ignores a "
		"background-coloured glyph the subject does not repeat"_q,
		u"reference=glyph-TL subject=large-TL "_q
			+ CornerContainmentText(ignored));
	const auto caught = ReadCornerContainment(plainPatch, state.beyondArc);
	Check(
		!caught.contained() && (caught.changed >= 1),
		u"corner-patch self-test: containment catches a media pixel "
		"beyond the arc"_q,
		u"reference=large-TL subject=beyond-arc-TL "_q
			+ CornerContainmentText(caught));
}

void CheckChromeGate(const State &state) {
	const auto &gate = LargePatch(state, PatchCorner::TopRight);
	const auto touching = GateCornerChrome(gate, state.touching);
	Check(
		touching.refusal.startsWith(u"chrome-touches-arc"_q),
		u"corner-patch self-test: a chrome region touching the arc is "
		"refused by its gate"_q,
		u"gate=large-TR "_q + CornerChromeText(touching));
	const auto clear = GateCornerChrome(gate, state.clear);
	Check(
		clear.accepted()
			&& (clear.arcPixels == 0)
			&& (clear.backgroundPixels == 0),
		u"corner-patch self-test: a chrome region clear of the arc and "
		"the background is accepted"_q,
		u"gate=large-TR "_q + CornerChromeText(clear));
	const auto with = CompareCornerCoverage(
		state.chromeSubject,
		gate,
		kCoverageSameKind,
		&clear);
	const auto without = CompareCornerCoverage(
		state.chromeSubject,
		gate,
		kCoverageSameKind);
	Check(
		with.same() && without.refusal.isEmpty() && !without.same(),
		u"corner-patch self-test: the accepted chrome is left out of the "
		"coverage compare, which differs without it"_q,
		u"subject=chrome-subject-TR reference=large-TR badge="_q
			+ ColorHex(BadgeColor())
			+ u" glyph="_q
			+ PointText(state.chromeGlyph)
			+ u" with{"_q
			+ CornerCoverageCompareText(with)
			+ u"} without{"_q
			+ CornerCoverageCompareText(without)
			+ u"}"_q);
}

void CheckSecondReference(const State &state) {
	const auto with = ReadCornerContainment(
		state.shadowA,
		state.shadowSubject,
		state.shadowB.image);
	const auto without = ReadCornerContainment(
		state.shadowA,
		state.shadowSubject);
	Check(
		with.refusal.isEmpty()
			&& (with.widened > 0)
			&& (with.changed >= 1)
			&& without.contained(),
		u"corner-patch self-test: a second reference of another media "
		"colour widens the mask by the shared shadow and catches a "
		"changed shadow pixel"_q,
		u"reference=shadow-mediaA-BR second=shadow-mediaB-BR "
		"subject=shadow-subject-BR shadowPixel="_q
			+ PointText(state.shadowPixel)
			+ u" with{"_q
			+ CornerContainmentText(with)
			+ u"} without{"_q
			+ CornerContainmentText(without)
			+ u"}"_q);
}

} // namespace

void AppendCornerPatchSelfTest(not_null<Runner*> runner) {
	const auto state = std::make_shared<State>();
	const auto fixtureGate = [=] {
		return state->fixtureGate;
	};

	runner->add({
		.name = u"corner-patch self-test: synthetic patches painted with "
			"the product's corner rounding"_q,
		.run = [=] {
			PrepareFixture(state.get());
		},
	});
	runner->add({
		.name = u"corner-patch self-test: a Large arc and a Small arc are "
			"told apart"_q,
		.skipReason = [=] {
			return ArcRadiiSkipReason(*state);
		},
		.run = [=] {
			CheckArcRadii(*state);
		},
	});
	runner->add({
		.name = u"corner-patch self-test: a square corner and a shifted "
			"arc are told apart"_q,
		.skipReason = fixtureGate,
		.run = [=] {
			CheckSquareAndShift(*state);
		},
	});
	runner->add({
		.name = u"corner-patch self-test: twins with the same arc and "
			"different media colours compare equal"_q,
		.skipReason = fixtureGate,
		.run = [=] {
			CheckColorTwins(*state);
		},
	});
	runner->add({
		.name = u"corner-patch self-test: each corner reads its "
			"background at its own corner-most pixel"_q,
		.skipReason = fixtureGate,
		.run = [=] {
			CheckCornerBackgrounds(*state);
		},
	});
	runner->add({
		.name = u"corner-patch self-test: readings that cannot be made "
			"are refused by name"_q,
		.skipReason = fixtureGate,
		.run = [=] {
			CheckRefusals(*state);
		},
	});
	runner->add({
		.name = u"corner-patch self-test: a background-coloured glyph "
			"inside the media is not background"_q,
		.skipReason = fixtureGate,
		.run = [=] {
			CheckEnclosedGlyph(*state);
		},
	});
	runner->add({
		.name = u"corner-patch self-test: chrome exclusion is gated"_q,
		.skipReason = fixtureGate,
		.run = [=] {
			CheckChromeGate(*state);
		},
	});
	runner->add({
		.name = u"corner-patch self-test: a second reference widens the "
			"background mask by shared zero-coverage pixels"_q,
		.skipReason = fixtureGate,
		.run = [=] {
			CheckSecondReference(*state);
		},
	});
}

} // namespace Test

#endif // _DEBUG
