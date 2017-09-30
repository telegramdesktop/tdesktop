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
#include "info/info_wrap_widget.h"

#include <rpl/flatten_latest.h>
#include "info/profile/info_profile_widget.h"
#include "info/media/info_media_widget.h"
#include "info/info_content_widget.h"
#include "info/info_memento.h"
#include "info/info_top_bar.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/fade_wrap.h"
#include "window/window_controller.h"
#include "auth_session.h"
#include "lang/lang_keys.h"
#include "styles/style_info.h"
#include "styles/style_profile.h"

namespace Info {

WrapWidget::WrapWidget(
	QWidget *parent,
	not_null<Window::Controller*> controller,
	Wrap wrap,
	not_null<Memento*> memento)
: RpWidget(parent)
, _controller(controller) {
	applyState(wrap, memento);
}

not_null<PeerData*> WrapWidget::peer() const {
	return _content->peer();
}

Wrap WrapWidget::wrap() const {
	return _content->wrap();
}

void WrapWidget::setWrap(Wrap wrap) {
	_content->setWrap(wrap);
	setupTop(wrap, _content->section(), _content->peer()->id);
	finishShowContent();
}

void WrapWidget::createTabs() {
	_topTabs.create(this, st::infoTabs);
	auto sections = QStringList();
	sections.push_back(lang(lng_profile_info_section));
	sections.push_back(lang(lng_info_tab_media));
	_topTabs->setSections(sections);
	_topTabs->sectionActivated()
		| rpl::map([](int index) { return static_cast<Tab>(index); })
		| rpl::start_with_next(
			[this](Tab tab) { showTab(tab); },
			lifetime());

	_topTabs->move(0, 0);
	_topTabs->resizeToWidth(width());
	_topTabs->show();
}

void WrapWidget::showTab(Tab tab) {
	showContent(createContent(tab));
}

void WrapWidget::setupTabbedTop(const Section &section) {
	switch (section.type()) {
	case Section::Type::Profile:
		setupTabs(Tab::Profile);
		break;
	case Section::Type::Media:
		switch (section.mediaType()) {
		case Section::MediaType::Photo:
		case Section::MediaType::Video:
		case Section::MediaType::File:
			setupTabs(Tab::Media);
			break;
		default:
			setupTabs(Tab::None);
			break;
		}
		break;
	case Section::Type::CommonGroups:
		setupTabs(Tab::None);
		break;
	}
}

void WrapWidget::setupTop(
		Wrap wrap,
		const Section &section,
		PeerId peerId) {
	if (wrap == Wrap::Side) {
		setupTabbedTop(section);
	} else {
		setupTabs(Tab::None);
	}
	if (_topTabs) {
		_topBar.destroy();
	} else {
		createTopBar(wrap, section, peerId);
	}
}

void WrapWidget::createTopBar(
		Wrap wrap,
		const Section &section,
		PeerId peerId) {
	_topBar = object_ptr<TopBar>(
		this,
		(wrap == Wrap::Layer)
			? st::infoLayerTopBar
			: st::infoTopBar);
	_topBar->setTitle(TitleValue(
		section,
		peerId));
	if (wrap != Wrap::Layer) {
		_topBar->enableBackButton(true);
		_topBar->backRequest()
			| rpl::start_with_next([this] {
				_controller->showBackFromStack();
			}, _topBar->lifetime());
	}
	if (wrap == Wrap::Layer) {
		auto close = _topBar->addButton(object_ptr<Ui::IconButton>(
			_topBar,
			st::infoLayerTopBarClose));
		close->clicks()
			| rpl::start_with_next([this] {
			_controller->hideSpecialLayer();
		}, close->lifetime());
	}
}

not_null<Ui::RpWidget*> WrapWidget::topWidget() const {
	if (_topTabs) {
		return _topTabs;
	}
	return _topBar;
}

void WrapWidget::showContent(object_ptr<ContentWidget> content) {
	_content = std::move(content);
	_content->show();

	finishShowContent();
}

void WrapWidget::finishShowContent() {
	_topShadow.create(this);
	_topShadow->toggleOn((wrap() == Wrap::Side)
		? rpl::single(true)
		: _content->desiredShadowVisibility());

	if (_topTabs) {
		_topTabs->finishAnimating();
	}
	_topShadow->finishAnimating();

	updateContentGeometry();

	_desiredHeights.fire(desiredHeightForContent());
}

rpl::producer<int> WrapWidget::desiredHeightForContent() const {
	auto result = _content->desiredHeightValue();
	if (_topTabs) {
		result = std::move(result)
			| rpl::map(func::add(_topTabs->height()));
	}
	return result;
}

object_ptr<ContentWidget> WrapWidget::createContent(Tab tab) {
	switch (tab) {
	case Tab::Profile: return createProfileWidget();
	case Tab::Media: return createMediaWidget();
	}
	Unexpected("Tab value in Info::WrapWidget::createInner()");
}

object_ptr<Profile::Widget> WrapWidget::createProfileWidget() {
	auto result = object_ptr<Profile::Widget>(
		this,
		Wrap::Side,
		controller(),
		_content->peer());
	return result;
}

object_ptr<Media::Widget> WrapWidget::createMediaWidget() {
	auto result = object_ptr<Media::Widget>(
		this,
		Wrap::Side,
		controller(),
		_content->peer(),
		Media::Widget::Type::Photo);
	return result;
}

bool WrapWidget::hasTopBarShadow() const {
	return _topShadow->toggled();
}

QPixmap WrapWidget::grabForShowAnimation(
		const Window::SectionSlideParams &params) {
	if (params.withTopBarShadow) _topShadow->hide(anim::type::instant);
	auto result = myGrab(this);
	if (params.withTopBarShadow) _topShadow->show(anim::type::instant);
	return result;
}

void WrapWidget::setInnerFocus() {
	_content->setInnerFocus();
}

void WrapWidget::showFinished() {
}

bool WrapWidget::showInternal(
		not_null<Window::SectionMemento*> memento) {
	if (auto infoMemento = dynamic_cast<Memento*>(memento.get())) {
		if (infoMemento->peerId() == peer()->id) {
			applyState(_content->wrap(), infoMemento);
			return true;
		}
	}
	return false;
}

std::unique_ptr<Window::SectionMemento> WrapWidget::createMemento() {
	auto result = std::make_unique<Memento>(peer()->id);
	saveState(result.get());
	return std::move(result);
}

rpl::producer<int> WrapWidget::desiredHeightValue() const {
	return
		rpl::single(desiredHeightForContent())
		| rpl::then(_desiredHeights.events())
		| rpl::flatten_latest();
}

void WrapWidget::saveState(not_null<Memento*> memento) {
	memento->setInner(_content->createMemento());
}

QRect WrapWidget::contentGeometry() const {
	return rect().marginsRemoved({ 0, topWidget()->height(), 0, 0 });
}

void WrapWidget::applyState(Wrap wrap, not_null<Memento*> memento) {
	// Validates contentGeometry().
	setupTop(wrap, memento->section(), memento->peerId());
	showContent(memento->content()->createWidget(
		this,
		wrap,
		controller(),
		contentGeometry()));
}

void WrapWidget::setupTabs(Tab tab) {
	_tab = tab;
	if (_tab == Tab::None) {
		_topTabs.destroy();
	} else if (!_topTabs) {
		createTabs();
	} else {
		_topTabs->setActiveSection(static_cast<int>(tab));
	}
}

void WrapWidget::resizeEvent(QResizeEvent *e) {
	topWidget()->resizeToWidth(width());
	updateContentGeometry();
}

void WrapWidget::updateContentGeometry() {
	if (_content) {
		_topShadow->resizeToWidth(width());
		_topShadow->moveToLeft(0, topWidget()->height());
		_content->setGeometry(contentGeometry());
	}
}

void WrapWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.fillRect(e->rect(), st::profileBg);
}

bool WrapWidget::wheelEventFromFloatPlayer(QEvent *e) {
	return _content->wheelEventFromFloatPlayer(e);
}

QRect WrapWidget::rectForFloatPlayer() const {
	return _content->rectForFloatPlayer();
}

} // namespace Info
