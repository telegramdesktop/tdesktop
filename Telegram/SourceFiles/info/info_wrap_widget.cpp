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
#include <rpl/combine.h>
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
#include "window/window_slide_animation.h"
#include "auth_session.h"
#include "lang/lang_keys.h"
#include "styles/style_info.h"
#include "styles/style_profile.h"

namespace Info {

struct WrapWidget::StackItem {
	std::unique_ptr<ContentMemento> section;
	std::unique_ptr<ContentMemento> anotherTab;
};

WrapWidget::WrapWidget(
	QWidget *parent,
	not_null<Window::Controller*> controller,
	Wrap wrap,
	not_null<Memento*> memento)
: SectionWidget(parent, controller)
, _wrap(wrap)
, _topShadow(this) {
	_topShadow->toggleOn(topShadowToggledValue());
	showNewContent(memento->content());
}

not_null<PeerData*> WrapWidget::peer() const {
	return _content->peer();
}

Wrap WrapWidget::wrap() const {
	return _wrap.current();
}

void WrapWidget::setWrap(Wrap wrap) {
	if (_wrap.current() != wrap) {
		_wrap = wrap;
		setupTop(_content->section(), _content->peer()->id);
		finishShowContent();
	}
}

void WrapWidget::createTabs() {
	_topTabs.create(this, st::infoTabs);
	auto sections = QStringList();
	sections.push_back(lang(lng_profile_info_section).toUpper());
	sections.push_back(lang(lng_info_tab_media).toUpper());
	_topTabs->setSections(sections);
	_topTabs->sectionActivated()
		| rpl::map([](int index) { return static_cast<Tab>(index); })
		| rpl::start_with_next(
			[this](Tab tab) { showTab(tab); },
			lifetime());

	_topTabs->move(0, 0);
	_topTabs->resizeToWidth(width());
	_topTabs->show();

	_topTabsBackground.create(this, st::profileBg);
	_topTabsBackground->setAttribute(Qt::WA_OpaquePaintEvent);

	_topTabsBackground->move(0, 0);
	_topTabsBackground->resize(
		width(),
		_topTabs->height() - st::lineWidth);
	_topTabsBackground->show();
}

void WrapWidget::forceContentRepaint() {
	// WA_OpaquePaintEvent on TopBar creates render glitches when
	// animating the LayerWidget's height :( Fixing by repainting.
	if (_topTabs) {
		_topTabsBackground->update();
	} else if (_topBar) {
		_topBar->update();
	}
	_content->update();
}

void WrapWidget::showTab(Tab tab) {
	Expects(_content != nullptr);
	auto direction = (tab > _tab)
		? SlideDirection::FromRight
		: SlideDirection::FromLeft;
	auto newAnotherMemento = _content->createMemento();
	auto newContent = _anotherTabMemento
		? createContent(_anotherTabMemento.get())
		: createContent(tab);
	auto animationParams = SectionSlideParams();
//	animationParams.withFade = (wrap() == Wrap::Layer);
	animationParams.withTabs = true;
	animationParams.withTopBarShadow = hasTopBarShadow()
			&& newContent->hasTopBarShadow();
	animationParams.oldContentCache = grabForShowAnimation(
		animationParams);

	showContent(std::move(newContent));

	showAnimated(direction, animationParams);

	_anotherTabMemento = std::move(newAnotherMemento);
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
		const Section &section,
		PeerId peerId) {
	if (wrap() == Wrap::Side && _historyStack.empty()) {
		setupTabbedTop(section);
	} else {
		setupTabs(Tab::None);
	}
	if (_topTabs) {
		_topBar.destroy();
	} else {
		createTopBar(section, peerId);
	}
}

void WrapWidget::createTopBar(
		const Section &section,
		PeerId peerId) {
	_topBar.create(
		this,
		(wrap() == Wrap::Layer)
			? st::infoLayerTopBar
			: st::infoTopBar);

	_topBar->setTitle(TitleValue(
		section,
		peerId));
	if (wrap() != Wrap::Layer || !_historyStack.empty()) {
		_topBar->enableBackButton(true);
		_topBar->backRequest()
			| rpl::start_with_next([this] {
				showBackFromStack();
			}, _topBar->lifetime());
	}
	if (wrap() == Wrap::Layer) {
		auto close = _topBar->addButton(object_ptr<Ui::IconButton>(
			_topBar,
			st::infoLayerTopBarClose));
		close->addClickHandler([this] {
			controller()->hideSpecialLayer();
		});
	}

	_topBar->move(0, 0);
	_topBar->resizeToWidth(width());
	_topBar->show();
}

void WrapWidget::showBackFromStack() {
	auto params = Window::SectionShow(
		Window::SectionShow::Way::Backward);
	if (!_historyStack.empty()) {
		auto last = std::move(_historyStack.back());
		_historyStack.pop_back();
		_anotherTabMemento = std::move(last.anotherTab);
		showNewContent(
			last.section.get(),
			params);
	} else {
		controller()->showBackFromStack(params);
	}
}

not_null<Ui::RpWidget*> WrapWidget::topWidget() const {
	if (_topTabs) {
		return _topTabsBackground;
	}
	return _topBar;
}

void WrapWidget::showContent(object_ptr<ContentWidget> content) {
	_content = std::move(content);
	_content->show();
	finishShowContent();
}

void WrapWidget::finishShowContent() {
	updateContentGeometry();
	_desiredHeights.fire(desiredHeightForContent());
	_desiredShadowVisibilities.fire(_content->desiredShadowVisibility());
	_topShadow->raise();
	_topShadow->finishAnimating();
	if (_topTabs) {
		_topTabs->raise();
		_topTabs->finishAnimating();
	}
}

rpl::producer<bool> WrapWidget::topShadowToggledValue() const {
	using namespace rpl::mappers;
	return rpl::combine(
		_wrap.value(),
		_desiredShadowVisibilities.events() | rpl::flatten_latest(),
		($1 == Wrap::Side) || $2);
}

rpl::producer<int> WrapWidget::desiredHeightForContent() const {
	using namespace rpl::mappers;
	return rpl::combine(
		_content->desiredHeightValue(),
		topWidget()->heightValue(),
		$1 + $2);
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
		_wrap.value(),
		controller(),
		_content->peer());
	return result;
}

