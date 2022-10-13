/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/info_wrap_widget.h"

#include "info/profile/info_profile_widget.h"
#include "info/profile/info_profile_values.h"
#include "info/media/info_media_widget.h"
#include "info/info_content_widget.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "info/info_top_bar.h"
#include "settings/cloud_password/settings_cloud_password_email_confirm.h"
#include "settings/settings_chat.h"
#include "settings/settings_main.h"
#include "settings/settings_premium.h"
#include "ui/effects/ripple_animation.h" // MaskByDrawer.
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/search_field_controller.h"
#include "core/application.h"
#include "calls/calls_instance.h"
#include "core/shortcuts.h"
#include "window/window_session_controller.h"
#include "window/window_slide_animation.h"
#include "window/window_peer_menu.h"
#include "boxes/peer_list_box.h"
#include "ui/boxes/confirm_box.h"
#include "main/main_session.h"
#include "mtproto/mtproto_config.h"
#include "data/data_download_manager.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "data/data_user.h"
#include "data/data_forum_topic.h"
#include "mainwidget.h"
#include "lang/lang_keys.h"
#include "styles/style_chat.h" // popupMenuExpandedSeparator
#include "styles/style_info.h"
#include "styles/style_profile.h"
#include "styles/style_menu_icons.h"
#include "styles/style_layers.h"

namespace Info {
namespace {

const style::InfoTopBar &TopBarStyle(Wrap wrap) {
	return (wrap == Wrap::Layer)
		? st::infoLayerTopBar
		: st::infoTopBar;
}

[[nodiscard]] bool HasCustomTopBar(not_null<const Controller*> controller) {
	const auto section = controller->section();
	return (section.type() == Section::Type::Settings
		&& (section.settingsType() == ::Settings::PremiumId()));
}

} // namespace

struct WrapWidget::StackItem {
	std::shared_ptr<ContentMemento> section;
//	std::shared_ptr<ContentMemento> anotherTab;
};

WrapWidget::WrapWidget(
	QWidget *parent,
	not_null<Window::SessionController*> window,
	Wrap wrap,
	not_null<Memento*> memento)
: SectionWidget(parent, window, rpl::producer<PeerData*>())
, _wrap(wrap)
, _controller(createController(window, memento->content()))
, _topShadow(this)
, _bottomShadow(this) {
	_topShadow->toggleOn(
		topShadowToggledValue(
		) | rpl::filter([](bool shown) {
			return true;
		}));

	_bottomShadow->toggleOn(
		_desiredBottomShadowVisibilities.events(
		) | rpl::flatten_latest() | rpl::distinct_until_changed());

	_wrap.changes(
	) | rpl::start_with_next([this] {
		setupTop();
		finishShowContent();
	}, lifetime());
	selectedListValue(
	) | rpl::start_with_next([this](SelectedItems &&items) {
		InvokeQueued(this, [this, items = std::move(items)]() mutable {
			if (_topBar) {
				_topBar->setSelectedItems(std::move(items));
			}
		});
	}, lifetime());
	restoreHistoryStack(memento->takeStack());

	if (const auto topic = _controller->topic()) {
		topic->destroyed(
		) | rpl::start_with_next([=] {
			if (_wrap.current() == Wrap::Layer) {
				_controller->parentController()->hideSpecialLayer();
			} else if (_wrap.current() == Wrap::Narrow) {
				_controller->parentController()->showBackFromStack(
					Window::SectionShow(
						anim::type::normal,
						anim::activation::background));
			} else {
				_removeRequests.fire({});
			}
		}, lifetime());
	}
}

void WrapWidget::setupShortcuts() {
	Shortcuts::Requests(
	) | rpl::filter([=] {
		return requireTopBarSearch()
			&& (Core::App().activeWindow()
				== &_controller->parentController()->window());
	}) | rpl::start_with_next([=](not_null<Shortcuts::Request*> request) {
		using Command = Shortcuts::Command;
		request->check(Command::Search) && request->handle([=] {
			_topBar->showSearch();
			return true;
		});
	}, lifetime());
}

void WrapWidget::restoreHistoryStack(
		std::vector<std::shared_ptr<ContentMemento>> stack) {
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
		_controller->parentController()->activeChatValue()
	) | rpl::filter(
		(_1 == Wrap::Side) && _2
	) | rpl::map(
		_2
	) | rpl::start_with_next([this](Dialogs::Key key) {
		injectActiveProfile(key);
	}, lifetime());

}

