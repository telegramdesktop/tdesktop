/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/editor_layer_widget.h"

#include "ui/painter.h"

#include <QtGui/QGuiApplication>

namespace Editor {
namespace {

[[nodiscard]] int BlurOverlayAlpha(bool night) {
	return night ? 128 : 192;
}

} // namespace

LayerWidget::LayerWidget(
	not_null<QWidget*> parent,
	base::unique_qptr<Ui::RpWidget> content)
: Ui::LayerWidget(parent)
, _content(std::move(content))
, _backdrop(
	this,
	[=] { return Ui::CaptureUnderLayer(this); },
	BlurOverlayAlpha) {
	_content->setParent(this);
	_content->show();

	paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(this);
		_backdrop.paint(p);
	}, lifetime());
}

bool LayerWidget::eventHook(QEvent *e) {
	return RpWidget::eventHook(e);
}

void LayerWidget::start() {
	_backdrop.start();

	sizeValue(
	) | rpl::on_next([=](const QSize &size) {
		_content->resize(size);
	}, lifetime());
}

void LayerWidget::parentResized() {
	resizeToWidth(parentWidget()->width());
	if (!_backdrop.started()) {
		start();
	}
}

void LayerWidget::keyPressEvent(QKeyEvent *e) {
	forwardKeyEvent(e);
}

void LayerWidget::keyReleaseEvent(QKeyEvent *e) {
	forwardKeyEvent(e);
}

void LayerWidget::forwardKeyEvent(not_null<QKeyEvent*> e) {
	// Qt bubbles a key event ignored by the content back to its parent.
	if (_forwardingKeyEvent) {
		_keyEventIgnored = true;
		e->accept();
		return;
	}
	_forwardingKeyEvent = true;
	_keyEventIgnored = false;
	QGuiApplication::sendEvent(_content.get(), e);
	_forwardingKeyEvent = false;
	e->setAccepted(!_keyEventIgnored);
}

int LayerWidget::resizeGetHeight(int newWidth) {
	return parentWidget()->height();
}

bool LayerWidget::closeByOutsideClick() const {
	return false;
}

} // namespace Editor
