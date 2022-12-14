/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/editor_layer_widget.h"

#include <QtGui/QGuiApplication>

namespace Editor {

LayerWidget::LayerWidget(
	not_null<QWidget*> parent,
	base::unique_qptr<Ui::RpWidget> content)
: Ui::LayerWidget(parent)
, _content(std::move(content)) {
	_content->setParent(this);
	_content->show();

	paintRequest(
	) | rpl::start_with_next([=](const QRect &clip) {
		auto p = QPainter(this);
		p.fillRect(clip, st::photoEditorBg);
	}, lifetime());

	sizeValue(
	) | rpl::start_with_next([=](const QSize &size) {
		_content->resize(size);
	}, lifetime());
}

void LayerWidget::parentResized() {
	resizeToWidth(parentWidget()->width());
}

void LayerWidget::keyPressEvent(QKeyEvent *e) {
	QGuiApplication::sendEvent(_content.get(), e);
}

int LayerWidget::resizeGetHeight(int newWidth) {
	return parentWidget()->height();
}

bool LayerWidget::closeByOutsideClick() const {
	return false;
}

} // namespace Editor
