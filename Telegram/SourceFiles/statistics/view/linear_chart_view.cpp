/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/view/linear_chart_view.h"

#include "data/data_statistics.h"
#include "statistics/statistics_common.h"
#include "ui/effects/animation_value_f.h"
#include "ui/painter.h"
#include "styles/style_boxes.h"
#include "styles/style_statistics.h"

namespace Statistic {
namespace {

constexpr auto kAlphaDuration = float64(350);

float64 Ratio(const LinearChartView::CachedLineRatios &ratios, int id) {
	return (id == 1) ? ratios.first : ratios.second;
}

void PaintChartLine(
		QPainter &p,
		int lineIndex,
		const Data::StatisticalChart &chartData,
		const Limits &xIndices,
		const Limits &xPercentageLimits,
		const Limits &heightLimits,
		const QSize &size,
		const LinearChartView::CachedLineRatios &ratios) {
	const auto &line = chartData.lines[lineIndex];

	auto chartPoints = QPolygonF();

	const auto localStart = std::max(0, int(xIndices.min));
	const auto localEnd = std::min(
		int(chartData.xPercentage.size() - 1),
		int(xIndices.max));

	const auto ratio = Ratio(ratios, line.id);

	for (auto i = localStart; i <= localEnd; i++) {
		if (line.y[i] < 0) {
			continue;
		}
		const auto xPoint = size.width()
			* ((chartData.xPercentage[i] - xPercentageLimits.min)
				/ (xPercentageLimits.max - xPercentageLimits.min));
		const auto yPercentage = (line.y[i] * ratio - heightLimits.min)
			/ float64(heightLimits.max - heightLimits.min);
		const auto yPoint = (1. - yPercentage) * size.height();
		chartPoints << QPointF(xPoint, yPoint);
	}
	p.setPen(QPen(line.color, st::statisticsChartLineWidth));
	p.setBrush(Qt::NoBrush);
	p.drawPolyline(chartPoints);
}

} // namespace

LinearChartView::LinearChartView(bool isDouble)
: _cachedLineRatios(CachedLineRatios{ isDouble ? 0 : 1, isDouble ? 0 : 1 }) {
}

LinearChartView::~LinearChartView() = default;

void LinearChartView::paint(
		QPainter &p,
		const Data::StatisticalChart &chartData,
		const Limits &xIndices,
		const Limits &xPercentageLimits,
		const Limits &heightLimits,
		const QRect &rect,
		bool footer) {

	const auto cacheToken = LinearChartView::CacheToken(
		xIndices,
		xPercentageLimits,
		heightLimits,
		rect.size());

	const auto imageSize = rect.size() * style::DevicePixelRatio();
	const auto cacheScale = 1. / style::DevicePixelRatio();
	auto &caches = (footer ? _footerCaches : _mainCaches);

	for (auto i = 0; i < chartData.lines.size(); i++) {
		const auto &line = chartData.lines[i];
		p.setOpacity(alpha(line.id));
		if (!p.opacity()) {
			continue;
		}

		auto &cache = caches[line.id];

		const auto isSameToken = (cache.lastToken == cacheToken);
		if ((isSameToken && cache.hq)
			|| (p.opacity() < 1. && !isEnabled(line.id))) {
			p.drawImage(rect.topLeft(), cache.image);
			continue;
		}
		cache.hq = isSameToken;
		auto image = QImage();
		image = QImage(
			imageSize * (isSameToken ? 1. : cacheScale),
			QImage::Format_ARGB32_Premultiplied);
		image.setDevicePixelRatio(style::DevicePixelRatio());
		image.fill(Qt::transparent);
		{
			auto imagePainter = QPainter(&image);
			auto hq = PainterHighQualityEnabler(imagePainter);
			if (!isSameToken) {
				imagePainter.scale(cacheScale, cacheScale);
			}

			PaintChartLine(
				imagePainter,
				i,
				chartData,
				xIndices,
				xPercentageLimits,
				heightLimits,
				rect.size(),
				_cachedLineRatios);
		}

		if (!isSameToken) {
			image = image.scaled(
				imageSize,
				Qt::IgnoreAspectRatio,
				Qt::FastTransformation);
		}
		p.drawImage(rect.topLeft(), image);
		cache.lastToken = cacheToken;
		cache.image = std::move(image);
	}
}

void LinearChartView::paintSelectedXIndex(
		QPainter &p,
		const Data::StatisticalChart &chartData,
		const Limits &xPercentageLimits,
		const Limits &heightLimits,
		const QRect &rect,
		int selectedXIndex,
		float64 progress) {
	if (selectedXIndex < 0) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	auto o = ScopedPainterOpacity(p, progress);
	p.setBrush(st::boxBg);
	const auto r = st::statisticsDetailsDotRadius;
	const auto i = selectedXIndex;
	const auto isSameToken = (_selectedPoints.lastXIndex == selectedXIndex)
		&& (_selectedPoints.lastHeightLimits.min == heightLimits.min)
		&& (_selectedPoints.lastHeightLimits.max == heightLimits.max)
		&& (_selectedPoints.lastXLimits.min == xPercentageLimits.min)
		&& (_selectedPoints.lastXLimits.max == xPercentageLimits.max);
	auto linePainted = false;
	for (const auto &line : chartData.lines) {
		const auto lineAlpha = alpha(line.id);
		const auto useCache = isSameToken
			|| (lineAlpha < 1. && !isEnabled(line.id));
		if (!useCache) {
			// Calculate.
			const auto r = Ratio(_cachedLineRatios, line.id);
			const auto xPoint = rect.width()
				* ((chartData.xPercentage[i] - xPercentageLimits.min)
					/ (xPercentageLimits.max - xPercentageLimits.min));
			const auto yPercentage = (line.y[i] * r - heightLimits.min)
				/ float64(heightLimits.max - heightLimits.min);
			const auto yPoint = (1. - yPercentage) * rect.height();
			_selectedPoints.points[line.id] = QPointF(xPoint, yPoint)
				+ rect.topLeft();
		}

		if (!linePainted) {
			const auto lineRect = QRectF(
				rect.x()
					+ begin(_selectedPoints.points)->second.x()
					- (st::lineWidth / 2.),
				rect.y(),
				st::lineWidth,
				rect.height());
			p.fillRect(lineRect, st::windowSubTextFg);
			linePainted = true;
		}

		// Paint.
		auto o = ScopedPainterOpacity(p, lineAlpha * p.opacity());
		p.setPen(QPen(line.color, st::statisticsChartLineWidth));
		p.drawEllipse(_selectedPoints.points[line.id], r, r);
	}
	_selectedPoints.lastXIndex = selectedXIndex;
	_selectedPoints.lastHeightLimits = heightLimits;
	_selectedPoints.lastXLimits = xPercentageLimits;
}

int LinearChartView::findXIndexByPosition(
		const Data::StatisticalChart &chartData,
		const Limits &xPercentageLimits,
		const QRect &rect,
		float64 x) {
	if (x < rect.x()) {
		return 0;
	} else if (x > (rect.x() + rect.width())) {
		return chartData.xPercentage.size() - 1;
	}
	const auto pointerRatio = std::clamp(
		(x - rect.x()) / rect.width(),
		0.,
		1.);
	const auto rawXPercentage = anim::interpolateF(
		xPercentageLimits.min,
		xPercentageLimits.max,
		pointerRatio);
	const auto it = ranges::lower_bound(
		chartData.xPercentage,
		rawXPercentage);
	const auto left = rawXPercentage - (*(it - 1));
	const auto right = (*it) - rawXPercentage;
	const auto nearestXPercentageIt = ((right) > (left)) ? (it - 1) : it;
	return std::distance(
		begin(chartData.xPercentage),
		nearestXPercentageIt);
}

void LinearChartView::setEnabled(int id, bool enabled, crl::time now) {
	const auto it = _entries.find(id);
	if (it == end(_entries)) {
		_entries[id] = Entry{ .enabled = enabled, .startedAt = now };
	} else if (it->second.enabled != enabled) {
		auto &entry = it->second;
		entry.enabled = enabled;
		entry.startedAt = now
			- kAlphaDuration * (enabled ? entry.alpha : (1. - entry.alpha));
	}
	_isFinished = false;
}

bool LinearChartView::isFinished() const {
	return _isFinished;
}

bool LinearChartView::isEnabled(int id) const {
	const auto it = _entries.find(id);
	return (it == end(_entries)) ? true : it->second.enabled;
}

float64 LinearChartView::alpha(int id) const {
	const auto it = _entries.find(id);
	return (it == end(_entries)) ? 1. : it->second.alpha;
}

AbstractChartView::HeightLimits LinearChartView::heightLimits(
		Data::StatisticalChart &chartData,
		Limits xIndices) {
	if (!_cachedLineRatios.first) {
		// Double Linear calculation.
		if (chartData.lines.size() != 2) {
			_cachedLineRatios.first = 1.;
			_cachedLineRatios.second = 1.;
		} else {
			const auto firstMax = chartData.lines.front().maxValue;
			const auto secondMax = chartData.lines.back().maxValue;
			if (firstMax > secondMax) {
				_cachedLineRatios.first = 1.;
				_cachedLineRatios.second = firstMax / float64(secondMax);
			} else {
				_cachedLineRatios.first = secondMax / float64(firstMax);
				_cachedLineRatios.second = 1.;
			}
		}
	}

	auto minValue = std::numeric_limits<int>::max();
	auto maxValue = 0;

	auto minValueFull = std::numeric_limits<int>::max();
	auto maxValueFull = 0;
	for (auto &l : chartData.lines) {
		if (!isEnabled(l.id)) {
			continue;
		}
		const auto r = Ratio(_cachedLineRatios, l.id);
		const auto lineMax = l.segmentTree.rMaxQ(xIndices.min, xIndices.max);
		const auto lineMin = l.segmentTree.rMinQ(xIndices.min, xIndices.max);
		maxValue = std::max(int(lineMax * r), maxValue);
		minValue = std::min(int(lineMin * r), minValue);

		maxValueFull = std::max(int(l.maxValue * r), maxValueFull);
		minValueFull = std::min(int(l.minValue * r), minValueFull);
	}
	return {
		.full = Limits{ float64(minValueFull), float64(maxValueFull) },
		.ranged = Limits{ float64(minValue), float64(maxValue) },
	};
}

void LinearChartView::tick(crl::time now) {
	auto finishedCount = 0;
	auto idsToRemove = std::vector<int>();
	for (auto &[id, entry] : _entries) {
		if (!entry.startedAt) {
			continue;
		}
		const auto progress = (now - entry.startedAt) / kAlphaDuration;
		entry.alpha = std::clamp(
			entry.enabled ? progress : (1. - progress),
			0.,
			1.);
		if (entry.alpha == 1.) {
			idsToRemove.push_back(id);
		}
		if (progress >= 1.) {
			finishedCount++;
		}
	}
	_isFinished = (finishedCount == _entries.size());
	for (const auto &id : idsToRemove) {
		_entries.remove(id);
	}
}

} // namespace Statistic
