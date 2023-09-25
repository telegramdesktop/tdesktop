/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "statistics/view/stack_linear_chart_view.h"

#include "data/data_statistics.h"
#include "statistics/point_details_widget.h"
#include "statistics/view/stack_chart_common.h"
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

inline float64 InterpolationRatio(float64 from, float64 to, float64 result) {
	return (result - from) / (to - from);
};

} // namespace

StackLinearChartView::StackLinearChartView() = default;

StackLinearChartView::~StackLinearChartView() = default;

void StackLinearChartView::paint(QPainter &p, const PaintContext &c) {
	if (!_transitionProgress && !c.footer) {
		prepareZoom(c, TransitionStep::ZoomedOut);
	}
	if (_pendingPrepareCachedTransition) {
		_pendingPrepareCachedTransition = false;
		prepareZoom(c, TransitionStep::PrepareToZoomIn);
	}

	StackLinearChartView::paintChartOrZoomAnimation(p, c);
}

void StackLinearChartView::prepareZoom(
		const PaintContext &c,
		TransitionStep step) {
	if (step == TransitionStep::ZoomedOut) {
		constexpr auto kOffset = float64(2);
		_cachedTransition.zoomedOutXIndices = {
			float64(std::max(0., c.xIndices.min - kOffset)),
			float64(std::min(
				float64(c.chartData.xPercentage.size() - 1),
				c.xIndices.max + kOffset)),
		};
	} else if (step == TransitionStep::PrepareToZoomIn) {
		const auto &[zoomedStart, zoomedEnd] =
			_cachedTransition.zoomedOutXIndices;
		_cachedTransition.lines = std::vector<Transition::TransitionLine>(
			c.chartData.lines.size(),
			Transition::TransitionLine());

		const auto xPercentageLimits = Limits{
			c.chartData.xPercentage[_cachedTransition.zoomedOutXIndices.min],
			c.chartData.xPercentage[_cachedTransition.zoomedOutXIndices.max],
		};

		for (auto j = 0; j < 2; j++) {
			const auto i = int((j == 1) ? zoomedEnd : zoomedStart);
			auto stackOffset = 0;
			auto sum = 0.;
			auto drawingLinesCount = 0;
			for (const auto &line : c.chartData.lines) {
				if (!isEnabled(line.id)) {
					continue;
				}
				if (line.y[i] > 0) {
					sum += line.y[i] * alpha(line.id);
					drawingLinesCount++;
				}
			}

			for (auto k = 0; k < c.chartData.lines.size(); k++) {
				auto &linePoint = (j
					? _cachedTransition.lines[k].end
					: _cachedTransition.lines[k].start);
				const auto &line = c.chartData.lines[k];
				if (!isEnabled(line.id)) {
					continue;
				}
				const auto yPercentage = (drawingLinesCount == 1)
					? (line.y[i] ? alpha(line.id) : 0)
					: (sum ? (line.y[i] * alpha(line.id) / sum) : 0);

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
		applyParts(_cachedTransition.textParts);
	}
}

void StackLinearChartView::applyParts(const std::vector<PiePartData> &parts) {
	for (auto k = 0; k < parts.size(); k++) {
		_cachedTransition.lines[k].angle = parts[k].stackedAngle;
	}
}

void StackLinearChartView::saveZoomRange(const PaintContext &c) {
	const auto zoomedXPercentage = Limits{
		anim::interpolateF(
			_localZoom.limit.min,
			_localZoom.limit.max,
			c.xPercentageLimits.min),
		anim::interpolateF(
			_localZoom.limit.min,
			_localZoom.limit.max,
			c.xPercentageLimits.max),
	};
	const auto zoomedXIndices = FindNearestElements(
		c.chartData.xPercentage,
		zoomedXPercentage);
	_localZoom.rangeIndices = zoomedXIndices;
	_localZoom.range = zoomedXPercentage;
}

void StackLinearChartView::savePieTextParts(const PaintContext &c) {
	_cachedTransition.textParts = partsPercentage(
		c.chartData,
		_localZoom.rangeIndices);
}

auto StackLinearChartView::partsPercentage(
		const Data::StatisticalChart &chartData,
		const Limits &xIndices) -> std::vector<PiePartData> {
	auto result = std::vector<PiePartData>();
	result.reserve(chartData.lines.size());
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
		const auto percentage = 0.01
			* std::round((sums[k] / float64(totalSum)) * 100.);
		stackedPercentage += percentage;
		result.push_back({ percentage, stackedPercentage * 360. - 180. });
	}
	return result;
}

void StackLinearChartView::paintChartOrZoomAnimation(
		QPainter &p,
		const PaintContext &c) {
	if (_transitionProgress == 1.) {
		if (c.footer) {
			paintZoomedFooter(p, c);
		} else {
			paintZoomed(p, c);
		}
		return p.setOpacity(0.);
	}
	const auto hasTransitionAnimation = _transitionProgress && !c.footer;
	const auto &[localStart, localEnd] = c.footer
		? Limits{ 0., float64(c.chartData.xPercentage.size() - 1) }
		: _cachedTransition.zoomedOutXIndices;
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

	const auto xPercentageLimits = Limits{
		c.chartData.xPercentage[localStart],
		c.chartData.xPercentage[localEnd],
	};

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
		rectPath.addRect(c.rect);
		const auto r = anim::interpolateF(
			1.,
			kCircleSizeRatio,
			_transitionProgress);
		const auto per = anim::interpolateF(0., 100., _transitionProgress);
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

		for (auto k = 0; k < c.chartData.lines.size(); k++) {
			const auto &line = c.chartData.lines[k];
			if (!isEnabled(line.id)) {
				continue;
			}
			if (line.y[i] > 0) {
				sum += line.y[i] * alpha(line.id);
				drawingLinesCount++;
			}
			lastEnabled = k;
		}

		for (auto k = 0; k < c.chartData.lines.size(); k++) {
			const auto &line = c.chartData.lines[k];
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

			const auto xPoint = c.rect.width()
				* ((c.chartData.xPercentage[i] - xPercentageLimits.min)
					/ (xPercentageLimits.max - xPercentageLimits.min));

			if (!yPercentage && isLastLine) {
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
						|| center.x() < (c.rect.width() * ((i == localEnd)
							? 1.
							: ((c.chartData.xPercentage[i + 1] - xLimits.min)
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
				const auto bottomLeft = QPointF(c.rect.x(), rect::bottom(c.rect));
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
					chartPath.lineTo(rect::right(c.rect), rect::bottom(c.rect));
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

	const auto opacity = c.footer ? (1. - _transitionProgress) : 1.;
	for (auto k = int(c.chartData.lines.size() - 1); k >= 0; k--) {
		if (paths[k].isEmpty()) {
			continue;
		}
		const auto &line = c.chartData.lines[k];
		p.setOpacity(alpha(line.id) * opacity);
		p.setPen(Qt::NoPen);
		p.fillPath(paths[k], line.color);
	}
	p.setOpacity(opacity);
	if (!c.footer) {
		constexpr auto kAlphaTextPart = 0.6;
		const auto progress = std::clamp(
			(_transitionProgress - kAlphaTextPart) / (1. - kAlphaTextPart),
			0.,
			1.);
		if (progress > 0) {
			auto o = ScopedPainterOpacity(p, progress);
			paintPieText(p, c);
		}
	} else {
		paintZoomedFooter(p, c);
	}

	// Fix ugly outline.
	if (!c.footer || !_transitionProgress) {
		p.setBrush(Qt::transparent);
		p.setPen(st::boxBg);
		p.drawPath(ovalPath);
	}

	if (!ovalPath.isEmpty()) {
		p.setClipRect(c.rect, Qt::NoClip);
	}
	p.setOpacity(1. - _transitionProgress);
}

void StackLinearChartView::paintZoomed(QPainter &p, const PaintContext &c) {
	if (c.footer) {
		return;
	}

	saveZoomRange(c);
	const auto parts = partsPercentage(c.chartData, _localZoom.rangeIndices);
	applyParts(parts);

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
	if (_piePartController.isFinished()) {
		_piePartAnimation.stop();
	}
	paintPieText(p, c);

	if (selectedLineIndex >= 0) {
		const auto &[zoomedStart, zoomedEnd] = _localZoom.rangeIndices;
		const auto &line = c.chartData.lines[selectedLineIndex];
		auto sum = 0;
		for (auto i = zoomedStart; i <= zoomedEnd; i++) {
			sum += line.y[i];
		}
		sum *= alpha(line.id);
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
	auto o = ScopedPainterOpacity(p, _transitionProgress);
	auto hq = PainterHighQualityEnabler(p);
	const auto &[zoomedStart, zoomedEnd] = _localZoom.limitIndices;
	const auto &[leftStart, w] = ComputeLeftStartAndStep(
		c.chartData,
		{
			c.chartData.xPercentage[zoomedStart],
			c.chartData.xPercentage[zoomedEnd],
		},
		c.rect,
		zoomedStart);
	for (auto i = zoomedStart; i <= zoomedEnd; i++) {
		auto sum = 0.;
		auto lastEnabledId = int(0);
		for (const auto &line : c.chartData.lines) {
			if (!isEnabled(line.id)) {
				continue;
			}
			sum += line.y[i] * alpha(line.id);
			lastEnabledId = line.id;
		}

		auto stack = 0.;
		for (auto k = int(c.chartData.lines.size() - 1); k >= 0; k--) {
			const auto &line = c.chartData.lines[k];
			if (!isEnabled(line.id)) {
				continue;
			}
			const auto visibleHeight = c.rect.height() * (line.y[i] / sum);
			const auto height = (line.id == lastEnabledId)
				? c.rect.height()
				: visibleHeight;

			const auto column = QRectF(
				leftStart + (i - zoomedStart) * w,
				stack,
				w,
				height);

			p.setPen(Qt::NoPen);
			p.fillRect(column, line.color);
			stack += visibleHeight;
		}
	}
}

void StackLinearChartView::paintPieText(QPainter &p, const PaintContext &c) {
	if (_transitionProgress == 1.) {
		savePieTextParts(c);
	}
	const auto &parts = _cachedTransition.textParts;

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

bool StackLinearChartView::skipSelectedTranslation() const {
	return (_entries.size() == (_cachedTransition.lines.size() - 1));
}

void StackLinearChartView::paintSelectedXIndex(
		QPainter &p,
		const PaintContext &c,
		int selectedXIndex,
		float64 progress) {
	if (selectedXIndex < 0) {
		return;
	}
	p.setBrush(st::boxBg);
	const auto r = st::statisticsDetailsDotRadius;
	const auto i = selectedXIndex;
	const auto isSameToken = (_selectedPoints.lastXIndex == selectedXIndex)
		&& (_selectedPoints.lastHeightLimits.min == c.heightLimits.min)
		&& (_selectedPoints.lastHeightLimits.max == c.heightLimits.max)
		&& (_selectedPoints.lastXLimits.min == c.xPercentageLimits.min)
		&& (_selectedPoints.lastXLimits.max == c.xPercentageLimits.max);
	for (const auto &line : c.chartData.lines) {
		const auto lineAlpha = alpha(line.id);
		const auto useCache = isSameToken
			|| (lineAlpha < 1. && !isEnabled(line.id));
		if (!useCache) {
			// Calculate.
			const auto xPoint = c.rect.width()
				* ((c.chartData.xPercentage[i] - c.xPercentageLimits.min)
					/ (c.xPercentageLimits.max - c.xPercentageLimits.min));
			const auto yPercentage = (line.y[i] - c.heightLimits.min)
				/ float64(c.heightLimits.max - c.heightLimits.min);
			_selectedPoints.points[line.id] = QPointF(xPoint, 0)
				+ c.rect.topLeft();
		}

		{
			const auto lineRect = QRectF(
				c.rect.x()
					+ begin(_selectedPoints.points)->second.x()
					- (st::lineWidth / 2.),
				c.rect.y(),
				st::lineWidth,
				c.rect.height());
			p.fillRect(lineRect, st::windowSubTextFg);
		}
	}
	_selectedPoints.lastXIndex = selectedXIndex;
	_selectedPoints.lastHeightLimits = c.heightLimits;
	_selectedPoints.lastXLimits = c.xPercentageLimits;
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

auto StackLinearChartView::maybeLocalZoom(
		const LocalZoomArgs &args) -> LocalZoomResult {
	// 7 days.
	constexpr auto kLimitLength = int(27);
	// 1 day in middle of limits.
	constexpr auto kRangeLength = int(1);
	constexpr auto kLeftSide = int(kLimitLength / 2);
	constexpr auto kRightSide = int(kLimitLength / 2 + kRangeLength);

	_transitionProgress = args.progress;
	if (args.type == LocalZoomArgs::Type::SkipCalculation) {
		return { true, _localZoom.limit, _localZoom.range };
	} else if (args.type == LocalZoomArgs::Type::CheckAvailability) {
		return { .hasZoom = true };
	} else if (args.type == LocalZoomArgs::Type::Prepare) {
		_pendingPrepareCachedTransition = true;
	}
	const auto xIndex = args.xIndex;
	const auto &xPercentage = args.chartData.xPercentage;
	const auto backIndex = (xPercentage.size() - 1);
	const auto localRangeIndex = (xIndex == backIndex)
		? (backIndex - kRangeLength)
		: xIndex;
	_localZoom.range = {
		xPercentage[localRangeIndex],
		xPercentage[localRangeIndex + kRangeLength],
	};
	_localZoom.rangeIndices = {
		float64(localRangeIndex),
		float64(localRangeIndex + kRangeLength),
	};
	if (xIndex < kLeftSide) {
		_localZoom.limitIndices = { 0, kLimitLength };
	} else if (xIndex > (backIndex - kRightSide)) {
		_localZoom.limitIndices = {
			float64(backIndex - kLimitLength),
			float64(backIndex),
		};
	} else {
		_localZoom.limitIndices = {
			float64(xIndex - kLeftSide),
			float64(xIndex + kRightSide),
		};
	}
	_localZoom.limit = {
		anim::interpolateF(
			0.,
			xPercentage[_localZoom.limitIndices.min],
			args.progress),
		anim::interpolateF(
			1.,
			xPercentage[_localZoom.limitIndices.max],
			args.progress),
	};
	const auto resultRange = Limits{
		InterpolationRatio(
			_localZoom.limit.min,
			_localZoom.limit.max,
			_localZoom.range.min),
		InterpolationRatio(
			_localZoom.limit.min,
			_localZoom.limit.max,
			_localZoom.range.max),
	};
	return { true, _localZoom.limit, resultRange };
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
