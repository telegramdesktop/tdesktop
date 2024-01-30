/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mainwidget.h"

#include "api/api_updates.h"
#include "api/api_views.h"
#include "data/data_document_media.h"
#include "data/data_document_resolver.h"
#include "data/data_forum_topic.h"
#include "data/data_web_page.h"
#include "data/data_game.h"
#include "data/data_peer_values.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "data/data_folder.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_chat_filters.h"
#include "data/data_scheduled_messages.h"
#include "data/data_file_origin.h"
#include "data/data_histories.h"
#include "data/stickers/data_stickers.h"
#include "ui/chat/chat_theme.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/focus_persister.h"
#include "ui/resize_area.h"
#include "window/window_connecting_widget.h"
#include "window/window_top_bar_wrap.h"
#include "window/notifications_manager.h"
#include "window/window_slide_animation.h"
#include "window/window_history_hider.h"
#include "window/window_controller.h"
#include "window/window_peer_menu.h"
#include "window/themes/window_theme.h"
#include "chat_helpers/tabbed_selector.h" // TabbedSelector::refreshStickers
#include "chat_helpers/message_field.h"
#include "info/info_memento.h"
#include "apiwrap.h"
#include "dialogs/dialogs_widget.h"
#include "history/history_widget.h"
#include "history/history_item_helpers.h" // GetErrorTextForSending.
#include "history/view/media/history_view_media.h"
#include "history/view/history_view_service_message.h"
#include "history/view/history_view_sublist_section.h"
#include "lang/lang_keys.h"
#include "lang/lang_cloud_manager.h"
#include "inline_bots/inline_bot_layout_item.h"
#include "ui/boxes/confirm_box.h"
#include "boxes/peer_list_controllers.h"
#include "storage/storage_account.h"
#include "main/main_domain.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_panel.h"
#include "media/player/media_player_widget.h"
#include "media/player/media_player_dropdown.h"
#include "media/player/media_player_instance.h"
#include "base/qthelp_regex.h"
#include "mtproto/mtproto_dc_options.h"
#include "core/update_checker.h"
#include "core/shortcuts.h"
#include "core/application.h"
#include "core/changelogs.h"
#include "core/mime_type.h"
#include "calls/calls_call.h"
#include "calls/calls_instance.h"
#include "calls/calls_top_bar.h"
#include "calls/group/calls_group_call.h"
#include "export/export_settings.h"
#include "export/export_manager.h"
#include "export/view/export_view_top_bar.h"
#include "export/view/export_view_panel_controller.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "main/main_account.h"
#include "support/support_helper.h"
#include "storage/storage_user_photos.h"
#include "styles/style_dialogs.h"
#include "styles/style_chat.h"
#include "styles/style_window.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QMimeData>

enum StackItemType {
	HistoryStackItem,
	SectionStackItem,
};

class StackItem {
public:
	explicit StackItem(PeerData *peer) : _peer(peer) {
	}

	[[nodiscard]] PeerData *peer() const {
		return _peer;
	}

	void setThirdSectionMemento(
		std::shared_ptr<Window::SectionMemento> memento);
	[[nodiscard]] auto takeThirdSectionMemento()
	-> std::shared_ptr<Window::SectionMemento> {
		return std::move(_thirdSectionMemento);
	}

	void setThirdSectionWeak(QPointer<Window::SectionWidget> section) {
		_thirdSectionWeak = section;
	}
	[[nodiscard]] QPointer<Window::SectionWidget> thirdSectionWeak() const {
		return _thirdSectionWeak;
	}

	[[nodiscard]] rpl::lifetime &lifetime() {
		return _lifetime;
	}

	[[nodiscard]] virtual StackItemType type() const = 0;
	[[nodiscard]] rpl::producer<> removeRequests() const {
		return rpl::merge(
			_thirdSectionRemoveRequests.events(),
			sectionRemoveRequests());
	}
	virtual ~StackItem() = default;

private:
	[[nodiscard]] virtual rpl::producer<> sectionRemoveRequests() const = 0;

	PeerData *_peer = nullptr;
	QPointer<Window::SectionWidget> _thirdSectionWeak;
	std::shared_ptr<Window::SectionMemento> _thirdSectionMemento;
	rpl::event_stream<> _thirdSectionRemoveRequests;

	rpl::lifetime _lifetime;

};

class StackItemHistory final : public StackItem {
public:
	StackItemHistory(
		not_null<History*> history,
		MsgId msgId,
		QVector<FullMsgId> replyReturns)
	: StackItem(history->peer)
	, history(history)
	, msgId(msgId)
	, replyReturns(replyReturns) {
	}

	StackItemType type() const override {
		return HistoryStackItem;
	}

	not_null<History*> history;
	MsgId msgId;
	QVector<FullMsgId> replyReturns;

private:
	rpl::producer<> sectionRemoveRequests() const override {
		return rpl::never<>();
	}

};

class StackItemSection : public StackItem {
public:
	StackItemSection(
		std::shared_ptr<Window::SectionMemento> memento);

	StackItemType type() const override {
		return SectionStackItem;
	}
	std::shared_ptr<Window::SectionMemento> takeMemento() {
		return std::move(_memento);
	}

private:
	rpl::producer<> sectionRemoveRequests() const override;

	std::shared_ptr<Window::SectionMemento> _memento;

};

void StackItem::setThirdSectionMemento(
		std::shared_ptr<Window::SectionMemento> memento) {
	_thirdSectionMemento = std::move(memento);
	if (const auto memento = _thirdSectionMemento.get()) {
		memento->removeRequests(
		) | rpl::start_to_stream(_thirdSectionRemoveRequests, _lifetime);
	}
}

StackItemSection::StackItemSection(
	std::shared_ptr<Window::SectionMemento> memento)
: StackItem(nullptr)
, _memento(std::move(memento)) {
}

rpl::producer<> StackItemSection::sectionRemoveRequests() const {
	if (const auto topic = _memento->topicForRemoveRequests()) {
		return rpl::merge(_memento->removeRequests(), topic->destroyed());
	}
	return _memento->removeRequests();
}

struct MainWidget::SettingBackground {
	explicit SettingBackground(const Data::WallPaper &data);

	Data::WallPaper data;
	std::shared_ptr<Data::DocumentMedia> dataMedia;
	base::binary_guard generating;
};

MainWidget::SettingBackground::SettingBackground(
	const Data::WallPaper &data)
: data(data) {
}

MainWidget::MainWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller)
, _dialogsWidth(st::columnMinimalWidthLeft)
, _thirdColumnWidth(st::columnMinimalWidthThird)
, _sideShadow(isPrimary()
	? base::make_unique_q<Ui::PlainShadow>(this)
	: nullptr)
, _dialogs(isPrimary()
	? base::make_unique_q<Dialogs::Widget>(
		this,
		_controller,
		Dialogs::Widget::Layout::Main)
	: nullptr)
, _history(std::in_place, this, _controller)
, _playerPlaylist(this, _controller)
, _changelogs(Core::Changelogs::Create(&controller->session())) {
	if (isPrimary()) {
		setupConnectingWidget();
	}

	_history->cancelRequests(
	) | rpl::start_with_next([=] {
		handleHistoryBack();
	}, lifetime());

	Core::App().calls().currentCallValue(
	) | rpl::start_with_next([=](Calls::Call *call) {
		setCurrentCall(call);
	}, lifetime());
	Core::App().calls().currentGroupCallValue(
	) | rpl::start_with_next([=](Calls::GroupCall *call) {
		setCurrentGroupCall(call);
	}, lifetime());
	if (_callTopBar) {
		_callTopBar->finishAnimating();
	}

	controller->window().setDefaultFloatPlayerDelegate(
		floatPlayerDelegate());

	Core::App().floatPlayerClosed(
	) | rpl::start_with_next([=](FullMsgId itemId) {
		floatPlayerClosed(itemId);
	}, lifetime());

	Core::App().exportManager().currentView(
	) | rpl::start_with_next([=](Export::View::PanelController *view) {
		setCurrentExportView(view);
	}, lifetime());
	if (_exportTopBar) {
		_exportTopBar->finishAnimating();
	}

	Media::Player::instance()->closePlayerRequests(
	) | rpl::start_with_next([=] {
		closeBothPlayers();
	}, lifetime());

	Media::Player::instance()->updatedNotifier(
	) | rpl::start_with_next([=](const Media::Player::TrackState &state) {
		handleAudioUpdate(state);
	}, lifetime());
	handleAudioUpdate(Media::Player::instance()->getState(AudioMsgId::Type::Song));
	handleAudioUpdate(Media::Player::instance()->getState(AudioMsgId::Type::Voice));
	if (_player) {
		_player->finishAnimating();
	}

	rpl::merge(
		_controller->dialogsListFocusedChanges(),
		_controller->dialogsListDisplayForcedChanges()
	) | rpl::start_with_next([=] {
		updateDialogsWidthAnimated();
	}, lifetime());

	rpl::merge(
		Core::App().settings().dialogsWidthRatioChanges() | rpl::to_empty,
		Core::App().settings().thirdColumnWidthChanges() | rpl::to_empty
	) | rpl::start_with_next([=] {
		updateControlsGeometry();
	}, lifetime());

	session().changes().historyUpdates(
		Data::HistoryUpdate::Flag::MessageSent
	) | rpl::start_with_next([=](const Data::HistoryUpdate &update) {
		const auto history = update.history;
		history->forgetScrollState();
		if (const auto from = history->peer->migrateFrom()) {
			auto &owner = history->owner();
			if (const auto migrated = owner.historyLoaded(from)) {
				migrated->forgetScrollState();
			}
		}
	}, lifetime());

	session().changes().entryUpdates(
		Data::EntryUpdate::Flag::LocalDraftSet
	) | rpl::start_with_next([=](const Data::EntryUpdate &update) {
		auto params = Window::SectionShow();
		params.reapplyLocalDraft = true;
		controller->showThread(
			update.entry->asThread(),
			ShowAtUnreadMsgId,
			params);
		controller->hideLayer();
	}, lifetime());

	// MSVC BUG + REGRESSION rpl::mappers::tuple :(
	using namespace rpl::mappers;
	_controller->activeChatValue(
	) | rpl::map([](Dialogs::Key key) {
		const auto peer = key.peer();
		const auto topic = key.topic();
		auto canWrite = topic
			? Data::CanSendAnyOfValue(
				topic,
				Data::TabbedPanelSendRestrictions())
			: peer
			? Data::CanSendAnyOfValue(

				peer, Data::TabbedPanelSendRestrictions())
			: rpl::single(false);
		return std::move(
			canWrite
		) | rpl::map([=](bool can) {
			return std::make_tuple(key, can);
		});
	}) | rpl::flatten_latest(
	) | rpl::start_with_next([this](Dialogs::Key key, bool canWrite) {
		updateThirdColumnToCurrentChat(key, canWrite);
	}, lifetime());

	QCoreApplication::instance()->installEventFilter(this);

	Media::Player::instance()->tracksFinished(
	) | rpl::start_with_next([=](AudioMsgId::Type type) {
		if (type == AudioMsgId::Type::Voice) {
			const auto songState = Media::Player::instance()->getState(
				AudioMsgId::Type::Song);
			if (!songState.id || IsStoppedOrStopping(songState.state)) {
				Media::Player::instance()->stopAndClose();
			}
		} else if (type == AudioMsgId::Type::Song) {
			const auto songState = Media::Player::instance()->getState(
				AudioMsgId::Type::Song);
			if (!songState.id) {
				Media::Player::instance()->stopAndClose();
			}
		}
	}, lifetime());

	_controller->adaptive().changes(
	) | rpl::start_with_next([=] {
		handleAdaptiveLayoutUpdate();
	}, lifetime());

	if (_dialogs) {
		_dialogs->show();
	}
	if (_dialogs && isOneColumn()) {
		_history->hide();
	} else {
		_history->show();
	}
	orderWidgets();

	if (!Core::UpdaterDisabled()) {
		Core::UpdateChecker checker;
		checker.start();
	}

	cSetOtherOnline(0);

	_history->start();
}