object_ptr<Media::Widget> WrapWidget::createMediaWidget() {
	auto result = object_ptr<Media::Widget>(
		this,
		_wrap.value(),
		controller(),
		_content->peer(),
		Media::Widget::Type::Photo);
	return result;
}

object_ptr<ContentWidget> WrapWidget::createContent(
		not_null<ContentMemento*> memento) {
	return memento->createWidget(
		this,
		_wrap.value(),
		controller(),
		contentGeometry());
}

bool WrapWidget::hasTopBarShadow() const {
	return _topShadow->toggled();
}

QPixmap WrapWidget::grabForShowAnimation(
		const Window::SectionSlideParams &params) {
	if (params.withTopBarShadow) {
		_topShadow->setVisible(false);
	} else {
		_topShadow->toggle(_topShadow->toggled(), anim::type::instant);
	}
	if (params.withTabs && _topTabs) {
		_topTabs->hide();
	}
	auto result = myGrab(this);
	if (params.withTopBarShadow) {
		_topShadow->setVisible(true);
	}
	if (params.withTabs && _topTabs) {
		_topTabs->show();
	}
	return result;
}

void WrapWidget::showAnimatedHook(
		const Window::SectionSlideParams &params) {
	if (params.withTabs && _topTabs) {
		_topTabs->show();
		_topTabsBackground->show();
	}
}

void WrapWidget::doSetInnerFocus() {
	_content->setInnerFocus();
}

void WrapWidget::showFinishedHook() {
	// Restore shadow visibility after showChildren() call.
	_topShadow->toggle(_topShadow->toggled(), anim::type::instant);
}

bool WrapWidget::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	if (auto infoMemento = dynamic_cast<Memento*>(memento.get())) {
		auto content = infoMemento->content();
		if (!_content->showInternal(content)) {
			showNewContent(
				content,
				params);
		}
		return true;
	}
	return false;
}

std::unique_ptr<Window::SectionMemento> WrapWidget::createMemento() {
	auto result = std::make_unique<Memento>(_content->peer()->id);
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

void WrapWidget::showNewContent(
		not_null<ContentMemento*> memento,
		const Window::SectionShow &params) {
	auto saveToStack = (_content != nullptr)
		&& (params.way == Window::SectionShow::Way::Forward);
	auto needAnimation = (_content != nullptr)
		&& (params.animated != anim::type::instant);
	auto animationParams = SectionSlideParams();
	auto newContent = object_ptr<ContentWidget>(nullptr);
	if (needAnimation) {
		newContent = createContent(memento);
		animationParams.withTopBarShadow = hasTopBarShadow()
			&& newContent->hasTopBarShadow();
		animationParams.oldContentCache = grabForShowAnimation(
			animationParams);
//		animationParams.withFade = (wrap() == Wrap::Layer);
	}
	if (saveToStack) {
		auto item = StackItem();
		item.section = _content->createMemento();
		if (_anotherTabMemento) {
			item.anotherTab = std::move(_anotherTabMemento);
		}
		_historyStack.push_back(std::move(item));
	} else if (params.way == Window::SectionShow::Way::ClearStack) {
		_historyStack.clear();
	}
	if (newContent) {
		setupTop(newContent->section(), newContent->peer()->id);
		showContent(std::move(newContent));
	} else {
		showNewContent(memento);
	}
	if (animationParams) {
		showAnimated(
			saveToStack
				? SlideDirection::FromRight
				: SlideDirection::FromLeft,
			animationParams);
	}
}

void WrapWidget::showNewContent(not_null<ContentMemento*> memento) {
	// Validates contentGeometry().
	setupTop(memento->section(), memento->peerId());
	showContent(createContent(memento));
}

void WrapWidget::setupTabs(Tab tab) {
	_tab = tab;
	if (_tab == Tab::None) {
		_topTabs.destroy();
		_topTabsBackground.destroy();
	} else if (!_topTabs) {
		createTabs();
	} else {
		_topTabs->setActiveSection(static_cast<int>(tab));
	}
}

void WrapWidget::resizeEvent(QResizeEvent *e) {
	if (_topTabs) {
		_topTabs->resizeToWidth(width());
		_topTabsBackground->resize(
			width(),
			_topTabs->height() - st::lineWidth);
	} else if (_topBar) {
		_topBar->resizeToWidth(width());
	}
	updateContentGeometry();
}

void WrapWidget::updateContentGeometry() {
	if (_content) {
		_topShadow->resizeToWidth(width());
		_topShadow->moveToLeft(0, topWidget()->height());
		_content->setGeometry(contentGeometry());
	}
}

bool WrapWidget::wheelEventFromFloatPlayer(QEvent *e) {
	return _content->wheelEventFromFloatPlayer(e);
}

QRect WrapWidget::rectForFloatPlayer() const {
	return _content->rectForFloatPlayer();
}

WrapWidget::~WrapWidget() = default;

} // namespace Info
