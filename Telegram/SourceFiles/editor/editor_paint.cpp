/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/editor_paint.h"

#include "base/event_filter.h"
#include "styles/style_boxes.h"

#include <QGraphicsItemGroup>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>

namespace Editor {
namespace {

constexpr auto kViewStyle = "QGraphicsView {\
		background-color: transparent;\
		border: 0px\
	}"_cs;

std::shared_ptr<QGraphicsScene> EnsureScene(PhotoModifications &mods) {
	if (!mods.paint) {
		mods.paint = std::make_shared<QGraphicsScene>(nullptr);
	}
	return mods.paint;
}

} // namespace

Paint::Paint(
	not_null<Ui::RpWidget*> parent,
	PhotoModifications &modifications,
	const QSize &imageSize)
: RpWidget(parent)
, _scene(EnsureScene(modifications))
, _view(base::make_unique_q<QGraphicsView>(_scene.get(), this))
, _imageSize(imageSize) {
	Expects(modifications.paint != nullptr);

	_view->show();
	_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	_view->setStyleSheet(kViewStyle.utf8());

	_scene->setSceneRect(0, 0, imageSize.width(), imageSize.height());

	initDrawing();

}

void Paint::applyTransform(QRect geometry, int angle, bool flipped) {
	if (geometry.isEmpty()) {
		return;
	}
	setGeometry(geometry);
	const auto size = geometry.size();

	const auto rotatedImageSize = QMatrix()
		.rotate(angle)
		.mapRect(QRect(QPoint(), _imageSize));

	const auto ratioW = size.width() / float64(rotatedImageSize.width())
		* (flipped ? -1 : 1);
	const auto ratioH = size.height() / float64(rotatedImageSize.height());

	_view->setTransform(QTransform().scale(ratioW, ratioH).rotate(angle));
	_view->setGeometry(QRect(QPoint(), size));
}

void Paint::initDrawing() {
	using Result = base::EventFilterResult;

	_brushData.size = 10;
	_brushData.color = Qt::red;

	auto callback = [=](not_null<QEvent*> event) {
		const auto type = event->type();
		const auto isPress = (type == QEvent::GraphicsSceneMousePress);
		const auto isMove = (type == QEvent::GraphicsSceneMouseMove);
		const auto isRelease = (type == QEvent::GraphicsSceneMouseRelease);
		if (!isPress && !isMove && !isRelease) {
			return Result::Continue;
		}
		const auto e = static_cast<QGraphicsSceneMouseEvent*>(event.get());

		const auto &size = _brushData.size;
		const auto &color = _brushData.color;
		const auto mousePoint = e->scenePos();
		if (isPress) {
			auto dot = _scene->addEllipse(
				mousePoint.x() - size / 2,
				mousePoint.y() - size / 2,
				size,
				size,
				QPen(Qt::NoPen),
				QBrush(color));
			_brushData.group = _scene->createItemGroup(
				QList<QGraphicsItem*>{ std::move(dot) });
		}
		if (isMove && _brushData.group) {
			_brushData.group->addToGroup(_scene->addLine(
				_brushData.lastPoint.x(),
				_brushData.lastPoint.y(),
				mousePoint.x(),
				mousePoint.y(),
				QPen(color, size, Qt::SolidLine, Qt::RoundCap)));
		}
		if (isRelease) {
			_brushData.group = nullptr;
		}
		_brushData.lastPoint = mousePoint;

		return Result::Cancel;
	};

	base::install_event_filter(this, _scene.get(), std::move(callback));
}

void Paint::applyMode(PhotoEditorMode mode) {
	setAttribute(
		Qt::WA_TransparentForMouseEvents,
		mode == PhotoEditorMode::Transform);
}

std::shared_ptr<QGraphicsScene> Paint::saveScene() const {
	return _scene;
}

} // namespace Editor
