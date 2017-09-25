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
#include "ui/focus_persister.h"
#include "ui/widgets/buttons.h"
#include "window/section_widget.h"
#include "window/window_controller.h"
#include "window/main_window.h"
#include "auth_session.h"
#include "styles/style_info.h"
#include "styles/style_settings.h"
#include "styles/style_window.h"
#include "styles/style_boxes.h"

namespace Info {
namespace {

constexpr auto kThirdSectionInfoTimeoutMs = 1000;

} // namespace

LayerWrap::LayerWrap(
	not_null<Window::Controller*> controller,
	not_null<Memento*> memento)
: _controller(controller)
, _content(createContent(controller, memento))
, _topBar(createTopBar()) {
	setupHeightConsumers();
}

LayerWrap::LayerWrap(
	not_null<Window::Controller*> controller,
	not_null<MoveMemento*> memento)
: _controller(controller)
, _content(memento->content(this, Wrap::Layer))
, _topBar(createTopBar()) {
	setupHeightConsumers();
}

void LayerWrap::setupHeightConsumers() {
	_content->desiredHeightValue()
		| rpl::start([this](int height) {
			_desiredHeight = height;
			resizeToWidth(width());
		}, lifetime());
	heightValue()
		| rpl::start([this](int height) {
			_content->resize(
				width(),
				height - _topBar->bottomNoMargins() - st::boxRadius);
		}, lifetime());
}

object_ptr<TopBar> LayerWrap::createTopBar() {
	auto result = object_ptr<TopBar>(
		this,
		st::infoLayerTopBar);
	auto close = result->addButton(object_ptr<Ui::IconButton>(
		result.data(),
		st::infoLayerTopBarClose));
	close->clicks()
		| rpl::start([this](auto&&) {
			_controller->hideSpecialLayer();
		}, close->lifetime());
	result->setTitle(TitleValue(
		_content->section(),
		_content->peer()));
	return result;
}

object_ptr<ContentWidget> LayerWrap::createContent(
		not_null<Window::Controller*> controller,
		not_null<Memento*> memento) {
	return memento->content()->createWidget(
		this,
		Wrap::Layer,
		controller,
		QRect());
}

void LayerWrap::showFinished() {
}

void LayerWrap::parentResized() {
	auto parentSize = parentWidget()->size();
	auto parentWidth = parentSize.width();
	if (parentWidth < MinimalSupportedWidth()) {
		Ui::FocusPersister persister(this);
		auto localCopy = _controller;
		auto memento = MoveMemento(std::move(_content), Wrap::Narrow);
		localCopy->hideSpecialLayer(anim::type::instant);
		localCopy->showSection(
			std::move(memento),
			anim::type::instant,
			anim::activation::background);
	} else if (_controller->canShowThirdSectionWithoutResize()) {
		takeToThirdSection();
	} else {
		auto newWidth = qMin(
			parentWidth - 2 * st::infoMinimalLayerMargin,
			st::infoDesiredWidth);
		resizeToWidth(newWidth);
	}
}

bool LayerWrap::takeToThirdSection() {
	Ui::FocusPersister persister(this);
	auto localCopy = _controller;
	auto memento = MoveMemento(std::move(_content), Wrap::Side);
	localCopy->hideSpecialLayer(anim::type::instant);

	Auth().data().setThirdSectionInfoEnabled(true);
	Auth().saveDataDelayed(kThirdSectionInfoTimeoutMs);
	localCopy->showSection(
		std::move(memento),
		anim::type::instant,
		anim::activation::background);
	return true;
}

int LayerWrap::MinimalSupportedWidth() {
	auto minimalMargins = 2 * st::infoMinimalLayerMargin;
	return st::infoMinimalWidth + minimalMargins;
}

int LayerWrap::resizeGetHeight(int newWidth) {
	if (!parentWidget() || !_content) {
		return 0;
	}

	// First resize content to new width and get the new desired height.
	_topBar->resizeToWidth(newWidth);
	_topBar->moveToLeft(0, st::boxRadius, newWidth);
	_content->resizeToWidth(newWidth);
	_content->moveToLeft(0, _topBar->bottomNoMargins(), newWidth);

	auto parentSize = parentWidget()->size();
	auto windowWidth = parentSize.width();
	auto windowHeight = parentSize.height();
	auto maxHeight = _topBar->height() + _desiredHeight;
	auto newHeight = st::boxRadius + maxHeight + st::boxRadius;
	if (newHeight > windowHeight || newWidth >= windowWidth) {
		newHeight = windowHeight;
	}

	setRoundedCorners(newHeight < windowHeight);

	moveToLeft((windowWidth - newWidth) / 2, (windowHeight - newHeight) / 2);

	_topBar->update();
	_content->update();
	update();

	return newHeight;
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
