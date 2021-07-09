/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/editor_crop.h"

#include "styles/style_editor.h"

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

std::tuple<int, int, int, int> RectEdges(const QRectF &r) {
	return { r.left(), r.top(), r.left() + r.width(), r.top() + r.height() };
}

QPoint PointOfEdge(Qt::Edges e, const QRectF &r) {
	switch(e) {
	case kETL: return QPoint(r.x(), r.y());
	case kETR: return QPoint(r.x() + r.width(), r.y());
	case kEBL: return QPoint(r.x(), r.y() + r.height());
	case kEBR: return QPoint(r.x() + r.width(), r.y() + r.height());
	default: return QPoint();
	}
}

QSizeF FlipSizeByRotation(const QSizeF &size, int angle) {
	return (((angle / 90) % 2) == 1) ? size.transposed() : size;
}

} // namespace

Crop::Crop(
	not_null<Ui::RpWidget*> parent,
	const PhotoModifications &modifications,
	const QSize &imageSize,
	EditorData data)
: RpWidget(parent)
, _pointSize(st::photoEditorCropPointSize)
, _pointSizeH(_pointSize / 2.)
, _innerMargins(QMarginsF(_pointSizeH, _pointSizeH, _pointSizeH, _pointSizeH)
	.toMargins())
, _offset(_innerMargins.left(), _innerMargins.top())
, _edgePointMargins(_pointSizeH, _pointSizeH, -_pointSizeH, -_pointSizeH)
, _imageSize(imageSize)
, _data(std::move(data))
, _cropOriginal(modifications.crop.isValid()
	? modifications.crop
	: QRectF(QPoint(), _imageSize))
, _angle(modifications.angle)
, _flipped(modifications.flipped)
, _keepAspectRatio(_data.keepAspectRatio) {

	setMouseTracking(true);

	paintRequest(
	) | rpl::start_with_next([=] {
		Painter p(this);

		p.fillPath(_painterPath, st::photoCropFadeBg);
		paintPoints(p);
	}, lifetime());

}

