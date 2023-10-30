/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/view/stack_linear_chart_view.h"

#include "data/data_statistics_chart.h"
#include "statistics/chart_lines_filter_controller.h"
#include "statistics/view/stack_chart_common.h"
#include "statistics/widgets/point_details_widget.h"
#include "ui/effects/animation_value_f.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "styles/style_statistics.h"

#include <QtCore/QtMath>

namespace Statistic {
namespace {

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

inline float64 InterpolationRatio(float64 from, float64 to, float64 result) {
	return (result - from) / (to - from);
};

[[nodiscard]] Limits FindAdditionalZoomedOutXIndices(const PaintContext &c) {
	constexpr auto kOffset = int(1);
	auto &xPercentage = c.chartData.xPercentage;
	auto leftResult = 0.;
	{
		auto i = std::max(int(c.xIndices.min) - kOffset, 0);
		if (xPercentage[i] > c.xPercentageLimits.min) {
			while (true) {
				i--;
				if (i < 0) {
					leftResult = 0;
					break;
				} else if (!(xPercentage[i] > c.xPercentageLimits.min)) {
					leftResult = i;
					break;
				}
			}
		} else {
			leftResult = i;
		}
	}
	{
		const auto lastIndex = float64(xPercentage.size() - 1);
		auto i = std::min(lastIndex, float64(c.xIndices.max) + kOffset);
		if (xPercentage[i] < c.xPercentageLimits.max) {
			while (true) {
				i++;
				if (i > lastIndex) {
					return { leftResult, lastIndex };
				} else if (!(xPercentage[i] < c.xPercentageLimits.max)) {
					return { leftResult, i };
				}
			}
		} else {
			return { leftResult, i };
		}
	}
}

} // namespace

void StackLinearChartView::ChangingPiePartController::setParts(
		const std::vector<PiePartData::Part> &was,
		const std::vector<PiePartData::Part> &now) {
	if (_animValues.size() != was.size()) {
		_animValues = std::vector<anim::value>(was.size(), anim::value());
		for (auto i = 0; i < was.size(); i++) {
			_animValues[i] = anim::value(
				was[i].roundedPercentage,
				now[i].roundedPercentage);
		}
	} else {
		for (auto i = 0; i < was.size(); i++) {
			_animValues[i] = anim::value(
				_animValues[i].current(),
				now[i].roundedPercentage);
		}
	}
	_startedAt = crl::now();
	_isFinished = false;
}

void StackLinearChartView::ChangingPiePartController::update() {
	const auto progress = std::clamp(
		(crl::now() - _startedAt) / float64(st::slideWrapDuration),
		0.,
		1.);
	auto totalSum = 0.;
	auto finished = true;
	auto result = std::vector<float64>();
	result.reserve(_animValues.size());
	for (auto &anim : _animValues) {
		anim.update(progress, anim::easeOutCubic);
		if (finished && (anim.current() != anim.to())) {
			finished = false;
		}
		const auto value = anim.current();
		result.push_back(value);
		totalSum += value;
	}
	_isFinished = finished;
	_current = PiePartsPercentage(result, totalSum, false);
}

PiePartData StackLinearChartView::ChangingPiePartController::current() const {
	return _current;
}

bool StackLinearChartView::ChangingPiePartController::isFinished() const {
	return _isFinished;
}

StackLinearChartView::StackLinearChartView() {
	_piePartAnimation.init([=] { AbstractChartView::update(); });
}

StackLinearChartView::~StackLinearChartView() = default;

void StackLinearChartView::paint(QPainter &p, const PaintContext &c) {
	if (!_transition.progress && !c.footer) {
		prepareZoom(c, TransitionStep::ZoomedOut);
	}
	if (_transition.pendingPrepareToZoomIn) {
		_transition.pendingPrepareToZoomIn = false;
		prepareZoom(c, TransitionStep::PrepareToZoomIn);
	}

	StackLinearChartView::paintChartOrZoomAnimation(p, c);
}

void StackLinearChartView::prepareZoom(
		const PaintContext &c,
		TransitionStep step) {
	if (step == TransitionStep::ZoomedOut) {
		_transition.zoomedOutXIndicesAdditional
			= FindAdditionalZoomedOutXIndices(c);
		_transition.zoomedOutXIndices = c.xIndices;
		_transition.zoomedOutXPercentage = c.xPercentageLimits;
	} else if (step == TransitionStep::PrepareToZoomIn) {
		const auto &[zoomedStart, zoomedEnd] =
			_transition.zoomedOutXIndices;
		_transition.lines = std::vector<Transition::TransitionLine>(
			c.chartData.lines.size(),
			Transition::TransitionLine());

		const auto xPercentageLimits = _transition.zoomedOutXPercentage;
		const auto &linesFilter = linesFilterController();

		for (auto j = 0; j < 2; j++) {
			const auto i = int((j == 1) ? zoomedEnd : zoomedStart);
			auto stackOffset = 0;
			auto sum = 0.;
			auto drawingLinesCount = 0;
			for (const auto &line : c.chartData.lines) {
				if (line.y[i] > 0) {
					const auto alpha = linesFilter->alpha(line.id);
					sum += line.y[i] * alpha;
					if (alpha > 0.) {
						drawingLinesCount++;
					}
				}
			}

			for (auto k = 0; k < c.chartData.lines.size(); k++) {
				auto &linePoint = (j
					? _transition.lines[k].end
					: _transition.lines[k].start);
				const auto &line = c.chartData.lines[k];
				const auto yPercentage = (drawingLinesCount == 1)
					? (line.y[i] ? linesFilter->alpha(line.id) : 0)
					: (sum
						? (line.y[i] * linesFilter->alpha(line.id) / sum)
						: 0);

				const auto xPoint = c.rect.width()
					* ((c.chartData.xPercentage[i] - xPercentageLimits.min)
						/ (xPercentageLimits.max - xPercentageLimits.min));
				const auto height = yPercentage * c.rect.height();
				const auto yPoint = rect::bottom(c.rect)
					- height
					- stackOffset;
				linePoint = { xPoint, yPoint };
				stackOffset += height;
			}
		}

		savePieTextParts(c);
		applyParts(_transition.textParts);
	}
}

void StackLinearChartView::applyParts(
		const std::vector<PiePartData::Part> &parts) {
	for (auto k = 0; k < parts.size(); k++) {
		_transition.lines[k].angle = parts[k].stackedAngle;
	}
}

void StackLinearChartView::saveZoomRange(const PaintContext &c) {
	_transition.zoomedInRangeXIndices = FindStackXIndicesFromRawXPercentages(
		c.chartData,
		c.xPercentageLimits,
		_transition.zoomedInLimitXIndices);
	_transition.zoomedInRange = {
		c.chartData.xPercentage[_transition.zoomedInRangeXIndices.min],
		c.chartData.xPercentage[_transition.zoomedInRangeXIndices.max],
	};
}

void StackLinearChartView::savePieTextParts(const PaintContext &c) {
	auto data = PiePartsPercentageByIndices(
		c.chartData,
		linesFilterController(),
		_transition.zoomedInRangeXIndices);
	_transition.textParts = std::move(data.parts);
	_pieHasSinglePart = data.pieHasSinglePart;
}

void StackLinearChartView::paintChartOrZoomAnimation(
		QPainter &p,
		const PaintContext &c) {
	if (_transition.progress == 1.) {
		if (c.footer) {
			paintZoomedFooter(p, c);
		} else {
			paintZoomed(p, c);
		}
		return p.setOpacity(0.);
	}
	const auto &linesFilter = linesFilterController();
	const auto hasTransitionAnimation = _transition.progress && !c.footer;
	const auto &[localStart, localEnd] = c.footer
		? Limits{ 0., float64(c.chartData.xPercentage.size() - 1) }
		: _transition.zoomedOutXIndicesAdditional;
	_skipPoints = std::vector<bool>(c.chartData.lines.size(), false);
	auto paths = std::vector<QPainterPath>(
		c.chartData.lines.size(),
		QPainterPath());

	const auto center = QPointF(c.rect.center());

	const auto rotate = [&](float64 ang, const QPointF &p) {
		return QTransform()
			.translate(center.x(), center.y())
			.rotate(ang)
			.translate(-center.x(), -center.y())
			.map(p);
	};

	const auto xPercentageLimits = !c.footer
		? _transition.zoomedOutXPercentage
		: Limits{
			c.chartData.xPercentage[localStart],
			c.chartData.xPercentage[localEnd],
		};

	auto straightLineProgress = 0.;
	auto hasEmptyPoint = false;

	auto ovalPath = QPainterPath();
	if (hasTransitionAnimation) {
		constexpr auto kStraightLinePart = 0.6;
		straightLineProgress = std::clamp(
			_transition.progress / kStraightLinePart,
			0.,
			1.);
		auto rectPath = QPainterPath();
		rectPath.addRect(c.rect);
		const auto r = anim::interpolateF(
			1.,
			kCircleSizeRatio,
			_transition.progress);
		const auto per = anim::interpolateF(0., 100., _transition.progress);
		const auto side = (c.rect.width() / 2.) * r;
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

		const auto xPoint = c.rect.width()
			* ((c.chartData.xPercentage[i] - xPercentageLimits.min)
				/ (xPercentageLimits.max - xPercentageLimits.min));

		for (auto k = 0; k < c.chartData.lines.size(); k++) {
			const auto &line = c.chartData.lines[k];
			const auto alpha = linesFilter->alpha(line.id);
			if (!alpha) {
				continue;
			}
			if (line.y[i] > 0) {
				sum += line.y[i] * alpha;
				drawingLinesCount++;
			}
			lastEnabled = k;
		}

		for (auto k = 0; k < c.chartData.lines.size(); k++) {
			const auto &line = c.chartData.lines[k];
			const auto isLastLine = (k == lastEnabled);
			const auto lineAlpha = linesFilter->alpha(line.id);
			if (isLastLine && (lineAlpha < 1.)) {
				hasEmptyPoint = true;
			}
			if (!lineAlpha) {
				continue;
			}
			const auto &transitionLine = hasTransitionAnimation
				? _transition.lines[k]
				: Transition::TransitionLine();
			const auto &y = line.y;

			auto &chartPath = paths[k];

			const auto yPercentage = (drawingLinesCount == 1)
				? float64(y[i] ? lineAlpha : 0.)
				: float64(sum ? (y[i] * lineAlpha / sum) : 0.);

			if (isLastLine && !yPercentage) {
				hasEmptyPoint = true;
			}
			const auto height = yPercentage * c.rect.height();
			const auto yPoint = rect::bottom(c.rect) - height - stackOffset;
			// startFromY[k] = yPoint;

			auto angle = 0.;
			auto resultPoint = QPointF(xPoint, yPoint);
			auto pointZero = QPointF(xPoint, c.rect.y() + c.rect.height());
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
					const auto resultAngle = _transition.progress * angle;
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
						|| center.x() < (c.rect.width() * ((i == localEnd)
							? 1.
							: ((c.chartData.xPercentage[i + 1] - xLimits.min)
								/ (xLimits.max - xLimits.min))));
					if (isNextXPointAfterCenter) {
						pointZero = resultPoint = QPointF()
							+ center * straightLineProgress
							+ resultPoint * revProgress;
					} else {
						const auto resultAngle = _transition.progress * angle
							+ _transition.progress * transitionLine.angle;
						resultPoint = rotate(resultAngle, resultPoint);
						pointZero = rotate(resultAngle, pointZero);
					}
				}
			}

			if (i == localStart) {
				const auto bottomLeft = QPointF(
					c.rect.x(),
					rect::bottom(c.rect));
				const auto local = (hasTransitionAnimation && !isLastLine)
					? rotate(
						_transition.progress * angle
							+ _transition.progress * transitionLine.angle,
						bottomLeft - QPointF(center.x(), 0))
					: bottomLeft;
				chartPath.setFillRule(Qt::WindingFill);
				chartPath.moveTo(local);
				_skipPoints[k] = false;
			}

			const auto yRatio = 1. - (isLastLine ? _transition.progress : 0.);
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
						_transition.progress * angle
							+ _transition.progress * transitionLine.angle,
						transitionLine.start);