MainWidget::~MainWidget() = default;

Main::Session &MainWidget::session() const {
	return _controller->session();
}

not_null<Window::SessionController*> MainWidget::controller() const {
	return _controller;
}

void MainWidget::setupConnectingWidget() {
	using namespace rpl::mappers;
	_connecting = std::make_unique<Window::ConnectionState>(
		this,
		&session().account(),
		_controller->adaptive().oneColumnValue() | rpl::map(!_1));
	_controller->connectingBottomSkipValue(
	) | rpl::start_with_next([=](int skip) {
		_connecting->setBottomSkip(skip);
	}, lifetime());
}

not_null<Media::Player::FloatDelegate*> MainWidget::floatPlayerDelegate() {
	return static_cast<Media::Player::FloatDelegate*>(this);
}

not_null<Ui::RpWidget*> MainWidget::floatPlayerWidget() {
	return this;
}

void MainWidget::floatPlayerToggleGifsPaused(bool paused) {
	constexpr auto kReason = Window::GifPauseReason::RoundPlaying;
	if (paused) {
		_controller->enableGifPauseReason(kReason);
	} else {
		_controller->disableGifPauseReason(kReason);
	}
}

auto MainWidget::floatPlayerGetSection(Window::Column column)
-> not_null<Media::Player::FloatSectionDelegate*> {
	if (isThreeColumn()) {
		if (_dialogs && column == Window::Column::First) {
			return _dialogs;
		} else if (column == Window::Column::Second
			|| !_dialogs
			|| !_thirdSection) {
			if (_mainSection) {
				return _mainSection;
			}
			return _history;
		}
		return _thirdSection;
	} else if (isNormalColumn()) {
		if (_dialogs && column == Window::Column::First) {
			return _dialogs;
		} else if (_mainSection) {
			return _mainSection;
		}
		return _history;
	} else if (_mainSection) {
		return _mainSection;
	} else if (!isOneColumn() || _history->peer()) {
		return _history;
	}
	Assert(_dialogs != nullptr);
	return _dialogs;
}

void MainWidget::floatPlayerEnumerateSections(Fn<void(
		not_null<Media::Player::FloatSectionDelegate*> widget,
		Window::Column widgetColumn)> callback) {
	if (isThreeColumn()) {
		if (_dialogs) {
			callback(_dialogs, Window::Column::First);
		}
		if (_mainSection) {
			callback(_mainSection, Window::Column::Second);
		} else {
			callback(_history, Window::Column::Second);
		}
		if (_thirdSection) {
			callback(_thirdSection, Window::Column::Third);
		}
	} else if (isNormalColumn()) {
		if (_dialogs) {
			callback(_dialogs, Window::Column::First);
		}
		if (_mainSection) {
			callback(_mainSection, Window::Column::Second);
		} else {
			callback(_history, Window::Column::Second);
		}
	} else {
		if (_mainSection) {
			callback(_mainSection, Window::Column::Second);
		} else if (!isOneColumn() || _history->peer()) {
			callback(_history, Window::Column::Second);
		} else {
			Assert(_dialogs != nullptr);
			callback(_dialogs, Window::Column::First);
		}
	}
}

bool MainWidget::floatPlayerIsVisible(not_null<HistoryItem*> item) {
	return session().data().queryItemVisibility(item);
}

void MainWidget::floatPlayerClosed(FullMsgId itemId) {
	if (_player) {
		const auto voiceData = Media::Player::instance()->current(
			AudioMsgId::Type::Voice);
		if (voiceData.contextId() == itemId) {
			stopAndClosePlayer();
		}
	}
}

void MainWidget::floatPlayerDoubleClickEvent(
		not_null<const HistoryItem*> item) {
	_controller->showMessage(item);
}

bool MainWidget::setForwardDraft(
		not_null<Data::Thread*> thread,
		Data::ForwardDraft &&draft) {
	const auto history = thread->owningHistory();
	const auto items = session().data().idsToItems(draft.ids);
	const auto topicRootId = thread->topicRootId();
	const auto error = GetErrorTextForSending(
		history->peer,
		{
			.topicRootId = topicRootId,
			.forward = &items,
			.ignoreSlowmodeCountdown = true,
		});
	if (!error.isEmpty()) {
		_controller->show(Ui::MakeInformBox(error));
		return false;
	}

	history->setForwardDraft(topicRootId, std::move(draft));
	_controller->showThread(
		thread,
		ShowAtUnreadMsgId,
		SectionShow::Way::Forward);
	return true;
}

bool MainWidget::shareUrl(
		not_null<Data::Thread*> thread,
		const QString &url,
		const QString &text) const {
	if (!Data::CanSendTexts(thread)) {
		_controller->show(Ui::MakeInformBox(tr::lng_share_cant()));
		return false;
	}
	const auto textWithTags = TextWithTags{
		url + '\n' + text,
		TextWithTags::Tags()
	};
	const auto cursor = MessageCursor{
		int(url.size()) + 1,
		int(url.size()) + 1 + int(text.size()),
		Ui::kQFixedMax
	};
	const auto history = thread->owningHistory();
	const auto topicRootId = thread->topicRootId();
	history->setLocalDraft(std::make_unique<Data::Draft>(
		textWithTags,
		FullReplyTo{ .topicRootId = topicRootId },
		cursor,
		Data::WebPageDraft()));
	history->clearLocalEditDraft(topicRootId);
	history->session().changes().entryUpdated(
		thread,
		Data::EntryUpdate::Flag::LocalDraftSet);
	return true;
}

bool MainWidget::sendPaths(
		not_null<Data::Thread*> thread,
		const QStringList &paths) {
	if (!Data::CanSendAnyOf(thread, Data::FilesSendRestrictions())) {
		_controller->show(Ui::MakeInformBox(
			tr::lng_forward_send_files_cant()));
		return false;
	} else if (const auto error = Data::AnyFileRestrictionError(
			thread->peer())) {
		_controller->show(Ui::MakeInformBox(*error));
		return false;
	} else {
		_controller->showThread(
			thread,
			ShowAtTheEndMsgId,
			Window::SectionShow::Way::ClearStack);
	}
	return (_controller->activeChatCurrent().thread() == thread)
		&& (_mainSection
			? _mainSection->confirmSendingFiles(paths)
			: _history->confirmSendingFiles(paths));
}

bool MainWidget::filesOrForwardDrop(
		not_null<Data::Thread*> thread,
		not_null<const QMimeData*> data) {
	if (const auto forum = thread->asForum()) {
		Window::ShowDropMediaBox(
			_controller,
			Core::ShareMimeMediaData(data),
			forum);
		if (_hider) {
			_hider->startHide();
			clearHider(_hider);
		}
		return true;
	}
	if (data->hasFormat(u"application/x-td-forward"_q)) {
		auto draft = Data::ForwardDraft{
			.ids = session().data().takeMimeForwardIds(),
		};
		if (setForwardDraft(thread, std::move(draft))) {
			return true;
		}
		// We've already released the mouse button,
		// so the forwarding is cancelled.
		if (_hider) {
			_hider->startHide();
			clearHider(_hider);
		}
		return false;
	} else if (!Data::CanSendAnyOf(thread, Data::FilesSendRestrictions())) {
		_controller->show(Ui::MakeInformBox(
			tr::lng_forward_send_files_cant()));
		return false;
	} else if (const auto error = Data::AnyFileRestrictionError(
			thread->peer())) {
		_controller->show(Ui::MakeInformBox(*error));
		return false;
	} else {
		_controller->showThread(
			thread,
			ShowAtTheEndMsgId,
			Window::SectionShow::Way::ClearStack);
		if (_controller->activeChatCurrent().thread() != thread) {
			return false;
		}
		(_mainSection
			? _mainSection->confirmSendingFiles(data)
			: _history->confirmSendingFiles(data));
		return true;
	}
}

bool MainWidget::notify_switchInlineBotButtonReceived(const QString &query, UserData *samePeerBot, MsgId samePeerReplyTo) {
	return _history->notify_switchInlineBotButtonReceived(query, samePeerBot, samePeerReplyTo);
}

void MainWidget::clearHider(not_null<Window::HistoryHider*> instance) {
	if (_hider != instance) {
		return;
	}
	_hider.release();
}

void MainWidget::hiderLayer(base::unique_qptr<Window::HistoryHider> hider) {
	if (!_dialogs || _controller->window().locked()) {
		return;
	}

	_hider = std::move(hider);
	_hider->setParent(this);

	_hider->hidden(
	) | rpl::start_with_next([=, instance = _hider.get()] {
		clearHider(instance);
		instance->hide();
		instance->deleteLater();
	}, _hider->lifetime());

	_hider->show();
	updateControlsGeometry();
	_dialogs->setInnerFocus();

	floatPlayerCheckVisibility();
}

void MainWidget::showDragForwardInfo() {
	hiderLayer(base::make_unique_q<Window::HistoryHider>(
		this,
		tr::lng_forward_choose(tr::now)));
}

