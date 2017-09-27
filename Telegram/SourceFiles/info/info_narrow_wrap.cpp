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
#include "info/info_narrow_wrap.h"

#include <rpl/flatten_latest.h>
#include <rpl/filter.h>
#include "info/profile/info_profile_widget.h"
#include "info/info_media_widget.h"
#include "info/info_memento.h"
#include "info/info_top_bar.h"
#include "info/info_layer_wrap.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/buttons.h"
#include "ui/effects/widget_fade_wrap.h"
#include "window/window_controller.h"
#include "window/main_window.h"
#include "mainwindow.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "styles/style_info.h"
#include "styles/style_profile.h"

namespace Info {

NarrowWrap::NarrowWrap(
	QWidget *parent,
	not_null<Window::Controller*> controller,
	not_null<Memento*> memento)
: Window::SectionWidget(parent, controller)
, _topShadow(this, object_ptr<Ui::PlainShadow>(this, st::shadowFg)) {
	_topShadow->hideFast();
	_topShadow->raise();
	setInternalState(geometry(), memento);
}

NarrowWrap::NarrowWrap(
	QWidget *parent,
	not_null<Window::Controller*> controller,
	not_null<MoveMemento*> memento)
: Window::SectionWidget(parent, controller)
, _topShadow(this, object_ptr<Ui::PlainShadow>(this, st::shadowFg)) {
	_topShadow->hideFast();
	_topShadow->raise();
	restoreState(memento);
}

object_ptr<LayerWidget> NarrowWrap::moveContentToLayer(
		int availableWidth) {
	if (width() < LayerWrap::MinimalSupportedWidth()) {
		return nullptr;
	}
	return MoveMemento(
		std::move(_content),
		Wrap::Layer
	).createLayer(controller());
}

not_null<PeerData*> NarrowWrap::peer() const {
	return _content->peer();
}

void NarrowWrap::showContent(object_ptr<ContentWidget> content) {
	_content = std::move(content);
	_content->setGeometry(contentGeometry());
	_content->show();

	_topBar = createTopBar();

	_desiredHeights.fire(desiredHeightForContent());
}

object_ptr<TopBar> NarrowWrap::createTopBar() {
	auto result = object_ptr<TopBar>(
		this,
		st::infoLayerTopBar);
	result->enableBackButton(true);
	result->backRequest()
		| rpl::start_with_next([this](auto&&) {
			this->controller()->showBackFromStack();
		}, result->lifetime());
	result->setTitle(TitleValue(
		_content->section(),
		_content->peer()));
	return result;
}

rpl::producer<int> NarrowWrap::desiredHeightForContent() const {
	return _content->desiredHeightValue();
}

QPixmap NarrowWrap::grabForShowAnimation(
		const Window::SectionSlideParams &params) {
	anim::SetDisabled(true);
	if (params.withTopBarShadow) _topShadow->hide();
	auto result = myGrab(this);
	if (params.withTopBarShadow) _topShadow->show();
	anim::SetDisabled(false);
	return result;
}

void NarrowWrap::doSetInnerFocus() {
//	_content->setInnerFocus();
}

bool NarrowWrap::hasTopBarShadow() const {
	return !_topShadow->isHidden() && !_topShadow->animating();
}

bool NarrowWrap::showInternal(
		not_null<Window::SectionMemento*> memento) {
	if (auto infoMemento = dynamic_cast<Memento*>(memento.get())) {
		if (infoMemento->peerId() == peer()->id) {
			restoreState(infoMemento);
			return true;
		}
	}
	return false;
}

void NarrowWrap::setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento) {
	setGeometry(geometry);
	restoreState(memento);
}

std::unique_ptr<Window::SectionMemento> NarrowWrap::createMemento() {
	auto result = std::make_unique<Memento>(peer()->id);
	saveState(result.get());
	return std::move(result);
}

rpl::producer<int> NarrowWrap::desiredHeight() const {
	return
		rpl::single(desiredHeightForContent())
		| rpl::then(_desiredHeights.events())
		| rpl::flatten_latest();
}

void NarrowWrap::saveState(not_null<Memento*> memento) {
	memento->setInner(_content->createMemento());
}

QRect NarrowWrap::contentGeometry() const {
	return rect().marginsRemoved(
		QMargins(0, _topBar ? _topBar->bottomNoMargins() : 0, 0, 0));
}

void NarrowWrap::restoreState(not_null<Memento*> memento) {
	showContent(memento->content()->createWidget(
		this,
		Wrap::Narrow,
		controller(),
		contentGeometry()));
}

void NarrowWrap::restoreState(not_null<MoveMemento*> memento) {
	showContent(memento->content(this, Wrap::Narrow));
}

void NarrowWrap::resizeEvent(QResizeEvent *e) {
	if (_topBar) {
		_topBar->resizeToWidth(width());
		_topBar->moveToLeft(0, 0);
	}
	if (_content) {
		_content->setGeometry(contentGeometry());
	}
}

void NarrowWrap::paintEvent(QPaintEvent *e) {
	SectionWidget::paintEvent(e);
	if (animating()) {
		return;
	}

	Painter p(this);
	p.fillRect(e->rect(), st::profileBg);
}

bool NarrowWrap::wheelEventFromFloatPlayer(QEvent *e) {
	return _content->wheelEventFromFloatPlayer(e);
}

QRect NarrowWrap::rectForFloatPlayer() const {
	return _content->rectForFloatPlayer();
}

} // namespace Info
