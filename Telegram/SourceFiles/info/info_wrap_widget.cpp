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
#include <rpl/take.h>
#include <rpl/combine.h>
#include "info/profile/info_profile_widget.h"
#include "info/profile/info_profile_members.h"
#include "info/profile/info_profile_values.h"
#include "info/media/info_media_widget.h"
#include "info/info_content_widget.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "info/info_top_bar.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/search_field_controller.h"
#include "calls/calls_instance.h"
#include "window/window_controller.h"
#include "window/window_slide_animation.h"
#include "window/window_peer_menu.h"
#include "boxes/peer_list_box.h"
#include "auth_session.h"
#include "mainwidget.h"
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
//	std::unique_ptr<ContentMemento> anotherTab;
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
				if (_topBar) _topBar->setSelectedItems(std::move(items));
			});
		}, lifetime());
	restoreHistoryStack(memento->takeStack());
}

void WrapWidget::restoreHistoryStack(
		std::vector<std::unique_ptr<ContentMemento>> stack) {
	Expects(!stack.empty());
	Expects(!hasStackHistory());

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

	startInjectingActivePeerProfiles();

	showNewContent(content.get());
}

void WrapWidget::startInjectingActivePeerProfiles() {
	using namespace rpl::mappers;
	rpl::combine(
		_wrap.value(),
		_controller->parentController()->activePeer.value())
		| rpl::filter((_1 == Wrap::Side) && (_2 != nullptr))
		| rpl::map(_2)
		| rpl::start_with_next([this](not_null<PeerData*> peer) {
			injectActivePeerProfile(peer);
		}, lifetime());

}