void MainWidget::hideDragForwardInfo() {
	if (_hider) {
		_hider->startHide();
		_hider.release();
	}
}

void MainWidget::sendBotCommand(Bot::SendCommandRequest request) {
	const auto type = _mainSection
		? _mainSection->sendBotCommand(request)
		: Window::SectionActionResult::Fallback;
	if (type == Window::SectionActionResult::Fallback) {
		_controller->showPeerHistory(
			request.peer,
			SectionShow::Way::Forward,
			ShowAtTheEndMsgId);
		_history->sendBotCommand(request);
	}
}

void MainWidget::hideSingleUseKeyboard(FullMsgId replyToId) {
	_history->hideSingleUseKeyboard(replyToId);
}

void MainWidget::searchMessages(const QString &query, Dialogs::Key inChat) {
	if (controller()->isPrimary()) {
		_dialogs->searchMessages(query, inChat);
		if (isOneColumn()) {
			_controller->clearSectionStack();
		} else {
			_dialogs->setInnerFocus();
		}
	} else {
		if (const auto sublist = inChat.sublist()) {
			controller()->showSection(
				std::make_shared<HistoryView::SublistMemento>(sublist));
		} else if (!Data::SearchTagsFromQuery(query).empty()) {
			inChat = controller()->session().data().history(
				controller()->session().user());
		}
		if ((!_mainSection
			|| !_mainSection->searchInChatEmbedded(inChat, query))
			&& !_history->searchInChatEmbedded(inChat, query)) {
			const auto account = &session().account();
			if (const auto window = Core::App().windowFor(account)) {
				if (const auto controller = window->sessionController()) {
					controller->content()->searchMessages(query, inChat);
					controller->widget()->activate();
				}
			}
		}
	}
}

void MainWidget::handleAudioUpdate(const Media::Player::TrackState &state) {
	using State = Media::Player::State;
	const auto document = state.id.audio();
	const auto item = session().data().message(state.id.contextId());
	if (!Media::Player::IsStoppedOrStopping(state.state)) {
		const auto ttlSeconds = item
			&& item->media()
			&& item->media()->ttlSeconds();
		if (!ttlSeconds) {
			createPlayer();
		}
	} else if (state.state == State::StoppedAtStart) {
		Media::Player::instance()->stopAndClose();
	}

	if (item) {
		session().data().requestItemRepaint(item);
	}
	if (document) {
		if (const auto items = InlineBots::Layout::documentItems()) {
			if (const auto i = items->find(document); i != items->end()) {
				for (const auto &item : i->second) {
					item->update();
				}
			}
		}
	}
}

void MainWidget::closeBothPlayers() {
	if (_player) {
		_player->hide(anim::type::normal);
	}
	_playerPlaylist->hideIgnoringEnterEvents();
}

void MainWidget::stopAndClosePlayer() {
	if (_player) {
		_player->entity()->stopAndClose();
	}
}

void MainWidget::createPlayer() {
	if (!_player) {
		_player.create(
			this,
			object_ptr<Media::Player::Widget>(this, this, _controller),
			_controller->adaptive().oneColumnValue());
		rpl::merge(
			_player->heightValue() | rpl::map_to(true),
			_player->shownValue()
		) | rpl::start_with_next(
			[this] { playerHeightUpdated(); },
			_player->lifetime());
		_player->entity()->setCloseCallback([=] {
			Media::Player::instance()->stopAndClose();
		});
		_player->entity()->setShowItemCallback([=](
				not_null<const HistoryItem*> item) {
			_controller->showMessage(item);
		});

		_player->entity()->togglePlaylistRequests(
		) | rpl::start_with_next([=](bool shown) {
			if (!shown) {
				_playerPlaylist->hideFromOther();
				return;
			} else if (_playerPlaylist->isHidden()) {
				auto position = mapFromGlobal(QCursor::pos()).x();
				auto bestPosition = _playerPlaylist->bestPositionFor(position);
				if (rtl()) bestPosition = position + 2 * (position - bestPosition) - _playerPlaylist->width();
				updateMediaPlaylistPosition(bestPosition);
			}
			_playerPlaylist->showFromOther();
		}, _player->lifetime());

		orderWidgets();
		if (_showAnimation) {
			_player->show(anim::type::instant);
			_player->setVisible(false);
			Shortcuts::ToggleMediaShortcuts(true);
		} else {
			_player->hide(anim::type::instant);
		}
	}
	if (_player && !_player->toggled()) {
		if (!_showAnimation) {
			_player->show(anim::type::normal);
			_playerHeight = _contentScrollAddToY = _player->contentHeight();
			updateControlsGeometry();
			Shortcuts::ToggleMediaShortcuts(true);
		}
	}
}

void MainWidget::playerHeightUpdated() {
	if (!_player) {
		// Player could be already "destroyDelayed", but still handle events.
		return;
	}
	auto playerHeight = _player->contentHeight();
	if (playerHeight != _playerHeight) {
		_contentScrollAddToY += playerHeight - _playerHeight;
		_playerHeight = playerHeight;
		updateControlsGeometry();
	}
	if (!_playerHeight && _player->isHidden()) {
		const auto state = Media::Player::instance()->getState(Media::Player::instance()->getActiveType());
		if (!state.id || Media::Player::IsStoppedOrStopping(state.state)) {
			_player.destroyDelayed();
		}
	}
}

void MainWidget::setCurrentCall(Calls::Call *call) {
	if (!call && _currentGroupCall) {
		return;
	}
	_currentCallLifetime.destroy();
	_currentCall = call;
	if (_currentCall) {
		_callTopBar.destroy();
		_currentCall->stateValue(
		) | rpl::start_with_next([=](Calls::Call::State state) {
			using State = Calls::Call::State;
			if (state != State::Established) {
				destroyCallTopBar();
			} else if (!_callTopBar) {
				createCallTopBar();
			}
		}, _currentCallLifetime);
	} else {
		destroyCallTopBar();
	}
}

void MainWidget::setCurrentGroupCall(Calls::GroupCall *call) {
	if (!call && _currentCall) {
		return;
	}
	_currentCallLifetime.destroy();
	_currentGroupCall = call;
	if (_currentGroupCall) {
		_callTopBar.destroy();
		_currentGroupCall->stateValue(
		) | rpl::start_with_next([=](Calls::GroupCall::State state) {
			using State = Calls::GroupCall::State;
			if (state != State::Creating
				&& state != State::Waiting
				&& state != State::Joining
				&& state != State::Joined
				&& state != State::Connecting) {
				destroyCallTopBar();
			} else if (!_callTopBar) {
				createCallTopBar();
			}
		}, _currentCallLifetime);
	} else {
		destroyCallTopBar();
	}
}

void MainWidget::createCallTopBar() {
	Expects(_currentCall != nullptr || _currentGroupCall != nullptr);

	const auto show = controller()->uiShow();
	_callTopBar.create(
		this,
		(_currentCall
			? object_ptr<Calls::TopBar>(this, _currentCall, show)
			: object_ptr<Calls::TopBar>(this, _currentGroupCall, show)));
	_callTopBar->entity()->initBlobsUnder(this, _callTopBar->geometryValue());
	_callTopBar->heightValue(
	) | rpl::start_with_next([this](int value) {
		callTopBarHeightUpdated(value);
	}, lifetime());
	orderWidgets();
	if (_showAnimation) {
		_callTopBar->show(anim::type::instant);
		_callTopBar->setVisible(false);
	} else {
		_callTopBar->hide(anim::type::instant);
		_callTopBar->show(anim::type::normal);
		_callTopBarHeight = _contentScrollAddToY = _callTopBar->height();
		updateControlsGeometry();
	}
}

void MainWidget::destroyCallTopBar() {
	if (_callTopBar) {
		_callTopBar->hide(anim::type::normal);
	}
}

void MainWidget::callTopBarHeightUpdated(int callTopBarHeight) {
	if (!callTopBarHeight && !_currentCall && !_currentGroupCall) {
		_callTopBar.destroyDelayed();
	}
	if (callTopBarHeight != _callTopBarHeight) {
		_contentScrollAddToY += callTopBarHeight - _callTopBarHeight;
		_callTopBarHeight = callTopBarHeight;
		updateControlsGeometry();
	}
}

void MainWidget::setCurrentExportView(Export::View::PanelController *view) {
	_currentExportView = view;
	if (_currentExportView) {
		_currentExportView->progressState(
		) | rpl::start_with_next([=](Export::View::Content &&data) {
			if (!data.rows.empty()
				&& data.rows[0].id == Export::View::Content::kDoneId) {
				LOG(("Export Info: Destroy top bar by Done."));
				destroyExportTopBar();
			} else if (!_exportTopBar) {
				LOG(("Export Info: Create top bar by State."));
				createExportTopBar(std::move(data));
			} else {
				_exportTopBar->entity()->updateData(std::move(data));
			}
		}, _exportViewLifetime);
	} else {
		_exportViewLifetime.destroy();

		LOG(("Export Info: Destroy top bar by controller removal."));
		destroyExportTopBar();
	}
}

void MainWidget::createExportTopBar(Export::View::Content &&data) {
	_exportTopBar.create(
		this,
		object_ptr<Export::View::TopBar>(this, std::move(data)),
		_controller->adaptive().oneColumnValue());
	_exportTopBar->entity()->clicks(
	) | rpl::start_with_next([=] {
		if (_currentExportView) {
			_currentExportView->activatePanel();
		}
	}, _exportTopBar->lifetime());
	orderWidgets();
	if (_showAnimation) {
		_exportTopBar->show(anim::type::instant);
		_exportTopBar->setVisible(false);
	} else {
		_exportTopBar->hide(anim::type::instant);
		_exportTopBar->show(anim::type::normal);
		_exportTopBarHeight = _contentScrollAddToY = _exportTopBar->contentHeight();
		updateControlsGeometry();
	}
	rpl::merge(
		_exportTopBar->heightValue() | rpl::map_to(true),
		_exportTopBar->shownValue()
	) | rpl::start_with_next([=] {
		exportTopBarHeightUpdated();
	}, _exportTopBar->lifetime());
}

void MainWidget::destroyExportTopBar() {
	if (_exportTopBar) {
		_exportTopBar->hide(anim::type::normal);
	}
}

void MainWidget::exportTopBarHeightUpdated() {
	if (!_exportTopBar) {
		// Player could be already "destroyDelayed", but still handle events.
		return;
	}
	const auto exportTopBarHeight = _exportTopBar->contentHeight();
	if (exportTopBarHeight != _exportTopBarHeight) {
		_contentScrollAddToY += exportTopBarHeight - _exportTopBarHeight;
		_exportTopBarHeight = exportTopBarHeight;
		updateControlsGeometry();
	}
	if (!_exportTopBarHeight && _exportTopBar->isHidden()) {
		_exportTopBar.destroyDelayed();
	}
}