void WrapWidget::injectActiveProfile(Dialogs::Key key) {
	if (const auto peer = key.peer()) {
		injectActivePeerProfile(peer);
	}
}

void WrapWidget::injectActivePeerProfile(not_null<PeerData*> peer) {
	const auto firstPeer = hasStackHistory()
		? _historyStack.front().section->peer()
		: _controller->peer();
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
	const auto expectedType = peer->sharedMediaInfo()
		? Section::Type::Media
		: Section::Type::Profile;
	const auto expectedMediaType = peer->sharedMediaInfo()
		? Section::MediaType::Photo
		: Section::MediaType::kCount;
	if (firstSectionType != expectedType
		|| firstSectionMediaType != expectedMediaType
		|| firstPeer != peer) {
		auto section = peer->sharedMediaInfo()
			? Section(Section::MediaType::Photo)
			: Section(Section::Type::Profile);
		injectActiveProfileMemento(std::move(
			Memento(peer, section).takeStack().front()));
	}
}

void WrapWidget::injectActiveProfileMemento(
		std::shared_ptr<ContentMemento> memento) {
	auto injected = StackItem();
	injected.section = std::move(memento);
	_historyStack.insert(
		_historyStack.begin(),
		std::move(injected));
	if (_content) {
		setupTop();
		finishShowContent();
	}
}

std::unique_ptr<Controller> WrapWidget::createController(
		not_null<Window::SessionController*> window,
		not_null<ContentMemento*> memento) {
	auto result = std::make_unique<Controller>(
		this,
		window,
		memento);
	return result;
}

Key WrapWidget::key() const {
	return _controller->key();
}

Dialogs::RowDescriptor WrapWidget::activeChat() const {
	if (const auto peer = key().peer()) {
		return Dialogs::RowDescriptor(
			peer->owner().history(peer),
			FullMsgId());
	} else if (key().settingsSelf() || key().isDownloads() || key().poll()) {
		return Dialogs::RowDescriptor();
	}
	Unexpected("Owner in WrapWidget::activeChat().");
}

void WrapWidget::forceContentRepaint() {
	// WA_OpaquePaintEvent on TopBar creates render glitches when
	// animating the LayerWidget's height :( Fixing by repainting.
	if (_topBar) {
		_topBar->update();
	}
	_content->update();
}

void WrapWidget::setupTop() {
	if (HasCustomTopBar(_controller.get())) {
		_topBar.destroy();
		return;
	}
	createTopBar();
}

void WrapWidget::createTopBar() {
	const auto wrapValue = wrap();
	auto selectedItems = _topBar
		? _topBar->takeSelectedItems()
		: SelectedItems(Section::MediaType::kCount);
	_topBar.create(
		this,
		_controller.get(),
		TopBarStyle(wrapValue),
		std::move(selectedItems));
	_topBar->selectionActionRequests(
	) | rpl::start_with_next([=](SelectionAction action) {
		_content->selectionAction(action);
	}, _topBar->lifetime());

	if (hasBackButton()) {
		_topBar->enableBackButton();
		_topBar->backRequest(
		) | rpl::start_with_next([=] {
			checkBeforeClose([=] { _controller->showBackFromStack(); });
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
			checkBeforeClose([=] {
				_controller->parentController()->hideSpecialLayer();
			});
		});
	} else if (requireTopBarSearch()) {
		auto search = _controller->searchFieldController();
		Assert(search != nullptr);
		setupShortcuts();
		_topBar->createSearchView(
			search,
			_controller->searchEnabledByContent(),
			_controller->takeSearchStartsFocused());
	}
	const auto section = _controller->section();
	if (section.type() == Section::Type::Profile
		&& (wrapValue != Wrap::Side || hasStackHistory())) {
		addTopBarMenuButton();
		addProfileCallsButton();
	} else if (section.type() == Section::Type::Settings
		&& (section.settingsType()
				== ::Settings::CloudPasswordEmailConfirmId()
			|| section.settingsType() == ::Settings::Main::Id()
			|| section.settingsType() == ::Settings::Chat::Id())) {
		addTopBarMenuButton();
	} else if (section.type() == Section::Type::Downloads) {
		auto &manager = Core::App().downloadManager();
		rpl::merge(
			rpl::single(false),
			manager.loadingListChanges() | rpl::map_to(false),
			manager.loadedAdded() | rpl::map_to(true),
			manager.loadedRemoved() | rpl::map_to(false)
		) | rpl::start_with_next([=, &manager](bool definitelyHas) {
			const auto has = [&] {
				for ([[maybe_unused]] const auto id : manager.loadingList()) {
					return true;
				}
				for ([[maybe_unused]] const auto id : manager.loadedList()) {
					return true;
				}
				return false;
			};
			if (!definitelyHas && !has()) {
				_topBarMenuToggle = nullptr;
			} else if (!_topBarMenuToggle) {
				addTopBarMenuButton();
			}
		}, _topBar->lifetime());
	}

	_topBar->lower();
	_topBar->resizeToWidth(width());
	_topBar->finishAnimating();
	_topBar->show();
}