void WrapWidget::injectActivePeerProfile(not_null<PeerData*> peer) {
	const auto firstPeerId = hasStackHistory()
		? _historyStack.front().section->peerId()
		: _controller->peerId();
	const auto firstSectionType = hasStackHistory()
		? _historyStack.front().section->section().type()
		: _controller->section().type();
	const auto firstSectionMediaType = [&] {
		if (firstSectionType == Section::Type::Profile) {
			return Section::MediaType::kCount;
		}
		return hasStackHistory()
			? _historyStack.front().section->section().mediaType()
			: _controller->section().mediaType();
	}();
	const auto expectedType = peer->isSelf()
		? Section::Type::Media
		: Section::Type::Profile;
	const auto expectedMediaType = peer->isSelf()
		? Section::MediaType::Photo
		: Section::MediaType::kCount;
	if (firstSectionType != expectedType
		|| firstSectionMediaType != expectedMediaType
		|| firstPeerId != peer->id) {
		auto injected = StackItem();
		auto section = peer->isSelf()
			? Section(Section::MediaType::Photo)
			: Section(Section::Type::Profile);
		injected.section = std::move(
			Memento(peer->id, section).takeStack().front());
		_historyStack.insert(
			_historyStack.begin(),
			std::move(injected));
		if (_content) {
			setupTop();
			finishShowContent();
		}
	}
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

// This was done for tabs support.
//
//void WrapWidget::createTabs() {
//	_topTabs.create(this, st::infoTabs);
//	auto sections = QStringList();
//	sections.push_back(lang(lng_profile_info_section).toUpper());
//	sections.push_back(lang(lng_info_tab_media).toUpper());
//	_topTabs->setSections(sections);
//	_topTabs->setActiveSection(static_cast<int>(_tab));
//	_topTabs->finishAnimating();
//
//	_topTabs->sectionActivated()
//		| rpl::map([](int index) { return static_cast<Tab>(index); })
//		| rpl::start_with_next(
//			[this](Tab tab) { showTab(tab); },
//			lifetime());
//
//	_topTabs->move(0, 0);
//	_topTabs->resizeToWidth(width());
//	_topTabs->show();
//
//	_topTabsBackground.create(this, st::profileBg);
//	_topTabsBackground->setAttribute(Qt::WA_OpaquePaintEvent);
//
//	_topTabsBackground->move(0, 0);
//	_topTabsBackground->resize(
//		width(),
//		_topTabs->height() - st::lineWidth);
//	_topTabsBackground->show();
//}

void WrapWidget::forceContentRepaint() {
	// WA_OpaquePaintEvent on TopBar creates render glitches when
	// animating the LayerWidget's height :( Fixing by repainting.

	// This was done for tabs support.
	//
	//if (_topTabs) {
	//	_topTabsBackground->update();
	//}

	if (_topBar) {
		_topBar->update();
	}
	_content->update();
}

// This was done for tabs support.
//
//void WrapWidget::showTab(Tab tab) {
//	if (_tab == tab) {
//		return;
//	}
//	Expects(_content != nullptr);
//	auto direction = (tab > _tab)
//		? SlideDirection::FromRight
//		: SlideDirection::FromLeft;
//	auto newAnotherMemento = _content->createMemento();
//	if (!_anotherTabMemento) {
//		_anotherTabMemento = createTabMemento(tab);
//	}
//	auto newController = createController(
//		_controller->parentController(),
//		_anotherTabMemento.get());
//	auto newContent = createContent(
//		_anotherTabMemento.get(),
//		newController.get());
//	auto animationParams = SectionSlideParams();
////	animationParams.withFade = (wrap() == Wrap::Layer);
//	animationParams.withTabs = true;
//	animationParams.withTopBarShadow = hasTopBarShadow()
//			&& newContent->hasTopBarShadow();
//	animationParams.oldContentCache = grabForShowAnimation(
//		animationParams);
//
//	_controller = std::move(newController);
//	showContent(std::move(newContent));
//
//	showAnimated(direction, animationParams);
//
//	_anotherTabMemento = std::move(newAnotherMemento);
//	_tab = tab;
//}
//
//void WrapWidget::setupTabbedTop() {
//	auto section = _controller->section();
//	switch (section.type()) {
//	case Section::Type::Profile:
//		setupTabs(Tab::Profile);
//		break;
//	case Section::Type::Media:
//		switch (section.mediaType()) {
//		case Section::MediaType::Photo:
//		case Section::MediaType::Video:
//		case Section::MediaType::File:
//			setupTabs(Tab::Media);
//			break;
//		default:
//			setupTabs(Tab::None);
//			break;
//		}
//		break;
//	case Section::Type::CommonGroups:
//  case Section::Type::Members:
//		setupTabs(Tab::None);
//		break;
//	}
//}

void WrapWidget::setupTop() {
	// This was done for tabs support.
	//
	//if (wrap() == Wrap::Side && !hasStackHistory()) {
	//	setupTabbedTop();
	//} else {
	//	setupTabs(Tab::None);
	//}
	//if (_topTabs) {
	//	_topBar.destroy();
	//} else {
	//	createTopBar();
	//}
	createTopBar();
}

void WrapWidget::createTopBar() {
	const auto wrapValue = wrap();
	auto selectedItems = _topBar
		? _topBar->takeSelectedItems()
		: SelectedItems(Section::MediaType::kCount);
	_topBar.create(this, TopBarStyle(wrapValue), std::move(selectedItems));
	_topBar->cancelSelectionRequests()
		| rpl::start_with_next([this](auto) {
			_content->cancelSelection();
		}, _topBar->lifetime());

	_topBar->setTitle(TitleValue(
		_controller->section(),
		_controller->peer(),
		!hasStackHistory()));
	if (wrapValue == Wrap::Narrow || hasStackHistory()) {
		_topBar->enableBackButton();
		_topBar->backRequest()
			| rpl::start_with_next([this] {
				_controller->showBackFromStack();
			}, _topBar->lifetime());
	} else if (wrapValue == Wrap::Side) {
		auto close = _topBar->addButton(
			base::make_unique_q<Ui::IconButton>(
				_topBar,
				st::infoTopBarClose));
		close->addClickHandler([this] {
			_controller->parentController()->closeThirdSection();
		});
	}
	if (wrapValue == Wrap::Layer) {
		auto close = _topBar->addButton(
			base::make_unique_q<Ui::IconButton>(
				_topBar,
				st::infoLayerTopBarClose));
		close->addClickHandler([this] {
			_controller->parentController()->hideSpecialLayer();
		});
	} else if (requireTopBarSearch()) {
		auto search = _controller->searchFieldController();
		Assert(search != nullptr);
		_topBar->createSearchView(
			search,
			_controller->searchEnabledByContent(),
			_controller->takeSearchStartsFocused());
	}
	if (_controller->section().type() == Section::Type::Profile
		&& (wrapValue != Wrap::Side || hasStackHistory())) {
		addProfileMenuButton();
		addProfileCallsButton();
//		addProfileNotificationsButton();
	}

	_topBar->lower();
	_topBar->resizeToWidth(width());
	_topBar->finishAnimating();
	_topBar->show();
}

void WrapWidget::addProfileMenuButton() {
	Expects(_topBar != nullptr);

	_topBarMenuToggle.reset(_topBar->addButton(
		base::make_unique_q<Ui::IconButton>(
			_topBar,
			(wrap() == Wrap::Layer
				? st::infoLayerTopBarMenu
				: st::infoTopBarMenu))));
	_topBarMenuToggle->addClickHandler([this] {
		showProfileMenu();
	});
}

void WrapWidget::addProfileCallsButton() {
	Expects(_topBar != nullptr);

	const auto user = _controller->peer()->asUser();
	if (!user || user->isSelf() || !Global::PhoneCallsEnabled()) {
		return;
	}

	Notify::PeerUpdateValue(
		user,
		Notify::PeerUpdate::Flag::UserHasCalls
	) | rpl::filter([=] {
		return user->hasCalls();
	}) | rpl::take(
		1
	) | rpl::start_with_next([=] {
		_topBar->addButton(
			base::make_unique_q<Ui::IconButton>(
				_topBar,
				(wrap() == Wrap::Layer
					? st::infoLayerTopBarCall
					: st::infoTopBarCall))
		)->addClickHandler([=] {
			Calls::Current().startOutgoingCall(user);
		});
	}, _topBar->lifetime());

	if (user && user->callsStatus() == UserData::CallsStatus::Unknown) {
		user->updateFull();
	}
}

void WrapWidget::addProfileNotificationsButton() {
	Expects(_topBar != nullptr);

	const auto peer = _controller->peer();
	auto notifications = _topBar->addButton(
		base::make_unique_q<Ui::IconButton>(
			_topBar,
			(wrap() == Wrap::Layer
				? st::infoLayerTopBarNotifications
				: st::infoTopBarNotifications)));
	notifications->addClickHandler([peer] {
		const auto muteState = peer->isMuted()
			? Data::NotifySettings::MuteChange::Unmute
			: Data::NotifySettings::MuteChange::Mute;
		App::main()->updateNotifySettings(peer, muteState);
	});
	Profile::NotificationsEnabledValue(peer)
		| rpl::start_with_next([notifications](bool enabled) {
			const auto iconOverride = enabled
				? &st::infoNotificationsActive
				: nullptr;
			const auto rippleOverride = enabled
				? &st::lightButtonBgOver
				: nullptr;
			notifications->setIconOverride(iconOverride, iconOverride);
			notifications->setRippleColorOverride(rippleOverride);
		}, notifications->lifetime());
}

void WrapWidget::showProfileMenu() {
	if (_topBarMenu) {
		_topBarMenu->hideAnimated(
			Ui::InnerDropdown::HideOption::IgnoreShow);
		return;
	}
	_topBarMenu = base::make_unique_q<Ui::DropdownMenu>(this);

	_topBarMenu->setHiddenCallback([this] {
		InvokeQueued(this, [this] { _topBarMenu = nullptr; });
		if (auto toggle = _topBarMenuToggle.get()) {
			toggle->setForceRippled(false);
		}
	});
	_topBarMenu->setShowStartCallback([this] {
		if (auto toggle = _topBarMenuToggle.get()) {
			toggle->setForceRippled(true);
		}
	});
	_topBarMenu->setHideStartCallback([this] {
		if (auto toggle = _topBarMenuToggle.get()) {
			toggle->setForceRippled(false);
		}
	});
	_topBarMenuToggle->installEventFilter(_topBarMenu.get());

	Window::FillPeerMenu(
		_controller->parentController(),
		_controller->peer(),
		[this](const QString &text, base::lambda<void()> callback) {
			return _topBarMenu->addAction(text, std::move(callback));
		},
		Window::PeerMenuSource::Profile);
	auto position = (wrap() == Wrap::Layer)
		? st::infoLayerTopBarMenuPosition
		: st::infoTopBarMenuPosition;
	_topBarMenu->moveToRight(position.x(), position.y());
	_topBarMenu->showAnimated(Ui::PanelAnimation::Origin::TopRight);
}

bool WrapWidget::requireTopBarSearch() const {
	if (!_controller->searchFieldController()) {
		return false;
	} else if (_controller->wrap() == Wrap::Layer
		|| _controller->section().type() == Section::Type::Profile) {
		return false;
	} else if (hasStackHistory()) {
		return true;
	}
	// This was for top-level tabs support.
	//
	//auto section = _controller->section();
	//return (section.type() != Section::Type::Media)
	//	|| !Media::TypeToTabIndex(section.mediaType()).has_value();
	return false;
}

bool WrapWidget::showBackFromStackInternal(
		const Window::SectionShow &params) {
	if (hasStackHistory()) {
		auto last = std::move(_historyStack.back());
		_historyStack.pop_back();
		showNewContent(
			last.section.get(),
			params.withWay(Window::SectionShow::Way::Backward));
		//_anotherTabMemento = std::move(last.anotherTab);
		return true;
	}
	return (wrap() == Wrap::Layer);
}

not_null<Ui::RpWidget*> WrapWidget::topWidget() const {
	// This was done for tabs support.
	//
	//if (_topTabs) {
	//	return _topTabsBackground;
	//}
	return _topBar;
}

void WrapWidget::showContent(object_ptr<ContentWidget> content) {
	_content = std::move(content);
	_content->show();
	_additionalScroll = 0;
	//_anotherTabMemento = nullptr;
	finishShowContent();
}

void WrapWidget::finishShowContent() {
	_content->setIsStackBottom(!hasStackHistory());
	updateContentGeometry();
	_desiredHeights.fire(desiredHeightForContent());
	_desiredShadowVisibilities.fire(_content->desiredShadowVisibility());
	_selectedLists.fire(_content->selectedListValue());
	_scrollTillBottomChanges.fire(_content->scrollTillBottomChanges());
	_topShadow->raise();
	_topShadow->finishAnimating();

	// This was done for tabs support.
	//
	//if (_topTabs) {
	//	_topTabs->raise();
	//}
}

rpl::producer<bool> WrapWidget::topShadowToggledValue() const {
	// Allows always showing shadow for specific wrap value.
	// Was done for top level tabs support.
	//
	//using namespace rpl::mappers;
	//return rpl::combine(
	//	_controller->wrapValue(),
	//	_desiredShadowVisibilities.events() | rpl::flatten_latest(),
	//	(_1 == Wrap::Side) || _2);
	return _desiredShadowVisibilities.events()
		| rpl::flatten_latest();
}

rpl::producer<int> WrapWidget::desiredHeightForContent() const {
	using namespace rpl::mappers;
	return rpl::combine(
		_content->desiredHeightValue(),
		topWidget()->heightValue(),
		_1 + _2);
}

rpl::producer<SelectedItems> WrapWidget::selectedListValue() const {
	return _selectedLists.events() | rpl::flatten_latest();
}

// Was done for top level tabs support.
//
//std::unique_ptr<ContentMemento> WrapWidget::createTabMemento(
//		Tab tab) {
//	switch (tab) {
//	case Tab::Profile: return std::make_unique<Profile::Memento>(
//		_controller->peerId(),
//		_controller->migratedPeerId());
//	case Tab::Media: return std::make_unique<Media::Memento>(
//		_controller->peerId(),
//		_controller->migratedPeerId(),
//		Media::Type::Photo);
//	}
//	Unexpected("Tab value in Info::WrapWidget::createInner()");
//}

object_ptr<ContentWidget> WrapWidget::createContent(
		not_null<ContentMemento*> memento,
		not_null<Controller*> controller) {
	return memento->createWidget(
		this,
		controller,
		contentGeometry());
}

// Was done for top level tabs support.
//
//void WrapWidget::convertProfileFromStackToTab() {
//	if (!hasStackHistory()) {
//		return;
//	}
//	auto &entry = _historyStack[0];
//	if (entry.section->section().type() != Section::Type::Profile) {
//		return;
//	}
//	auto convertInsideStack = (_historyStack.size() > 1);
//	auto checkSection = convertInsideStack
//		? _historyStack[1].section->section()
//		: _controller->section();
//	auto &anotherMemento = convertInsideStack
//		? _historyStack[1].anotherTab
//		: _anotherTabMemento;
//	if (checkSection.type() != Section::Type::Media) {
//		return;
//	}
//	if (!Info::Media::TypeToTabIndex(checkSection.mediaType())) {
//		return;
//	}
//	anotherMemento = std::move(entry.section);
//	_historyStack.erase(_historyStack.begin());
//}

rpl::producer<Wrap> WrapWidget::wrapValue() const {
	return _wrap.value();
}

void WrapWidget::setWrap(Wrap wrap) {
	// Was done for top level tabs support.
	//
	//if (_wrap.current() != Wrap::Side && wrap == Wrap::Side) {
	//	convertProfileFromStackToTab();
	//}
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
	//if (params.withTabs && _topTabs) {
	//	_topTabs->hide();
	//}
	auto result = myGrab(this);
	if (params.withTopBarShadow) {
		_topShadow->setVisible(true);
	}
	//if (params.withTabs && _topTabs) {
	//	_topTabs->show();
	//}
	return result;
}

void WrapWidget::showAnimatedHook(
		const Window::SectionSlideParams &params) {
	//if (params.withTabs && _topTabs) {
	//	_topTabs->show();
	//	_topTabsBackground->show();
	//}
	if (params.withTopBarShadow) {
		_topShadow->setVisible(true);
	}
	_topBarSurrogate = createTopBarSurrogate(this);
}

void WrapWidget::doSetInnerFocus() {
	if (!_topBar->focusSearchField()) {
		_content->setInnerFocus();
	}
}

void WrapWidget::showFinishedHook() {
	// Restore shadow visibility after showChildren() call.
	_topShadow->toggle(_topShadow->toggled(), anim::type::instant);
	_topBarSurrogate.destroy();
}

bool WrapWidget::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	if (auto infoMemento = dynamic_cast<Memento*>(memento.get())) {
		if (!_controller || infoMemento->stackSize() > 1) {
			return false;
		}
		auto content = infoMemento->content();
		auto skipInternal = hasStackHistory()
			&& (params.way == Window::SectionShow::Way::ClearStack);
		if (_controller->validateMementoPeer(content)) {
			if (!skipInternal && _content->showInternal(content)) {
				highlightTopBar();
				return true;

			// This was done for tabs support.
			//
			//} else if (_topTabs) {
			//	// If we open the profile being in the media tab.
			//	// Just switch back to the profile tab.
			//	auto type = content->section().type();
			//	if (type == Section::Type::Profile
			//		&& _tab != Tab::Profile) {
			//		_anotherTabMemento = std::move(infoMemento->takeStack().back());
			//		_topTabs->setActiveSection(static_cast<int>(Tab::Profile));
			//		return true;
			//	} else if (type == Section::Type::Media
			//		&& _tab != Tab::Media
			//		&& Media::TypeToTabIndex(content->section().mediaType()).has_value()) {
			//		_anotherTabMemento = std::move(infoMemento->takeStack().back());
			//		_topTabs->setActiveSection(static_cast<int>(Tab::Media));
			//		return true;
			//	}
			}
		}

		// If we're in a nested section and we're asked to show
		// a chat profile that is at the bottom of the stack we'll
		// just go back in the stack all the way instead of pushing.
		if (returnToFirstStackFrame(content, params)) {
			return true;
		}

		showNewContent(
			content,
			params);
		return true;
	}
	return false;
}