SendMenu::Type MainWidget::sendMenuType() const {
	return _history->sendMenuType();
}

bool MainWidget::sendExistingDocument(not_null<DocumentData*> document) {
	return sendExistingDocument(document, {});
}

bool MainWidget::sendExistingDocument(
		not_null<DocumentData*> document,
		Api::SendOptions options) {
	return _history->sendExistingDocument(document, options);
}

void MainWidget::dialogsCancelled() {
	if (_hider) {
		_hider->startHide();
		clearHider(_hider);
	}
	_history->activate();
}

void MainWidget::setChatBackground(
		const Data::WallPaper &background,
		QImage &&image) {
	using namespace Window::Theme;

	if (isReadyChatBackground(background, image)) {
		setReadyChatBackground(background, std::move(image));
		return;
	}

	_background = std::make_unique<SettingBackground>(background);
	if (const auto document = _background->data.document()) {
		_background->dataMedia = document->createMediaView();
		_background->dataMedia->thumbnailWanted(
			_background->data.fileOrigin());
	}
	_background->data.loadDocument();
	checkChatBackground();

	const auto tile = Data::IsLegacy1DefaultWallPaper(background);
	Window::Theme::Background()->downloadingStarted(tile);
}

bool MainWidget::isReadyChatBackground(
		const Data::WallPaper &background,
		const QImage &image) const {
	return !image.isNull() || !background.document();
}

void MainWidget::setReadyChatBackground(
		const Data::WallPaper &background,
		QImage &&image) {
	using namespace Window::Theme;

	if (image.isNull()
		&& !background.document()
		&& background.localThumbnail()) {
		image = background.localThumbnail()->original();
	}

	const auto resetToDefault = image.isNull()
		&& !background.document()
		&& background.backgroundColors().empty()
		&& !Data::IsLegacy1DefaultWallPaper(background);
	const auto ready = resetToDefault
		? Data::DefaultWallPaper()
		: background;

	Background()->set(ready, std::move(image));
	const auto tile = Data::IsLegacy1DefaultWallPaper(ready);
	Background()->setTile(tile);
	Ui::ForceFullRepaint(this);
}

bool MainWidget::chatBackgroundLoading() {
	return (_background != nullptr);
}

float64 MainWidget::chatBackgroundProgress() const {
	if (_background) {
		if (_background->generating) {
			return 1.;
		} else if (const auto document = _background->data.document()) {
			return _background->dataMedia->progress();
		}
	}
	return 1.;
}

void MainWidget::checkChatBackground() {
	if (!_background || _background->generating) {
		return;
	}
	const auto &media = _background->dataMedia;
	Assert(media != nullptr);
	if (!media->loaded()) {
		return;
	}

	const auto document = _background->data.document();
	Assert(document != nullptr);

	const auto generateCallback = [=](QImage &&image) {
		const auto background = base::take(_background);
		const auto ready = image.isNull()
			? Data::DefaultWallPaper()
			: background->data;
		setReadyChatBackground(ready, std::move(image));
	};
	_background->generating = Data::ReadBackgroundImageAsync(
		media.get(),
		Ui::PreprocessBackgroundImage,
		generateCallback);
}

Image *MainWidget::newBackgroundThumb() {
	return !_background
		? nullptr
		: _background->data.localThumbnail()
		? _background->data.localThumbnail()
		: _background->dataMedia
		? _background->dataMedia->thumbnail()
		: nullptr;
}

void MainWidget::setInnerFocus() {
	if (_hider || !_history->peer()) {
		if (!_hider && _mainSection) {
			_mainSection->setInnerFocus();
		} else if (!_hider && _thirdSection) {
			_thirdSection->setInnerFocus();
		} else {
			Assert(_dialogs != nullptr);
			_dialogs->setInnerFocus();
		}
	} else if (_mainSection) {
		_mainSection->setInnerFocus();
	} else if (_history->peer() || !_thirdSection) {
		_history->setInnerFocus();
	} else {
		_thirdSection->setInnerFocus();
	}
}

void MainWidget::clearBotStartToken(PeerData *peer) {
	if (peer && peer->isUser() && peer->asUser()->isBot()) {
		peer->asUser()->botInfo->startToken = QString();
	}
}

void MainWidget::ctrlEnterSubmitUpdated() {
	_history->updateFieldSubmitSettings();
}

void MainWidget::showChooseReportMessages(
		not_null<PeerData*> peer,
		Ui::ReportReason reason,
		Fn<void(MessageIdsList)> done) {
	_history->setChooseReportMessagesDetails(reason, std::move(done));
	_controller->showPeerHistory(
		peer,
		SectionShow::Way::Forward,
		ShowForChooseMessagesMsgId);
	controller()->showToast(tr::lng_report_please_select_messages(tr::now));
}

void MainWidget::clearChooseReportMessages() {
	_history->setChooseReportMessagesDetails({}, nullptr);
}

void MainWidget::toggleChooseChatTheme(
		not_null<PeerData*> peer,
		std::optional<bool> show) {
	_history->toggleChooseChatTheme(peer, show);
}

bool MainWidget::showHistoryInDifferentWindow(
		PeerId peerId,
		const SectionShow &params,
		MsgId showAtMsgId) {
	const auto peer = session().data().peer(peerId);
	const auto account = &session().account();
	auto primary = Core::App().separateWindowForAccount(account);
	if (const auto separate = Core::App().separateWindowForPeer(peer)) {
		if (separate == &_controller->window()) {
			return false;
		}
		separate->sessionController()->showPeerHistory(
			peerId,
			params,
			showAtMsgId);
		separate->activate();
		return true;
	} else if (isPrimary()) {
		if (primary && primary != &_controller->window()) {
			primary->sessionController()->showPeerHistory(
				peerId,
				params,
				showAtMsgId);
			primary->activate();
			return true;
		}
		return false;
	} else if (!peerId) {
		return true;
	} else if (singlePeer()->id == peerId) {
		return false;
	} else if (!primary) {
		Core::App().domain().activate(account);
		primary = Core::App().separateWindowForAccount(account);
	}
	if (primary && &primary->account() == account) {
		primary->sessionController()->showPeerHistory(
			peerId,
			params,
			showAtMsgId);
		primary->activate();
	}
	return true;
}

void MainWidget::showHistory(
		PeerId peerId,
		const SectionShow &params,
		MsgId showAtMsgId) {
	if (peerId && _controller->window().locked()) {
		return;
	} else if (auto peer = session().data().peerLoaded(peerId)) {
		if (peer->migrateTo()) {
			peer = peer->migrateTo();
			peerId = peer->id;
			if (showAtMsgId > 0) showAtMsgId = -showAtMsgId;
		}
		const auto unavailable = peer->computeUnavailableReason();
		if (!unavailable.isEmpty()) {
			Assert(isPrimary());
			if (params.activation != anim::activation::background) {
				_controller->show(Ui::MakeInformBox(unavailable));
				_controller->window().activate();
			}
			return;
		}
	}
	if ((IsServerMsgId(showAtMsgId) || Data::IsScheduledMsgId(showAtMsgId))
		&& _mainSection
		&& _mainSection->showMessage(peerId, params, showAtMsgId)) {
		session().data().hideShownSpoilers();
		return;
	} else if (showHistoryInDifferentWindow(peerId, params, showAtMsgId)) {
		return;
	}

	if (peerId && params.activation != anim::activation::background) {
		_controller->window().activate();
	}

	const auto alreadyThatPeer = _history->peer()
		&& (_history->peer()->id == peerId);
	if (!alreadyThatPeer
		&& preventsCloseSection(
			[=] { showHistory(peerId, params, showAtMsgId); },
			params)) {
		return;
	}

	using OriginMessage = SectionShow::OriginMessage;
	if (const auto origin = std::get_if<OriginMessage>(&params.origin)) {
		if (const auto returnTo = session().data().message(origin->id)) {
			if (returnTo->history()->peer->id == peerId) {
				_history->pushReplyReturn(returnTo);
			}
		}
	}

	_controller->setDialogsListFocused(false);
	_a_dialogsWidth.stop();

	using Way = SectionShow::Way;
	auto way = params.way;
	bool back = (way == Way::Backward || !peerId);
	bool foundInStack = !peerId;
	if (foundInStack || (way == Way::ClearStack)) {
		for (const auto &item : _stack) {
			clearBotStartToken(item->peer());
		}
		_stack.clear();
	} else {
		for (auto i = 0, s = int(_stack.size()); i < s; ++i) {
			if (_stack.at(i)->type() == HistoryStackItem && _stack.at(i)->peer()->id == peerId) {
				foundInStack = true;
				while (int(_stack.size()) > i + 1) {
					clearBotStartToken(_stack.back()->peer());
					_stack.pop_back();
				}
				_stack.pop_back();
				if (!back) {
					back = true;
				}
				break;
			}
		}
		if (const auto activeChat = _controller->activeChatCurrent()) {
			if (const auto peer = activeChat.peer()) {
				if (way == Way::Forward && peer->id == peerId) {
					way = _mainSection ? Way::Backward : Way::ClearStack;
				}
			}
		}
	}

	const auto wasActivePeer = _controller->activeChatCurrent().peer();
	if (params.activation != anim::activation::background) {
		_controller->window().hideSettingsAndLayer();
	}

	auto animatedShow = [&] {
		if (_showAnimation
			|| Core::App().passcodeLocked()
			|| (params.animated == anim::type::instant)) {
			return false;
		}
		if (!peerId) {
			if (isOneColumn()) {
				return _dialogs && _dialogs->isHidden();
			} else {
				return false;
			}
		}
		if (_history->isHidden()) {
			if (!isOneColumn() && way == Way::ClearStack) {
				return false;
			}
			return (_mainSection != nullptr)
				|| (isOneColumn() && _dialogs && !_dialogs->isHidden());
		}
		if (back || way == Way::Forward) {
			return true;
		}
		return false;
	};

	auto animationParams = animatedShow()
		? prepareHistoryAnimation(peerId)
		: Window::SectionSlideParams();

	if (!back && (way != Way::ClearStack)) {
		// This may modify the current section, for example remove its contents.
		saveSectionInStack(params);
	}

	if (_history->peer()
		&& _history->peer()->id != peerId
		&& way != Way::Forward) {
		clearBotStartToken(_history->peer());
	}
	_history->showHistory(
		peerId,
		showAtMsgId,
		params.highlightPart,
		params.highlightPartOffsetHint);
	if (alreadyThatPeer && params.reapplyLocalDraft) {
		_history->applyDraft(HistoryWidget::FieldHistoryAction::NewEntry);
	}

	auto noPeer = !_history->peer();
	auto onlyDialogs = noPeer && isOneColumn();
	_mainSection.destroy();

	updateControlsGeometry();

	if (noPeer) {
		_controller->setActiveChatEntry(Dialogs::Key());
		_controller->setChatStyleTheme(_controller->defaultChatTheme());
	}

	if (onlyDialogs) {
		Assert(_dialogs != nullptr);
		_history->hide();
		if (!_showAnimation) {
			if (animationParams) {
				auto direction = back ? Window::SlideDirection::FromLeft : Window::SlideDirection::FromRight;
				_dialogs->showAnimated(direction, animationParams);
			} else {
				_dialogs->showFast();
			}
		}
	} else {
		const auto nowActivePeer = _controller->activeChatCurrent().peer();
		if (nowActivePeer && nowActivePeer != wasActivePeer) {
			session().api().views().removeIncremented(nowActivePeer);
		}
		if (isOneColumn() && _dialogs && !_dialogs->isHidden()) {
			_dialogs->hide();
		}
		if (!_showAnimation) {
			if (!animationParams.oldContentCache.isNull()) {
				_history->showAnimated(
					back
						? Window::SlideDirection::FromLeft
						: Window::SlideDirection::FromRight,
					animationParams);
			} else {
				_history->show();
				crl::on_main(this, [=] {
					_controller->widget()->setInnerFocus();
				});
			}
		}
	}

	if (_dialogs && !_dialogs->isHidden()) {
		if (!back) {
			if (const auto history = _history->history()) {
				_dialogs->scrollToEntry(Dialogs::RowDescriptor(
					history,
					FullMsgId(history->peer->id, showAtMsgId)));
			}
		}
		_dialogs->update();
	}

	floatPlayerCheckVisibility();
}

