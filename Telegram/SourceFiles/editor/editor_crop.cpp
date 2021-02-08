/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/editor_crop.h"

#include "styles/style_boxes.h"

namespace Editor {
namespace {

constexpr auto kETL = Qt::TopEdge | Qt::LeftEdge;
constexpr auto kETR = Qt::TopEdge | Qt::RightEdge;
constexpr auto kEBL = Qt::BottomEdge | Qt::LeftEdge;
constexpr auto kEBR = Qt::BottomEdge | Qt::RightEdge;
constexpr auto kEAll = Qt::TopEdge
	| Qt::LeftEdge
	| Qt::BottomEdge
	| Qt::RightEdge;

std::tuple<int, int, int, int> RectEdges(const QRect &r) {
	return { r.left(), r.top(), r.left() + r.width(), r.top() + r.height() };
}

QPoint PointOfEdge(Qt::Edges e, const QRect &r) {
	switch(e) {
	case kETL: return QPoint(r.x(), r.y());
	case kETR: return QPoint(r.x() + r.width(), r.y());
	case kEBL: return QPoint(r.x(), r.y() + r.height());
	case kEBR: return QPoint(r.x() + r.width(), r.y() + r.height());
	default: return QPoint();
	}
}

} // namespace

Crop::Crop(
	not_null<Ui::RpWidget*> parent,
	const PhotoModifications &modifications,
	const QSize &imageSize)
: RpWidget(parent)
, _pointSize(st::cropPointSize)
, _pointSizeH(_pointSize / 2.)
, _innerMargins(QMarginsF(_pointSizeH, _pointSizeH, _pointSizeH, _pointSizeH)
	.toMargins())
, _offset(_innerMargins.left(), _innerMargins.top())
, _edgePointMargins(_pointSizeH, _pointSizeH, -_pointSizeH, -_pointSizeH) {

	_angle = modifications.angle;
	_flipped = modifications.flipped;
	_cropRect = modifications.crop;
	if (_cropRect.isValid()) {
		const auto inner = QRect(QPoint(), imageSize);
		_innerRect = QRect(
			QPoint(),
			QMatrix().rotate(-_angle).mapRect(inner).size());
	}

	setMouseTracking(true);

	paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(this);

		p.fillPath(_painterPath, st::photoCropFadeBg);

		paintPoints(p);

	}, lifetime());

}

void Crop::applyTransform(QRect geometry, int angle, bool flipped) {
	if (geometry.isEmpty()) {
		return;
	}
	setGeometry(geometry);

	const auto nowInner = QRect(QPoint(), geometry.size()) - _innerMargins;
	const auto wasInner = _innerRect.isEmpty() ? nowInner : _innerRect;
	const auto nowInnerF = QRectF(QPointF(), QSizeF(nowInner.size()));
	const auto wasInnerF = QRectF(QPointF(), QSizeF(wasInner.size()));

	_innerRect = nowInner;

	if (_cropRect.isEmpty()) {
		setCropRect(_innerRect.translated(-_offset));
	}

	const auto angleTo = (angle - _angle) * (flipped ? -1 : 1);
	const auto flippedChanges = (_flipped != flipped);

	const auto nowInnerCenter = nowInnerF.center();
	const auto nowInnerRotated = QMatrix()
		.translate(nowInnerCenter.x(), nowInnerCenter.y())
		.rotate(-angleTo)
		.translate(-nowInnerCenter.x(), -nowInnerCenter.y())
		.mapRect(nowInnerF);

	const auto nowCropRect = resizedCropRect(wasInnerF, nowInnerRotated)
		.translated(nowInnerRotated.topLeft());

	const auto nowInnerRotatedCenter = nowInnerRotated.center();

	setCropRect(QMatrix()
		.translate(nowInnerRotatedCenter.x(), nowInnerRotatedCenter.y())
		.rotate(angleTo)
		.scale(flippedChanges ? -1 : 1, 1)
		.translate(-nowInnerRotatedCenter.x(), -nowInnerRotatedCenter.y())
		.mapRect(nowCropRect)
		.toRect());

	{
		// Check boundaries.
		const auto p = _cropRectPaint.center();
		computeDownState(p);
		performMove(p);
		clearDownState();
	}

	_flipped = flipped;
	_angle = angle;
}

void Crop::paintPoints(Painter &p) {
	p.save();
	p.setPen(Qt::NoPen);
	p.setBrush(st::photoCropPointFg);
	for (const auto &r : ranges::views::values(_edges)) {
		p.drawRect(r);
	}
	p.restore();
}

void Crop::setCropRect(QRect &&rect) {
	_cropRect = std::move(rect);
	_cropRectPaint = _cropRect.translated(_offset);
	updateEdges();

	_painterPath.clear();
	_painterPath.addRect(_innerRect);
	_painterPath.addRect(_cropRectPaint);
}

void Crop::setCropRectPaint(QRect &&rect) {
	rect.translate(-_offset);
	setCropRect(std::move(rect));
}

