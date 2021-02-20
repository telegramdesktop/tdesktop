/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/photo_editor_layer_widget.h"

#include "editor/photo_editor.h"

namespace Editor {

LayerWidget::LayerWidget(
	not_null<Ui::RpWidget*> parent,
	not_null<Window::Controller*> window,
	std::shared_ptr<Image> photo,
	PhotoModifications modifications,
	Fn<void(PhotoModifications)> &&doneCallback)
: Ui::LayerWidget(parent)
, _content(base::make_unique_q<PhotoEditor>(
	this,
	photo,
	std::move(modifications))) {

	paintRequest(
	) | rpl::start_with_next([=](const QRect &clip) {
		Painter p(this);

		p.fillRect(clip, st::boxBg);
	}, lifetime());

	_content->cancelRequests(
	) | rpl::start_with_next([=] {
		closeLayer();
	}, lifetime());

	_content->doneRequests(
	) | rpl::start_with_next([=, done = std::move(doneCallback)](
			const PhotoModifications &mods) {
		doneCallback(mods);
		closeLayer();
	}, lifetime());

	sizeValue(
	) | rpl::start_with_next([=](const QSize &size) {
		_content->resize(size);
	}, lifetime());
}

void LayerWidget::parentResized() {
	resizeToWidth(parentWidget()->width());
}

int LayerWidget::resizeGetHeight(int newWidth) {
	return parentWidget()->height();
}

bool LayerWidget::closeByOutsideClick() const {
	return false;
}

} // namespace Editor