void MainWidget::showMessage(
		not_null<const HistoryItem*> item,
		const SectionShow &params) {
	const auto peerId = item->history()->peer->id;
	const auto itemId = item->id;
	if (!v::is_null(params.origin)) {
		if (_mainSection) {
			if (_mainSection->showMessage(peerId, params, itemId)) {
				return;
			}
		} else if (_history->peer() == item->history()->peer) {
			showHistory(peerId, params, itemId);
			return;
		}
	}
	if (const auto topic = item->topic()) {
		_controller->showTopic(topic, item->id, params);
	} else {
		_controller->showPeerHistory(
			item->history(),
			params,
			item->id);
	}
}

void MainWidget::showForum(
		not_null<Data::Forum*> forum,
		const SectionShow &params) {
	Expects(isPrimary() || (singlePeer() && singlePeer()->forum() == forum));

	_dialogs->showForum(forum, params);

	if (params.activation != anim::activation::background) {
		_controller->hideLayer();
	}
}

PeerData *MainWidget::peer() const {
	return _history->peer();
}

Ui::ChatTheme *MainWidget::customChatTheme() const {
	return _history->customChatTheme();
}

bool MainWidget::saveSectionInStack(
		const SectionShow &params,
		Window::SectionWidget *newMainSection) {
	if (_mainSection) {
		if (auto memento = _mainSection->createMemento()) {
			if (params.dropSameFromStack
				&& newMainSection
				&& newMainSection->sameTypeAs(memento.get())) {
				// When choosing saved sublist we want to save the original
				// "Saved Messages" in the stack, but don't save every
				// sublist in a new stack entry when clicking them through.
				return false;
			}
			_stack.push_back(std::make_unique<StackItemSection>(
				std::move(memento)));
		} else {
			return false;
		}
	} else if (const auto history = _history->history()) {
		_stack.push_back(std::make_unique<StackItemHistory>(
			history,
			_history->msgId(),
			_history->replyReturns()));
	} else {
		// We pretend that we "saved" the chats list state in stack,
		// so that we do animate a transition from chats list to a section.
		return true;
	}
	const auto raw = _stack.back().get();
	raw->setThirdSectionWeak(_thirdSection.data());
	raw->removeRequests(
	) | rpl::start_with_next([=] {
		for (auto i = begin(_stack); i != end(_stack); ++i) {
			if (i->get() == raw) {
				_stack.erase(i);
				return;
			}
		}
	}, raw->lifetime());
	return true;
}

void MainWidget::showSection(
		std::shared_ptr<Window::SectionMemento> memento,
		const SectionShow &params) {
	if (_mainSection && _mainSection->showInternal(
			memento.get(),
			params)) {
		if (params.activation != anim::activation::background) {
			_controller->window().hideSettingsAndLayer();
		}
		if (const auto entry = _mainSection->activeChat(); entry.key) {
			_controller->setActiveChatEntry(entry);
		}
		return;
	//
	// Now third section handles only its own showSection() requests.
	// General showSection() should show layer or main_section instead.
	//
	//} else if (_thirdSection && _thirdSection->showInternal(
	//		&memento,
	//		params)) {
	//	return;
	}

	if (preventsCloseSection(
		[=] { showSection(memento, params); },
		params)) {
		return;
	}

	// If the window was not resized, but we've enabled
	// tabbedSelectorSectionEnabled or thirdSectionInfoEnabled
	// we need to update adaptive layout to Adaptive::ThirdColumn().
	updateColumnLayout();

	showNewSection(std::move(memento), params);
}

void MainWidget::updateColumnLayout() {
	updateWindowAdaptiveLayout();
}

Window::SectionSlideParams MainWidget::prepareThirdSectionAnimation(Window::SectionWidget *section) {
	Expects(_thirdSection != nullptr);

	Window::SectionSlideParams result;
	result.withTopBarShadow = section->hasTopBarShadow();
	if (!_thirdSection->hasTopBarShadow()) {
		result.withTopBarShadow = false;
	}
	floatPlayerHideAll();
	result.oldContentCache = _thirdSection->grabForShowAnimation(result);
	floatPlayerShowVisible();
	return result;
}

Window::SectionSlideParams MainWidget::prepareShowAnimation(
		bool willHaveTopBarShadow) {
	Window::SectionSlideParams result;
	result.withTopBarShadow = willHaveTopBarShadow;
	if (_mainSection) {
		if (!_mainSection->hasTopBarShadow()) {
			result.withTopBarShadow = false;
		}
	} else if (!_history->peer()) {
		result.withTopBarShadow = false;
	}

	floatPlayerHideAll();
	if (_player) {
		_player->entity()->hideShadowAndDropdowns();
	}
	const auto playerPlaylistVisible = !_playerPlaylist->isHidden();
	if (playerPlaylistVisible) {
		_playerPlaylist->hide();
	}
	const auto hiderVisible = (_hider && !_hider->isHidden());
	if (hiderVisible) {
		_hider->hide();
	}

	auto sectionTop = getMainSectionTop();
	if (_mainSection) {
		result.oldContentCache = _mainSection->grabForShowAnimation(result);
	} else if (!isOneColumn() || !_history->isHidden()) {
		result.oldContentCache = _history->grabForShowAnimation(result);
	} else {
		result.oldContentCache = Ui::GrabWidget(this, QRect(
			0,
			sectionTop,
			_dialogsWidth,
			height() - sectionTop));
	}

	if (_hider && hiderVisible) {
		_hider->show();
	}
	if (playerPlaylistVisible) {
		_playerPlaylist->show();
	}
	if (_player) {
		_player->entity()->showShadowAndDropdowns();
	}
	floatPlayerShowVisible();

	return result;
}

Window::SectionSlideParams MainWidget::prepareMainSectionAnimation(Window::SectionWidget *section) {
	return prepareShowAnimation(section->hasTopBarShadow());
}

Window::SectionSlideParams MainWidget::prepareHistoryAnimation(PeerId historyPeerId) {
	return prepareShowAnimation(historyPeerId != 0);
}

Window::SectionSlideParams MainWidget::prepareDialogsAnimation() {
	return prepareShowAnimation(false);
}

void MainWidget::showNewSection(
		std::shared_ptr<Window::SectionMemento> memento,
		const SectionShow &params) {
	using Column = Window::Column;

	if (_controller->window().locked()) {
		return;
	}
	auto saveInStack = (params.way == SectionShow::Way::Forward);
	const auto thirdSectionTop = getThirdSectionTop();
	const auto newThirdGeometry = QRect(
		width() - st::columnMinimalWidthThird,
		thirdSectionTop,
		st::columnMinimalWidthThird,
		height() - thirdSectionTop);
	auto newThirdSection = (isThreeColumn() && params.thirdColumn)
		? memento->createWidget(
			this,
			_controller,
			Column::Third,
			newThirdGeometry)
		: nullptr;
	const auto layerRect = parentWidget()->rect();
	if (newThirdSection) {
		saveInStack = false;
	} else if (auto layer = memento->createLayer(_controller, layerRect)) {
		if (params.activation != anim::activation::background) {
			_controller->hideLayer(anim::type::instant);
		}
		_controller->showSpecialLayer(std::move(layer));
		return;
	}

	if (params.activation != anim::activation::background) {
		_controller->window().hideSettingsAndLayer();
	}

	_controller->setDialogsListFocused(false);
	_a_dialogsWidth.stop();

	auto mainSectionTop = getMainSectionTop();
	auto newMainGeometry = QRect(
		_history->x(),
		mainSectionTop,
		_history->width(),
		height() - mainSectionTop);
	auto newMainSection = newThirdSection
		? nullptr
		: memento->createWidget(
			this,
			_controller,
			isOneColumn() ? Column::First : Column::Second,
			newMainGeometry);
	Assert(newMainSection || newThirdSection);

	auto animatedShow = [&] {
		if (_showAnimation
			|| Core::App().passcodeLocked()
			|| (params.animated == anim::type::instant)
			|| memento->instant()) {
			return false;
		}
		if (!isOneColumn() && params.way == SectionShow::Way::ClearStack) {
			return false;
		} else if (isOneColumn()
			|| (newThirdSection && _thirdSection)
			|| (newMainSection && isMainSectionShown())) {
			return true;
		}
		return false;
	}();
	auto animationParams = animatedShow
		? (newThirdSection
			? prepareThirdSectionAnimation(newThirdSection)
			: prepareMainSectionAnimation(newMainSection))
		: Window::SectionSlideParams();

	setFocus(); // otherwise dialogs widget could be focused.

	if (saveInStack) {
		// This may modify the current section, for example remove its contents.
		if (!saveSectionInStack(params, newMainSection)) {
			saveInStack = false;
			animatedShow = false;
			animationParams = Window::SectionSlideParams();
		}
	}
	auto &settingSection = newThirdSection
		? _thirdSection
		: _mainSection;
	if (newThirdSection) {
		_thirdSection = std::move(newThirdSection);
		_thirdSection->removeRequests(
		) | rpl::start_with_next([=] {
			_thirdSection.destroy();
			_thirdShadow.destroy();
			updateControlsGeometry();
		}, _thirdSection->lifetime());
		if (!_thirdShadow) {
			_thirdShadow.create(this);
			_thirdShadow->show();
			orderWidgets();
		}
		updateControlsGeometry();
	} else {
		_mainSection = std::move(newMainSection);
		_history->finishAnimating();
		_history->showHistory(PeerId(), MsgId());

		if (const auto entry = _mainSection->activeChat(); entry.key) {
			_controller->setActiveChatEntry(entry);
		}

		// Depends on SessionController::activeChatEntry
		// for tabbed selector showing in the third column.
		updateControlsGeometry();

		_history->hide();
		if (isOneColumn() && _dialogs) {
			_dialogs->hide();
		}
	}

	if (animationParams) {
		auto back = (params.way == SectionShow::Way::Backward);
		auto direction = (back || settingSection->forceAnimateBack())
			? Window::SlideDirection::FromLeft
			: Window::SlideDirection::FromRight;
		if (isOneColumn()) {
			_controller->removeLayerBlackout();
		}
		settingSection->showAnimated(direction, animationParams);
	} else {
		settingSection->showFast();
	}

	floatPlayerCheckVisibility();
	orderWidgets();
}

