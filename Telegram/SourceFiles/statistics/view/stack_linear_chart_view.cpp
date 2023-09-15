/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/view/stack_linear_chart_view.h"

#include "data/data_statistics.h"
#include "statistics/point_details_widget.h"
#include "ui/effects/animation_value_f.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "styles/style_statistics.h"

#include <QtCore/QtMath>

namespace Statistic {
namespace {

constexpr auto kAlphaDuration = float64(200);
constexpr auto kCircleSizeRatio = 0.42;
constexpr auto kMinTextScaleRatio = 0.3;
constexpr auto kPieAngleOffset = 90;

constexpr auto kRightTop = short(0);
constexpr auto kRightBottom = short(1);
constexpr auto kLeftBottom = short(2);
constexpr auto kLeftTop = short(3);

[[nodiscard]] short QuarterForPoint(const QRect &r, const QPointF &p) {
	if (p.x() >= r.center().x() && p.y() <= r.center().y()) {
		return kRightTop;
	} else if (p.x() >= r.center().x() && p.y() >= r.center().y()) {
		return kRightBottom;
	} else if (p.x() < r.center().x() && p.y() >= r.center().y()) {
		return kLeftBottom;
	} else {
		return kLeftTop;
	}
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
	const auto wasXIndices = _lastPaintedXIndices;
	_lastPaintedXIndices = {
		float64(std::max(0., xIndices.min - kOffset)),
		float64(std::min(
			float64(chartData.xPercentage.size() - 1),
			xIndices.max + kOffset)),
	};
	if ((wasXIndices.min != _lastPaintedXIndices.min)
		|| (wasXIndices.max != _lastPaintedXIndices.max)) {

		const auto &[localStart, localEnd] = _lastPaintedXIndices;
		_cachedTransition.lines = std::vector<Transition::TransitionLine>(
			chartData.lines.size(),
			Transition::TransitionLine());

		for (auto j = 0; j < 2; j++) {
			const auto i = int((j == 1) ? localEnd : localStart);
			auto stackOffset = 0;
			auto sum = 0.;
			auto drawingLinesCount = 0;
			for (const auto &line : chartData.lines) {
				if (!isEnabled(line.id)) {
					continue;
				}
				if (line.y[i] > 0) {
					sum += line.y[i] * alpha(line.id);
					drawingLinesCount++;
				}
			}

			for (auto k = 0; k < chartData.lines.size(); k++) {
				auto &linePoint = (j
					? _cachedTransition.lines[k].end
					: _cachedTransition.lines[k].start);
				const auto &line = chartData.lines[k];
				if (!isEnabled(line.id)) {
					continue;
				}
				const auto yPercentage = (drawingLinesCount == 1)
					? (line.y[i] ? alpha(line.id) : 0)
					: (sum ? (line.y[i] * alpha(line.id) / sum) : 0);

				const auto xPoint = rect.width()
					* ((chartData.xPercentage[i] - xPercentageLimits.min)
						/ (xPercentageLimits.max - xPercentageLimits.min));
				const auto height = yPercentage * rect.height();
				const auto yPoint = rect::bottom(rect) - height - stackOffset;
				linePoint = { xPoint, yPoint };
				stackOffset += height;
			}
		}

		auto sums = std::vector<float64>();
		sums.reserve(chartData.lines.size());
		auto totalSum = 0;
		for (const auto &line : chartData.lines) {
			auto sum = 0;
			for (auto i = xIndices.min; i <= xIndices.max; i++) {
				sum += line.y[i];
			}
			sum *= alpha(line.id);
			totalSum += sum;
			sums.push_back(sum);
		}
		auto stackedPercentage = 0.;
		for (auto k = 0; k < sums.size(); k++) {
			const auto percentage = (sums[k] / float64(totalSum));
			stackedPercentage += percentage;
			_cachedTransition.lines[k].angle = stackedPercentage * 360 - 180.;
		}
	}

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
	const auto context = PaintContext{
		chartData,
		xPercentageLimits,
		heightLimits,
		rect,
		footer
	};
	if (_transitionProgress == 1) {
		return paintZoomed(p, context);
	}
	const auto &[localStart, localEnd] = _lastPaintedXIndices;
	_skipPoints = std::vector<bool>(chartData.lines.size(), false);
	auto paths = std::vector<QPainterPath>(
		chartData.lines.size(),
		QPainterPath());

	const auto center = QPointF(rect.center());

	const auto rotate = [&](float64 ang, const QPointF &p) {
		return QTransform()
			.translate(center.x(), center.y())
			.rotate(ang)
			.translate(-center.x(), -center.y())
			.map(p);
	};

	const auto hasTransitionAnimation = _transitionProgress && !footer;

	auto straightLineProgress = 0.;
	auto hasEmptyPoint = false;

	auto ovalPath = QPainterPath();
	if (hasTransitionAnimation) {
		constexpr auto kStraightLinePart = 0.6;
		straightLineProgress = std::clamp(
			_transitionProgress / kStraightLinePart,
			0.,
			1.);
		auto rectPath = QPainterPath();
		rectPath.addRect(rect);
		const auto r = anim::interpolateF(
			1.,
			kCircleSizeRatio,
			_transitionProgress);
		const auto per = anim::interpolateF(0., 100., _transitionProgress);
		const auto side = (rect.width() / 2.) * r;
		const auto rectF = QRectF(
			center - QPointF(side, side),
			center + QPointF(side, side));
		ovalPath.addRoundedRect(rectF, per, per, Qt::RelativeSize);
		ovalPath = ovalPath.intersected(rectPath);
	}

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
			const auto isLastLine = (k == lastEnabled);
			const auto &transitionLine = _cachedTransition.lines[k];
			if (!isEnabled(line.id)) {
				continue;
			}
			const auto &y = line.y;
			const auto lineAlpha = alpha(line.id);

			auto &chartPath = paths[k];

			const auto yPercentage = (drawingLinesCount == 1)
				? float64(y[i] ? lineAlpha : 0.)
				: float64(sum ? (y[i] * lineAlpha / sum) : 0.);

			const auto xPoint = rect.width()
				* ((chartData.xPercentage[i] - xPercentageLimits.min)
					/ (xPercentageLimits.max - xPercentageLimits.min));

			if (!yPercentage && isLastLine) {
				hasEmptyPoint = true;
			}
			const auto height = yPercentage * rect.height();
			const auto yPoint = rect::bottom(rect) - height - stackOffset;
			// startFromY[k] = yPoint;

			auto angle = 0.;
			auto resultPoint = QPointF(xPoint, yPoint);
			auto pointZero = QPointF(xPoint, rect.y() + rect.height());
			// if (i == localEnd) {
			// 	endXPoint = xPoint;
			// } else if (i == localStart) {
			// 	startXPoint = xPoint;
			// }
			if (hasTransitionAnimation && !isLastLine) {
				const auto point1 = (resultPoint.x() < center.x())
					? transitionLine.start
					: transitionLine.end;

				const auto diff = center - point1;
				const auto yTo = point1.y()
					+ diff.y() * (resultPoint.x() - point1.x()) / diff.x();
				const auto yToResult = yTo * straightLineProgress;
				const auto revProgress = (1. - straightLineProgress);

				resultPoint.setY(resultPoint.y() * revProgress + yToResult);
				pointZero.setY(pointZero.y() * revProgress + yToResult);

				{
					const auto angleK = diff.y() / float64(diff.x());
					angle = (angleK > 0)
						? (-std::atan(angleK)) * (180. / M_PI)
						: (std::atan(std::abs(angleK))) * (180. / M_PI);
					angle -= 90;
				}

				if (resultPoint.x() >= center.x()) {
					const auto resultAngle = _transitionProgress * angle;
					const auto rotated = rotate(resultAngle, resultPoint);
					resultPoint = QPointF(
						std::max(rotated.x(), center.x()),
						rotated.y());

					pointZero = QPointF(
						std::max(pointZero.x(), center.x()),
						rotate(resultAngle, pointZero).y());
				} else {
					const auto &xLimits = xPercentageLimits;
					const auto isNextXPointAfterCenter = false
						|| center.x() < (rect.width() * ((i == localEnd)
							? 1.
							: ((chartData.xPercentage[i + 1] - xLimits.min)
								/ (xLimits.max - xLimits.min))));
					if (isNextXPointAfterCenter) {
						pointZero = resultPoint = QPointF()
							+ center * straightLineProgress
							+ resultPoint * revProgress;
					} else {
						const auto resultAngle = _transitionProgress * angle
							+ _transitionProgress * transitionLine.angle;
						resultPoint = rotate(resultAngle, resultPoint);
						pointZero = rotate(resultAngle, pointZero);
					}
				}
			}

			if (i == localStart) {
				const auto bottomLeft = QPointF(rect.x(), rect::bottom(rect));
				const auto local = (hasTransitionAnimation && !isLastLine)
					? rotate(
						_transitionProgress * angle
							+ _transitionProgress * transitionLine.angle,
						bottomLeft - QPointF(center.x(), 0))
					: bottomLeft;
				chartPath.setFillRule(Qt::WindingFill);
				chartPath.moveTo(local);
				_skipPoints[k] = false;
			}

			const auto yRatio = 1. - (isLastLine ? _transitionProgress : 0.);
			if ((!yPercentage)
				&& (i > 0 && (y[i - 1] == 0))
				&& (i < localEnd && (y[i + 1] == 0))
				&& (!hasTransitionAnimation)) {
				if (!_skipPoints[k]) {
					chartPath.lineTo(pointZero.x(), pointZero.y() * yRatio);
				}
				_skipPoints[k] = true;
			} else {
				if (_skipPoints[k]) {
					chartPath.lineTo(pointZero.x(), pointZero.y() * yRatio);
				}
				chartPath.lineTo(resultPoint.x(), resultPoint.y() * yRatio);
				_skipPoints[k] = false;
			}

			if (i == localEnd) {
				if (hasTransitionAnimation && !isLastLine) {
					{
						const auto diff = center - transitionLine.start;
						const auto angleK = diff.y() / diff.x();
						angle = (angleK > 0)
							? ((-std::atan(angleK)) * (180. / M_PI))
							: ((std::atan(std::abs(angleK))) * (180. / M_PI));
						angle -= 90;
					}

					const auto local = rotate(
						_transitionProgress * angle
							+ _transitionProgress * transitionLine.angle,
						transitionLine.start);

					const auto ending = true
						&& (std::abs(resultPoint.x() - local.x()) < 0.001)
						&& ((local.y() < center.y()
								&& resultPoint.y() < center.y())
							|| (local.y() > center.y()
								&& resultPoint.y() > center.y()));
					const auto endQuarter = (!ending)
						? QuarterForPoint(rect, resultPoint)
						: kRightTop;
					const auto startQuarter = (!ending)
						? QuarterForPoint(rect, local)
						: (transitionLine.angle == -180.)
						? kRightTop
						: kLeftTop;

					for (auto q = endQuarter; q <= startQuarter; q++) {
						chartPath.lineTo(
							(q == kLeftTop || q == kLeftBottom)
								? rect.x()
								: rect::right(rect),
							(q == kLeftTop || q == kRightTop)
								? rect.y()
								: rect::right(rect));
					}
				} else {
					chartPath.lineTo(rect::right(rect), rect::bottom(rect));
				}
			}

			stackOffset += height;
		}
	}