void WrapWidget::highlightTopBar() {
	if (_topBar) {
		_topBar->highlight();
	}
}

std::unique_ptr<Window::SectionMemento> WrapWidget::createMemento() {
	auto stack = std::vector<std::unique_ptr<ContentMemento>>();
	stack.reserve(_historyStack.size() + 1);
	for (auto &stackItem : base::take(_historyStack)) {
		stack.push_back(std::move(stackItem.section));
	}
	stack.push_back(_content->createMemento());

	// We're not in valid state anymore and supposed to be destroyed.
	_controller = nullptr;

	return std::make_unique<Memento>(std::move(stack));
}

rpl::producer<int> WrapWidget::desiredHeightValue() const {
	return _desiredHeights.events_starting_with(desiredHeightForContent())
		| rpl::flatten_latest();
}

QRect WrapWidget::contentGeometry() const {
	return rect().marginsRemoved({ 0, topWidget()->height(), 0, 0 });
}

bool WrapWidget::returnToFirstStackFrame(
		not_null<ContentMemento*> memento,
		const Window::SectionShow &params) {
	if (!hasStackHistory()) {
		return false;
	}
	auto firstPeerId = _historyStack.front().section->peerId();
	auto firstSection = _historyStack.front().section->section();
	if (firstPeerId == memento->peerId()
		&& firstSection.type() == memento->section().type()
		&& firstSection.type() == Section::Type::Profile) {
		_historyStack.resize(1);
		_controller->showBackFromStack();
		return true;
	}
	return false;
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
		_controller->parentController(),
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
		//if (_anotherTabMemento) {
		//	item.anotherTab = std::move(_anotherTabMemento);
		//}
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
		if (Ui::InFocusChain(this)) {
			setFocus();
		}
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

// This was done for tabs support.
//
//void WrapWidget::setupTabs(Tab tab) {
//	_tab = tab;
//	if (_tab == Tab::None) {
//		_topTabs.destroy();
//		_topTabsBackground.destroy();
//	} else if (!_topTabs) {
//		createTabs();
//	} else {
//		_topTabs->setActiveSection(static_cast<int>(tab));
//	}
//}

void WrapWidget::resizeEvent(QResizeEvent *e) {
	// This was done for tabs support.
	//
	//if (_topTabs) {
	//	_topTabs->resizeToWidth(width());
	//	_topTabsBackground->resize(
	//		width(),
	//		_topTabs->height() - st::lineWidth);
	//}
	if (_topBar) {
		_topBar->resizeToWidth(width());
	}
	updateContentGeometry();
}

void WrapWidget::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		if (hasStackHistory() || wrap() != Wrap::Layer) {
			_controller->showBackFromStack();
			return;
		}
	}
	SectionWidget::keyPressEvent(e);
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