void MainWidget::checkMainSectionToLayer() {
	if (!_mainSection) {
		return;
	}
	Ui::FocusPersister persister(this);
	if (auto layer = _mainSection->moveContentToLayer(rect())) {
		dropMainSection(_mainSection);
		_controller->showSpecialLayer(
			std::move(layer),
			anim::type::instant);
	}
}

void MainWidget::dropMainSection(Window::SectionWidget *widget) {
	if (_mainSection != widget) {
		return;
	}
	_mainSection.destroy();
	_controller->showBackFromStack(
		SectionShow(
			anim::type::instant,
			anim::activation::background));
}

PeerData *MainWidget::singlePeer() const {
	return _controller->singlePeer();
}

bool MainWidget::isPrimary() const {
	return _controller->isPrimary();
}

bool MainWidget::isMainSectionShown() const {
	return _mainSection || _history->peer();
}

bool MainWidget::isThirdSectionShown() const {
	return _thirdSection != nullptr;
}

Dialogs::RowDescriptor MainWidget::resolveChatNext(
		Dialogs::RowDescriptor from) const {
	return _dialogs ? _dialogs->resolveChatNext(from) : Dialogs::RowDescriptor();
}

Dialogs::RowDescriptor MainWidget::resolveChatPrevious(
		Dialogs::RowDescriptor from) const {
	return _dialogs ? _dialogs->resolveChatPrevious(from) : Dialogs::RowDescriptor();
}

bool MainWidget::stackIsEmpty() const {
	return _stack.empty();
}

bool MainWidget::preventsCloseSection(Fn<void()> callback) const {
	if (Core::App().passcodeLocked()) {
		return false;
	}
	auto copy = callback;
	return (_mainSection && _mainSection->preventsClose(std::move(copy)))
		|| (_history && _history->preventsClose(std::move(callback)));
}

bool MainWidget::preventsCloseSection(
		Fn<void()> callback,
		const SectionShow &params) const {
	return !params.thirdColumn
		&& (params.activation != anim::activation::background)
		&& preventsCloseSection(std::move(callback));
}

void MainWidget::showBackFromStack(
		const SectionShow &params) {
	if (preventsCloseSection([=] { showBackFromStack(params); }, params)) {
		return;
	}

	if (_stack.empty()) {
		if (isPrimary()) {
			_controller->clearSectionStack(params);
		}
		crl::on_main(this, [=] {
			_controller->widget()->setInnerFocus();
		});
		return;
	}
	auto item = std::move(_stack.back());
	_stack.pop_back();
	if (auto currentHistoryPeer = _history->peer()) {
		clearBotStartToken(currentHistoryPeer);
	}
	_thirdSectionFromStack = item->takeThirdSectionMemento();
	if (item->type() == HistoryStackItem) {
		auto historyItem = static_cast<StackItemHistory*>(item.get());
		_controller->showPeerHistory(
			historyItem->peer()->id,
			params.withWay(SectionShow::Way::Backward),
			ShowAtUnreadMsgId);
		_history->setReplyReturns(
			historyItem->peer()->id,
			std::move(historyItem->replyReturns));
	} else if (item->type() == SectionStackItem) {
		auto sectionItem = static_cast<StackItemSection*>(item.get());
		showNewSection(
			sectionItem->takeMemento(),
			params.withWay(SectionShow::Way::Backward));
	}
	if (_thirdSectionFromStack && _thirdSection) {
		_controller->showSection(
			base::take(_thirdSectionFromStack),
			SectionShow(
				SectionShow::Way::ClearStack,
				anim::type::instant,
				anim::activation::background));

	}
}

void MainWidget::orderWidgets() {
	if (_dialogs) {
		_dialogs->raiseWithTooltip();
	}
	if (_player) {
		_player->raise();
	}
	if (_exportTopBar) {
		_exportTopBar->raise();
	}
	if (_callTopBar) {
		_callTopBar->raise();
	}
	if (_sideShadow) {
		_sideShadow->raise();
	}
	if (_thirdShadow) {
		_thirdShadow->raise();
	}
	if (_firstColumnResizeArea) {
		_firstColumnResizeArea->raise();
	}
	if (_thirdColumnResizeArea) {
		_thirdColumnResizeArea->raise();
	}
	if (_connecting) {
		_connecting->raise();
	}
	floatPlayerRaiseAll();
	_playerPlaylist->raise();
	if (_player) {
		_player->entity()->raiseDropdowns();
	}
	if (_hider) _hider->raise();
}

QPixmap MainWidget::grabForShowAnimation(const Window::SectionSlideParams &params) {
	QPixmap result;
	floatPlayerHideAll();
	if (_player) {
		_player->entity()->hideShadowAndDropdowns();
	}
	const auto playerPlaylistVisible = !_playerPlaylist->isHidden();
	if (playerPlaylistVisible) {
		_playerPlaylist->hide();
	}
	const auto hiderVisible = (_hider && !_hider->isHidden());
	if (hiderVisible) {
		_hider->hide();
	}

	auto sectionTop = getMainSectionTop();
	if (isOneColumn()) {
		result = Ui::GrabWidget(this, QRect(
			0,
			sectionTop,
			width(),
			height() - sectionTop));
	} else {
		if (_sideShadow) {
			_sideShadow->hide();
		}
		if (_thirdShadow) {
			_thirdShadow->hide();
		}
		result = Ui::GrabWidget(this, QRect(
			_dialogsWidth,
			sectionTop,
			width() - _dialogsWidth,
			height() - sectionTop));
		if (_sideShadow) {
			_sideShadow->show();
		}
		if (_thirdShadow) {
			_thirdShadow->show();
		}
	}
	if (_hider && hiderVisible) {
		_hider->show();
	}
	if (playerPlaylistVisible) {
		_playerPlaylist->show();
	}
	if (_player) {
		_player->entity()->showShadowAndDropdowns();
	}
	floatPlayerShowVisible();
	return result;
}

void MainWidget::windowShown() {
	_history->windowShown();
}

void MainWidget::dialogsToUp() {
	if (_dialogs) {
		_dialogs->jumpToTop();
	}
}

void MainWidget::checkActivation() {
	_history->checkActivation();
	if (_mainSection) {
		_mainSection->checkActivation();
	}
}

void MainWidget::showAnimated(QPixmap oldContentCache, bool back) {
	_showAnimation = nullptr;

	showAll();
	floatPlayerHideAll();
	auto newContentCache = Ui::GrabWidget(this);
	hideAll();
	floatPlayerShowVisible();

	_showAnimation = std::make_unique<Window::SlideAnimation>();
	_showAnimation->setDirection(back
		? Window::SlideDirection::FromLeft
		: Window::SlideDirection::FromRight);
	_showAnimation->setRepaintCallback([=] { update(); });
	_showAnimation->setFinishedCallback([=] { showFinished(); });
	_showAnimation->setPixmaps(oldContentCache, newContentCache);
	_showAnimation->start();

	show();
}

void MainWidget::showFinished() {
	_showAnimation = nullptr;

	showAll();
	activate();
}

void MainWidget::paintEvent(QPaintEvent *e) {
	if (_background) {
		checkChatBackground();
	}
	if (_showAnimation) {
		auto p = QPainter(this);
		_showAnimation->paintContents(p);
	}
}

int MainWidget::getMainSectionTop() const {
	return _callTopBarHeight + _exportTopBarHeight + _playerHeight;
}

int MainWidget::getThirdSectionTop() const {
	return 0;
}

void MainWidget::hideAll() {
	if (_dialogs) {
		_dialogs->hide();
	}
	_history->hide();
	if (_mainSection) {
		_mainSection->hide();
	}
	if (_thirdSection) {
		_thirdSection->hide();
	}
	if (_sideShadow) {
		_sideShadow->hide();
	}
	if (_thirdShadow) {
		_thirdShadow->hide();
	}
	if (_player) {
		_player->setVisible(false);
		_playerHeight = 0;
	}
	if (_callTopBar) {
		_callTopBar->setVisible(false);
		_callTopBarHeight = 0;
	}
}