					const auto ending = true
						&& (std::abs(resultPoint.x() - local.x()) < 0.001)
						&& ((local.y() < center.y()
								&& resultPoint.y() < center.y())
							|| (local.y() > center.y()
								&& resultPoint.y() > center.y()));
					const auto endQuarter = (!ending)
						? QuarterForPoint(c.rect, resultPoint)
						: kRightTop;
					const auto startQuarter = (!ending)
						? QuarterForPoint(c.rect, local)
						: (transitionLine.angle == -180.)
						? kRightTop
						: kLeftTop;

					for (auto q = endQuarter; q <= startQuarter; q++) {
						chartPath.lineTo(
							(q == kLeftTop || q == kLeftBottom)
								? c.rect.x()
								: rect::right(c.rect),
							(q == kLeftTop || q == kRightTop)
								? c.rect.y()
								: rect::right(c.rect));
					}
				} else {
					chartPath.lineTo(
						rect::right(c.rect),
						rect::bottom(c.rect));
				}
			}

			stackOffset += height;
		}
	}

	auto hq = PainterHighQualityEnabler(p);

	p.fillRect(c.rect + QMargins(0, 0, 0, st::lineWidth), st::boxBg);
	if (!ovalPath.isEmpty()) {
		p.setClipPath(ovalPath);
	}

	if (hasEmptyPoint) {
		p.fillRect(c.rect, st::boxDividerBg);
	}

	const auto opacity = c.footer ? (1. - _transition.progress) : 1.;
	for (auto k = int(c.chartData.lines.size() - 1); k >= 0; k--) {
		if (paths[k].isEmpty()) {
			continue;
		}
		const auto &line = c.chartData.lines[k];
		p.setPen(Qt::NoPen);
		p.fillPath(paths[k], line.color);
	}
	p.setOpacity(opacity);
	if (!c.footer) {
		constexpr auto kAlphaTextPart = 0.6;
		const auto progress = std::clamp(
			(_transition.progress - kAlphaTextPart) / (1. - kAlphaTextPart),
			0.,
			1.);
		if (progress > 0) {
			auto o = ScopedPainterOpacity(p, progress);
			paintPieText(p, c);
		}
	} else if (_transition.progress) {
		paintZoomedFooter(p, c);
	}

	// Fix ugly outline.
	if (!c.footer || !_transition.progress) {
		p.setBrush(Qt::transparent);
		p.setPen(st::boxBg);
		p.drawPath(ovalPath);
	}

	if (!ovalPath.isEmpty()) {
		p.setClipRect(c.rect, Qt::NoClip);
	}
	p.setOpacity(1. - _transition.progress);
}