void WrapWidget::checkBeforeClose(Fn<void()> close) {
	_controller->parentController()->hideLayer();
	close();
}

void WrapWidget::addTopBarMenuButton() {
	Expects(_topBar != nullptr);

	{
		const auto guard = gsl::finally([&] { _topBarMenu = nullptr; });
		showTopBarMenu(true);
		if (_topBarMenu->empty()) {
			return;
		}
	}

	_topBarMenuToggle.reset(_topBar->addButton(
		base::make_unique_q<Ui::IconButton>(
			_topBar,
			(wrap() == Wrap::Layer
				? st::infoLayerTopBarMenu
				: st::infoTopBarMenu))));
	_topBarMenuToggle->addClickHandler([this] {
		showTopBarMenu(false);
	});
}

bool WrapWidget::closeByOutsideClick() const {
	return true;
}

void WrapWidget::addProfileCallsButton() {
	Expects(_topBar != nullptr);

	const auto peer = key().peer();
	const auto user = peer ? peer->asUser() : nullptr;
	if (!user || user->sharedMediaInfo()) {
		return;
	}

	user->session().changes().peerFlagsValue(
		user,
		Data::PeerUpdate::Flag::HasCalls
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
			Core::App().calls().startOutgoingCall(user, false);
		});
	}, _topBar->lifetime());

	if (user && user->callsStatus() == UserData::CallsStatus::Unknown) {
		user->updateFull();
	}
}

void WrapWidget::showTopBarMenu(bool check) {
	if (_topBarMenu) {
		_topBarMenu->hideMenu(true);
		return;
	}
	_topBarMenu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuExpandedSeparator);

	_topBarMenu->setDestroyedCallback([this] {
		InvokeQueued(this, [this] { _topBarMenu = nullptr; });
		if (auto toggle = _topBarMenuToggle.get()) {
			toggle->setForceRippled(false);
		}
	});

	const auto addAction = Ui::Menu::CreateAddActionCallback(_topBarMenu);
	if (key().isDownloads()) {
		addAction(
			tr::lng_context_delete_all_files(tr::now),
			[=] { deleteAllDownloads(); },
			&st::menuIconDelete);
	} else if (const auto peer = key().peer()) {
		const auto topic = key().topic();
		Window::FillDialogsEntryMenu(
			_controller->parentController(),
			Dialogs::EntryState{
				.key = (topic
					? Dialogs::Key{ topic }
					: Dialogs::Key{ peer->owner().history(peer) }),
				.section = Dialogs::EntryState::Section::Profile,
			},
			addAction);
	} else if (const auto self = key().settingsSelf()) {
		const auto showOther = [=](::Settings::Type type) {
			const auto controller = _controller.get();
			_topBarMenu = nullptr;
			controller->showSettings(type);
		};
		::Settings::FillMenu(
			_controller->parentController(),
			_controller->section().settingsType(),
			showOther,
			addAction);
	} else {
		_topBarMenu = nullptr;
		return;
	}
	_topBarMenu->setForcedOrigin(Ui::PanelAnimation::Origin::TopRight);
	if (check) {
		return;
	}
	_topBarMenuToggle->setForceRippled(true);
	_topBarMenu->popup(_topBarMenuToggle->mapToGlobal(
		st::infoLayerTopBarMenuPosition));
}