void MainWidget::showAll() {
	if (cPasswordRecovered()) {
		cSetPasswordRecovered(false);
		_controller->show(Ui::MakeInformBox(
			tr::lng_cloud_password_updated()));
	}
	if (isOneColumn()) {
		if (_sideShadow) {
			_sideShadow->hide();
		}
		if (_hider) {
			_hider->hide();
		}
		if (_mainSection) {
			_mainSection->show();
		} else if (_history->peer()) {
			_history->show();
			_history->updateControlsGeometry();
		} else {
			Assert(_dialogs != nullptr);
			_dialogs->showFast();
			_history->hide();
		}
		if (_dialogs && isMainSectionShown()) {
			_dialogs->hide();
		}
	} else {
		if (_sideShadow) {
			_sideShadow->show();
		}
		if (_hider) {
			_hider->show();
		}
		if (_dialogs) {
			_dialogs->showFast();
		}
		if (_mainSection) {
			_mainSection->show();
		} else {
			_history->show();
			_history->updateControlsGeometry();
		}
		if (_thirdSection) {
			_thirdSection->show();
		}
		if (_thirdShadow) {
			_thirdShadow->show();
		}
	}
	if (_player) {
		_player->setVisible(true);
		_playerHeight = _player->contentHeight();
	}
	if (_callTopBar) {
		_callTopBar->setVisible(true);

		// show() could've send pending resize event that would update
		// the height value and destroy the top bar if it was hiding.
		if (_callTopBar) {
			_callTopBarHeight = _callTopBar->height();
		}
	}
	updateControlsGeometry();
	floatPlayerCheckVisibility();

	_controller->widget()->checkActivation();
}

void MainWidget::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void MainWidget::updateControlsGeometry() {
	if (!width()) {
		return;
	}
	updateWindowAdaptiveLayout();
	if (_dialogs) {
		if (Core::App().settings().dialogsWidthRatio() > 0) {
			_a_dialogsWidth.stop();
		}
		if (!_a_dialogsWidth.animating()) {
			_dialogs->stopWidthAnimation();
		}
	}
	if (isThreeColumn()) {
		if (!_thirdSection
			&& !_controller->takeThirdSectionFromLayer()) {
			auto params = Window::SectionShow(
				Window::SectionShow::Way::ClearStack,
				anim::type::instant,
				anim::activation::background);
			const auto active = _controller->activeChatCurrent();
			if (const auto thread = active.thread()) {
				if (Core::App().settings().tabbedSelectorSectionEnabled()) {
					if (_mainSection) {
						_mainSection->pushTabbedSelectorToThirdSection(
							thread,
							params);
					} else {
						_history->pushTabbedSelectorToThirdSection(
							thread,
							params);
					}
				} else if (Core::App().settings().thirdSectionInfoEnabled()) {
					_controller->showSection(
						(thread->asTopic()
							? std::make_shared<Info::Memento>(
								thread->asTopic())
							: Info::Memento::Default(
								thread->asHistory()->peer)),
						params.withThirdColumn());
				}
			}
		}
	} else {
		_thirdSection.destroy();
		_thirdShadow.destroy();
	}
	const auto mainSectionTop = getMainSectionTop();
	auto dialogsWidth = _dialogs
		? qRound(_a_dialogsWidth.value(_dialogsWidth))
		: isOneColumn()
		? width()
		: 0;
	if (isOneColumn()) {
		if (_callTopBar) {
			_callTopBar->resizeToWidth(dialogsWidth);
			_callTopBar->moveToLeft(0, 0);
		}
		if (_exportTopBar) {
			_exportTopBar->resizeToWidth(dialogsWidth);
			_exportTopBar->moveToLeft(0, _callTopBarHeight);
		}
		if (_player) {
			_player->resizeToWidth(dialogsWidth);
			_player->moveToLeft(0, _callTopBarHeight + _exportTopBarHeight);
		}
		const auto mainSectionGeometry = QRect(
			0,
			mainSectionTop,
			dialogsWidth,
			height() - mainSectionTop);
		if (_dialogs) {
			_dialogs->setGeometryWithTopMoved(
				mainSectionGeometry,
				_contentScrollAddToY);
		}
		_history->setGeometryWithTopMoved(
			mainSectionGeometry,
			_contentScrollAddToY);
		if (_hider) _hider->setGeometry(0, 0, dialogsWidth, height());
	} else {
		auto thirdSectionWidth = _thirdSection ? _thirdColumnWidth : 0;
		if (_thirdSection) {
			auto thirdSectionTop = getThirdSectionTop();
			_thirdSection->setGeometry(
				width() - thirdSectionWidth,
				thirdSectionTop,
				thirdSectionWidth,
				height() - thirdSectionTop);
		}
		const auto shadowTop = _controller->window().verticalShadowTop();
		const auto shadowHeight = height() - shadowTop;
		if (_dialogs) {
			accumulate_min(
				dialogsWidth,
				width() - st::columnMinimalWidthMain);
			_dialogs->setGeometryToLeft(0, 0, dialogsWidth, height());
		}
		if (_sideShadow) {
			_sideShadow->setGeometryToLeft(
				dialogsWidth,
				shadowTop,
				st::lineWidth,
				shadowHeight);
		}
		if (_thirdShadow) {
			_thirdShadow->setGeometryToLeft(
				width() - thirdSectionWidth - st::lineWidth,
				shadowTop,
				st::lineWidth,
				shadowHeight);
		}
		const auto mainSectionWidth = width()
			- dialogsWidth
			- thirdSectionWidth;
		if (_callTopBar) {
			_callTopBar->resizeToWidth(mainSectionWidth);
			_callTopBar->moveToLeft(dialogsWidth, 0);
		}
		if (_exportTopBar) {
			_exportTopBar->resizeToWidth(mainSectionWidth);
			_exportTopBar->moveToLeft(dialogsWidth, _callTopBarHeight);
		}
		if (_player) {
			_player->resizeToWidth(mainSectionWidth);
			_player->moveToLeft(
				dialogsWidth,
				_callTopBarHeight + _exportTopBarHeight);
		}
		_history->setGeometryWithTopMoved(QRect(
			dialogsWidth,
			mainSectionTop,
			mainSectionWidth,
			height() - mainSectionTop
		), _contentScrollAddToY);
		if (_hider) {
			_hider->setGeometryToLeft(
				dialogsWidth,
				0,
				mainSectionWidth,
				height());
		}
	}
	if (_mainSection) {
		const auto mainSectionGeometry = QRect(
			_history->x(),
			mainSectionTop,
			_history->width(),
			height() - mainSectionTop);
		_mainSection->setGeometryWithTopMoved(
			mainSectionGeometry,
			_contentScrollAddToY);
	}
	refreshResizeAreas();
	if (_player) {
		_player->entity()->updateDropdownsGeometry();
	}
	updateMediaPlaylistPosition(_playerPlaylist->x());
	_contentScrollAddToY = 0;

	floatPlayerUpdatePositions();
}

void MainWidget::refreshResizeAreas() {
	if (!isOneColumn() && _dialogs) {
		ensureFirstColumnResizeAreaCreated();
		_firstColumnResizeArea->setGeometryToLeft(
			_history->x(),
			0,
			st::historyResizeWidth,
			height());
	} else if (_firstColumnResizeArea) {
		_firstColumnResizeArea.destroy();
	}

	if (isThreeColumn() && _thirdSection) {
		ensureThirdColumnResizeAreaCreated();
		_thirdColumnResizeArea->setGeometryToLeft(
			_thirdSection->x(),
			0,
			st::historyResizeWidth,
			height());
	} else if (_thirdColumnResizeArea) {
		_thirdColumnResizeArea.destroy();
	}
}

template <typename MoveCallback, typename FinishCallback>
void MainWidget::createResizeArea(
		object_ptr<Ui::ResizeArea> &area,
		MoveCallback &&moveCallback,
		FinishCallback &&finishCallback) {
	area.create(this);
	area->show();
	area->addMoveLeftCallback(
		std::forward<MoveCallback>(moveCallback));
	area->addMoveFinishedCallback(
		std::forward<FinishCallback>(finishCallback));
	orderWidgets();
}

void MainWidget::ensureFirstColumnResizeAreaCreated() {
	Expects(_dialogs != nullptr);

	if (_firstColumnResizeArea) {
		return;
	}
	auto moveLeftCallback = [=](int globalLeft) {
		auto newWidth = globalLeft - mapToGlobal(QPoint(0, 0)).x();
		auto newRatio = (newWidth < st::columnMinimalWidthLeft / 2)
			? 0.
			: float64(newWidth) / width();
		Core::App().settings().setDialogsWidthRatio(newRatio);
	};
	auto moveFinishedCallback = [=] {
		if (isOneColumn()) {
			return;
		}
		if (Core::App().settings().dialogsWidthRatio() > 0) {
			Core::App().settings().setDialogsWidthRatio(
				float64(_dialogsWidth) / width());
		}
		Core::App().saveSettingsDelayed();
	};
	createResizeArea(
		_firstColumnResizeArea,
		std::move(moveLeftCallback),
		std::move(moveFinishedCallback));
}

void MainWidget::ensureThirdColumnResizeAreaCreated() {
	if (_thirdColumnResizeArea) {
		return;
	}
	auto moveLeftCallback = [=](int globalLeft) {
		auto newWidth = mapToGlobal(QPoint(width(), 0)).x() - globalLeft;
		Core::App().settings().setThirdColumnWidth(newWidth);
	};
	auto moveFinishedCallback = [=] {
		if (!isThreeColumn() || !_thirdSection) {
			return;
		}
		Core::App().settings().setThirdColumnWidth(std::clamp(
			Core::App().settings().thirdColumnWidth(),
			st::columnMinimalWidthThird,
			st::columnMaximalWidthThird));
		Core::App().saveSettingsDelayed();
	};
	createResizeArea(
		_thirdColumnResizeArea,
		std::move(moveLeftCallback),
		std::move(moveFinishedCallback));
}

void MainWidget::updateDialogsWidthAnimated() {
	if (!_dialogs || Core::App().settings().dialogsWidthRatio() > 0) {
		return;
	}
	auto dialogsWidth = _dialogsWidth;
	updateWindowAdaptiveLayout();
	if (!Core::App().settings().dialogsWidthRatio()
		&& (_dialogsWidth != dialogsWidth
			|| _a_dialogsWidth.animating())) {
		_dialogs->startWidthAnimation();
		_a_dialogsWidth.start(
			[this] { updateControlsGeometry(); },
			dialogsWidth,
			_dialogsWidth,
			st::dialogsWidthDuration,
			anim::easeOutCirc);
		updateControlsGeometry();
	}
}

bool MainWidget::saveThirdSectionToStackBack() const {
	return !_stack.empty()
		&& _thirdSection != nullptr
		&& _stack.back()->thirdSectionWeak() == _thirdSection.data();
}

auto MainWidget::thirdSectionForCurrentMainSection(
	Dialogs::Key key)