	auto hq = PainterHighQualityEnabler(p);

	p.fillRect(rect, st::boxBg);
	if (!ovalPath.isEmpty()) {
		p.setClipPath(ovalPath);
	}

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
	{
		constexpr auto kAlphaTextPart = 0.6;
		const auto progress = std::clamp(
			(_transitionProgress - kAlphaTextPart) / (1. - kAlphaTextPart),
			0.,
			1.);
		if (progress > 0) {
			auto o = ScopedPainterOpacity(p, progress);
			paintPieText(p, context);
		}
	}

	// Fix ugly outline.
	if (!footer || !_transitionProgress) {
		p.setBrush(Qt::transparent);
		p.setPen(st::boxBg);
		p.drawPath(ovalPath);
	}
}

void StackLinearChartView::paintZoomed(QPainter &p, const PaintContext &c) {
	if (c.footer) {
		return;
	}
	const auto center = QPointF(c.rect.center());
	const auto side = (c.rect.width() / 2.) * kCircleSizeRatio;
	const auto rectF = QRectF(
		center - QPointF(side, side),
		center + QPointF(side, side));

	auto hq = PainterHighQualityEnabler(p);
	auto selectedLineIndex = -1;
	for (auto k = 0; k < c.chartData.lines.size(); k++) {
		const auto previous = k
			? _cachedTransition.lines[k - 1].angle
			: -180;
		const auto now = _cachedTransition.lines[k].angle;

		const auto &line = c.chartData.lines[k];
		p.setBrush(line.color);
		p.setPen(Qt::NoPen);
		const auto textAngle = (previous + kPieAngleOffset)
			+ (now - previous) / 2.;
		const auto partOffset = _piePartController.offset(line.id, textAngle);
		p.translate(partOffset);
		p.drawPie(
			rectF,
			-(previous + kPieAngleOffset) * 16,
			-(now - previous) * 16);
		p.translate(-partOffset);
		if (_piePartController.selected() == line.id) {
			selectedLineIndex = k;
		}
	}
	paintPieText(p, c);

	if (selectedLineIndex >= 0) {
		const auto &[localStart, localEnd] = _lastPaintedXIndices;
		const auto &line = c.chartData.lines[selectedLineIndex];
		auto sum = 0;
		for (auto i = localStart; i <= localEnd; i++) {
			sum += line.y[i];
		}
		sum *= alpha(line.id);
		if (sum > 0) {
			PaintDetails(p, line, sum, c.rect);
		}
	}
}