void WrapWidget::deleteAllDownloads() {
	auto &manager = Core::App().downloadManager();
	const auto phrase = tr::lng_downloads_delete_sure_all(tr::now);
	const auto added = manager.loadedHasNonCloudFile()
		? QString()
		: tr::lng_downloads_delete_in_cloud(tr::now);
	const auto deleteSure = [=, &manager](Fn<void()> close) {
		Ui::PostponeCall(this, close);
		manager.deleteAll();
	};
	_controller->parentController()->show(Ui::MakeConfirmBox({
		.text = phrase + (added.isEmpty() ? QString() : "\n\n" + added),
		.confirmed = deleteSure,
		.confirmText = tr::lng_box_delete(tr::now),
		.confirmStyle = &st::attentionBoxButton,
	}));
}

bool WrapWidget::requireTopBarSearch() const {
	if (!_topBar || !_controller->searchFieldController()) {
		return false;
	} else if (_controller->wrap() == Wrap::Layer
		|| _controller->section().type() == Section::Type::Profile) {
		return false;
	} else if (key().isDownloads()) {
		return false;
	} else if (hasStackHistory()) {
		return true;
	}
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
		return true;
	}
	return (wrap() == Wrap::Layer);
}

void WrapWidget::removeFromStack(const std::vector<Section> &sections) {
	for (const auto &section : sections) {
		const auto it = ranges::find_if(_historyStack, [&](
				const StackItem &item) {
			const auto &s = item.section->section();
			if (s.type() != section.type()) {
				return false;
			} else if (s.type() == Section::Type::Media) {
				return (s.mediaType() == section.mediaType());
			} else if (s.type() == Section::Type::Settings) {
				return (s.settingsType() == section.settingsType());
			}
			return false;
		});
		if (it != end(_historyStack)) {
			_historyStack.erase(it);
		}
	}
}

not_null<Ui::RpWidget*> WrapWidget::topWidget() const {
	return _topBar;
}

void WrapWidget::showContent(object_ptr<ContentWidget> content) {
	if (auto old = std::exchange(_content, std::move(content))) {
		if (Ui::InFocusChain(old)) {
			// Prevent activating dialogs filter field while animating.
			setFocus();
		}
		old->hide();

		// Content destructor may invoke closeBox() that will try to
		// start layer animation. If we won't detach old content from
		// its parent WrapWidget layer animation will be started with a
		// partially destructed grand-child widget and result in a crash.
		old->setParent(nullptr);
		old.destroy();
	}
	_additionalScroll = 0;
	_content->show();
	finishShowContent();
}

void WrapWidget::finishShowContent() {
	updateContentGeometry();
	_content->setIsStackBottom(!hasStackHistory());
	if (_topBar) {
		_topBar->setTitle(_content->title());
	}
	_desiredHeights.fire(desiredHeightForContent());
	_desiredShadowVisibilities.fire(_content->desiredShadowVisibility());
	_desiredBottomShadowVisibilities.fire(
		_content->desiredBottomShadowVisibility());
	_selectedLists.fire(_content->selectedListValue());
	_scrollTillBottomChanges.fire(_content->scrollTillBottomChanges());
	_topShadow->raise();
	_topShadow->finishAnimating();
	_bottomShadow->raise();
	_bottomShadow->finishAnimating();
	_contentChanges.fire({});

	_content->scrollBottomSkipValue(
	) | rpl::start_with_next([=] {
		updateContentGeometry();
	}, _content->lifetime());
}

rpl::producer<bool> WrapWidget::topShadowToggledValue() const {
	return _desiredShadowVisibilities.events()
		| rpl::flatten_latest(
		) | rpl::map([=](bool v) { return v && (_topBar != nullptr); });
}

