/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/view/linear_chart_view.h"

#include "data/data_statistics_chart.h"
#include "statistics/chart_lines_filter_controller.h"
#include "statistics/statistics_common.h"
#include "ui/effects/animation_value_f.h"
#include "ui/painter.h"
#include "styles/style_boxes.h"
#include "styles/style_statistics.h"

namespace Statistic {
namespace {

void PaintChartLine(
		QPainter &p,
		int lineIndex,
		const PaintContext &c,
		const DoubleLineRatios &ratios) {
	const auto &line = c.chartData.lines[lineIndex];

	auto chartPoints = QPolygonF();

	constexpr auto kOffset = float64(2);
	const auto localStart = int(std::max(0., c.xIndices.min - kOffset));
	const auto localEnd = int(std::min(
		float64(c.chartData.xPercentage.size() - 1),
		c.xIndices.max + kOffset));

	const auto ratio = ratios.ratio(line.id);

	for (auto i = localStart; i <= localEnd; i++) {
		if (line.y[i] < 0) {
			continue;
		}
		const auto xPoint = c.rect.width()
			* ((c.chartData.xPercentage[i] - c.xPercentageLimits.min)
				/ (c.xPercentageLimits.max - c.xPercentageLimits.min));
		const auto yPercentage = (line.y[i] * ratio - c.heightLimits.min)
			/ float64(c.heightLimits.max - c.heightLimits.min);
		const auto yPoint = (1. - yPercentage) * c.rect.height();
		chartPoints << QPointF(xPoint, yPoint);
	}
	p.setPen(QPen(
		line.color,
		c.footer ? st::lineWidth : st::statisticsChartLineWidth));
	p.setBrush(Qt::NoBrush);
	p.drawPolyline(chartPoints);
}

} // namespace

LinearChartView::LinearChartView(bool isDouble)
: _cachedLineRatios(isDouble) {
}

LinearChartView::~LinearChartView() = default;

void LinearChartView::paint(QPainter &p, const PaintContext &c) {
	const auto cacheToken = LinearChartView::CacheToken(
		c.xIndices,
		c.xPercentageLimits,
		c.heightLimits,
		c.rect.size());

	const auto opacity = p.opacity();
	const auto linesFilter = linesFilterController();
	const auto imageSize = c.rect.size() * style::DevicePixelRatio();
	const auto cacheScale = 1. / style::DevicePixelRatio();
	auto &caches = (c.footer ? _footerCaches : _mainCaches);

	for (auto i = 0; i < c.chartData.lines.size(); i++) {
		const auto &line = c.chartData.lines[i];
		p.setOpacity(linesFilter->alpha(line.id));
		if (!p.opacity()) {
			continue;
		}

		auto &cache = caches[line.id];

		const auto isSameToken = (cache.lastToken == cacheToken);
		if ((isSameToken && cache.hq)
			|| (p.opacity() < 1. && !linesFilter->isEnabled(line.id))) {
			p.drawImage(c.rect.topLeft(), cache.image);
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

			PaintChartLine(imagePainter, i, c, _cachedLineRatios);
		}

		if (!isSameToken) {
			image = image.scaled(
				imageSize,
				Qt::IgnoreAspectRatio,
				Qt::FastTransformation);
		}
		p.drawImage(c.rect.topLeft(), image);
		cache.lastToken = cacheToken;
		cache.image = std::move(image);
	}
	p.setOpacity(opacity);
}

void LinearChartView::paintSelectedXIndex(
		QPainter &p,
		const PaintContext &c,
		int selectedXIndex,
		float64 progress) {
	if (selectedXIndex < 0) {
		return;
	}
	const auto linesFilter = linesFilterController();
	auto hq = PainterHighQualityEnabler(p);
	auto o = ScopedPainterOpacity(p, progress);
	p.setBrush(st::boxBg);
	const auto r = st::statisticsDetailsDotRadius;
	const auto i = selectedXIndex;
	const auto isSameToken = _selectedPoints.isSame(selectedXIndex, c);
	auto linePainted = false;
	for (const auto &line : c.chartData.lines) {
		const auto lineAlpha = linesFilter->alpha(line.id);
		const auto useCache = isSameToken
			|| (lineAlpha < 1. && !linesFilter->isEnabled(line.id));
		if (!useCache) {
			// Calculate.
			const auto r = _cachedLineRatios.ratio(line.id);
			const auto xPoint = c.rect.width()
				* ((c.chartData.xPercentage[i] - c.xPercentageLimits.min)
					/ (c.xPercentageLimits.max - c.xPercentageLimits.min));
			const auto yPercentage = (line.y[i] * r - c.heightLimits.min)
				/ float64(c.heightLimits.max - c.heightLimits.min);
			const auto yPoint = (1. - yPercentage) * c.rect.height();
			_selectedPoints.points[line.id] = QPointF(xPoint, yPoint)
				+ c.rect.topLeft();
		}

		if (!linePainted && lineAlpha) {
			[[maybe_unused]] const auto o = ScopedPainterOpacity(
				p,
				p.opacity() * progress * kRulerLineAlpha);
			const auto lineRect = QRectF(
				begin(_selectedPoints.points)->second.x()
					- (st::lineWidth / 2.),
				c.rect.y(),
				st::lineWidth,
				c.rect.height());
			p.fillRect(lineRect, st::boxTextFg);
			linePainted = true;
		}

		// Paint.
		auto o = ScopedPainterOpacity(p, lineAlpha * p.opacity());
		p.setPen(QPen(line.color, st::statisticsChartLineWidth));
		p.drawEllipse(_selectedPoints.points[line.id], r, r);
	}
	_selectedPoints.lastXIndex = selectedXIndex;
	_selectedPoints.lastHeightLimits = c.heightLimits;
	_selectedPoints.lastXLimits = c.xPercentageLimits;
}

int LinearChartView::findXIndexByPosition(
		const Data::StatisticalChart &chartData,
		const Limits &xPercentageLimits,
		const QRect &rect,
		float64 x) {
	if ((x < rect.x()) || (x > (rect.x() + rect.width()))) {
		return -1;
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
	const auto nearest = ((right) > (left)) ? (it - 1) : it;
	const auto resultXPercentageIt = ((*nearest) > xPercentageLimits.max)
		? (nearest - 1)
		: ((*nearest) < xPercentageLimits.min)
		? (nearest + 1)
		: nearest;
	return std::distance(begin(chartData.xPercentage), resultXPercentageIt);
}

AbstractChartView::HeightLimits LinearChartView::heightLimits(
		Data::StatisticalChart &chartData,
		Limits xIndices) {
	if (!_cachedLineRatios) {
		_cachedLineRatios.init(chartData);
	}

	return DefaultHeightLimits(
		_cachedLineRatios,
		linesFilterController(),
		chartData,
		xIndices);
}

} // namespace Statistic