-> std::shared_ptr<Window::SectionMemento> {
	if (_thirdSectionFromStack) {
		return std::move(_thirdSectionFromStack);
	} else if (const auto topic = key.topic()) {
		return std::make_shared<Info::Memento>(topic);
	} else if (const auto peer = key.peer()) {
		return std::make_shared<Info::Memento>(
			peer,
			Info::Memento::DefaultSection(peer));
	} else if (const auto sublist = key.sublist()) {
		return std::make_shared<Info::Memento>(
			session().user(),
			Info::Memento::DefaultSection(session().user()));
	}
	Unexpected("Key in MainWidget::thirdSectionForCurrentMainSection().");
}

void MainWidget::updateThirdColumnToCurrentChat(
		Dialogs::Key key,
		bool canWrite) {
	auto saveOldThirdSection = [&] {
		if (saveThirdSectionToStackBack()) {
			_stack.back()->setThirdSectionMemento(
				_thirdSection->createMemento());
			_thirdSection.destroy();
		}
	};
	auto &settings = Core::App().settings();
	auto params = Window::SectionShow(
		Window::SectionShow::Way::ClearStack,
		anim::type::instant,
		anim::activation::background);
	auto switchInfoFast = [&] {
		saveOldThirdSection();

		//
		// Like in _controller->showPeerInfo()
		//
		if (isThreeColumn()
			&& !settings.thirdSectionInfoEnabled()) {
			settings.setThirdSectionInfoEnabled(true);
			Core::App().saveSettingsDelayed();
		}

		_controller->showSection(
			thirdSectionForCurrentMainSection(key),
			params.withThirdColumn());
	};
	auto switchTabbedFast = [&](not_null<Data::Thread*> thread) {
		saveOldThirdSection();
		return _mainSection
			? _mainSection->pushTabbedSelectorToThirdSection(thread, params)
			: _history->pushTabbedSelectorToThirdSection(thread, params);
	};
	if (isThreeColumn()
		&& settings.tabbedSelectorSectionEnabled()
		&& key) {
		if (!canWrite) {
			switchInfoFast();
			settings.setTabbedSelectorSectionEnabled(true);
			settings.setTabbedReplacedWithInfo(true);
		} else if (settings.tabbedReplacedWithInfo()
			&& key.thread()
			&& switchTabbedFast(key.thread())) {
			settings.setTabbedReplacedWithInfo(false);
		}
	} else {
		settings.setTabbedReplacedWithInfo(false);
		if (!key) {
			if (_thirdSection) {
				_thirdSection.destroy();
				_thirdShadow.destroy();
				updateControlsGeometry();
			}
		} else if (isThreeColumn()
			&& settings.thirdSectionInfoEnabled()) {
			switchInfoFast();
		}
	}
}

void MainWidget::updateMediaPlaylistPosition(int x) {
	if (_player) {
		auto playlistLeft = x;
		auto playlistWidth = _playerPlaylist->width();
		auto playlistTop = _player->y() + _player->height();
		auto rightEdge = width();
		if (playlistLeft + playlistWidth > rightEdge) {
			playlistLeft = rightEdge - playlistWidth;
		} else if (playlistLeft < 0) {
			playlistLeft = 0;
		}
		_playerPlaylist->move(playlistLeft, playlistTop);
	}
}

void MainWidget::returnTabbedSelector() {
	if (!_mainSection || !_mainSection->returnTabbedSelector()) {
		_history->returnTabbedSelector();
	}
}

bool MainWidget::eventFilter(QObject *o, QEvent *e) {
	const auto widget = o->isWidgetType()
		? static_cast<QWidget*>(o)
		: nullptr;
	if (e->type() == QEvent::FocusIn) {
		if (widget && (widget->window() == window())) {
			if (_history == widget || _history->isAncestorOf(widget)
				|| (_mainSection
					&& (_mainSection == widget
						|| _mainSection->isAncestorOf(widget)))
				|| (_thirdSection
					&& (_thirdSection == widget
						|| _thirdSection->isAncestorOf(widget)))) {
				_controller->setDialogsListFocused(false);
			} else if (_dialogs
				&& (_dialogs == widget
					|| _dialogs->isAncestorOf(widget))) {
				_controller->setDialogsListFocused(true);
			}
		}
	} else if (e->type() == QEvent::MouseButtonPress) {
		if (widget && (widget->window() == window())) {
			const auto event = static_cast<QMouseEvent*>(e);
			if (event->button() == Qt::BackButton) {
				if (!Core::App().hideMediaView()) {
					handleHistoryBack();
				}
				return true;
			}
		}
	} else if (e->type() == QEvent::Wheel) {
		if (widget && (widget->window() == window())) {
			if (const auto result = floatPlayerFilterWheelEvent(o, e)) {
				return *result;
			}
		}
	}
	return RpWidget::eventFilter(o, e);
}

void MainWidget::handleAdaptiveLayoutUpdate() {
	showAll();
	if (_sideShadow) {
		_sideShadow->setVisible(!isOneColumn());
	}
	if (_player) {
		_player->updateAdaptiveLayout();
	}
}

void MainWidget::handleHistoryBack() {
	const auto openedFolder = _controller->openedFolder().current();
	const auto openedForum = _controller->shownForum().current();
	const auto rootPeer = !_stack.empty()
		? _stack.front()->peer()
		: _history->peer()
		? _history->peer()
		: _mainSection
		? _mainSection->activeChat().key.peer()
		: nullptr;
	const auto rootHistory = rootPeer
		? rootPeer->owner().historyLoaded(rootPeer)
		: nullptr;
	const auto rootFolder = rootHistory ? rootHistory->folder() : nullptr;
	if (openedForum && (!rootPeer || rootPeer->forum() != openedForum)) {
		_controller->closeForum();
	} else if (!openedFolder
		|| (rootFolder == openedFolder)
		|| (!_dialogs || _dialogs->isHidden())) {
		_controller->showBackFromStack();
		if (_dialogs) {
			_dialogs->setInnerFocus();
		}
	} else {
		_controller->closeFolder();
	}
}

void MainWidget::updateWindowAdaptiveLayout() {
	auto layout = _controller->computeColumnLayout();
	auto dialogsWidthRatio = Core::App().settings().dialogsWidthRatio();

	// Check if we are in a single-column layout in a wide enough window
	// for the normal layout. If so, switch to the normal layout.
	if (layout.windowLayout == Window::Adaptive::WindowLayout::OneColumn) {
		auto chatWidth = layout.chatWidth;
		//if (session().settings().tabbedSelectorSectionEnabled()
		//	&& chatWidth >= _history->minimalWidthForTabbedSelectorSection()) {
		//	chatWidth -= _history->tabbedSelectorSectionWidth();
		//}
		auto minimalNormalWidth = st::columnMinimalWidthLeft
			+ st::columnMinimalWidthMain;
		if (chatWidth >= minimalNormalWidth) {
			// Switch layout back to normal in a wide enough window.
			layout.windowLayout = Window::Adaptive::WindowLayout::Normal;
			layout.dialogsWidth = st::columnMinimalWidthLeft;
			layout.chatWidth = layout.bodyWidth - layout.dialogsWidth;
			dialogsWidthRatio = float64(layout.dialogsWidth) / layout.bodyWidth;
		}
	}

	// Check if we are going to create the third column and shrink the
	// dialogs widget to provide a wide enough chat history column.
	// Don't shrink the column on the first call, when window is inited.
	if (layout.windowLayout == Window::Adaptive::WindowLayout::ThreeColumn
		&& _controller->widget()->positionInited()) {
		//auto chatWidth = layout.chatWidth;
		//if (_history->willSwitchToTabbedSelectorWithWidth(chatWidth)) {
		//	auto thirdColumnWidth = _history->tabbedSelectorSectionWidth();
		//	auto twoColumnsWidth = (layout.bodyWidth - thirdColumnWidth);
		//	auto sameRatioChatWidth = twoColumnsWidth - qRound(dialogsWidthRatio * twoColumnsWidth);
		//	auto desiredChatWidth = qMax(sameRatioChatWidth, HistoryView::WideChatWidth());
		//	chatWidth -= thirdColumnWidth;
		//	auto extendChatBy = desiredChatWidth - chatWidth;
		//	accumulate_min(extendChatBy, layout.dialogsWidth - st::columnMinimalWidthLeft);
		//	if (extendChatBy > 0) {
		//		layout.dialogsWidth -= extendChatBy;
		//		layout.chatWidth += extendChatBy;
		//		dialogsWidthRatio = float64(layout.dialogsWidth) / layout.bodyWidth;
		//	}
		//}
	}

	Core::App().settings().setDialogsWidthRatio(dialogsWidthRatio);

	auto useSmallColumnWidth = !isOneColumn()
		&& !dialogsWidthRatio
		&& !_controller->forceWideDialogs();
	_dialogsWidth = !_dialogs
		? 0
		: useSmallColumnWidth
		? _controller->dialogsSmallColumnWidth()
		: layout.dialogsWidth;
	_thirdColumnWidth = layout.thirdWidth;
	_controller->adaptive().setWindowLayout(layout.windowLayout);
}

int MainWidget::backgroundFromY() const {
	return -getMainSectionTop();
}

bool MainWidget::contentOverlapped(const QRect &globalRect) {
	return _history->contentOverlapped(globalRect)
		|| _playerPlaylist->overlaps(globalRect);
}

void MainWidget::activate() {
	if (_showAnimation) {
		return;
	} else if (const auto paths = cSendPaths(); !paths.isEmpty()) {
		const auto interpret = u"interpret://"_q;
		cSetSendPaths(QStringList());
		if (paths[0].startsWith(interpret)) {
			const auto error = Support::InterpretSendPath(
				_controller,
				paths[0].mid(interpret.size()));
			if (!error.isEmpty()) {
				_controller->show(Ui::MakeInformBox(error));
			}
		} else {
			const auto chosen = [=](not_null<Data::Thread*> thread) {
				return sendPaths(thread, paths);
			};
			Window::ShowChooseRecipientBox(_controller, chosen);
		}
	} else if (_mainSection) {
		_mainSection->setInnerFocus();
	} else if (_hider) {
		Assert(_dialogs != nullptr);
		_dialogs->setInnerFocus();
	} else if (!_controller->isLayerShown()) {
		if (_history->peer()) {
			_history->activate();
		} else {
			Assert(_dialogs != nullptr);
			_dialogs->setInnerFocus();
		}
	}
	_controller->widget()->fixOrder();
}

bool MainWidget::animatingShow() const {
	return _showAnimation != nullptr;
}

bool MainWidget::isOneColumn() const {
	return _controller->adaptive().isOneColumn();
}

bool MainWidget::isNormalColumn() const {
	return _controller->adaptive().isNormal();
}

bool MainWidget::isThreeColumn() const {
	return _controller->adaptive().isThreeColumn();
}
