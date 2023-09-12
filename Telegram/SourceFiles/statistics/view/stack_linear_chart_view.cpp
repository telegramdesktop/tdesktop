/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/view/stack_linear_chart_view.h"

#include "ui/effects/animation_value_f.h"
#include "data/data_statistics.h"
#include "ui/painter.h"
#include "styles/style_statistics.h"

namespace Statistic {
namespace {

constexpr auto kAlphaDuration = float64(200);

struct LeftStartAndStep final {
	float64 start = 0.;
	float64 step = 0.;
};

[[nodiscard]] LeftStartAndStep ComputeLeftStartAndStep(
		const Data::StatisticalChart &chartData,
		const Limits &xPercentageLimits,
		const QRect &rect,
		float64 xIndexStart) {
	const auto fullWidth = rect.width()
		/ (xPercentageLimits.max - xPercentageLimits.min);
	const auto offset = fullWidth * xPercentageLimits.min;
	const auto p = (chartData.xPercentage.size() < 2)
		? 1.
		: chartData.xPercentage[1] * fullWidth;
	const auto w = chartData.xPercentage[1] * (fullWidth - p);
	const auto leftStart = rect.x()
		+ chartData.xPercentage[xIndexStart] * (fullWidth - p)
		- offset;
	return { leftStart, w };
}

} // namespace

StackLinearChartView::StackLinearChartView() = default;

StackLinearChartView::~StackLinearChartView() = default;

void StackLinearChartView::paint(
		QPainter &p,
		const Data::StatisticalChart &chartData,
		const Limits &xIndices,
		const Limits &xPercentageLimits,
		const Limits &heightLimits,
		const QRect &rect,
		bool footer) {
	constexpr auto kOffset = float64(2);
	_lastPaintedXIndices = {
		float64(std::max(0., xIndices.min - kOffset)),
		float64(std::min(
			float64(chartData.xPercentage.size() - 1),
			xIndices.max + kOffset)),
	};

	StackLinearChartView::paint(
		p,
		chartData,
		xPercentageLimits,
		heightLimits,
		rect,
		footer);
}

void StackLinearChartView::paint(
		QPainter &p,
		const Data::StatisticalChart &chartData,
		const Limits &xPercentageLimits,
		const Limits &heightLimits,
		const QRect &rect,
		bool footer) {
	const auto &[localStart, localEnd] = _lastPaintedXIndices;
	const auto &[leftStart, w] = ComputeLeftStartAndStep(
		chartData,
		xPercentageLimits,
		rect,
		localStart);

	auto skipPoints = std::vector<bool>(chartData.lines.size(), false);
	auto paths = std::vector<QPainterPath>(chartData.lines.size(), QPainterPath());

	for (auto i = localStart; i <= localEnd; i++) {
		auto stackOffset = 0.;
		auto sum = 0.;
		auto lastEnabled = int(0);

		auto drawingLinesCount = int(0);

		for (auto k = 0; k < chartData.lines.size(); k++) {
			const auto &line = chartData.lines[k];
			if (!isEnabled(line.id)) {
				continue;
			}
			if (line.y[i] > 0) {
				sum += line.y[i] * alpha(line.id);
				drawingLinesCount++;
			}
			lastEnabled = k;
		}

		for (auto k = 0; k < chartData.lines.size(); k++) {
			const auto &line = chartData.lines[k];
			if (!isEnabled(line.id)) {
				continue;
			}
			const auto &y = line.y;
			const auto lineAlpha = alpha(line.id);

			auto &chartPath = paths[k];

			auto yPercentage = 0.;

			if (drawingLinesCount == 1) {
				if (y[i] == 0) {
					yPercentage = 0;
				} else {
					yPercentage = lineAlpha;
				}
			} else {
				if (sum == 0) {
					yPercentage = 0;
				} else {
					yPercentage = y[i] * lineAlpha / sum;
				}
			}

			const auto xPoint = rect.width()
				* ((chartData.xPercentage[i] - xPercentageLimits.min)
					/ (xPercentageLimits.max - xPercentageLimits.min));
			const auto nextXPoint = (i == localEnd)
				? rect.width()
				: rect.width()
					* ((chartData.xPercentage[i + 1] - xPercentageLimits.min)
						/ (xPercentageLimits.max - xPercentageLimits.min));

			const auto height = (yPercentage) * rect.height();
			const auto yPoint = rect.y() + rect.height() - height - stackOffset;

			auto yPointZero = rect.y() + rect.height();
			auto xPointZero = xPoint;

			if (i == localStart) {
				auto localX = rect.x();
				auto localY = rect.y() + rect.height();
				chartPath.moveTo(localX, localY);
				skipPoints[k] = false;
			}

			const auto transitionProgress = 0.;
			if ((yPercentage == 0)
				&& (i > 0 && (y[i - 1] == 0))
				&& (i < localEnd && (y[i + 1] == 0))) {
				if (!skipPoints[k]) {
					if (k == lastEnabled) {
						chartPath.lineTo(xPointZero, yPointZero * (1. - transitionProgress));
					} else {
						chartPath.lineTo(xPointZero, yPointZero);
					}
				}
				skipPoints[k] = true;
			} else {
				if (skipPoints[k]) {
					if (k == lastEnabled) {
						chartPath.lineTo(xPointZero, yPointZero * (1. - transitionProgress));
					} else {
						chartPath.lineTo(xPointZero, yPointZero);
					}
				}
				if (k == lastEnabled) {
					chartPath.lineTo(xPoint, yPoint * (1. - transitionProgress));
				} else {
					chartPath.lineTo(xPoint, yPoint);
				}
				skipPoints[k] = false;
			}

			if (i == localEnd) {
				auto localX = rect.x() + rect.width();
				auto localY = rect.y() + rect.height();
					chartPath.lineTo(localX, localY);
			}

			stackOffset += height;
		}
	}

	auto hq = PainterHighQualityEnabler(p);

	for (auto k = int(chartData.lines.size() - 1); k >= 0; k--) {
		if (paths[k].isEmpty()) {
			continue;
		}
		const auto &line = chartData.lines[k];
		p.setOpacity(alpha(line.id));
		p.setPen(Qt::NoPen);
		p.fillPath(paths[k], line.color);
	}
	p.setOpacity(1.);
}

void StackLinearChartView::paintSelectedXIndex(
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
	p.setBrush(st::boxBg);
	const auto r = st::statisticsDetailsDotRadius;
	const auto i = selectedXIndex;
	const auto isSameToken = (_selectedPoints.lastXIndex == selectedXIndex)
		&& (_selectedPoints.lastHeightLimits.min == heightLimits.min)
		&& (_selectedPoints.lastHeightLimits.max == heightLimits.max)
		&& (_selectedPoints.lastXLimits.min == xPercentageLimits.min)
		&& (_selectedPoints.lastXLimits.max == xPercentageLimits.max);
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
			_selectedPoints.points[line.id] = QPointF(xPoint, 0)
				+ rect.topLeft();
		}

