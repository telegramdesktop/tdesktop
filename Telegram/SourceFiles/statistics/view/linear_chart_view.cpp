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

void PaintChartLine(
		QPainter &p,
		int lineIndex,
		const Data::StatisticalChart &chartData,
		const Limits &xIndices,
		const Limits &xPercentageLimits,
		const Limits &heightLimits,
		const QSize &size) {
	const auto &line = chartData.lines[lineIndex];

	auto chartPoints = QPolygonF();

	const auto localStart = std::max(0, int(xIndices.min));
	const auto localEnd = std::min(
		int(chartData.xPercentage.size() - 1),
		int(xIndices.max));

	for (auto i = localStart; i <= localEnd; i++) {
		if (line.y[i] < 0) {
			continue;
		}
		const auto xPoint = size.width()
			* ((chartData.xPercentage[i] - xPercentageLimits.min)
				/ (xPercentageLimits.max - xPercentageLimits.min));
		const auto yPercentage = (line.y[i] - heightLimits.min)
			/ float64(heightLimits.max - heightLimits.min);
		const auto yPoint = (1. - yPercentage) * size.height();
		chartPoints << QPointF(xPoint, yPoint);
	}
	p.setPen(QPen(line.color, st::statisticsChartLineWidth));
	p.setBrush(Qt::NoBrush);
	p.drawPolyline(chartPoints);
}

} // namespace

LinearChartView::LinearChartView() = default;

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
				rect.size());
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
		int selectedXIndex) {
	if (selectedXIndex < 0) {
		return;
	}
	p.setBrush(st::boxBg);
	const auto r = st::statisticsDetailsDotRadius;
	const auto i = selectedXIndex;
	const auto isSameToken = (_selectedPoints.lastXIndex == selectedXIndex)
		&& (_selectedPoints.lastHeightLimits.min == heightLimits.min)
		&& (_selectedPoints.lastHeightLimits.max == heightLimits.max);
	for (const auto &line : chartData.lines) {
		const auto lineAlpha = alpha(line.id);
		const auto useCache = isSameToken
			|| (lineAlpha < 1. && !isEnabled(line.id));
		if (!useCache) {
			// Calculate.
			const auto xPoint = rect.width()
				* ((chartData.xPercentage[i] - xPercentageLimits.min)
					/ (xPercentageLimits.max - xPercentageLimits.min));
			const auto yPercentage = (line.y[i] - heightLimits.min)
				/ float64(heightLimits.max - heightLimits.min);
			const auto yPoint = (1. - yPercentage) * rect.height();
			_selectedPoints.points[line.id] = QPointF(xPoint, yPoint)
				+ rect.topLeft();
		}

		// Paint.
		auto o = ScopedPainterOpacity(p, lineAlpha * p.opacity());
		p.setPen(QPen(line.color, st::statisticsChartLineWidth));
		p.drawEllipse(_selectedPoints.points[line.id], r, r);
	}
	_selectedPoints.lastXIndex = selectedXIndex;
	_selectedPoints.lastHeightLimits = heightLimits;
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
	auto minValue = std::numeric_limits<int>::max();
	auto maxValue = 0;

	auto minValueFull = std::numeric_limits<int>::max();
	auto maxValueFull = 0;
	for (auto &l : chartData.lines) {
		if (!isEnabled(l.id)) {
			continue;
		}
		const auto lineMax = l.segmentTree.rMaxQ(xIndices.min, xIndices.max);
		const auto lineMin = l.segmentTree.rMinQ(xIndices.min, xIndices.max);
		maxValue = std::max(lineMax, maxValue);
		minValue = std::min(lineMin, minValue);

		maxValueFull = std::max(l.maxValue, maxValueFull);
		minValueFull = std::min(l.minValue, minValueFull);
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