void Crop::updateEdges() {
	const auto &s = _pointSize;
	const auto &m = _edgePointMargins;
	const auto &r = _cropRectPaint;
	for (const auto &e : { kETL, kETR, kEBL, kEBR }) {
		_edges[e] = QRectF(PointOfEdge(e, r), QSize(s, s)) + m;
	}
}

Qt::Edges Crop::mouseState(const QPoint &p) {
	for (const auto &[e, r] : _edges) {
		if (r.contains(p)) {
			return e;
		}
	}
	if (_cropRectPaint.contains(p)) {
		return kEAll;
	}
	return Qt::Edges();
}

void Crop::mousePressEvent(QMouseEvent *e) {
	computeDownState(e->pos());
}

void Crop::mouseReleaseEvent(QMouseEvent *e) {
	clearDownState();
}

void Crop::computeDownState(const QPoint &p) {
	const auto edge = mouseState(p);
	const auto &inner = _innerRect;
	const auto &crop = _cropRectPaint;
	const auto [iLeft, iTop, iRight, iBottom] = RectEdges(inner);
	const auto [cLeft, cTop, cRight, cBottom] = RectEdges(crop);
	_down = InfoAtDown{
		.rect = crop,
		.edge = edge,
		.point = (p - PointOfEdge(edge, crop)),
		.borders = InfoAtDown::Borders{
			.left = iLeft - cLeft,
			.right = iRight - cRight,
			.top = iTop - cTop,
			.bottom = iBottom - cBottom,
		}
	};
}

void Crop::clearDownState() {
	_down = InfoAtDown();
}

void Crop::performCrop(const QPoint &pos) {
	const auto &crop = _down.rect;
	const auto &pressedEdge = _down.edge;
	const auto hasLeft = (pressedEdge & Qt::LeftEdge);
	const auto hasTop = (pressedEdge & Qt::TopEdge);
	const auto hasRight = (pressedEdge & Qt::RightEdge);
	const auto hasBottom = (pressedEdge & Qt::BottomEdge);
	const auto diff = [&] {
		const auto diff = pos - PointOfEdge(pressedEdge, crop) - _down.point;
		const auto hFactor = hasLeft ? 1 : -1;
		const auto vFactor = hasTop ? 1 : -1;
		const auto &borders = _down.borders;

		const auto hMin = hFactor * crop.width() - hFactor * st::cropMinSize;
		const auto vMin = vFactor * crop.height() - vFactor * st::cropMinSize;

		const auto x = std::clamp(
			diff.x(),
			hasLeft ? borders.left : hMin,
			hasLeft ? hMin : borders.right);
		const auto y = std::clamp(
			diff.y(),
			hasTop ? borders.top : vMin,
			hasTop ? vMin : borders.bottom);
		if (_keepAspectRatio) {
			const auto minDiff = std::min(std::abs(x), std::abs(y));
			return QPoint(minDiff * hFactor, minDiff * vFactor);
		}
		return QPoint(x, y);
	}();
	setCropRectPaint(crop - QMargins(
		hasLeft ? diff.x() : 0,
		hasTop ? diff.y() : 0,
		hasRight ? -diff.x() : 0,
		hasBottom ? -diff.y() : 0));
}

void Crop::performMove(const QPoint &pos) {
	const auto &inner = _down.rect;
	const auto &b = _down.borders;
	const auto diffX = std::clamp(pos.x() - _down.point.x(), b.left, b.right);
	const auto diffY = std::clamp(pos.y() - _down.point.y(), b.top, b.bottom);
	setCropRectPaint(inner.translated(diffX, diffY));
}

void Crop::mouseMoveEvent(QMouseEvent *e) {
	const auto pos = e->pos();
	const auto pressedEdge = _down.edge;

	if (pressedEdge) {
		if (pressedEdge == kEAll) {
			performMove(pos);
		} else if (pressedEdge) {
			performCrop(pos);
		}
		update();
	}

	const auto edge = pressedEdge ? pressedEdge : mouseState(pos);

	const auto cursor = ((edge == kETL) || (edge == kEBR))
		? style::cur_sizefdiag
		: ((edge == kETR) || (edge == kEBL))
		? style::cur_sizebdiag
		: (edge == kEAll)
		? style::cur_sizeall
		: style::cur_default;
	setCursor(cursor);
}

QRect Crop::innerRect() const {
	return _innerRect;
}

style::margins Crop::cropMargins() const {
	return _innerMargins;
}

QRect Crop::saveCropRect(const QRect &from, const QRect &to) {
	return resizedCropRect(QRectF(from), QRectF(to)).toRect();
}

QRectF Crop::resizedCropRect(const QRectF &from, const QRectF &to) {
	const auto ratioW = to.width() / float64(from.width());
	const auto ratioH = to.height() / float64(from.height());
	const auto &min = float64(st::cropMinSize);
	const auto &r = _cropRect;

	return QRectF(
		r.x() * ratioW,
		r.y() * ratioH,
		std::max(r.width() * ratioW, min),
		std::max(r.height() * ratioH, min));
}

} // namespace Editor