void StackLinearChartView::paintPieText(QPainter &p, const PaintContext &c) {
	const auto center = QPointF(c.rect.center());
	const auto side = (c.rect.width() / 2.) * kCircleSizeRatio;
	const auto rectF = QRectF(
		center - QPointF(side, side),
		center + QPointF(side, side));
	const auto &font = st::statisticsPieChartFont;
	const auto maxScale = side / (font->height * 2);
	const auto minScale = maxScale * kMinTextScaleRatio;
	p.setBrush(Qt::NoBrush);
	p.setPen(st::premiumButtonFg);
	p.setFont(font);
	const auto opacity = p.opacity();
	for (auto k = 0; k < c.chartData.lines.size(); k++) {
		const auto previous = k
			? _cachedTransition.lines[k - 1].angle
			: -180;
		const auto now = _cachedTransition.lines[k].angle;
		const auto percentage = (now - previous) / 360.;

		const auto rText = side * std::sqrt(1. - percentage);
		const auto textAngle = (previous + kPieAngleOffset)
			+ (now - previous) / 2.;
		const auto textRadians = textAngle * M_PI / 180.;
		const auto scale = (minScale) + percentage * (maxScale - minScale);
		const auto text = QString::number(std::round(percentage * 100))
			+ u"%"_q;
		const auto textW = font->width(text);
		const auto textH = font->height;
		const auto textXShift = textW / 2.;
		const auto textYShift = textW / 2.;
		const auto textRectCenter = rectF.center() + QPointF(
			(rText - textXShift * (1. - scale)) * std::cos(textRadians),
			(rText - textYShift * (1. - scale)) * std::sin(textRadians));
		const auto textRect = QRectF(
			textRectCenter - QPointF(textXShift, textYShift),
			textRectCenter + QPointF(textXShift, textYShift));
		const auto partOffset = _piePartController.offset(
			c.chartData.lines[k].id,
			textAngle);
		p.setTransform(
			QTransform()
				.translate(
					textRectCenter.x() + partOffset.x(),
					textRectCenter.y() + partOffset.y())
				.scale(scale, scale)
				.translate(-textRectCenter.x(), -textRectCenter.y()));
		p.setOpacity(opacity * alpha(c.chartData.lines[k].id));
		p.drawText(textRect, text, style::al_center);
	}
	p.resetTransform();
}