object_ptr<Ui::RpWidget> WrapWidget::createTopBarSurrogate(
		QWidget *parent) {
	if (hasStackHistory() || wrap() == Wrap::Narrow) {
		Assert(_topBar != nullptr);

		auto result = object_ptr<Ui::AbstractButton>(parent);
		result->addClickHandler([weak = make_weak(this)]{
			if (weak) {
				weak->_controller->showBackFromStack();
			}
		});
		result->setGeometry(_topBar->geometry());
		result->show();
		return std::move(result);
	}
	return nullptr;
}

void WrapWidget::updateGeometry(QRect newGeometry, int additionalScroll) {
	auto scrollChanged = (_additionalScroll != additionalScroll);
	auto geometryChanged = (geometry() != newGeometry);
	auto shrinkingContent = (additionalScroll < _additionalScroll);
	_additionalScroll = additionalScroll;

	if (geometryChanged) {
		if (shrinkingContent) {
			setGeometry(newGeometry);
		}
		if (scrollChanged) {
			_content->applyAdditionalScroll(additionalScroll);
		}
		if (!shrinkingContent) {
			setGeometry(newGeometry);
		}
	} else if (scrollChanged) {
		_content->applyAdditionalScroll(additionalScroll);
	}
}

int WrapWidget::scrollTillBottom(int forHeight) const {
	return _content->scrollTillBottom(forHeight - topWidget()->height());
}

rpl::producer<int> WrapWidget::scrollTillBottomChanges() const {
	return _scrollTillBottomChanges.events_starting_with(
		_content->scrollTillBottomChanges()
	) | rpl::flatten_latest();
}

WrapWidget::~WrapWidget() = default;

} // namespace Info
