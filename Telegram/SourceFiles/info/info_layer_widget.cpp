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
#include "info/info_layer_widget.h"

#include <rpl/mappers.h>
#include "info/info_content_widget.h"
#include "info/info_top_bar.h"
#include "info/info_memento.h"
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

LayerWidget::LayerWidget(
	not_null<Window::Controller*> controller,
	not_null<Memento*> memento)
: _controller(controller)
, _content(this, controller, Wrap::Layer, memento) {
	setupHeightConsumers();
}

LayerWidget::LayerWidget(
	not_null<Window::Controller*> controller,
	not_null<MoveMemento*> memento)
: _controller(controller)
, _content(memento->takeContent(this, Wrap::Layer)) {
	setupHeightConsumers();
}

void LayerWidget::setupHeightConsumers() {
	_content->desiredHeightValue()
		| rpl::start_with_next([this](int height) {
			if (!_content) return;
			accumulate_max(_desiredHeight, height);
			resizeToWidth(width());
			_content->forceContentRepaint();
		}, lifetime());
}

void LayerWidget::showFinished() {
}

void LayerWidget::parentResized() {
	auto parentSize = parentWidget()->size();
	auto parentWidth = parentSize.width();
	if (parentWidth < MinimalSupportedWidth()) {
		Ui::FocusPersister persister(this);
		auto localCopy = _controller;
		auto memento = MoveMemento(std::move(_content));
		localCopy->hideSpecialLayer(anim::type::instant);
		localCopy->showSection(
			std::move(memento),
			Window::SectionShow(
				Window::SectionShow::Way::Forward,
				anim::type::instant,
				anim::activation::background));
	} else if (_controller->canShowThirdSectionWithoutResize()) {
		takeToThirdSection();
	} else {
		auto newWidth = qMin(
			parentWidth - 2 * st::infoMinimalLayerMargin,
			st::infoDesiredWidth);
		resizeToWidth(newWidth);
	}
}

bool LayerWidget::takeToThirdSection() {
	Ui::FocusPersister persister(this);
	auto localCopy = _controller;
	auto memento = MoveMemento(std::move(_content));
	localCopy->hideSpecialLayer(anim::type::instant);

	Auth().data().setThirdSectionInfoEnabled(true);
	Auth().saveDataDelayed();
	localCopy->showSection(
		std::move(memento),
		Window::SectionShow(
			Window::SectionShow::Way::ClearStack,
			anim::type::instant,
			anim::activation::background));
	return true;
}

bool LayerWidget::showSectionInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	if (_content->showInternal(memento, params)) {
		if (params.activation != anim::activation::background) {
			Ui::hideLayer();
		}
		return true;
	}
	return false;
}

int LayerWidget::MinimalSupportedWidth() {
	auto minimalMargins = 2 * st::infoMinimalLayerMargin;
	return st::infoMinimalWidth + minimalMargins;
}

int LayerWidget::resizeGetHeight(int newWidth) {
	if (!parentWidget() || !_content) {
		return 0;
	}

	auto parentSize = parentWidget()->size();
	auto windowWidth = parentSize.width();
	auto windowHeight = parentSize.height();
	auto newHeight = st::boxRadius + _desiredHeight + st::boxRadius;
	if (newHeight > windowHeight || newWidth >= windowWidth) {
		newHeight = windowHeight;
	}
	auto layerTop = snap(
		windowHeight / 24,
		st::infoLayerTopMinimal,
		st::infoLayerTopMaximal);

	setRoundedCorners(layerTop + newHeight < windowHeight);

	// First resize content to new width and get the new desired height.
	auto contentTop = st::boxRadius;
	auto contentHeight = newHeight - contentTop;
	if (_roundedCorners) {
		contentHeight -= st::boxRadius;
	}
	_content->setGeometry(0, contentTop, newWidth, contentHeight);

	moveToLeft((windowWidth - newWidth) / 2, layerTop);

	return newHeight;
}

void LayerWidget::setRoundedCorners(bool rounded) {
	_roundedCorners = rounded;
//	setAttribute(Qt::WA_OpaquePaintEvent, !_roundedCorners);
}

void LayerWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto clip = e->rect();
	auto r = st::boxRadius;
	auto parts = RectPart::None | 0;
	if (clip.intersects({ 0, 0, width(), r })) {
		parts |= RectPart::FullTop;
	}
	if (_roundedCorners) {
		if (clip.intersects({ 0, height() - r, width(), r })) {
			parts |= RectPart::FullBottom;
		}
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

} // namespace Info