void StackLinearChartView::paintZoomed(QPainter &p, const PaintContext &c) {
	if (c.footer) {
		return;
	}

	const auto wasZoomedInRangeXIndices = _transition.zoomedInRangeXIndices;
	saveZoomRange(c);
	const auto &[zoomedStart, zoomedEnd] = _transition.zoomedInRangeXIndices;
	const auto partsData = PiePartsPercentageByIndices(
		c.chartData,
		linesFilterController(),
		_transition.zoomedInRangeXIndices);
	const auto xIndicesChanged = (wasZoomedInRangeXIndices.min != zoomedStart)
		|| (wasZoomedInRangeXIndices.max != zoomedEnd);
	if (xIndicesChanged) {
		const auto wasParts = PiePartsPercentageByIndices(
			c.chartData,
			linesFilterController(),
			wasZoomedInRangeXIndices);
		_changingPieController.setParts(wasParts.parts, partsData.parts);
		if (!_piePartAnimation.animating()) {
			_piePartAnimation.start();
		}
	}
	if (!_changingPieController.isFinished()) {
		_changingPieController.update();
	}
	_pieHasSinglePart = partsData.pieHasSinglePart;
	applyParts(partsData.parts);
	const auto &parts = _changingPieController.isFinished()
		? partsData.parts
		: _changingPieController.current().parts;

	p.fillRect(c.rect + QMargins(0, 0, 0, st::lineWidth), st::boxBg);
	const auto center = QPointF(c.rect.center());
	const auto side = (c.rect.width() / 2.) * kCircleSizeRatio;
	const auto rectF = QRectF(
		center - QPointF(side, side),
		center + QPointF(side, side));

	auto hq = PainterHighQualityEnabler(p);
	auto selectedLineIndex = -1;
	const auto skipTranslation = skipSelectedTranslation();
	for (auto k = 0; k < c.chartData.lines.size(); k++) {
		const auto previous = k
			? parts[k - 1].stackedAngle
			: -180;
		const auto now = parts[k].stackedAngle;

		const auto &line = c.chartData.lines[k];
		p.setBrush(line.color);
		p.setPen(Qt::NoPen);
		const auto textAngle = (previous + kPieAngleOffset)
			+ (now - previous) / 2.;
		const auto partOffset = skipTranslation
			? QPointF()
			: _piePartController.offset(line.id, textAngle);
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
	if (_piePartController.isFinished() && _changingPieController.isFinished()) {
		_piePartAnimation.stop();
	}
	paintPieText(p, c);

	if (selectedLineIndex >= 0) {
		const auto &line = c.chartData.lines[selectedLineIndex];
		auto sum = 0;
		for (auto i = zoomedStart; i <= zoomedEnd; i++) {
			sum += line.y[i];
		}
		sum *= linesFilterController()->alpha(line.id);
		if (sum > 0) {
			PaintDetails(p, line, sum, c.rect);
		}
	}
}

void StackLinearChartView::paintZoomedFooter(
		QPainter &p,
		const PaintContext &c) {
	if (!c.footer) {
		return;
	}
	auto o = ScopedPainterOpacity(p, _transition.progress);
	auto hq = PainterHighQualityEnabler(p);
	const auto &[zoomedStart, zoomedEnd] = _transition.zoomedInLimitXIndices;
	const auto sideW = st::statisticsChartFooterSideWidth;
	const auto width = c.rect.width() - sideW * 2.;
	const auto leftStart = c.rect.x() + sideW;
	const auto &xPercentage = c.chartData.xPercentage;
	auto previousX = leftStart;
	// Read FindStackXIndicesFromRawXPercentages.
	const auto offset = (xPercentage[zoomedEnd] == 1.) ? 0 : 1;
	for (auto i = zoomedStart; i <= zoomedEnd; i++) {
		auto sum = 0.;
		auto lastEnabledId = int(0);
		for (const auto &line : c.chartData.lines) {
			const auto alpha = linesFilterController()->alpha(line.id);
			sum += line.y[i] * alpha;
			if (alpha > 0.) {
				lastEnabledId = line.id;
			}
		}

		const auto columnMargins = QMarginsF(
			(i == zoomedStart) ? sideW : 0,
			0,
			(i == zoomedEnd - offset) ? sideW : 0,
			0);

		const auto next = std::clamp(i + offset, zoomedStart, zoomedEnd);
		const auto xPointPercentage =
			(xPercentage[next] - xPercentage[zoomedStart])
				/ (xPercentage[zoomedEnd] - xPercentage[zoomedStart]);
		const auto xPoint = leftStart + width * xPointPercentage;

		auto stack = 0.;
		for (auto k = int(c.chartData.lines.size() - 1); k >= 0; k--) {
			const auto &line = c.chartData.lines[k];
			const auto visibleHeight = c.rect.height()
				* (line.y[i] * linesFilterController()->alpha(line.id) / sum);
			if (!visibleHeight) {
				continue;
			}
			const auto height = (line.id == lastEnabledId)
				? c.rect.height()
				: visibleHeight;

			const auto column = columnMargins + QRectF(
				previousX,
				stack,
				xPoint - previousX,
				height);

			p.setPen(Qt::NoPen);
			p.fillRect(column, line.color);
			stack += visibleHeight;
		}
		previousX = xPoint;
	}
}

void StackLinearChartView::paintPieText(QPainter &p, const PaintContext &c) {
	constexpr auto kMinPercentage = 0.039;
	if (_transition.progress == 1.) {
		savePieTextParts(c);
	}
	const auto &parts = _changingPieController.isFinished()
		? _transition.textParts
		: _changingPieController.current().parts;

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
	const auto skipTranslation = skipSelectedTranslation();
	for (auto k = 0; k < c.chartData.lines.size(); k++) {
		const auto previous = k
			? parts[k - 1].stackedAngle
			: -180;
		const auto now = parts[k].stackedAngle;
		const auto percentage = parts[k].roundedPercentage;
		if (percentage <= kMinPercentage) {
			continue;
		}

		const auto rText = side * std::sqrt(1. - percentage);
		const auto textAngle = (now == previous)
			? 0.
			: ((previous + kPieAngleOffset) + (now - previous) / 2.);
		const auto textRadians = textAngle * M_PI / 180.;
		const auto scale = (maxScale == minScale)
			? 0.
			: (minScale) + percentage * (maxScale - minScale);
		const auto text = parts[k].percentageText;
		const auto textW = font->width(text);
		const auto textXShift = textW / 2.;
		const auto textYShift = textW / 2.;
		const auto textRectCenter = rectF.center() + QPointF(
			(rText - textXShift * (1. - scale)) * std::cos(textRadians),
			(rText - textYShift * (1. - scale)) * std::sin(textRadians));
		const auto textRect = QRectF(
			textRectCenter - QPointF(textXShift, textYShift),
			textRectCenter + QPointF(textXShift, textYShift));
		const auto partOffset = skipTranslation
			? QPointF()
			: _piePartController.offset(c.chartData.lines[k].id, textAngle);
		p.setTransform(
			QTransform()
				.translate(
					textRectCenter.x() + partOffset.x(),
					textRectCenter.y() + partOffset.y())
				.scale(scale, scale)
				.translate(-textRectCenter.x(), -textRectCenter.y()));
		p.setOpacity(opacity
			* linesFilterController()->alpha(c.chartData.lines[k].id));
		p.drawText(textRect, text, style::al_center);
	}
	p.resetTransform();
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
		const auto p = (crl::now() - was) / float64(st::slideWrapDuration);
		const auto progress = ((p > 0) && (p < 1)) ? (1. - p) : 0.;
		_startedAt[id] = crl::now() - (st::slideWrapDuration * progress);
	}
}