rpl::producer<int> WrapWidget::desiredHeightForContent() const {
	using namespace rpl::mappers;
	return rpl::single(0) | rpl::then(rpl::combine(
		_content->desiredHeightValue(),
		(_topBar ? _topBar->heightValue() : rpl::single(0)),
		_1 + _2));
}

rpl::producer<SelectedItems> WrapWidget::selectedListValue() const {
	return _selectedLists.events() | rpl::flatten_latest();
}

object_ptr<ContentWidget> WrapWidget::createContent(
		not_null<ContentMemento*> memento,
		not_null<Controller*> controller) {
	return memento->createWidget(
		this,
		controller,
		contentGeometry());
}

rpl::producer<Wrap> WrapWidget::wrapValue() const {
	return _wrap.value();
}

void WrapWidget::setWrap(Wrap wrap) {
	_wrap = wrap;
}

rpl::producer<> WrapWidget::contentChanged() const {
	return _contentChanges.events();
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
	const auto expanding = _expanding;
	if (expanding) {
		_grabbingForExpanding = true;
	}
	auto result = Ui::GrabWidget(this);
	if (expanding) {
		_grabbingForExpanding = false;
	}
	if (params.withTopBarShadow) {
		_topShadow->setVisible(true);
	}
	return result;
}

void WrapWidget::showAnimatedHook(
		const Window::SectionSlideParams &params) {
	if (params.withTopBarShadow) {
		_topShadow->setVisible(true);
	}
	_topBarSurrogate = createTopBarSurrogate(this);
}

void WrapWidget::doSetInnerFocus() {
	if (!_topBar || !_topBar->focusSearchField()) {
		_content->setInnerFocus();
	}
}

void WrapWidget::showFinishedHook() {
	// Restore shadow visibility after showChildren() call.
	_topShadow->toggle(_topShadow->toggled(), anim::type::instant);
	_bottomShadow->toggle(_bottomShadow->toggled(), anim::type::instant);
	_topBarSurrogate.destroy();
	_content->showFinished();
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
			}
		}

		// If we're in a nested section and we're asked to show
		// a chat profile that is at the bottom of the stack we'll
		// just go back in the stack all the way instead of pushing.
		if (returnToFirstStackFrame(content, params)) {
			return true;
		}

		showNewContent(content, params);
		return true;
	}
	return false;
}

void WrapWidget::highlightTopBar() {
	if (_topBar) {
		_topBar->highlight();
	}
}

std::shared_ptr<Window::SectionMemento> WrapWidget::createMemento() {
	auto stack = std::vector<std::shared_ptr<ContentMemento>>();
	stack.reserve(_historyStack.size() + 1);
	for (auto &stackItem : base::take(_historyStack)) {
		stack.push_back(std::move(stackItem.section));
	}
	stack.push_back(_content->createMemento());

	// We're not in valid state anymore and supposed to be destroyed.
	_controller = nullptr;

	return std::make_shared<Memento>(std::move(stack));
}

rpl::producer<int> WrapWidget::desiredHeightValue() const {
	return _desiredHeights.events_starting_with(desiredHeightForContent())
		| rpl::flatten_latest();
}

QRect WrapWidget::contentGeometry() const {
	const auto top = _topBar ? _topBar->height() : 0;
	return rect().marginsRemoved({ 0, top, 0, 0 });
}

