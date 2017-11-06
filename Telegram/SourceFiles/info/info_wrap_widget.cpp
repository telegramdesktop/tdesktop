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
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "info/info_top_bar.h"
#include "info/info_top_bar_override.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/search_field_controller.h"
#include "window/window_controller.h"
#include "window/window_slide_animation.h"
#include "auth_session.h"
#include "lang/lang_keys.h"
#include "styles/style_info.h"
#include "styles/style_profile.h"

namespace Info {
namespace {

const style::InfoTopBar &TopBarStyle(Wrap wrap) {
	return (wrap == Wrap::Layer)
		? st::infoLayerTopBar
		: st::infoTopBar;
}

} // namespace

struct WrapWidget::StackItem {
	std::unique_ptr<ContentMemento> section;
	std::unique_ptr<ContentMemento> anotherTab;
};

WrapWidget::WrapWidget(
	QWidget *parent,
	not_null<Window::Controller*> window,
	Wrap wrap,
	not_null<Memento*> memento)
: SectionWidget(parent, window)
, _wrap(wrap)
, _controller(createController(window, memento->content()))
, _topShadow(this) {
	_topShadow->toggleOn(topShadowToggledValue()
		| rpl::filter([](bool shown) {
			return true;
		}));
	_wrap.changes()
		| rpl::start_with_next([this] {
			setupTop();
			finishShowContent();
		}, lifetime());
	selectedListValue()
		| rpl::start_with_next([this](SelectedItems &&items) {
			InvokeQueued(this, [this, items = std::move(items)]() mutable {
				refreshTopBarOverride(std::move(items));
			});
		}, lifetime());
	restoreHistoryStack(memento->takeStack());
}

void WrapWidget::restoreHistoryStack(
		std::vector<std::unique_ptr<ContentMemento>> stack) {
	Expects(!stack.empty());
	Expects(_historyStack.empty());
	auto content = std::move(stack.back());
	stack.pop_back();
	if (!stack.empty()) {
		_historyStack.reserve(stack.size());
		for (auto &stackItem : stack) {
			auto item = StackItem();
			item.section = std::move(stackItem);
			_historyStack.push_back(std::move(item));
		}
	}
	showNewContent(content.get());
}

std::unique_ptr<Controller> WrapWidget::createController(
		not_null<Window::Controller*> window,
		not_null<ContentMemento*> memento) {
	auto result = std::make_unique<Controller>(
		this,
		window,
		memento);
	return result;
}

not_null<PeerData*> WrapWidget::peer() const {
	return _controller->peer();
}

void WrapWidget::createTabs() {
	_topTabs.create(this, st::infoTabs);
	auto sections = QStringList();
	sections.push_back(lang(lng_profile_info_section).toUpper());
	sections.push_back(lang(lng_info_tab_media).toUpper());
	_topTabs->setSections(sections);
	_topTabs->setActiveSection(static_cast<int>(_tab));
	_topTabs->finishAnimating();

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
	if (_tab == tab) {
		return;
	}
	Expects(_content != nullptr);
	auto direction = (tab > _tab)
		? SlideDirection::FromRight
		: SlideDirection::FromLeft;
	auto newAnotherMemento = _content->createMemento();
	if (!_anotherTabMemento) {
		_anotherTabMemento = createTabMemento(tab);
	}
	auto newController = createController(
		_controller->window(),
		_anotherTabMemento.get());
	auto newContent = createContent(
		_anotherTabMemento.get(),
		newController.get());
	auto animationParams = SectionSlideParams();
//	animationParams.withFade = (wrap() == Wrap::Layer);
	animationParams.withTabs = true;
	animationParams.withTopBarShadow = hasTopBarShadow()
			&& newContent->hasTopBarShadow();
	animationParams.oldContentCache = grabForShowAnimation(
		animationParams);

	_controller = std::move(newController);
	showContent(std::move(newContent));

	showAnimated(direction, animationParams);

	_anotherTabMemento = std::move(newAnotherMemento);
	_tab = tab;
}

void WrapWidget::setupTabbedTop() {
	auto section = _controller->section();
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

void WrapWidget::setupTop() {
	if (wrap() == Wrap::Side && _historyStack.empty()) {
		setupTabbedTop();
	} else {
		setupTabs(Tab::None);
	}
	if (_topTabs) {
		_topBar.destroy();
	} else {
		createTopBar();
	}
	refreshTopBarOverride();
}

void WrapWidget::createTopBar() {
	_topBar.create(this, TopBarStyle(wrap()));

	_topBar->setTitle(TitleValue(
		_controller->section(),
		_controller->peer()));
	if (wrap() != Wrap::Layer || !_historyStack.empty()) {
		_topBar->enableBackButton(true);
		_topBar->backRequest()
			| rpl::start_with_next([this] {
				showBackFromStack();
			}, _topBar->lifetime());
	}
	if (wrap() == Wrap::Layer) {
		auto close = _topBar->addButton(
			base::make_unique_q<Ui::IconButton>(
				_topBar,
				st::infoLayerTopBarClose));
		close->addClickHandler([this] {
			_controller->window()->hideSpecialLayer();
		});
	} else if (requireTopBarSearch()) {
		auto search = _controller->searchFieldController();
		Assert(search != nullptr);
		_topBar->createSearchView(
			search,
			_controller->searchEnabledByContent());
	}

	_topBar->move(0, 0);
	_topBar->resizeToWidth(width());
	_topBar->show();
}

void WrapWidget::refreshTopBarOverride(SelectedItems &&items) {
	if (items.list.empty()) {
		destroyTopBarOverride();
	} else if (_topBarOverride) {
		_topBarOverride->setItems(std::move(items));
	} else {
		createTopBarOverride(std::move(items));
	}
}

void WrapWidget::refreshTopBarOverride() {
	if (_topBarOverride) {
		auto items = _topBarOverride->takeItems();
		destroyTopBarOverride();
		createTopBarOverride(std::move(items));
	}
}

void WrapWidget::destroyTopBarOverride() {
	if (!_topBarOverride) {
		return;
	}
	auto widget = std::exchange(_topBarOverride, nullptr);
	auto handle = weak(widget.data());
	_topBarOverrideAnimation.start([this, handle] {
	}, 1., 0., st::slideWrapDuration);
	widget.destroy();
	if (_topTabs) {
		_topTabs->show();
	} else if (_topBar) {
		_topBar->show();
	}
}

void WrapWidget::createTopBarOverride(SelectedItems &&items) {
	Expects(_topBarOverride == nullptr);
	_topBarOverride.create(
		this,
		TopBarStyle(wrap()),
		std::move(items));
	if (_topTabs) {
		_topTabs->hide();
	} else if (_topBar) {
		_topBar->hide();
	}
	_topBarOverride->cancelRequests()
		| rpl::start_with_next([this](auto) {
			_content->cancelSelection();
		}, _topBarOverride->lifetime());
	_topBarOverride->moveToLeft(0, 0);
	_topBarOverride->resizeToWidth(width());
	_topBarOverride->show();
}

bool WrapWidget::requireTopBarSearch() const {
	if (!_controller->searchFieldController()) {
		return false;
	} else if (_controller->wrap() == Wrap::Layer) {
		return false;
	} else if (hasStackHistory()) {
		return true;
	}
	auto section = _controller->section();
	return (section.type() != Section::Type::Media)
		|| !Media::TypeToTabIndex(section.mediaType()).has_value();
}

void WrapWidget::showBackFromStack() {
	auto params = Window::SectionShow(
		Window::SectionShow::Way::Backward);
	if (!_historyStack.empty()) {
		auto last = std::move(_historyStack.back());
		_historyStack.pop_back();
		showNewContent(
			last.section.get(),
			params);
		_anotherTabMemento = std::move(last.anotherTab);
	} else {
		_controller->window()->showBackFromStack(params);
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
	_anotherTabMemento = nullptr;
	finishShowContent();
}

void WrapWidget::finishShowContent() {
	_content->setIsStackBottom(_historyStack.empty());
	updateContentGeometry();
	_desiredHeights.fire(desiredHeightForContent());
	_desiredShadowVisibilities.fire(_content->desiredShadowVisibility());
	_selectedLists.fire(_content->selectedListValue());
	_topShadow->raise();
	_topShadow->finishAnimating();
	if (_topTabs) {
		_topTabs->raise();
	}
}

rpl::producer<bool> WrapWidget::topShadowToggledValue() const {
	using namespace rpl::mappers;
	return rpl::combine(
		_controller->wrapValue(),
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

rpl::producer<SelectedItems> WrapWidget::selectedListValue() const {
	return _selectedLists.events() | rpl::flatten_latest();
}

std::unique_ptr<ContentMemento> WrapWidget::createTabMemento(
		Tab tab) {
	switch (tab) {
	case Tab::Profile: return std::make_unique<Profile::Memento>(
		_controller->peerId(),
		_controller->migratedPeerId());
	case Tab::Media: return std::make_unique<Media::Memento>(
		_controller->peerId(),
		_controller->migratedPeerId(),
		Media::Type::Photo);
	}
	Unexpected("Tab value in Info::WrapWidget::createInner()");
}

object_ptr<ContentWidget> WrapWidget::createContent(
		not_null<ContentMemento*> memento,
		not_null<Controller*> controller) {
	return memento->createWidget(
		this,
		controller,
		contentGeometry());
}

void WrapWidget::convertProfileFromStackToTab() {
	if (_historyStack.empty()) {
		return;
	}
	auto &entry = _historyStack[0];
	if (entry.section->section().type() != Section::Type::Profile) {
		return;
	}
	auto convertInsideStack = (_historyStack.size() > 1);
	auto checkSection = convertInsideStack
		? _historyStack[1].section->section()
		: _controller->section();
	auto &anotherMemento = convertInsideStack
		? _historyStack[1].anotherTab
		: _anotherTabMemento;
	if (checkSection.type() != Section::Type::Media) {
		return;
	}
	if (!Info::Media::TypeToTabIndex(checkSection.mediaType())) {
		return;
	}
	anotherMemento = std::move(entry.section);
	_historyStack.erase(_historyStack.begin());
}

void WrapWidget::setWrap(Wrap wrap) {
	if (_wrap.current() != Wrap::Side && wrap == Wrap::Side) {
		convertProfileFromStackToTab();
	}
	_wrap = wrap;
}

bool WrapWidget::hasTopBarShadow() const {
	return _topShadow->toggled();
}

QPixmap WrapWidget::grabForShowAnimation(
		const Window::SectionSlideParams &params) {
	if (params.withTopBarShadow) {
		_topShadow->setVisible(false);
	} else {
		_topShadow->setVisible(_topShadow->toggled());
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
	if (params.withTopBarShadow) {
		_topShadow->setVisible(true);
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
		if (infoMemento->stackSize() > 1) {
			return false;
		}
		auto content = infoMemento->content();
		if (_controller->validateMementoPeer(content)) {
			if (_content->showInternal(content)) {
				return true;
			}
		}
		showNewContent(
			content,
			params);
		return true;
	}
	return false;
}

std::unique_ptr<Window::SectionMemento> WrapWidget::createMemento() {
	auto stack = std::vector<std::unique_ptr<ContentMemento>>();
	stack.reserve(_historyStack.size() + 1);
	for (auto &stackItem : _historyStack) {
		stack.push_back(std::move(stackItem.section));
	}
	stack.push_back(_content->createMemento());
	return std::make_unique<Memento>(std::move(stack));
}

rpl::producer<int> WrapWidget::desiredHeightValue() const {
	return
		rpl::single(desiredHeightForContent())
		| rpl::then(_desiredHeights.events())
		| rpl::flatten_latest();
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
	auto newController = createController(
		_controller->window(),
		memento);
	auto newContent = object_ptr<ContentWidget>(nullptr);
	if (needAnimation) {
		newContent = createContent(memento, newController.get());
		animationParams.withTopBarShadow = hasTopBarShadow()
			&& newContent->hasTopBarShadow();
		animationParams.oldContentCache = grabForShowAnimation(
			animationParams);
		animationParams.withFade = (wrap() == Wrap::Layer);
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

	_controller = std::move(newController);
	if (newContent) {
		setupTop();
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
	setupTop();
	showContent(createContent(memento, _controller.get()));
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
	if (_topBarOverride) {
		_topBarOverride->resizeToWidth(width());
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
