/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/editor_crop.h"

#include "ui/userpic_view.h"
#include "styles/style_editor.h"
#include "styles/style_basic.h"
#include "styles/style_dialogs.h"

namespace Editor {
namespace {

constexpr auto kETL = Qt::TopEdge | Qt::LeftEdge;
constexpr auto kETR = Qt::TopEdge | Qt::RightEdge;
constexpr auto kEBL = Qt::BottomEdge | Qt::LeftEdge;
constexpr auto kEBR = Qt::BottomEdge | Qt::RightEdge;
constexpr auto kEL = Qt::Edges(Qt::LeftEdge);
constexpr auto kER = Qt::Edges(Qt::RightEdge);
constexpr auto kET = Qt::Edges(Qt::TopEdge);
constexpr auto kEB = Qt::Edges(Qt::BottomEdge);
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
	case kEL: return QPoint(r.x(), r.y() + (r.height() / 2.));
	case kER: return QPoint(r.x() + r.width(), r.y() + (r.height() / 2.));
	case kET: return QPoint(r.x() + (r.width() / 2.), r.y());
	case kEB: return QPoint(r.x() + (r.width() / 2.), r.y() + r.height());
	default: return QPoint();
	}
}

QSizeF FlipSizeByRotation(const QSizeF &size, int angle) {
	return (((angle / 90) % 2) == 1) ? size.transposed() : size;
}

[[nodiscard]] QRectF OriginalCrop(QSize outer, QSize inner) {
	const auto size = inner.scaled(outer, Qt::KeepAspectRatio);
	return QRectF(
		(outer.width() - size.width()) / 2,
		(outer.height() - size.height()) / 2,
		size.width(),
		size.height());
}

bool AdjustCropToInner(QRectF &crop, const QRectF &inner) {
	if (inner.isEmpty()) {
		return false;
	}
	const auto was = crop;
	crop.setWidth(std::min(crop.width(), inner.width()));
	crop.setHeight(std::min(crop.height(), inner.height()));
	if (crop.left() < inner.left()) {
		crop.moveLeft(inner.left());
	}
	if (crop.top() < inner.top()) {
		crop.moveTop(inner.top());
	}
	if (crop.right() > inner.right()) {
		crop.moveRight(inner.right());
	}
	if (crop.bottom() > inner.bottom()) {
		crop.moveBottom(inner.bottom());
	}
	return (crop != was);
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
	: !_data.exactSize.isEmpty()
	? OriginalCrop(_imageSize, _data.exactSize)
	: QRectF(QPoint(), _imageSize))
, _angle(modifications.angle)
, _flipped(modifications.flipped)
, _keepAspectRatio(_data.keepAspectRatio)
, _cornersLevel(modifications.cornersLevel) {

	setMouseTracking(true);

	paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(this);

		p.fillPath(_painterPath, st::photoCropFadeBg);
		paintFrame(p);
		const auto gridOpacity = _gridOpacityAnimation.value(
			_gridVisible ? 1. : 0.);
		if (gridOpacity > 0.) {
			paintGrid(p, gridOpacity);
		}
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

	auto matrix = QTransform()
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

	auto adjusted = false;
	const auto min = float64(st::photoEditorCropMinSize);
	if ((cropPaint.width() < min) || (cropPaint.height() < min)) {
		cropPaint.setWidth(std::max(
			std::min(min, _innerRect.width()),
			cropPaint.width()));
		cropPaint.setHeight(std::max(
			std::min(min, _innerRect.height()),
			cropPaint.height()));
		adjusted = true;
	}
	adjusted = AdjustCropToInner(cropPaint, _innerRect) || adjusted;
	setCropPaint(std::move(cropPaint));
	if (adjusted) {
		convertCropPaintToOriginal();
	}
}

QPainterPath Crop::cropPath() const {
	auto result = QPainterPath();
	if (_data.cropType == EditorData::CropType::Ellipse) {
		result.addEllipse(_cropPaint);
	} else if (_data.cropType == EditorData::CropType::RoundedRect) {
		const auto multiplier = RoundedCornersMultiplier(_cornersLevel);
		if (multiplier <= 0.) {
			result.addRect(_cropPaint);
		} else {
			const auto radius = std::min(
				_cropPaint.width(),
				_cropPaint.height()) * multiplier;
			result.addRoundedRect(_cropPaint, radius, radius);
		}
	} else {
		result.addRect(_cropPaint);
	}
	return result;
}