bool WrapWidget::returnToFirstStackFrame(
		not_null<ContentMemento*> memento,
		const Window::SectionShow &params) {
	if (!hasStackHistory()) {
		return false;
	}
	auto firstPeer = _historyStack.front().section->peer();
	auto firstSection = _historyStack.front().section->section();
	if (firstPeer == memento->peer()
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
	if (_controller && newController) {
		newController->takeStepData(_controller.get());
	}
	auto newContent = object_ptr<ContentWidget>(nullptr);
	if (needAnimation) {
		newContent = createContent(memento, newController.get());
		animationParams.withTopBarShadow = hasTopBarShadow()
			&& newContent->hasTopBarShadow();
		animationParams.oldContentCache = grabForShowAnimation(
			animationParams);
		const auto layer = (wrap() == Wrap::Layer);
		animationParams.withFade = layer;
		animationParams.topSkip = layer ? st::boxRadius : 0;

		if (HasCustomTopBar(_controller.get())
			|| HasCustomTopBar(newController.get())) {

			const auto s = QSize(
				newContent->width(),
				animationParams.topSkip);
			auto image = Ui::RippleAnimation::MaskByDrawer(s, false, [&](
					QPainter &p) {
				const auto r = QRect(0, 0, s.width(), s.height() * 2);
				p.drawRoundedRect(r, st::boxRadius, st::boxRadius);
			});
			animationParams.topMask = Ui::PixmapFromImage(std::move(image));
		}
	}
	if (saveToStack) {
		auto item = StackItem();
		item.section = _content->createMemento();
		_historyStack.push_back(std::move(item));
	} else if (params.way == Window::SectionShow::Way::ClearStack) {
		_historyStack.clear();
	}

	{
		if (hasBackButton()) {
			newContent->enableBackButton();
		}
	}
	{
		// Let old controller outlive old content widget.
		const auto oldController = std::exchange(
			_controller,
			std::move(newController));
		if (newContent) {
			setupTop();
			showContent(std::move(newContent));
		} else {
			showNewContent(memento);
		}
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

void WrapWidget::resizeEvent(QResizeEvent *e) {
	if (_topBar) {
		_topBar->resizeToWidth(width());
	}
	updateContentGeometry();
}

void WrapWidget::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape || e->key() == Qt::Key_Back) {
		if (hasStackHistory() || wrap() != Wrap::Layer) {
			checkBeforeClose([=] { _controller->showBackFromStack(); });
			return;
		}
	}
	SectionWidget::keyPressEvent(e);
}

void WrapWidget::updateContentGeometry() {
	if (_content) {
		if (_topBar) {
			_topShadow->resizeToWidth(width());
			_topShadow->moveToLeft(0, _topBar->height());
		}
		_content->setGeometry(contentGeometry());
		_bottomShadow->resizeToWidth(width());
		_bottomShadow->moveToLeft(
			0,
			_content->y()
				+ _content->height()
				- _content->scrollBottomSkip());
	}
}

bool WrapWidget::floatPlayerHandleWheelEvent(QEvent *e) {
	return _content->floatPlayerHandleWheelEvent(e);
}

QRect WrapWidget::floatPlayerAvailableRect() {
	return _content->floatPlayerAvailableRect();
}

object_ptr<Ui::RpWidget> WrapWidget::createTopBarSurrogate(
		QWidget *parent) {
	if (_topBar && hasBackButton()) {
		Assert(_topBar != nullptr);

		auto result = object_ptr<Ui::AbstractButton>(parent);
		result->addClickHandler([weak = Ui::MakeWeak(this)]{
			if (weak) {
				weak->_controller->showBackFromStack();
			}
		});
		result->setGeometry(_topBar->geometry());
		result->show();
		return result;
	}
	return nullptr;
}

void WrapWidget::updateGeometry(
		QRect newGeometry,
		bool expanding,
		int additionalScroll) {
	auto scrollChanged = (_additionalScroll != additionalScroll);
	auto geometryChanged = (geometry() != newGeometry);
	auto shrinkingContent = (additionalScroll < _additionalScroll);
	_additionalScroll = additionalScroll;
	_expanding = expanding;

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
	return _content->scrollTillBottom(forHeight
		- (_topBar ? _topBar->height() : 0));
}

int WrapWidget::scrollBottomSkip() const {
	return _content->scrollBottomSkip();
}

rpl::producer<int> WrapWidget::scrollTillBottomChanges() const {
	return _scrollTillBottomChanges.events_starting_with(
		_content->scrollTillBottomChanges()
	) | rpl::flatten_latest();
}

rpl::producer<bool> WrapWidget::grabbingForExpanding() const {
	return _grabbingForExpanding.value();
}

const Ui::RoundRect *WrapWidget::bottomSkipRounding() const {
	return _content->bottomSkipRounding();
}

bool WrapWidget::hasBackButton() const {
	return (wrap() == Wrap::Narrow || hasStackHistory());
}

WrapWidget::~WrapWidget() = default;

} // namespace Info
