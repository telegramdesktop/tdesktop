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
#include "info/info_section_widget.h"

#include "info/info_content_widget.h"
#include "info/info_wrap_widget.h"
#include "info/info_layer_widget.h"
#include "info/info_memento.h"
#include "info/info_controller.h"

namespace Info {

SectionWidget::SectionWidget(
	QWidget *parent,
	not_null<Window::Controller*> window,
	Wrap wrap,
	not_null<Memento*> memento)
: Window::SectionWidget(parent, window)
, _content(this, window, wrap, memento) {
	init();
}

SectionWidget::SectionWidget(
	QWidget *parent,
	not_null<Window::Controller*> window,
	Wrap wrap,
	not_null<MoveMemento*> memento)
: Window::SectionWidget(parent, window)
, _content(memento->takeContent(this, wrap)) {
	init();
}

void SectionWidget::init() {
	sizeValue(
	) | rpl::start_with_next([wrap = _content.data()](QSize size) {
		auto wrapGeometry = QRect{ { 0, 0 }, size };
		auto additionalScroll = 0;
		wrap->updateGeometry(wrapGeometry, additionalScroll);
	}, _content->lifetime());
}

PeerData *SectionWidget::activePeer() const {
	return _content->activePeer();
}

bool SectionWidget::hasTopBarShadow() const {
	return _content->hasTopBarShadow();
}

QPixmap SectionWidget::grabForShowAnimation(
		const Window::SectionSlideParams &params) {
	return _content->grabForShowAnimation(params);
}

void SectionWidget::doSetInnerFocus() {
	_content->setInnerFocus();
}

void SectionWidget::showFinishedHook() {
	_topBarSurrogate.destroy();
	_content->showFast();
}

void SectionWidget::showAnimatedHook(
		const Window::SectionSlideParams &params) {
	_topBarSurrogate = _content->createTopBarSurrogate(this);
}

bool SectionWidget::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	return _content->showInternal(memento, params);
}

std::unique_ptr<Window::SectionMemento> SectionWidget::createMemento() {
	return _content->createMemento();
}

object_ptr<Window::LayerWidget> SectionWidget::moveContentToLayer(
		QRect bodyGeometry) {
	if (_content->controller()->wrap() != Wrap::Narrow
		|| width() < LayerWidget::MinimalSupportedWidth()) {
		return nullptr;
	}
	return MoveMemento(
		std::move(_content)).createLayer(
			controller(),
			bodyGeometry);
}

bool SectionWidget::wheelEventFromFloatPlayer(QEvent *e) {
	return _content->wheelEventFromFloatPlayer(e);
}

QRect SectionWidget::rectForFloatPlayer() const {
	return _content->rectForFloatPlayer();
}

} // namespace Info