void Crop::applyTransform(
		const QRect &geometry,
		int angle,
		bool flipped,
		const QSizeF &scaledImageSize) {
	if (geometry.isEmpty()) {
		return;
	}
	setGeometry(geometry);
	_innerRect = QRectF(_offset, FlipSizeByRotation(scaledImageSize, angle));
	_ratio.w = scaledImageSize.width() / float64(_imageSize.width());
	_ratio.h = scaledImageSize.height() / float64(_imageSize.height());
	_flipped = flipped;
	_angle = angle;

	const auto cropHolder = QRectF(QPointF(), scaledImageSize);
	const auto cropHolderCenter = cropHolder.center();

	auto matrix = QMatrix()
		.translate(cropHolderCenter.x(), cropHolderCenter.y())
		.scale(flipped ? -1 : 1, 1)
		.rotate(angle)
		.translate(-cropHolderCenter.x(), -cropHolderCenter.y());

	const auto cropHolderRotated = matrix.mapRect(cropHolder);

	auto cropPaint = matrix
		.scale(_ratio.w, _ratio.h)
		.mapRect(_cropOriginal)
		.translated(
			-cropHolderRotated.x() + _offset.x(),
			-cropHolderRotated.y() + _offset.y());

	// Check boundaries.
	const auto min = float64(st::photoEditorCropMinSize);
	if ((cropPaint.width() < min) || (cropPaint.height() < min)) {
		cropPaint.setWidth(std::max(min, cropPaint.width()));
		cropPaint.setHeight(std::max(min, cropPaint.height()));

		const auto p = cropPaint.center().toPoint();
		setCropPaint(std::move(cropPaint));

		computeDownState(p);
		performMove(p);
		clearDownState();

		convertCropPaintToOriginal();
	} else {
		setCropPaint(std::move(cropPaint));
	}
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

void Crop::setCropPaint(QRectF &&rect) {
	_cropPaint = std::move(rect);

	updateEdges();

	_painterPath.clear();
	_painterPath.addRect(_innerRect);
	if (_data.cropType == EditorData::CropType::Ellipse) {
		_painterPath.addEllipse(_cropPaint);
	} else {
		_painterPath.addRect(_cropPaint);
	}
}

void Crop::convertCropPaintToOriginal() {
	const auto cropHolder = QMatrix()
		.scale(_ratio.w, _ratio.h)
		.mapRect(QRectF(QPointF(), FlipSizeByRotation(_imageSize, _angle)));
	const auto cropHolderCenter = cropHolder.center();

	const auto matrix = QMatrix()
		.translate(cropHolderCenter.x(), cropHolderCenter.y())
		.rotate(-_angle)
		.scale((_flipped ? -1 : 1) * 1. / _ratio.w, 1. / _ratio.h)
		.translate(-cropHolderCenter.x(), -cropHolderCenter.y());

	const auto cropHolderRotated = matrix.mapRect(cropHolder);

	_cropOriginal = matrix
		.mapRect(QRectF(_cropPaint).translated(-_offset))
		.translated(
			-cropHolderRotated.x(),
			-cropHolderRotated.y());
}

void Crop::updateEdges() {
	const auto &s = _pointSize;
	const auto &m = _edgePointMargins;
	const auto &r = _cropPaint;
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
	if (_cropPaint.contains(p)) {
		return kEAll;
	}
	return Qt::Edges();
}

void Crop::mousePressEvent(QMouseEvent *e) {
	computeDownState(e->pos());
}

void Crop::mouseReleaseEvent(QMouseEvent *e) {
	clearDownState();
	convertCropPaintToOriginal();
}

void Crop::computeDownState(const QPoint &p) {
	const auto edge = mouseState(p);
	const auto &inner = _innerRect;
	const auto &crop = _cropPaint;
	const auto [iLeft, iTop, iRight, iBottom] = RectEdges(inner);
	const auto [cLeft, cTop, cRight, cBottom] = RectEdges(crop);
	_down = InfoAtDown{
		.rect = crop,
		.edge = edge,
		.point = (p - PointOfEdge(edge, crop)),
		.cropRatio = (_cropOriginal.width() / _cropOriginal.height()),
		.borders = InfoAtDown::Borders{
			.left = iLeft - cLeft,
			.right = iRight - cRight,
			.top = iTop - cTop,
			.bottom = iBottom - cBottom,
		}
	};
	if (_keepAspectRatio && (edge != kEAll)) {
		const auto hasLeft = (edge & Qt::LeftEdge);
		const auto hasTop = (edge & Qt::TopEdge);

		const auto xSign = hasLeft ? -1 : 1;
		const auto ySign = hasTop ? -1 : 1;

		auto &xSide = (hasLeft ? _down.borders.left : _down.borders.right);
		auto &ySide = (hasTop ? _down.borders.top : _down.borders.bottom);

		const auto min = std::abs(std::min(xSign * xSide, ySign * ySide));
		const auto xIsMin = ((xSign * xSide) < (ySign * ySide));
		xSide = xSign * min;
		ySide = ySign * min;
		if (!xIsMin) {
			xSide *= _down.cropRatio;
		} else {
			ySide /= _down.cropRatio;
		}
	}
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
		auto diff = pos - PointOfEdge(pressedEdge, crop) - _down.point;
		const auto xFactor = hasLeft ? 1 : -1;
		const auto yFactor = hasTop ? 1 : -1;
		const auto &borders = _down.borders;
		const auto &cropRatio = _down.cropRatio;
		if (_keepAspectRatio) {
			const auto diffSign = xFactor * yFactor;
			diff = (cropRatio != 1.)
				? QPoint(diff.x(), (1. / cropRatio) * diff.x() * diffSign)
				// For square/circle.
				: ((diff.x() * xFactor) < (diff.y() * yFactor))
				? QPoint(diff.x(), diff.x() * diffSign)
				: QPoint(diff.y() * diffSign, diff.y());
		}

		const auto &minSize = st::photoEditorCropMinSize;
		const auto xMin = xFactor * int(crop.width() - minSize);
		// const auto xMin = int(xFactor * crop.width()
		// 	- xFactor * minSize * ((cropRatio > 1.) ? cropRatio : 1.));
		const auto yMin = yFactor * int(crop.height() - minSize);
		// const auto yMin = int(yFactor * crop.height()
		// 	- yFactor * minSize * ((cropRatio < 1.) ? (1. / cropRatio) : 1.));

		const auto x = std::clamp(
			diff.x(),
			hasLeft ? borders.left : xMin,
			hasLeft ? xMin : borders.right);
		const auto y = std::clamp(
			diff.y(),
			hasTop ? borders.top : yMin,
			hasTop ? yMin : borders.bottom);
		return QPoint(x, y);
	}();
	setCropPaint(crop - QMargins(
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
	setCropPaint(inner.translated(diffX, diffY));
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

style::margins Crop::cropMargins() const {
	return _innerMargins;
}

QRect Crop::saveCropRect() {
	const auto savedCrop = _cropOriginal.toRect();
	return (!savedCrop.topLeft().isNull() || (savedCrop.size() != _imageSize))
		? savedCrop
		: QRect();
}

} // namespace Editor
