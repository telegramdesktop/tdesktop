/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/view/stack_chart_view.h"

#include "ui/effects/animation_value_f.h"
#include "data/data_statistics.h"
#include "ui/painter.h"

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

StackChartView::StackChartView() = default;

StackChartView::~StackChartView() = default;

void StackChartView::paint(
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

	StackChartView::paint(
		p,
		chartData,
		xPercentageLimits,
		heightLimits,
		rect,
		footer);
}

void StackChartView::paint(
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

	const auto opacity = p.opacity();
	auto hq = PainterHighQualityEnabler(p);

	auto bottoms = std::vector<float64>(localEnd - localStart + 1, -rect.y());
	auto selectedBottoms = std::vector<float64>();
	const auto hasSelectedXIndex = !footer && (_lastSelectedXIndex >= 0);
	if (hasSelectedXIndex) {
		selectedBottoms = std::vector<float64>(chartData.lines.size(), 0);
		constexpr auto kSelectedAlpha = 0.5;
		p.setOpacity(
			anim::interpolateF(1.0, kSelectedAlpha, _lastSelectedXProgress));
	}

	for (auto i = 0; i < chartData.lines.size(); i++) {
		const auto &line = chartData.lines[i];
		auto path = QPainterPath();
		for (auto x = localStart; x <= localEnd; x++) {
			if (line.y[x] <= 0) {
				continue;
			}
			const auto xPoint = rect.width()
				* ((chartData.xPercentage[x] - xPercentageLimits.min)
					/ (xPercentageLimits.max - xPercentageLimits.min));
			const auto yPercentage = (line.y[x] - heightLimits.min)
				/ float64(heightLimits.max - heightLimits.min);
			const auto yPoint = yPercentage * rect.height() * alpha(line.id);

			const auto bottomIndex = x - localStart;
			const auto column = QRectF(
				leftStart + (x - localStart) * w,
				rect.height() - bottoms[bottomIndex] - yPoint,
				w,
				yPoint);
			if (hasSelectedXIndex && (x == _lastSelectedXIndex)) {
				selectedBottoms[i] = column.y();
			} else {
				path.addRect(column);
			}
			bottoms[bottomIndex] += yPoint;
		}
		p.fillPath(path, line.color);
	}

	for (auto i = 0; i < selectedBottoms.size(); i++) {
		p.setOpacity(opacity);
		if (selectedBottoms[i] <= 0) {
			continue;
		}
		const auto &line = chartData.lines[i];
		const auto yPercentage = (line.y[_lastSelectedXIndex] - heightLimits.min)
			/ float64(heightLimits.max - heightLimits.min);
		const auto yPoint = yPercentage * rect.height() * alpha(line.id);

		const auto column = QRectF(
			leftStart + (_lastSelectedXIndex - localStart) * w,
			selectedBottoms[i],
			w,
			yPoint);
		p.fillRect(column, line.color);
	}
}

void StackChartView::paintSelectedXIndex(
		QPainter &p,
		const Data::StatisticalChart &chartData,
		const Limits &xPercentageLimits,
		const Limits &heightLimits,
		const QRect &rect,
		int selectedXIndex,
		float64 progress) {
	_lastSelectedXIndex = selectedXIndex;
	_lastSelectedXProgress = progress;
	[[maybe_unused]] const auto o = ScopedPainterOpacity(p, progress);
	StackChartView::paint(
		p,
		chartData,
		xPercentageLimits,
		heightLimits,
		rect,
		false);
}

int StackChartView::findXIndexByPosition(
		const Data::StatisticalChart &chartData,
		const Limits &xPercentageLimits,
		const QRect &rect,
		float64 xPos) {
	if (xPos < rect.x()) {
		return 0;
	} else if (xPos > (rect.x() + rect.width())) {
		return chartData.xPercentage.size() - 1;
	}
	const auto &[localStart, localEnd] = _lastPaintedXIndices;
	const auto &[leftStart, w] = ComputeLeftStartAndStep(
		chartData,
		xPercentageLimits,
		rect,
		localStart);

	for (auto i = 0; i < chartData.lines.size(); i++) {
		const auto &line = chartData.lines[i];
		for (auto x = localStart; x <= localEnd; x++) {
			const auto left = leftStart + (x - localStart) * w;
			if ((xPos >= left) && (xPos < (left + w))) {
				return _lastSelectedXIndex = x;
			}
		}
	}
	return _lastSelectedXIndex = 0;
}

void StackChartView::setEnabled(int id, bool enabled, crl::time now) {
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

bool StackChartView::isFinished() const {
	return _isFinished;
}

bool StackChartView::isEnabled(int id) const {
	const auto it = _entries.find(id);
	return (it == end(_entries)) ? true : it->second.enabled;
}

float64 StackChartView::alpha(int id) const {
	const auto it = _entries.find(id);
	return (it == end(_entries)) ? 1. : it->second.alpha;
}

AbstractChartView::HeightLimits StackChartView::heightLimits(
		Data::StatisticalChart &chartData,
		Limits xIndices) {
	if (_cachedHeightLimits.ySum.empty()) {
		_cachedHeightLimits.ySum.reserve(chartData.x.size());

		auto maxValueFull = 0;
		for (auto i = 0; i < chartData.x.size(); i++) {
			auto sum = 0;
			for (const auto &line : chartData.lines) {
				if (isEnabled(line.id)) {
					sum += line.y[i];
				}
			}
			_cachedHeightLimits.ySum.push_back(sum);
			maxValueFull = std::max(sum, maxValueFull);
		}

		_cachedHeightLimits.ySumSegmentTree = SegmentTree(
			_cachedHeightLimits.ySum);
		_cachedHeightLimits.full = { 0., float64(maxValueFull) };
	}
	const auto max = _cachedHeightLimits.ySumSegmentTree.rMaxQ(
		xIndices.min,
		xIndices.max);
	return {
		.full = _cachedHeightLimits.full,
		.ranged = { 0., float64(max) },
	};
}

void StackChartView::tick(crl::time now) {
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

void StackChartView::update(float64 dt) {
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