		{
			const auto lineRect = QRectF(
				rect.x()
					+ begin(_selectedPoints.points)->second.x()
					- (st::lineWidth / 2.),
				rect.y(),
				st::lineWidth,
				rect.height());
			p.fillRect(lineRect, st::windowSubTextFg);
		}
	}
	_selectedPoints.lastXIndex = selectedXIndex;
	_selectedPoints.lastHeightLimits = heightLimits;
	_selectedPoints.lastXLimits = xPercentageLimits;
}

int StackLinearChartView::findXIndexByPosition(
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

void StackLinearChartView::setEnabled(int id, bool enabled, crl::time now) {
	const auto it = _entries.find(id);
	if (it == end(_entries)) {
		_entries[id] = Entry{
			.enabled = enabled,
			.startedAt = now,
			.anim = anim::value(enabled ? 0. : 1., enabled ? 1. : 0.),
		};
	} else if (it->second.enabled != enabled) {
		auto &entry = it->second;
		entry.enabled = enabled;
		entry.startedAt = now;
		entry.anim.start(enabled ? 1. : 0.);
	}
	_isFinished = false;
	_cachedHeightLimits = {};
}

bool StackLinearChartView::isFinished() const {
	return _isFinished;
}

bool StackLinearChartView::isEnabled(int id) const {
	const auto it = _entries.find(id);
	return (it == end(_entries)) ? true : it->second.enabled;
}

float64 StackLinearChartView::alpha(int id) const {
	const auto it = _entries.find(id);
	return (it == end(_entries)) ? 1. : it->second.alpha;
}

AbstractChartView::HeightLimits StackLinearChartView::heightLimits(
		Data::StatisticalChart &chartData,
		Limits xIndices) {
	constexpr auto kMaxStackLinear = 100.;
	return {
		.full = { 0, kMaxStackLinear },
		.ranged = { 0., kMaxStackLinear },
	};
}

void StackLinearChartView::tick(crl::time now) {
	for (auto &[id, entry] : _entries) {
		const auto dt = std::min(
			(now - entry.startedAt) / kAlphaDuration,
			1.);
		if (dt > 1.) {
			continue;
		}
		return update(dt);
	}
}

void StackLinearChartView::update(float64 dt) {
	auto finishedCount = 0;
	auto idsToRemove = std::vector<int>();
	for (auto &[id, entry] : _entries) {
		if (!entry.startedAt) {
			continue;
		}
		entry.anim.update(dt, anim::linear);
		const auto progress = entry.anim.current();
		entry.alpha = std::clamp(
			progress,
			0.,
			1.);
		if (entry.alpha == 1.) {
			idsToRemove.push_back(id);
		}
		if (entry.anim.current() == entry.anim.to()) {
			finishedCount++;
			entry.anim.finish();
		}
	}
	_isFinished = (finishedCount == _entries.size());
	for (const auto &id : idsToRemove) {
		_entries.remove(id);
	}
}

} // namespace Statistic