float64 StackLinearChartView::PiePartController::progress(int id) const {
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
		float64 angle) const {
	const auto offset = st::statisticsPieChartPartOffset * progress(id);
	const auto radians = angle * M_PI / 180.;
	return { std::cos(radians) * offset, std::sin(radians) * offset };
}

auto StackLinearChartView::PiePartController::selected() const -> LineId {
	return _selected;
}

bool StackLinearChartView::PiePartController::isFinished() const {
	for (const auto &[id, _] : _startedAt) {
		const auto p = progress(id);
		if (p > 0 && p < 1) {
			return false;
		}
	}
	return true;
}

void StackLinearChartView::handleMouseMove(
		const Data::StatisticalChart &chartData,
		const QRect &rect,
		const QPoint &p) {
	if (_transition.progress < 1) {
		return;
	}
	const auto center = rect.center();
	const auto theta = std::atan2(center.y() - p.y(), (center.x() - p.x()));
	const auto rawAngle = theta * (180. / M_PI) + 90.;
	const auto angle = (rawAngle > 180.) ? (rawAngle - 360.) : rawAngle;
	for (auto k = 0; k < chartData.lines.size(); k++) {
		const auto previous = k
			? _transition.lines[k - 1].angle
			: -180;
		const auto now = _transition.lines[k].angle;
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

bool StackLinearChartView::skipSelectedTranslation() const {
	return _pieHasSinglePart;
}

void StackLinearChartView::paintSelectedXIndex(
		QPainter &p,
		const PaintContext &c,
		int selectedXIndex,
		float64 progress) {
	if ((selectedXIndex < 0) || c.footer) {
		return;
	}
	const auto xPercentageLimits = _transition.zoomedOutXPercentage;
	p.setBrush(st::boxBg);
	const auto i = selectedXIndex;
	const auto isSameToken = (_selectedPoints.lastXIndex == selectedXIndex)
		&& (_selectedPoints.lastHeightLimits.min == c.heightLimits.min)
		&& (_selectedPoints.lastHeightLimits.max == c.heightLimits.max)
		&& (_selectedPoints.lastXLimits.min == xPercentageLimits.min)
		&& (_selectedPoints.lastXLimits.max == xPercentageLimits.max);
	{
		const auto useCache = isSameToken;
		if (!useCache) {
			// Calculate.
			const auto xPoint = c.rect.width()
				* ((c.chartData.xPercentage[i] - xPercentageLimits.min)
					/ (xPercentageLimits.max - xPercentageLimits.min));
			_selectedPoints.xPoint = xPoint;
		}

		{
			[[maybe_unused]] const auto o = ScopedPainterOpacity(
				p,
				p.opacity() * progress * kRulerLineAlpha);
			const auto lineRect = QRectF(
				_selectedPoints.xPoint - (st::lineWidth / 2.),
				c.rect.y(),
				st::lineWidth,
				c.rect.height());
			p.fillRect(lineRect, st::boxTextFg);
		}
	}
	_selectedPoints.lastXIndex = selectedXIndex;
	_selectedPoints.lastHeightLimits = c.heightLimits;
	_selectedPoints.lastXLimits = xPercentageLimits;
}

int StackLinearChartView::findXIndexByPosition(
		const Data::StatisticalChart &chartData,
		const Limits &xPercentageLimits,
		const QRect &rect,
		float64 x) {
	if (_transition.progress == 1.) {
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
	const auto &[localStart, localEnd] = _transition.zoomedOutXIndices;
	const auto rawXPercentage = anim::interpolateF(
		_transition.zoomedOutXPercentage.min,
		_transition.zoomedOutXPercentage.max,
		pointerRatio);
	const auto it = ranges::lower_bound(
		chartData.xPercentage,
		rawXPercentage);
	const auto left = rawXPercentage - (*(it - 1));
	const auto right = (*it) - rawXPercentage;
	const auto nearestXPercentageIt = ((right) > (left)) ? (it - 1) : it;
	return std::clamp(
		std::distance(begin(chartData.xPercentage), nearestXPercentageIt),
		std::ptrdiff_t(localStart),
		std::ptrdiff_t(localEnd));
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

auto StackLinearChartView::maybeLocalZoom(
		const LocalZoomArgs &args) -> LocalZoomResult {
	// 8 days.
	constexpr auto kLimitLength = int(8);
	// 1 day in middle of limits.
	constexpr auto kRangeLength = int(0);
	constexpr auto kLeftSide = int(kLimitLength / 2 + kRangeLength);
	constexpr auto kRightSide = int(kLimitLength / 2) + int(1);

	_transition.progress = args.progress;
	if (args.type == LocalZoomArgs::Type::SkipCalculation) {
		return { true, _transition.zoomedInLimit, _transition.zoomedInRange };
	} else if (args.type == LocalZoomArgs::Type::CheckAvailability) {
		return { .hasZoom = true };
	} else if (args.type == LocalZoomArgs::Type::Prepare) {
		_transition.pendingPrepareToZoomIn = true;
	}
	const auto xIndex = args.xIndex;
	const auto &xPercentage = args.chartData.xPercentage;
	const auto backIndex = (xPercentage.size() - 1);
	const auto localRangeIndex = (xIndex == backIndex)
		? (backIndex - kRangeLength)
		: xIndex;
	_transition.zoomedInRange = {
		xPercentage[localRangeIndex],
		xPercentage[localRangeIndex + kRangeLength],
	};
	_transition.zoomedInRangeXIndices = {
		float64(localRangeIndex),
		float64(localRangeIndex + kRangeLength),
	};
	_transition.zoomedInLimitXIndices = (xIndex < kLeftSide)
		? Limits{ 0, kLeftSide + kRightSide }
		: (xIndex > (backIndex - kRightSide - kRangeLength))
		? Limits{ float64(backIndex - kLimitLength), float64(backIndex) }
		: Limits{ float64(xIndex - kLeftSide), float64(xIndex + kRightSide) };
	_transition.zoomedInLimit = {
		anim::interpolateF(
			0.,
			xPercentage[_transition.zoomedInLimitXIndices.min],
			args.progress),
		anim::interpolateF(
			1.,
			xPercentage[_transition.zoomedInLimitXIndices.max],
			args.progress),
	};
	const auto oneDay = std::abs(xPercentage[localRangeIndex]
		- xPercentage[localRangeIndex + ((xIndex == backIndex) ? -1 : 1)]);
	// Read FindStackXIndicesFromRawXPercentages.
	const auto offset = (_transition.zoomedInLimitXIndices.max == backIndex)
		? -oneDay
		: 0.;
	const auto resultRange = Limits{
		InterpolationRatio(
			_transition.zoomedInLimit.min,
			_transition.zoomedInLimit.max,
			_transition.zoomedInRange.min + oneDay * 0.25 + offset),
		InterpolationRatio(
			_transition.zoomedInLimit.min,
			_transition.zoomedInLimit.max,
			_transition.zoomedInRange.max + oneDay * 0.75 + offset),
	};
	return { true, _transition.zoomedInLimitXIndices, resultRange };
}

} // namespace Statistic
