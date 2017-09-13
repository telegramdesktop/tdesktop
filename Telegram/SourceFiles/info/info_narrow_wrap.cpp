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
#include "info/info_profile_widget.h"
#include "info/info_media_widget.h"
#include "info/info_memento.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/shadow.h"
#include "lang/lang_keys.h"
#include "styles/style_info.h"
#include "styles/style_profile.h"

namespace Info {

NarrowWrap::NarrowWrap(
	QWidget *parent,
	not_null<Window::Controller*> controller,
	not_null<Memento*> memento)
: Window::SectionWidget(parent, controller)
, _peer(App::peer(memento->peerId())) {
	setInternalState(geometry(), memento);
}

void NarrowWrap::showInner(object_ptr<ContentWidget> inner) {
	_inner = std::move(inner);
	_inner->setGeometry(innerGeometry());
	_inner->show();

	_desiredHeights.fire(desiredHeightForInner()); 
}

rpl::producer<int> NarrowWrap::desiredHeightForInner() const {
	return _inner->desiredHeightValue();
}

object_ptr<Profile::Widget> NarrowWrap::createProfileWidget() {
	auto result = object_ptr<Profile::Widget>(
		this,
		Wrap::Narrow,
		controller(),
		_peer);
	return result;
}

object_ptr<Media::Widget> NarrowWrap::createMediaWidget() {
	auto result = object_ptr<Media::Widget>(
		this,
		Wrap::Narrow,
		controller(),
		_peer,
		Media::Widget::Type::Photo);
	return result;
}

QPixmap NarrowWrap::grabForShowAnimation(
		const Window::SectionSlideParams &params) {
//	if (params.withTopBarShadow) _tabsShadow->hide();
	auto result = myGrab(this);
//	if (params.withTopBarShadow) _tabsShadow->show();
	return result;
}

void NarrowWrap::doSetInnerFocus() {
	_inner->setInnerFocus();
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
		rpl::single(desiredHeightForInner())
		| rpl::then(_desiredHeights.events())
		| rpl::flatten_latest();
}

void NarrowWrap::saveState(not_null<Memento*> memento) {
	memento->setInner(_inner->createMemento());
}

QRect NarrowWrap::innerGeometry() const {
	return rect();
}

void NarrowWrap::restoreState(not_null<Memento*> memento) {
	showInner(memento->content()->createWidget(
		this,
		Wrap::Narrow,
		controller(),
		innerGeometry()));
}

void NarrowWrap::resizeEvent(QResizeEvent *e) {
	if (_inner) {
		_inner->setGeometry(innerGeometry());
	}
}

void NarrowWrap::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.fillRect(e->rect(), st::profileBg);
}

bool NarrowWrap::wheelEventFromFloatPlayer(
		QEvent *e,
		Window::Column myColumn,
		Window::Column playerColumn) {
	return _inner->wheelEventFromFloatPlayer(e);
}

QRect NarrowWrap::rectForFloatPlayer(
		Window::Column myColumn,
		Window::Column playerColumn) const {
	return _inner->rectForFloatPlayer();
}

} // namespace Info