void Crop::paintFrame(QPainter &p) {
	const auto framePath = cropPath();
	auto frameStroker = QPainterPathStroker();
	frameStroker.setWidth(st::lineWidth * 2);
	frameStroker.setJoinStyle(Qt::MiterJoin);
	frameStroker.setCapStyle(Qt::SquareCap);
	auto frameShape = frameStroker.createStroke(framePath);
	p.save();
	p.setRenderHint(QPainter::Antialiasing, true);
	p.fillPath(frameShape, st::photoCropPointFg);
	{
		const auto cornerLength = std::min(
			float64(st::photoEditorCropPointSize * 2),
			std::min(_cropPaint.width(), _cropPaint.height()) / 2.);
		if (cornerLength > 0.) {
			auto cornerLines = QPainterPath();
			cornerLines.moveTo(_cropPaint.left(), _cropPaint.top());
			cornerLines.lineTo(
				_cropPaint.left() + cornerLength,
				_cropPaint.top());
			cornerLines.moveTo(_cropPaint.left(), _cropPaint.top());
			cornerLines.lineTo(
				_cropPaint.left(),
				_cropPaint.top() + cornerLength);
			cornerLines.moveTo(_cropPaint.right(), _cropPaint.top());
			cornerLines.lineTo(
				_cropPaint.right() - cornerLength,
				_cropPaint.top());
			cornerLines.moveTo(_cropPaint.right(), _cropPaint.top());
			cornerLines.lineTo(
				_cropPaint.right(),
				_cropPaint.top() + cornerLength);
			cornerLines.moveTo(_cropPaint.left(), _cropPaint.bottom());
			cornerLines.lineTo(
				_cropPaint.left() + cornerLength,
				_cropPaint.bottom());
			cornerLines.moveTo(_cropPaint.left(), _cropPaint.bottom());
			cornerLines.lineTo(
				_cropPaint.left(),
				_cropPaint.bottom() - cornerLength);
			cornerLines.moveTo(_cropPaint.right(), _cropPaint.bottom());
			cornerLines.lineTo(
				_cropPaint.right() - cornerLength,
				_cropPaint.bottom());
			cornerLines.moveTo(_cropPaint.right(), _cropPaint.bottom());
			cornerLines.lineTo(
				_cropPaint.right(),
				_cropPaint.bottom() - cornerLength);

			auto cornerStroker = QPainterPathStroker();
			cornerStroker.setWidth(st::lineWidth * 4);
			cornerStroker.setJoinStyle(Qt::MiterJoin);
			cornerStroker.setCapStyle(Qt::SquareCap);
			auto cornerColor = st::photoCropPointFg->c;
			cornerColor.setAlpha(255);
			p.fillPath(cornerStroker.createStroke(cornerLines), cornerColor);
		}
	}
	p.restore();
}

void Crop::paintGrid(QPainter &p, float64 opacity) {
	if ((_cropPaint.width() <= 0.) || (_cropPaint.height() <= 0.)) {
		return;
	}
	p.save();
	p.setOpacity(opacity);
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setClipPath(cropPath());
	auto pen = QPen(st::photoCropPointFg, st::lineWidth);
	pen.setCapStyle(Qt::FlatCap);
	p.setPen(pen);
	const auto horizontalStep = _cropPaint.width() / 3.;
	const auto verticalStep = _cropPaint.height() / 3.;
	for (auto i = 1; i < 3; ++i) {
		const auto x = _cropPaint.left() + (horizontalStep * i);
		const auto y = _cropPaint.top() + (verticalStep * i);
		p.drawLine(
			QPointF(x, _cropPaint.top()),
			QPointF(x, _cropPaint.bottom()));
		p.drawLine(
			QPointF(_cropPaint.left(), y),
			QPointF(_cropPaint.right(), y));
	}
	p.restore();
}

void Crop::setCropPaint(QRectF &&rect) {
	AdjustCropToInner(rect, _innerRect);
	_cropPaint = std::move(rect);

	updateEdges();

	_painterPath.clear();
	_painterPath.addRect(_innerRect);
	_painterPath.addPath(cropPath());
}

void Crop::convertCropPaintToOriginal() {
	const auto cropHolder = QTransform()
		.scale(_ratio.w, _ratio.h)
		.mapRect(QRectF(QPointF(), FlipSizeByRotation(_imageSize, _angle)));
	const auto cropHolderCenter = cropHolder.center();

	const auto matrix = QTransform()
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
	const auto sideWidth = std::max(0., r.width() - s);
	const auto sideHeight = std::max(0., r.height() - s);
	for (const auto &e : { kETL, kETR, kEBL, kEBR }) {
		_edges[e] = QRectF(PointOfEdge(e, r), QSize(s, s)) + m;
	}
	if (!_keepAspectRatio) {
		_edges[kEL] = QRectF(
			r.left() - _pointSizeH,
			r.top() + _pointSizeH,
			s,
			sideHeight);
		_edges[kER] = QRectF(
			r.right() - _pointSizeH,
			r.top() + _pointSizeH,
			s,
			sideHeight);
		_edges[kET] = QRectF(
			r.left() + _pointSizeH,
			r.top() - _pointSizeH,
			sideWidth,
			s);
		_edges[kEB] = QRectF(
			r.left() + _pointSizeH,
			r.bottom() - _pointSizeH,
			sideWidth,
			s);
	} else {
		_edges.erase(kEL);
		_edges.erase(kER);
		_edges.erase(kET);
		_edges.erase(kEB);
	}
}

