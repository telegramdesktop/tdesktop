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
#include "info/info_side_wrap.h"

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

SideWrap::SideWrap(
	QWidget *parent,
	not_null<Window::Controller*> controller,
	not_null<PeerData*> peer)
: Window::SectionWidget(parent, controller)
, _peer(peer) {
}

void SideWrap::setupTabs() {
	_tabsShadow.create(this, st::shadowFg);
	_tabs.create(this, st::infoTabs);
	auto sections = QStringList();
	sections.push_back(lang(lng_profile_info_section));
	sections.push_back(lang(lng_info_tab_media));
	_tabs->setSections(sections);
	_tabs->sectionActivated()
		| rpl::map([](int index) { return static_cast<Tab>(index); })
		| rpl::on_next([this](Tab tab) { showTab(tab); })
		| rpl::start(_lifetime);

	_tabs->move(0, 0);
	_tabs->resizeToWidth(width());
	_tabs->show();

	_tabsShadow->setGeometry(
		0,
		_tabs->height() - st::lineWidth,
		width(),
		st::lineWidth);
	_tabsShadow->show();
}

void SideWrap::showTab(Tab tab) {
	showInner(createInner(tab));
}

void SideWrap::showInner(object_ptr<ContentWidget> inner) {
	_inner = std::move(inner);
	_inner->setGeometry(innerGeometry());
	_inner->show();

	_desiredHeights.fire(desiredHeightForInner()); 
}

rpl::producer<int> SideWrap::desiredHeightForInner() const {
	auto result = _inner->desiredHeightValue();
	if (_tabs) {
		result = std::move(result)
			| rpl::map(func::add(_tabs->height()));
	}
	return result;
}

object_ptr<ContentWidget> SideWrap::createInner(Tab tab) {
	switch (tab) {
	case Tab::Profile: return createProfileWidget();
	case Tab::Media: return createMediaWidget();
	}
	Unexpected("Tab value in Info::SideWrap::createInner()");
}

object_ptr<Profile::Widget> SideWrap::createProfileWidget() {
	auto result = object_ptr<Profile::Widget>(
		this,
		Wrap::Side,
		controller(),
		_peer);
	return result;
}

object_ptr<Media::Widget> SideWrap::createMediaWidget() {
	auto result = object_ptr<Media::Widget>(
		this,
		Wrap::Side,
		controller(),
		_peer,
		Media::Widget::Type::Photo);
	return result;
}

QPixmap SideWrap::grabForShowAnimation(
		const Window::SectionSlideParams &params) {
	if (params.withTopBarShadow) _tabsShadow->hide();
	auto result = myGrab(this);
	if (params.withTopBarShadow) _tabsShadow->show();
	return result;
}

void SideWrap::doSetInnerFocus() {
	_inner->setInnerFocus();
}

bool SideWrap::showInternal(
		not_null<Window::SectionMemento*> memento) {
	if (auto infoMemento = dynamic_cast<Memento*>(memento.get())) {
		if (infoMemento->peerId() == peer()->id) {
			restoreState(infoMemento);
			return true;
		}
	}
	return false;
}

void SideWrap::setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento) {
	setGeometry(geometry);
	restoreState(memento);
	if (_tabs) {
		_tabs->finishAnimations();
	}
}

std::unique_ptr<Window::SectionMemento> SideWrap::createMemento() {
	auto result = std::make_unique<Memento>(peer()->id);
	saveState(result.get());
	return std::move(result);
}

rpl::producer<int> SideWrap::desiredHeightValue() const {
	return
		rpl::single(desiredHeightForInner())
		| rpl::then(_desiredHeights.events())
		| rpl::flatten_latest();
}

void SideWrap::saveState(not_null<Memento*> memento) {
	memento->setInner(_inner->createMemento());
}

QRect SideWrap::innerGeometry() const {
	return (_tab == Tab::None)
		? rect()
		: rect().marginsRemoved({ 0, _tabs->height(), 0, 0 });
}

void SideWrap::restoreState(not_null<Memento*> memento) {
	switch (memento->section().type()) {
	case Section::Type::Profile:
		setCurrentTab(Tab::Profile);
		break;
	case Section::Type::Media:
		switch (memento->section().mediaType()) {
		case Section::MediaType::Photo:
		case Section::MediaType::Video:
		case Section::MediaType::File:
			setCurrentTab(Tab::Media);
			break;
		default:
			setCurrentTab(Tab::None);
			break;
		}
		break;
	}

	showInner(memento->content()->createWidget(
		this,
		Wrap::Side,
		controller(),
		innerGeometry()));
}

void SideWrap::setCurrentTab(Tab tab) {
	_tab = tab;
	if (_tab == Tab::None) {
		_tabs.destroy();
	} else if (!_tabs) {
		setupTabs();
	} else {
		_tabs->setActiveSection(static_cast<int>(tab));
	}
}

void SideWrap::resizeEvent(QResizeEvent *e) {
	if (_tabs) {
		_tabs->resizeToWidth(width());
	}
	if (_inner) {
		_inner->setGeometry(innerGeometry());
	}
}

void SideWrap::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.fillRect(e->rect(), st::profileBg);
}

bool SideWrap::wheelEventFromFloatPlayer(
		QEvent *e,
		Window::Column myColumn,
		Window::Column playerColumn) {
	return _inner->wheelEventFromFloatPlayer(e);
}

QRect SideWrap::rectForFloatPlayer(
		Window::Column myColumn,
		Window::Column playerColumn) const {
	return _inner->rectForFloatPlayer();
}

} // namespace Info
