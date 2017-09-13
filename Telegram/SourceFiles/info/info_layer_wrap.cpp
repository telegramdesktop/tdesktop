/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "info/info_layer_wrap.h"

#include "info/info_memento.h"
#include "info/info_top_bar.h"
#include "ui/rp_widget.h"
#include "ui/widgets/buttons.h"
#include "window/section_widget.h"
#include "window/window_controller.h"
#include "window/main_window.h"
#include "styles/style_info.h"
#include "styles/style_settings.h"
#include "styles/style_window.h"
#include "styles/style_boxes.h"

namespace Info {

LayerWrap::LayerWrap(
	not_null<Window::Controller*> controller,
	not_null<Memento*> memento)
: _topBar(createTopBar(controller, memento))
, _content(createContent(controller, memento)) {
	_content->desiredHeightValue()
		| rpl::on_next([this](int height) {
			_desiredHeight = height;
			resizeToDesiredHeight();
		})
		| rpl::start(lifetime());
}

object_ptr<TopBar> LayerWrap::createTopBar(
		not_null<Window::Controller*> controller,
		not_null<Memento*> memento) {
	auto result = object_ptr<TopBar>(
		this,
		st::infoLayerTopBar);
	result->addButton(object_ptr<Ui::IconButton>(
		result.data(),
		st::infoLayerTopBarClose));
	result->setTitle(TitleValue(
		memento->section(),
		App::peer(memento->peerId())));
	return result;
}

object_ptr<ContentWidget> LayerWrap::createContent(
		not_null<Window::Controller*> controller,
		not_null<Memento*> memento) {
	return memento->content()->createWidget(
		this,
		Wrap::Layer,
		controller,
		controller->window()->rect());
}

void LayerWrap::showFinished() {
}

void LayerWrap::parentResized() {
	auto parentSize = parentWidget()->size();
	auto windowWidth = parentSize.width();
	auto newWidth = st::settingsMaxWidth;
	auto newContentLeft = st::settingsMaxPadding;
	if (windowWidth <= st::settingsMaxWidth) {
		newWidth = windowWidth;
		newContentLeft = st::settingsMinPadding;
		if (windowWidth > st::windowMinWidth) {
			// Width changes from st::windowMinWidth to st::settingsMaxWidth.
			// Padding changes from st::settingsMinPadding to st::settingsMaxPadding.
			newContentLeft += ((newWidth - st::windowMinWidth) * (st::settingsMaxPadding - st::settingsMinPadding)) / (st::settingsMaxWidth - st::windowMinWidth);
		}
	} else if (windowWidth < st::settingsMaxWidth + 2 * st::settingsMargin) {
		newWidth = windowWidth - 2 * st::settingsMargin;
		newContentLeft = st::settingsMinPadding;
		if (windowWidth > st::windowMinWidth) {
			// Width changes from st::windowMinWidth to st::settingsMaxWidth.
			// Padding changes from st::settingsMinPadding to st::settingsMaxPadding.
			newContentLeft += ((newWidth - st::windowMinWidth) * (st::settingsMaxPadding - st::settingsMinPadding)) / (st::settingsMaxWidth - st::windowMinWidth);
		}
	}
	resizeToWidth(newWidth, newContentLeft);
}

void LayerWrap::resizeToWidth(int newWidth, int newContentLeft) {
	resize(newWidth, height());

	_topBar->resizeToWidth(newWidth);
	_topBar->moveToLeft(0, 0, newWidth);

	// Widget height depends on content height, so we
	// resize it here, not in the resizeEvent() handler.
	_content->resizeToWidth(newWidth);
	_content->moveToLeft(0, _topBar->height(), newWidth);

	resizeToDesiredHeight();
}

void LayerWrap::resizeToDesiredHeight() {
	if (!parentWidget()) return;

	auto parentSize = parentWidget()->size();
	auto windowWidth = parentSize.width();
	auto windowHeight = parentSize.height();
	auto maxHeight = _topBar->height() + _desiredHeight;
	auto newHeight = maxHeight + st::boxRadius;
	if (newHeight > windowHeight || width() >= windowWidth) {
		newHeight = windowHeight;
	}

	setRoundedCorners(newHeight < windowHeight);

	setGeometry((windowWidth - width()) / 2, (windowHeight - newHeight) / 2, width(), newHeight);
	update();
}

void LayerWrap::setRoundedCorners(bool rounded) {
	_roundedCorners = rounded;
	setAttribute(Qt::WA_OpaquePaintEvent, !_roundedCorners);
}

void LayerWrap::paintEvent(QPaintEvent *e) {
	if (_roundedCorners) {
		Painter p(this);
		auto clip = e->rect();
		auto r = st::boxRadius;
		auto parts = RectPart::None | 0;
		if (clip.intersects({ 0, 0, width(), r })) {
			parts |= RectPart::FullTop;
		}
		if (clip.intersects({ 0, height() - r, width(), r })) {
			parts |= RectPart::FullBottom;
		}
		if (parts) {
			App::roundRect(
				p,
				rect(),
				st::boxBg,
				BoxCorners,
				nullptr,
				parts);
		}
	}
}

} // namespace Info