Qt::Edges Crop::mouseState(const QPoint &p) {
	for (const auto &e : { kETL, kETR, kEBL, kEBR, kEL, kER, kET, kEB }) {
		if (const auto i = _edges.find(e); i != end(_edges)) {
			if (i->second.contains(p)) {
				return e;
			}
		}
	}
	if (_cropPaint.contains(p)) {
		return kEAll;
	}
	return Qt::Edges();
}

void Crop::mousePressEvent(QMouseEvent *e) {
	if (_data.fixedCrop && e->button() != Qt::LeftButton) {
		return;
	}
	computeDownState(e->pos());
	if (_down.edge) {
		setGridVisible(true, false);
	}
}

void Crop::mouseReleaseEvent(QMouseEvent *e) {
	if (_data.fixedCrop && e->button() != Qt::LeftButton) {
		return;
	}
	const auto hadEdge = bool(_down.edge);
	if (hadEdge) {
		setGridVisible(false, true);
	}
	clearDownState();
	convertCropPaintToOriginal();
}

void Crop::computeDownState(const QPoint &p) {
	const auto edge = mouseState(p);
	const auto &inner = _innerRect;
	const auto &crop = _cropPaint;
	const auto &[iLeft, iTop, iRight, iBottom] = RectEdges(inner);
	const auto &[cLeft, cTop, cRight, cBottom] = RectEdges(crop);
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

void Crop::setGridVisible(bool visible, bool animated) {
	if ((_gridVisible == visible) && !_gridOpacityAnimation.animating()) {
		return;
	}
	const auto from = _gridOpacityAnimation.value(_gridVisible ? 1. : 0.);
	const auto to = visible ? 1. : 0.;
	_gridVisible = visible;
	_gridOpacityAnimation.stop();
	if (animated) {
		_gridOpacityAnimation.start(
			[=] { update(); },
			from,
			to,
			st::photoEditorBarAnimationDuration * std::abs(to - from));
	}
	update();
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
		const auto minW = (_keepAspectRatio && cropRatio > 1.)
			? (minSize * cropRatio)
			: float64(minSize);
		const auto minH = (_keepAspectRatio && cropRatio < 1.)
			? (minSize / cropRatio)
			: float64(minSize);
		const auto xMin = xFactor * int(
			crop.width() - std::min(minW, crop.width()));
		const auto yMin = yFactor * int(
			crop.height() - std::min(minH, crop.height()));

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

	if (_data.fixedCrop && (e->buttons() & Qt::MiddleButton)) {
		return;
	}

	const auto edge = pressedEdge ? pressedEdge : mouseState(pos);

	const auto cursor = ((edge == kETL) || (edge == kEBR))
		? style::cur_sizefdiag
		: ((edge == kETR) || (edge == kEBL))
		? style::cur_sizebdiag
		: ((edge == kEL) || (edge == kER))
		? style::cur_sizehor
		: ((edge == kET) || (edge == kEB))
		? style::cur_sizever
		: (edge == kEAll)
		? style::cur_sizeall
		: style::cur_default;
	setCursor(cursor);
}

style::margins Crop::cropMargins() const {
	return _innerMargins;
}

void Crop::setAspectRatio(float64 ratio) {
	const auto free = (ratio <= 0.);
	_keepAspectRatio = !free;

	if (!free) {
		const auto maxW = _innerRect.width();
		const auto maxH = _innerRect.height();
		auto newW = maxW;
		auto newH = maxW / ratio;
		if (newH > maxH) {
			newH = maxH;
			newW = maxH * ratio;
		}

		const auto center = _cropPaint.center();
		auto adjusted = QRectF(
			center.x() - newW / 2.,
			center.y() - newH / 2.,
			newW,
			newH);

		AdjustCropToInner(adjusted, _innerRect);
		setCropPaint(std::move(adjusted));
		convertCropPaintToOriginal();
	} else {
		updateEdges();
	}
	update();
}

void Crop::setCornersLevel(RoundedCornersLevel level) {
	if (_cornersLevel == level) {
		return;
	}
	_cornersLevel = level;
	_painterPath.clear();
	_painterPath.addRect(_innerRect);
	_painterPath.addPath(cropPath());
	update();
}

QRect Crop::saveCropRect() {
	const auto savedCrop = _cropOriginal.toRect();
	return (!savedCrop.topLeft().isNull() || (savedCrop.size() != _imageSize))
		? savedCrop
		: QRect();
}

} // namespace Editor