void StackLinearChartView::setUpdateCallback(Fn<void()> callback) {
	if (callback) {
		_piePartAnimation.init([=] { callback(); });
	}
}

bool StackLinearChartView::PiePartController::set(int id) {
	if (_selected != id) {
		update(_selected);
		_selected = id;
		update(_selected);
		return true;
	}
	return false;
}

void StackLinearChartView::PiePartController::update(int id) {
	if (id >= 0) {
		const auto was = _startedAt[id];
		const auto p = (crl::now() - was) / st::slideWrapDuration;
		const auto progress = ((p > 0) && (p < 1)) ? (1. - p) : 0.;
		_startedAt[id] = crl::now() - (st::slideWrapDuration * progress);
	}
}

float64 StackLinearChartView::PiePartController::progress(int id) {
	const auto it = _startedAt.find(id);
	if (it == end(_startedAt)) {
		return 0.;
	}
	const auto at = it->second;
	const auto show = (_selected == id);
	const auto progress = std::clamp(
		(crl::now() - at) / float64(st::slideWrapDuration),
		0.,
		1.);
	return std::clamp(show ? progress : (1. - progress), 0., 1.);
}

QPointF StackLinearChartView::PiePartController::offset(
		LineId id,
		float64 angle) {
	const auto offset = st::statisticsPieChartPartOffset * progress(id);
	const auto radians = angle * M_PI / 180.;
	return { std::cos(radians) * offset, std::sin(radians) * offset };
}

auto StackLinearChartView::PiePartController::selected() const -> LineId {
	return _selected;
}

void StackLinearChartView::handleMouseMove(
		const Data::StatisticalChart &chartData,
		const QPoint &center,
		const QPoint &p) {
	if (_transitionProgress < 1) {
		return;
	}
	const auto theta = std::atan2(center.y() - p.y(), (center.x() - p.x()));
	const auto angle = [&] {
		const auto a = theta * (180. / M_PI) + 90.;
		return (a > 180.) ? (a - 360.) : a;
	}();
	for (auto k = 0; k < chartData.lines.size(); k++) {
		const auto previous = k
			? _cachedTransition.lines[k - 1].angle
			: -180;
		const auto now = _cachedTransition.lines[k].angle;
		if (angle > previous && angle <= now) {
			const auto id = p.isNull()
				? -1
				: chartData.lines[k].id;
			if (_piePartController.set(id)) {
				if (!_piePartAnimation.animating()) {
					_piePartAnimation.start();
				}
			}
			return;
		}
	}
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
	if (_transitionProgress == 1.) {
		return -1;
	} else if (x < rect.x()) {
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
