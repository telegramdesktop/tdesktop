/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mainwidget.h"

#include <rpl/combine.h>
#include <rpl/merge.h>
#include <rpl/flatten_latest.h>
#include "data/data_photo.h"
#include "data/data_document.h"
#include "data/data_web_page.h"
#include "data/data_game.h"
#include "data/data_peer_values.h"
#include "data/data_drafts.h"
#include "data/data_session.h"
#include "data/data_media_types.h"
#include "data/data_folder.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_scheduled_messages.h"
#include "ui/special_buttons.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "ui/toast/toast.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/image/image.h"
#include "ui/image/image_source.h"
#include "ui/focus_persister.h"
#include "ui/resize_area.h"
#include "ui/text_options.h"
#include "ui/emoji_config.h"
#include "window/section_memento.h"
#include "window/section_widget.h"
#include "window/window_connecting_widget.h"
#include "chat_helpers/tabbed_selector.h" // TabbedSelector::refreshStickers
#include "chat_helpers/message_field.h"
#include "chat_helpers/stickers.h"
#include "info/info_memento.h"
#include "info/info_controller.h"
#include "observer_peer.h"
#include "apiwrap.h"
#include "dialogs/dialogs_widget.h"
#include "dialogs/dialogs_key.h"
#include "history/history.h"
#include "history/history_widget.h"
#include "history/history_message.h"
#include "history/view/media/history_view_media.h"
#include "history/view/history_view_service_message.h"
#include "history/view/history_view_element.h"
#include "lang/lang_keys.h"
#include "lang/lang_cloud_manager.h"
#include "boxes/add_contact_box.h"
#include "storage/file_upload.h"
#include "mainwindow.h"
#include "inline_bots/inline_bot_layout_item.h"
#include "boxes/confirm_box.h"
#include "boxes/sticker_set_box.h"
#include "boxes/mute_settings_box.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/download_path_box.h"
#include "boxes/connection_box.h"
#include "storage/localstorage.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_panel.h"
#include "media/player/media_player_widget.h"
#include "media/player/media_player_volume_controller.h"
#include "media/player/media_player_instance.h"
#include "media/player/media_player_float.h"
#include "base/qthelp_regex.h"
#include "base/qthelp_url.h"
#include "base/flat_set.h"
#include "window/window_top_bar_wrap.h"
#include "window/notifications_manager.h"
#include "window/window_slide_animation.h"
#include "window/window_session_controller.h"
#include "window/themes/window_theme.h"
#include "window/window_history_hider.h"
#include "mtproto/dc_options.h"
#include "core/file_utilities.h"
#include "core/update_checker.h"
#include "core/shortcuts.h"
#include "core/application.h"
#include "base/unixtime.h"
#include "calls/calls_instance.h"
#include "calls/calls_top_bar.h"
#include "export/export_settings.h"
#include "export/view/export_view_top_bar.h"
#include "export/view/export_view_panel_controller.h"
#include "main/main_session.h"
#include "support/support_helper.h"
#include "storage/storage_facade.h"
#include "storage/storage_shared_media.h"
#include "storage/storage_user_photos.h"
#include "styles/style_dialogs.h"
#include "styles/style_history.h"
#include "styles/style_boxes.h"

namespace {

constexpr auto kChannelGetDifferenceLimit = 100;

// 1s wait after show channel history before sending getChannelDifference.
constexpr auto kWaitForChannelGetDifference = crl::time(1000);

// If nothing is received in 1 min we ping.
constexpr auto kNoUpdatesTimeout = 60 * 1000;

// If nothing is received in 1 min when was a sleepmode we ping.
constexpr auto kNoUpdatesAfterSleepTimeout = 60 * crl::time(1000);

// Send channel views each second.
constexpr auto kSendViewsTimeout = crl::time(1000);

// Cache background scaled image after 3s.
constexpr auto kCacheBackgroundTimeout = 3000;

enum class DataIsLoadedResult {
	NotLoaded = 0,
	FromNotLoaded = 1,
	MentionNotLoaded = 2,
	Ok = 3,
};

bool IsForceLogoutNotification(const MTPDupdateServiceNotification &data) {
	return qs(data.vtype()).startsWith(qstr("AUTH_KEY_DROP_"));
}

bool HasForceLogoutNotification(const MTPUpdates &updates) {
	const auto checkUpdate = [](const MTPUpdate &update) {
		if (update.type() != mtpc_updateServiceNotification) {
			return false;
		}
		return IsForceLogoutNotification(
			update.c_updateServiceNotification());
	};
	const auto checkVector = [&](const MTPVector<MTPUpdate> &list) {
		for (const auto &update : list.v) {
			if (checkUpdate(update)) {
				return true;
			}
		}
		return false;
	};
	switch (updates.type()) {
	case mtpc_updates:
		return checkVector(updates.c_updates().vupdates());
	case mtpc_updatesCombined:
		return checkVector(updates.c_updatesCombined().vupdates());
	case mtpc_updateShort:
		return checkUpdate(updates.c_updateShort().vupdate());
	}
	return false;
}

bool ForwardedInfoDataLoaded(
		not_null<Main::Session*> session,
		const MTPMessageFwdHeader &header) {
	return header.match([&](const MTPDmessageFwdHeader &data) {
		if (const auto channelId = data.vchannel_id()) {
			if (!session->data().channelLoaded(channelId->v)) {
				return false;
			}
			if (const auto fromId = data.vfrom_id()) {
				const auto from = session->data().user(fromId->v);
				// Minimal loaded is fine in this case.
				if (from->loadedStatus == PeerData::NotLoaded) {
					return false;
				}
			}
		} else if (const auto fromId = data.vfrom_id()) {
			// Fully loaded is required in this case.
			if (!session->data().userLoaded(fromId->v)) {
				return false;
			}
		}
		return true;
	});
}

bool MentionUsersLoaded(
		not_null<Main::Session*> session,
		const MTPVector<MTPMessageEntity> &entities) {
	for (const auto &entity : entities.v) {
		auto type = entity.type();
		if (type == mtpc_messageEntityMentionName) {
			if (!session->data().userLoaded(entity.c_messageEntityMentionName().vuser_id().v)) {
				return false;
			}
		} else if (type == mtpc_inputMessageEntityMentionName) {
			auto &inputUser = entity.c_inputMessageEntityMentionName().vuser_id();
			if (inputUser.type() == mtpc_inputUser) {
				if (!session->data().userLoaded(inputUser.c_inputUser().vuser_id().v)) {
					return false;
				}
			}
		}
	}
	return true;
}

DataIsLoadedResult AllDataLoadedForMessage(
		not_null<Main::Session*> session,
		const MTPMessage &message) {
	return message.match([&](const MTPDmessage &message) {
		if (const auto fromId = message.vfrom_id()) {
			if (!message.is_post()
				&& !session->data().userLoaded(fromId->v)) {
				return DataIsLoadedResult::FromNotLoaded;
			}
		}
		if (const auto viaBotId = message.vvia_bot_id()) {
			if (!session->data().userLoaded(viaBotId->v)) {
				return DataIsLoadedResult::NotLoaded;
			}
		}
		if (const auto fwd = message.vfwd_from()) {
			if (!ForwardedInfoDataLoaded(session, *fwd)) {
				return DataIsLoadedResult::NotLoaded;
			}
		}
		if (const auto entities = message.ventities()) {
			if (!MentionUsersLoaded(session, *entities)) {
				return DataIsLoadedResult::MentionNotLoaded;
			}
		}
		return DataIsLoadedResult::Ok;
	}, [&](const MTPDmessageService &message) {
		if (const auto fromId = message.vfrom_id()) {
			if (!message.is_post()
				&& !session->data().userLoaded(fromId->v)) {
				return DataIsLoadedResult::FromNotLoaded;
			}
		}
		return message.vaction().match(
		[&](const MTPDmessageActionChatAddUser &action) {
			for (const MTPint &userId : action.vusers().v) {
				if (!session->data().userLoaded(userId.v)) {
					return DataIsLoadedResult::NotLoaded;
				}
			}
			return DataIsLoadedResult::Ok;
		}, [&](const MTPDmessageActionChatJoinedByLink &action) {
			if (!session->data().userLoaded(action.vinviter_id().v)) {
				return DataIsLoadedResult::NotLoaded;
			}
			return DataIsLoadedResult::Ok;
		}, [&](const MTPDmessageActionChatDeleteUser &action) {
			if (!session->data().userLoaded(action.vuser_id().v)) {
				return DataIsLoadedResult::NotLoaded;
			}
			return DataIsLoadedResult::Ok;
		}, [](const auto &) {
			return DataIsLoadedResult::Ok;
		});
	}, [](const MTPDmessageEmpty &message) {
		return DataIsLoadedResult::Ok;
	});
}

} // namespace

enum StackItemType {
	HistoryStackItem,
	SectionStackItem,
};

class StackItem {
public:
	StackItem(PeerData *peer) : _peer(peer) {
	}

	PeerData *peer() const {
		return _peer;
	}

	void setThirdSectionMemento(
		std::unique_ptr<Window::SectionMemento> &&memento);
	std::unique_ptr<Window::SectionMemento> takeThirdSectionMemento() {
		return std::move(_thirdSectionMemento);
	}

	void setThirdSectionWeak(QPointer<Window::SectionWidget> section) {
		_thirdSectionWeak = section;
	}
	QPointer<Window::SectionWidget> thirdSectionWeak() const {
		return _thirdSectionWeak;
	}

	virtual StackItemType type() const = 0;
	virtual ~StackItem() = default;

private:
	PeerData *_peer = nullptr;
	QPointer<Window::SectionWidget> _thirdSectionWeak;
	std::unique_ptr<Window::SectionMemento> _thirdSectionMemento;

};

class StackItemHistory : public StackItem {
public:
	StackItemHistory(
		not_null<History*> history,
		MsgId msgId,
		QList<MsgId> replyReturns)
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
	QList<MsgId> replyReturns;

};

class StackItemSection : public StackItem {
public:
	StackItemSection(
		std::unique_ptr<Window::SectionMemento> &&memento);

	StackItemType type() const override {
		return SectionStackItem;
	}
	Window::SectionMemento *memento() const {
		return _memento.get();
	}

private:
	std::unique_ptr<Window::SectionMemento> _memento;

};

void StackItem::setThirdSectionMemento(
		std::unique_ptr<Window::SectionMemento> &&memento) {
	_thirdSectionMemento = std::move(memento);
}

StackItemSection::StackItemSection(
	std::unique_ptr<Window::SectionMemento> &&memento)
: StackItem(nullptr)
, _memento(std::move(memento)) {
}

struct MainWidget::SettingBackground {
	explicit SettingBackground(const Data::WallPaper &data);

	Data::WallPaper data;
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
, _sideShadow(this)
, _dialogs(this, _controller)
, _history(this, _controller)
, _playerPlaylist(this, _controller)
, _noUpdatesTimer([=] { sendPing(); })
, _byPtsTimer([=] { getDifferenceByPts(); })
, _bySeqTimer([=] { getDifference(); })
, _byMinChannelTimer([=] { getDifference(); })
, _onlineTimer([=] { updateOnline(); })
, _idleFinishTimer([=] { checkIdleFinish(); })
, _failDifferenceTimer([=] { getDifferenceAfterFail(); })
, _cacheBackgroundTimer([=] { cacheBackground(); })
, _viewsIncrementTimer([=] { viewsIncrement(); }) {
	_controller->setDefaultFloatPlayerDelegate(floatPlayerDelegate());
	_controller->floatPlayerClosed(
	) | rpl::start_with_next([=](FullMsgId itemId) {
		floatPlayerClosed(itemId);
	}, lifetime());

	_ptsWaiter.setRequesting(true);
	updateScrollColors();
	setupConnectingWidget();

	connect(_dialogs, SIGNAL(cancelled()), this, SLOT(dialogsCancelled()));
	connect(this, SIGNAL(dialogsUpdated()), _dialogs, SLOT(onListScroll()));
	connect(_history, &HistoryWidget::cancelled, [=] { handleHistoryBack(); });

	Media::Player::instance()->updatedNotifier(
	) | rpl::start_with_next([=](const Media::Player::TrackState &state) {
		handleAudioUpdate(state);
	}, lifetime());

	subscribe(session().calls().currentCallChanged(), [this](Calls::Call *call) { setCurrentCall(call); });

	session().data().currentExportView(
	) | rpl::start_with_next([=](Export::View::PanelController *view) {
		setCurrentExportView(view);
	}, lifetime());

	subscribe(_controller->dialogsListFocused(), [this](bool) {
		updateDialogsWidthAnimated();
	});
	subscribe(_controller->dialogsListDisplayForced(), [this](bool) {
		updateDialogsWidthAnimated();
	});
	rpl::merge(
		session().settings().dialogsWidthRatioChanges()
			| rpl::map([] { return rpl::empty_value(); }),
		session().settings().thirdColumnWidthChanges()
			| rpl::map([] { return rpl::empty_value(); })
	) | rpl::start_with_next(
		[this] { updateControlsGeometry(); },
		lifetime());

	// MSVC BUG + REGRESSION rpl::mappers::tuple :(
	using namespace rpl::mappers;
	_controller->activeChatValue(
	) | rpl::map([](Dialogs::Key key) {
		const auto peer = key.peer();
		auto canWrite = peer
			? Data::CanWriteValue(peer)
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

	using Update = Window::Theme::BackgroundUpdate;
	subscribe(Window::Theme::Background(), [this](const Update &update) {
		if (update.type == Update::Type::New || update.type == Update::Type::Changed) {
			clearCachedBackground();
		}
	});

	subscribe(Media::Player::instance()->playerWidgetOver(), [this](bool over) {
		if (over) {
			if (_playerPlaylist->isHidden()) {
				auto position = mapFromGlobal(QCursor::pos()).x();
				auto bestPosition = _playerPlaylist->bestPositionFor(position);
				if (rtl()) bestPosition = position + 2 * (position - bestPosition) - _playerPlaylist->width();
				updateMediaPlaylistPosition(bestPosition);
			}
			_playerPlaylist->showFromOther();
		} else {
			_playerPlaylist->hideFromOther();
		}
	});
	subscribe(Media::Player::instance()->tracksFinishedNotifier(), [this](AudioMsgId::Type type) {
		if (type == AudioMsgId::Type::Voice) {
			const auto songState = Media::Player::instance()->getState(AudioMsgId::Type::Song);
			if (!songState.id || IsStoppedOrStopping(songState.state)) {
				closeBothPlayers();
			}
		}
	});

	subscribe(Adaptive::Changed(), [this]() { handleAdaptiveLayoutUpdate(); });

	_dialogs->show();
	if (Adaptive::OneColumn()) {
		_history->hide();
	} else {
		_history->show();
	}

	orderWidgets();

	if (!Core::UpdaterDisabled()) {
		Core::UpdateChecker checker;
		checker.start();
	}
}

Main::Session &MainWidget::session() const {
	return _controller->session();
}

void MainWidget::setupConnectingWidget() {
	using namespace rpl::mappers;
	_connecting = std::make_unique<Window::ConnectionState>(
		this,
		Window::AdaptiveIsOneColumn() | rpl::map(!_1));
}

not_null<Media::Player::FloatDelegate*> MainWidget::floatPlayerDelegate() {
	return static_cast<Media::Player::FloatDelegate*>(this);
}

not_null<Ui::RpWidget*> MainWidget::floatPlayerWidget() {
	return this;
}

not_null<Window::SessionController*> MainWidget::floatPlayerController() {
	return _controller;
}

not_null<Window::AbstractSectionWidget*> MainWidget::floatPlayerGetSection(
		Window::Column column) {
	if (Adaptive::ThreeColumn()) {
		if (column == Window::Column::First) {
			return _dialogs;
		} else if (column == Window::Column::Second
			|| !_thirdSection) {
			if (_mainSection) {
				return _mainSection;
			}
			return _history;
		}
		return _thirdSection;
	} else if (Adaptive::Normal()) {
		if (column == Window::Column::First) {
			return _dialogs;
		} else if (_mainSection) {
			return _mainSection;
		}
		return _history;
	}
	if (Adaptive::OneColumn() && selectingPeer()) {
		return _dialogs;
	} else if (_mainSection) {
		return _mainSection;
	} else if (!Adaptive::OneColumn() || _history->peer()) {
		return _history;
	}
	return _dialogs;
}

void MainWidget::floatPlayerEnumerateSections(Fn<void(
		not_null<Window::AbstractSectionWidget*> widget,
		Window::Column widgetColumn)> callback) {
	if (Adaptive::ThreeColumn()) {
		callback(_dialogs, Window::Column::First);
		if (_mainSection) {
			callback(_mainSection, Window::Column::Second);
		} else {
			callback(_history, Window::Column::Second);
		}
		if (_thirdSection) {
			callback(_thirdSection, Window::Column::Third);
		}
	} else if (Adaptive::Normal()) {
		callback(_dialogs, Window::Column::First);
		if (_mainSection) {
			callback(_mainSection, Window::Column::Second);
		} else {
			callback(_history, Window::Column::Second);
		}
	} else {
		if (Adaptive::OneColumn() && selectingPeer()) {
			callback(_dialogs, Window::Column::First);
		} else if (_mainSection) {
			callback(_mainSection, Window::Column::Second);
		} else if (!Adaptive::OneColumn() || _history->peer()) {
			callback(_history, Window::Column::Second);
		} else {
			callback(_dialogs, Window::Column::First);
		}
	}
}

bool MainWidget::floatPlayerIsVisible(not_null<HistoryItem*> item) {
	auto isVisible = false;
	session().data().queryItemVisibility().notify({ item, &isVisible }, true);
	return isVisible;
}

void MainWidget::floatPlayerClosed(FullMsgId itemId) {
	if (_player) {
		const auto voiceData = Media::Player::instance()->current(
			AudioMsgId::Type::Voice);
		if (voiceData.contextId() == itemId) {
			_player->entity()->stopAndClose();
		}
	}
}

bool MainWidget::setForwardDraft(PeerId peerId, MessageIdsList &&items) {
	Expects(peerId != 0);

	const auto peer = session().data().peer(peerId);
	const auto error = GetErrorTextForSending(
		peer,
		session().data().idsToItems(items),
		true);
	if (!error.isEmpty()) {
		Ui::show(Box<InformBox>(error), LayerOption::KeepOther);
		return false;
	}

	peer->owner().history(peer)->setForwardDraft(std::move(items));
	_controller->showPeerHistory(
		peer,
		SectionShow::Way::Forward,
		ShowAtUnreadMsgId);
	_history->cancelReply();
	return true;
}

bool MainWidget::shareUrl(
		PeerId peerId,
		const QString &url,
		const QString &text) {
	Expects(peerId != 0);

	const auto peer = session().data().peer(peerId);
	if (!peer->canWrite()) {
		Ui::show(Box<InformBox>(tr::lng_share_cant(tr::now)));
		return false;
	}
	TextWithTags textWithTags = {
		url + '\n' + text,
		TextWithTags::Tags()
	};
	MessageCursor cursor = {
		url.size() + 1,
		url.size() + 1 + text.size(),
		QFIXED_MAX
	};
	auto history = peer->owner().history(peer);
	history->setLocalDraft(
		std::make_unique<Data::Draft>(textWithTags, 0, cursor, false));
	history->clearEditDraft();
	if (_history->peer() == peer) {
		_history->applyDraft();
	} else {
		Ui::showPeerHistory(peer, ShowAtUnreadMsgId);
	}
	return true;
}

void MainWidget::replyToItem(not_null<HistoryItem*> item) {
	if (_history->peer() == item->history()->peer
		|| _history->peer() == item->history()->peer->migrateTo()) {
		_history->replyToMessage(item);
	}
}

bool MainWidget::inlineSwitchChosen(PeerId peerId, const QString &botAndQuery) {
	Expects(peerId != 0);

	const auto peer = session().data().peer(peerId);
	if (!peer->canWrite()) {
		Ui::show(Box<InformBox>(tr::lng_inline_switch_cant(tr::now)));
		return false;
	}
	const auto h = peer->owner().history(peer);
	TextWithTags textWithTags = { botAndQuery, TextWithTags::Tags() };
	MessageCursor cursor = { botAndQuery.size(), botAndQuery.size(), QFIXED_MAX };
	h->setLocalDraft(std::make_unique<Data::Draft>(textWithTags, 0, cursor, false));
	h->clearEditDraft();
	const auto opened = _history->peer() && (_history->peer() == peer);
	if (opened) {
		_history->applyDraft();
	} else {
		Ui::showPeerHistory(peer, ShowAtUnreadMsgId);
	}
	return true;
}

void MainWidget::cancelForwarding(not_null<History*> history) {
	history->setForwardDraft({});
	_history->updateForwarding();
}

void MainWidget::finishForwarding(Api::SendAction action) {
	const auto history = action.history;
	auto toForward = history->validateForwardDraft();
	if (!toForward.empty()) {
		const auto error = GetErrorTextForSending(history->peer, toForward);
		if (!error.isEmpty()) {
			return;
		}

		session().api().forwardMessages(std::move(toForward), action);
		cancelForwarding(history);
	}

	session().data().sendHistoryChangeNotifications();
	historyToDown(history);
	dialogsToUp();
}

bool MainWidget::sendPaths(PeerId peerId) {
	Expects(peerId != 0);

	auto peer = session().data().peer(peerId);
	if (!peer->canWrite()) {
		Ui::show(Box<InformBox>(tr::lng_forward_send_files_cant(tr::now)));
		return false;
	} else if (const auto error = Data::RestrictionError(
			peer,
			ChatRestriction::f_send_media)) {
		Ui::show(Box<InformBox>(*error));
		return false;
	}
	Ui::showPeerHistory(peer, ShowAtTheEndMsgId);
	return _history->confirmSendingFiles(cSendPaths());
}

void MainWidget::onFilesOrForwardDrop(
		const PeerId &peerId,
		const QMimeData *data) {
	Expects(peerId != 0);

	if (data->hasFormat(qsl("application/x-td-forward"))) {
		if (!setForwardDraft(peerId, session().data().takeMimeForwardIds())) {
			// We've already released the mouse button, so the forwarding is cancelled.
			if (_hider) {
				_hider->startHide();
				clearHider(_hider);
			}
		}
	} else {
		auto peer = session().data().peer(peerId);
		if (!peer->canWrite()) {
			Ui::show(Box<InformBox>(tr::lng_forward_send_files_cant(tr::now)));
			return;
		}
		Ui::showPeerHistory(peer, ShowAtTheEndMsgId);
		_history->confirmSendingFiles(data);
	}
}

void MainWidget::notify_botCommandsChanged(UserData *bot) {
	_history->notify_botCommandsChanged(bot);
}

void MainWidget::notify_inlineBotRequesting(bool requesting) {
	_history->notify_inlineBotRequesting(requesting);
}

void MainWidget::notify_replyMarkupUpdated(const HistoryItem *item) {
	_history->notify_replyMarkupUpdated(item);
}

void MainWidget::notify_inlineKeyboardMoved(const HistoryItem *item, int oldKeyboardTop, int newKeyboardTop) {
	_history->notify_inlineKeyboardMoved(item, oldKeyboardTop, newKeyboardTop);
}

bool MainWidget::notify_switchInlineBotButtonReceived(const QString &query, UserData *samePeerBot, MsgId samePeerReplyTo) {
	return _history->notify_switchInlineBotButtonReceived(query, samePeerBot, samePeerReplyTo);
}

void MainWidget::notify_userIsBotChanged(UserData *bot) {
	_history->notify_userIsBotChanged(bot);
}

void MainWidget::notify_historyMuteUpdated(History *history) {
	_dialogs->notify_historyMuteUpdated(history);
}

void MainWidget::clearHider(not_null<Window::HistoryHider*> instance) {
	if (_hider != instance) {
		return;
	}
	_hider.release();
	if (Adaptive::OneColumn()) {
		if (_mainSection || (_history->peer() && _history->peer()->id)) {
			auto animationParams = ([=] {
				if (_mainSection) {
					return prepareMainSectionAnimation(_mainSection);
				}
				return prepareHistoryAnimation(_history->peer() ? _history->peer()->id : 0);
			})();
			_dialogs->hide();
			if (_mainSection) {
				_mainSection->showAnimated(Window::SlideDirection::FromRight, animationParams);
			} else {
				_history->showAnimated(Window::SlideDirection::FromRight, animationParams);
			}
			floatPlayerCheckVisibility();
		}
	}
}

void MainWidget::hiderLayer(base::unique_qptr<Window::HistoryHider> hider) {
	if (Core::App().locked()) {
		return;
	}

	_hider = std::move(hider);
	_hider->setParent(this);

	_hider->hidden(
	) | rpl::start_with_next([=, instance = _hider.get()] {
		clearHider(instance);
		instance->deleteLater();
	}, _hider->lifetime());

	_hider->confirmed(
	) | rpl::start_with_next([=] {
		_dialogs->onCancelSearch();
	}, _hider->lifetime());

	if (Adaptive::OneColumn()) {
		dialogsToUp();

		_hider->hide();
		auto animationParams = prepareDialogsAnimation();

		if (_mainSection) {
			_mainSection->hide();
		} else {
			_history->hide();
		}
		if (_dialogs->isHidden()) {
			_dialogs->show();
			updateControlsGeometry();
			_dialogs->showAnimated(Window::SlideDirection::FromLeft, animationParams);
		}
	} else {
		_hider->show();
		updateControlsGeometry();
		_dialogs->setInnerFocus();
	}
	floatPlayerCheckVisibility();
}

void MainWidget::showForwardLayer(MessageIdsList &&items) {
	auto callback = [=, items = std::move(items)](PeerId peer) mutable {
		return setForwardDraft(peer, std::move(items));
	};
	hiderLayer(base::make_unique_q<Window::HistoryHider>(
		this,
		tr::lng_forward_choose(tr::now),
		std::move(callback)));
}

void MainWidget::showSendPathsLayer() {
	hiderLayer(base::make_unique_q<Window::HistoryHider>(
		this,
		tr::lng_forward_choose(tr::now),
		[=](PeerId peer) { return sendPaths(peer); }));
	if (_hider) {
		connect(_hider, &QObject::destroyed, [] {
			cSetSendPaths(QStringList());
		});
	}
}

void MainWidget::cancelUploadLayer(not_null<HistoryItem*> item) {
	const auto itemId = item->fullId();
	session().uploader().pause(itemId);
	const auto stopUpload = [=] {
		Ui::hideLayer();
		if (const auto item = session().data().message(itemId)) {
			const auto history = item->history();
			if (!IsServerMsgId(itemId.msg)) {
				item->destroy();
				history->requestChatListMessage();
			} else {
				item->returnSavedMedia();
				session().uploader().cancel(item->fullId());
			}
			session().data().sendHistoryChangeNotifications();
		}
		session().uploader().unpause();
	};
	const auto continueUpload = [=] {
		session().uploader().unpause();
	};
	Ui::show(Box<ConfirmBox>(
		tr::lng_selected_cancel_sure_this(tr::now),
		tr::lng_selected_upload_stop(tr::now),
		tr::lng_continue(tr::now),
		stopUpload,
		continueUpload));
}

void MainWidget::deletePhotoLayer(PhotoData *photo) {
	if (!photo) return;
	Ui::show(Box<ConfirmBox>(tr::lng_delete_photo_sure(tr::now), tr::lng_box_delete(tr::now), crl::guard(this, [=] {
		session().api().clearPeerPhoto(photo);
		Ui::hideLayer();
	})));
}

void MainWidget::shareUrlLayer(const QString &url, const QString &text) {
	// Don't allow to insert an inline bot query by share url link.
	if (url.trimmed().startsWith('@')) {
		return;
	}
	auto callback = [=](PeerId peer) {
		return shareUrl(peer, url, text);
	};
	hiderLayer(base::make_unique_q<Window::HistoryHider>(
		this,
		tr::lng_forward_choose(tr::now),
		std::move(callback)));
}

void MainWidget::inlineSwitchLayer(const QString &botAndQuery) {
	auto callback = [=](PeerId peer) {
		return inlineSwitchChosen(peer, botAndQuery);
	};
	hiderLayer(base::make_unique_q<Window::HistoryHider>(
		this,
		tr::lng_inline_switch_choose(tr::now),
		std::move(callback)));
}

bool MainWidget::selectingPeer() const {
	return _hider ? true : false;
}

void MainWidget::removeDialog(Dialogs::Key key) {
	_dialogs->removeDialog(key);
}

void MainWidget::cacheBackground() {
	if (Window::Theme::Background()->colorForFill()) {
		return;
	} else if (Window::Theme::Background()->tile()) {
		auto &bg = Window::Theme::Background()->pixmapForTiled();

		auto result = QImage(_willCacheFor.width() * cIntRetinaFactor(), _willCacheFor.height() * cIntRetinaFactor(), QImage::Format_RGB32);
		result.setDevicePixelRatio(cRetinaFactor());
		{
			QPainter p(&result);
			auto left = 0;
			auto top = 0;
			auto right = _willCacheFor.width();
			auto bottom = _willCacheFor.height();
			auto w = bg.width() / cRetinaFactor();
			auto h = bg.height() / cRetinaFactor();
			auto sx = 0;
			auto sy = 0;
			auto cx = qCeil(_willCacheFor.width() / w);
			auto cy = qCeil(_willCacheFor.height() / h);
			for (int i = sx; i < cx; ++i) {
				for (int j = sy; j < cy; ++j) {
					p.drawPixmap(QPointF(i * w, j * h), bg);
				}
			}
		}
		_cachedX = 0;
		_cachedY = 0;
		_cachedBackground = App::pixmapFromImageInPlace(std::move(result));
	} else {
		auto &bg = Window::Theme::Background()->pixmap();

		QRect to, from;
		Window::Theme::ComputeBackgroundRects(_willCacheFor, bg.size(), to, from);
		_cachedX = to.x();
		_cachedY = to.y();
		_cachedBackground = App::pixmapFromImageInPlace(bg.toImage().copy(from).scaled(to.width() * cIntRetinaFactor(), to.height() * cIntRetinaFactor(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
		_cachedBackground.setDevicePixelRatio(cRetinaFactor());
	}
	_cachedFor = _willCacheFor;
}

crl::time MainWidget::highlightStartTime(not_null<const HistoryItem*> item) const {
	return _history->highlightStartTime(item);
}

MsgId MainWidget::currentReplyToIdFor(not_null<History*> history) const {
	if (_history->history() == history) {
		return _history->replyToId();
	} else if (const auto localDraft = history->localDraft()) {
		return localDraft->msgId;
	}
	return 0;
}

void MainWidget::sendBotCommand(PeerData *peer, UserData *bot, const QString &cmd, MsgId replyTo) {
	_history->sendBotCommand(peer, bot, cmd, replyTo);
}

void MainWidget::hideSingleUseKeyboard(PeerData *peer, MsgId replyTo) {
	_history->hideSingleUseKeyboard(peer, replyTo);
}

void MainWidget::app_sendBotCallback(
		not_null<const HistoryMessageMarkupButton*> button,
		not_null<const HistoryItem*> msg,
		int row,
		int column) {
	_history->app_sendBotCallback(button, msg, row, column);
}

bool MainWidget::insertBotCommand(const QString &cmd) {
	return _history->insertBotCommand(cmd);
}

void MainWidget::searchMessages(const QString &query, Dialogs::Key inChat) {
	_dialogs->searchMessages(query, inChat);
	if (Adaptive::OneColumn()) {
		Ui::showChatsList();
	} else {
		_dialogs->setInnerFocus();
	}
}

void MainWidget::itemEdited(not_null<HistoryItem*> item) {
	if (_history->peer() == item->history()->peer
		|| (_history->peer() && _history->peer() == item->history()->peer->migrateTo())) {
		_history->itemEdited(item);
	}
}

void MainWidget::checkLastUpdate(bool afterSleep) {
	auto n = crl::now();
	if (_lastUpdateTime && n > _lastUpdateTime + (afterSleep ? kNoUpdatesAfterSleepTimeout : kNoUpdatesTimeout)) {
		_lastUpdateTime = n;
		MTP::ping();
	}
}

void MainWidget::handleAudioUpdate(const Media::Player::TrackState &state) {
	using State = Media::Player::State;
	const auto document = state.id.audio();
	if (!Media::Player::IsStoppedOrStopping(state.state)) {
		createPlayer();
	} else if (state.state == State::StoppedAtStart) {
		closeBothPlayers();
	}

	if (const auto item = session().data().message(state.id.contextId())) {
		session().data().requestItemRepaint(item);
	}
	if (const auto items = InlineBots::Layout::documentItems()) {
		if (const auto i = items->find(document); i != items->end()) {
			for (const auto item : i->second) {
				item->update();
			}
		}
	}
}

void MainWidget::closeBothPlayers() {
	if (_player) {
		_player->hide(anim::type::normal);
	}
	_playerVolume.destroyDelayed();

	_playerPlaylist->hideIgnoringEnterEvents();
	Media::Player::instance()->stop(AudioMsgId::Type::Voice);
	Media::Player::instance()->stop(AudioMsgId::Type::Song);

	Shortcuts::ToggleMediaShortcuts(false);
}

void MainWidget::createPlayer() {
	if (!_player) {
		_player.create(this, object_ptr<Media::Player::Widget>(this));
		rpl::merge(
			_player->heightValue() | rpl::map([] { return true; }),
			_player->shownValue()
		) | rpl::start_with_next(
			[this] { playerHeightUpdated(); },
			_player->lifetime());
		_player->entity()->setCloseCallback([this] { closeBothPlayers(); });
		_playerVolume.create(this);
		_player->entity()->volumeWidgetCreated(_playerVolume);
		orderWidgets();
		if (_a_show.animating()) {
			_player->show(anim::type::instant);
			_player->setVisible(false);
			Shortcuts::ToggleMediaShortcuts(true);
		} else {
			_player->hide(anim::type::instant);
		}
	}
	if (_player && !_player->toggled()) {
		if (!_a_show.animating()) {
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
			_playerVolume.destroyDelayed();
			_player.destroyDelayed();
		}
	}
}

void MainWidget::setCurrentCall(Calls::Call *call) {
	_currentCall = call;
	if (_currentCall) {
		subscribe(_currentCall->stateChanged(), [this](Calls::Call::State state) {
			using State = Calls::Call::State;
			if (state == State::Established) {
				createCallTopBar();
			} else {
				destroyCallTopBar();
			}
		});
	} else {
		destroyCallTopBar();
	}
}

void MainWidget::createCallTopBar() {
	Expects(_currentCall != nullptr);

	_callTopBar.create(this, object_ptr<Calls::TopBar>(this, _currentCall));
	_callTopBar->heightValue(
	) | rpl::start_with_next([this](int value) {
		callTopBarHeightUpdated(value);
	}, lifetime());
	orderWidgets();
	if (_a_show.animating()) {
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
	if (!callTopBarHeight && !_currentCall) {
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
		}, _currentExportView->lifetime());
	} else {
		LOG(("Export Info: Destroy top bar by controller removal."));
		destroyExportTopBar();
	}
}

void MainWidget::createExportTopBar(Export::View::Content &&data) {
	_exportTopBar.create(
		this,
		object_ptr<Export::View::TopBar>(this, std::move(data)));
	_exportTopBar->entity()->clicks(
	) | rpl::start_with_next([=] {
		if (_currentExportView) {
			_currentExportView->activatePanel();
		}
	}, _exportTopBar->lifetime());
	orderWidgets();
	if (_a_show.animating()) {
		_exportTopBar->show(anim::type::instant);
		_exportTopBar->setVisible(false);
	} else {
		_exportTopBar->hide(anim::type::instant);
		_exportTopBar->show(anim::type::normal);
		_exportTopBarHeight = _contentScrollAddToY = _exportTopBar->contentHeight();
		updateControlsGeometry();
	}
	rpl::merge(
		_exportTopBar->heightValue() | rpl::map([] { return true; }),
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

void MainWidget::documentLoadProgress(FileLoader *loader) {
	if (const auto documentId = loader ? loader->objId() : 0) {
		documentLoadProgress(session().data().document(documentId));
	}
}

void MainWidget::documentLoadProgress(DocumentData *document) {
	session().data().requestDocumentViewRepaint(document);
	session().documentUpdated.notify(document, true);

	if (!document->loaded() && document->isAudioFile()) {
		Media::Player::instance()->documentLoadProgress(document);
	}
}

void MainWidget::documentLoadFailed(FileLoader *loader, bool started) {
	const auto documentId = loader ? loader->objId() : 0;
	if (!documentId) return;

	const auto document = session().data().document(documentId);
	if (started) {
		const auto origin = loader->fileOrigin();
		const auto failedFileName = loader->fileName();
		Ui::show(Box<ConfirmBox>(tr::lng_download_finish_failed(tr::now), crl::guard(this, [=] {
			Ui::hideLayer();
			if (document) {
				document->save(origin, failedFileName);
			}
		})));
	} else {
		// Sometimes we have LOCATION_INVALID error in documents / stickers.
		// Sometimes FILE_REFERENCE_EXPIRED could not be handled.
		//
		//Ui::show(Box<ConfirmBox>(tr::lng_download_path_failed(tr::now), tr::lng_download_path_settings(tr::now), crl::guard(this, [=] {
		//	Global::SetDownloadPath(QString());
		//	Global::SetDownloadPathBookmark(QByteArray());
		//	Ui::show(Box<DownloadPathBox>());
		//	Global::RefDownloadPathChanged().notify();
		//})));
	}

	if (document) {
		if (document->loading()) document->cancel();
		document->status = FileDownloadFailed;
	}
}

void MainWidget::inlineResultLoadProgress(FileLoader *loader) {
	//InlineBots::Result *result = InlineBots::resultFromLoader(loader);
	//if (!result) return;

	//result->loaded();

	//Ui::repaintInlineItem();
}

void MainWidget::inlineResultLoadFailed(FileLoader *loader, bool started) {
	//InlineBots::Result *result = InlineBots::resultFromLoader(loader);
	//if (!result) return;

	//result->loaded();

	//Ui::repaintInlineItem();
}

void MainWidget::onSendFileConfirm(
		const std::shared_ptr<FileLoadResult> &file,
		const std::optional<FullMsgId> &oldId) {
	_history->sendFileConfirmed(file, oldId);
}

bool MainWidget::onSendSticker(DocumentData *document) {
	return _history->sendExistingDocument(document);
}

void MainWidget::dialogsCancelled() {
	if (_hider) {
		_hider->startHide();
		clearHider(_hider);
	}
	_history->activate();
}

bool MainWidget::isIdle() const {
	return _isIdle;
}

void MainWidget::clearCachedBackground() {
	_cachedBackground = QPixmap();
	_cacheBackgroundTimer.cancel();
	update();
}

QPixmap MainWidget::cachedBackground(const QRect &forRect, int &x, int &y) {
	if (!_cachedBackground.isNull() && forRect == _cachedFor) {
		x = _cachedX;
		y = _cachedY;
		return _cachedBackground;
	}
	if (_willCacheFor != forRect || !_cacheBackgroundTimer.isActive()) {
		_willCacheFor = forRect;
		_cacheBackgroundTimer.callOnce(kCacheBackgroundTimeout);
	}
	return QPixmap();
}

void MainWidget::updateScrollColors() {
	_history->updateScrollColors();
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
	_background->data.loadDocument();
	checkChatBackground();

	const auto tile = Data::IsLegacy1DefaultWallPaper(background);
	using Update = Window::Theme::BackgroundUpdate;
	Window::Theme::Background()->notify(Update(Update::Type::Start, tile));
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
		&& background.thumbnail()
		&& background.thumbnail()->loaded()) {
		image = background.thumbnail()->original();
	}

	const auto resetToDefault = image.isNull()
		&& !background.document()
		&& !background.backgroundColor()
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
			return document->progress();
		} else if (const auto thumbnail = _background->data.thumbnail()) {
			return thumbnail->progress();
		}
	}
	return 1.;
}

void MainWidget::checkChatBackground() {
	if (!_background || _background->generating) {
		return;
	}
	const auto document = _background->data.document();
	Assert(document != nullptr);
	if (!document->loaded()) {
		return;
	}

	const auto generateCallback = [=](QImage &&image) {
		const auto background = base::take(_background);
		const auto ready = image.isNull()
			? Data::DefaultWallPaper()
			: background->data;
		setReadyChatBackground(ready, std::move(image));
	};
	_background->generating = Data::ReadImageAsync(
		document,
		Window::Theme::ProcessBackgroundImage,
		generateCallback);
}

Image *MainWidget::newBackgroundThumb() {
	return _background ? _background->data.thumbnail() : nullptr;
}

void MainWidget::messageDataReceived(ChannelData *channel, MsgId msgId) {
	_history->messageDataReceived(channel, msgId);
}

void MainWidget::updateBotKeyboard(History *h) {
	_history->updateBotKeyboard(h);
}

void MainWidget::pushReplyReturn(not_null<HistoryItem*> item) {
	_history->pushReplyReturn(item);
}

void MainWidget::setInnerFocus() {
	if (_hider || !_history->peer()) {
		if (!_hider && _mainSection) {
			_mainSection->setInnerFocus();
		} else if (!_hider && _thirdSection) {
			_thirdSection->setInnerFocus();
		} else {
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

void MainWidget::scheduleViewIncrement(HistoryItem *item) {
	PeerData *peer = item->history()->peer;
	auto i = _viewsIncremented.find(peer);
	if (i != _viewsIncremented.cend()) {
		if (i.value().contains(item->id)) return;
	} else {
		i = _viewsIncremented.insert(peer, ViewsIncrementMap());
	}
	i.value().insert(item->id, true);
	auto j = _viewsToIncrement.find(peer);
	if (j == _viewsToIncrement.cend()) {
		j = _viewsToIncrement.insert(peer, ViewsIncrementMap());
		_viewsIncrementTimer.callOnce(kSendViewsTimeout);
	}
	j.value().insert(item->id, true);
}

void MainWidget::viewsIncrement() {
	for (auto i = _viewsToIncrement.begin(); i != _viewsToIncrement.cend();) {
		if (_viewsIncrementRequests.contains(i.key())) {
			++i;
			continue;
		}

		QVector<MTPint> ids;
		ids.reserve(i.value().size());
		for (ViewsIncrementMap::const_iterator j = i.value().cbegin(), end = i.value().cend(); j != end; ++j) {
			ids.push_back(MTP_int(j.key()));
		}
		auto req = MTP::send(MTPmessages_GetMessagesViews(i.key()->input, MTP_vector<MTPint>(ids), MTP_bool(true)), rpcDone(&MainWidget::viewsIncrementDone, ids), rpcFail(&MainWidget::viewsIncrementFail), 0, 5);
		_viewsIncrementRequests.insert(i.key(), req);
		i = _viewsToIncrement.erase(i);
	}
}

void MainWidget::viewsIncrementDone(QVector<MTPint> ids, const MTPVector<MTPint> &result, mtpRequestId req) {
	auto &v = result.v;
	if (ids.size() == v.size()) {
		for (auto i = _viewsIncrementRequests.begin(); i != _viewsIncrementRequests.cend(); ++i) {
			if (i.value() == req) {
				PeerData *peer = i.key();
				ChannelId channel = peerToChannel(peer->id);
				for (int32 j = 0, l = ids.size(); j < l; ++j) {
					if (HistoryItem *item = session().data().message(channel, ids.at(j).v)) {
						item->setViewsCount(v.at(j).v);
					}
				}
				_viewsIncrementRequests.erase(i);
				break;
			}
		}
	}
	if (!_viewsToIncrement.isEmpty() && !_viewsIncrementTimer.isActive()) {
		_viewsIncrementTimer.callOnce(kSendViewsTimeout);
	}
}

bool MainWidget::viewsIncrementFail(const RPCError &error, mtpRequestId req) {
	if (MTP::isDefaultHandledError(error)) return false;

	for (auto i = _viewsIncrementRequests.begin(); i != _viewsIncrementRequests.cend(); ++i) {
		if (i.value() == req) {
			_viewsIncrementRequests.erase(i);
			break;
		}
	}
	if (!_viewsToIncrement.isEmpty() && !_viewsIncrementTimer.isActive()) {
		_viewsIncrementTimer.callOnce(kSendViewsTimeout);
	}
	return false;
}

void MainWidget::refreshDialog(Dialogs::Key key) {
	_dialogs->refreshDialog(key);
}

void MainWidget::choosePeer(PeerId peerId, MsgId showAtMsgId) {
	if (selectingPeer()) {
		_hider->offerPeer(peerId);
	} else {
		Ui::showPeerHistory(peerId, showAtMsgId);
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

void MainWidget::ui_showPeerHistory(
		PeerId peerId,
		const SectionShow &params,
		MsgId showAtMsgId) {
	if (auto peer = session().data().peerLoaded(peerId)) {
		if (peer->migrateTo()) {
			peer = peer->migrateTo();
			peerId = peer->id;
			if (showAtMsgId > 0) showAtMsgId = -showAtMsgId;
		}
		const auto unavailable = peer->unavailableReason();
		if (!unavailable.isEmpty()) {
			if (params.activation != anim::activation::background) {
				Ui::show(Box<InformBox>(unavailable));
			}
			return;
		}
	}

	_controller->dialogsListFocused().set(false, true);
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
					way = Way::ClearStack;
				}
			}
		}
	}

	const auto wasActivePeer = _controller->activeChatCurrent().peer();
	if (params.activation != anim::activation::background) {
		Ui::hideSettingsAndLayer();
	}
	if (_hider) {
		_hider->startHide();
		_hider.release();
	}

	auto animatedShow = [&] {
		if (_a_show.animating()
			|| Core::App().locked()
			|| (params.animated == anim::type::instant)) {
			return false;
		}
		if (!peerId) {
			if (Adaptive::OneColumn()) {
				return _dialogs->isHidden();
			} else {
				return false;
			}
		}
		if (_history->isHidden()) {
			if (!Adaptive::OneColumn() && way == Way::ClearStack) {
				return false;
			}
			return (_mainSection != nullptr)
				|| (Adaptive::OneColumn() && !_dialogs->isHidden());
		}
		if (back || way == Way::Forward) {
			return true;
		}
		return false;
	};

	auto animationParams = animatedShow() ? prepareHistoryAnimation(peerId) : Window::SectionSlideParams();

	if (!back && (way != Way::ClearStack)) {
		// This may modify the current section, for example remove its contents.
		saveSectionInStack();
	}

	if (_history->peer() && _history->peer()->id != peerId && way != Way::Forward) {
		clearBotStartToken(_history->peer());
	}
	_history->showHistory(peerId, showAtMsgId);

	auto noPeer = !_history->peer();
	auto onlyDialogs = noPeer && Adaptive::OneColumn();
	_mainSection.destroy();

	updateControlsGeometry();

	if (noPeer) {
		_controller->setActiveChatEntry(Dialogs::Key());
	}

	if (onlyDialogs) {
		_history->hide();
		if (!_a_show.animating()) {
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
			if (const auto channel = nowActivePeer->asChannel()) {
				channel->ptsWaitingForShortPoll(
					kWaitForChannelGetDifference);
			}
			_viewsIncremented.remove(nowActivePeer);
		}
		if (Adaptive::OneColumn() && !_dialogs->isHidden()) {
			_dialogs->hide();
		}
		if (!_a_show.animating()) {
			if (!animationParams.oldContentCache.isNull()) {
				_history->showAnimated(
					back
						? Window::SlideDirection::FromLeft
						: Window::SlideDirection::FromRight,
					animationParams);
			} else {
				_history->show();
				crl::on_main(App::wnd(), [] {
					App::wnd()->setInnerFocus();
				});
			}
		}
	}

	if (!_dialogs->isHidden()) {
		if (!back) {
			if (const auto history = _history->history()) {
				_dialogs->scrollToEntry(Dialogs::RowDescriptor(
					history,
					FullMsgId(history->channelId(), showAtMsgId)));
			}
		}
		_dialogs->update();
	}

	floatPlayerCheckVisibility();
}

PeerData *MainWidget::ui_getPeerForMouseAction() {
	return _history->ui_getPeerForMouseAction();
}

PeerData *MainWidget::peer() {
	return _history->peer();
}

void MainWidget::saveSectionInStack() {
	if (_mainSection) {
		if (auto memento = _mainSection->createMemento()) {
			_stack.push_back(std::make_unique<StackItemSection>(
				std::move(memento)));
			_stack.back()->setThirdSectionWeak(_thirdSection.data());
		}
	} else if (const auto history = _history->history()) {
		_stack.push_back(std::make_unique<StackItemHistory>(
			history,
			_history->msgId(),
			_history->replyReturns()));
		_stack.back()->setThirdSectionWeak(_thirdSection.data());
	}
}

void MainWidget::showSection(
		Window::SectionMemento &&memento,
		const SectionShow &params) {
	if (_mainSection && _mainSection->showInternal(
			&memento,
			params)) {
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

	// If the window was not resized, but we've enabled
	// tabbedSelectorSectionEnabled or thirdSectionInfoEnabled
	// we need to update adaptive layout to Adaptive::ThirdColumn().
	updateColumnLayout();

	showNewSection(
		std::move(memento),
		params);
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
	auto sectionTop = getThirdSectionTop();
	result.oldContentCache = _thirdSection->grabForShowAnimation(result);
	floatPlayerShowVisible();
	return result;
}

Window::SectionSlideParams MainWidget::prepareShowAnimation(
		bool willHaveTopBarShadow) {
	Window::SectionSlideParams result;
	result.withTopBarShadow = willHaveTopBarShadow;
	if (selectingPeer() && Adaptive::OneColumn()) {
		result.withTopBarShadow = false;
	} else if (_mainSection) {
		if (!_mainSection->hasTopBarShadow()) {
			result.withTopBarShadow = false;
		}
	} else if (!_history->peer()) {
		result.withTopBarShadow = false;
	}

	floatPlayerHideAll();
	if (_player) {
		_player->hideShadow();
	}
	auto playerVolumeVisible = _playerVolume && !_playerVolume->isHidden();
	if (playerVolumeVisible) {
		_playerVolume->hide();
	}
	auto playerPlaylistVisible = !_playerPlaylist->isHidden();
	if (playerPlaylistVisible) {
		_playerPlaylist->hide();
	}

	auto sectionTop = getMainSectionTop();
	if (selectingPeer() && Adaptive::OneColumn()) {
		result.oldContentCache = Ui::GrabWidget(this, QRect(
			0,
			sectionTop,
			_dialogsWidth,
			height() - sectionTop));
	} else if (_mainSection) {
		result.oldContentCache = _mainSection->grabForShowAnimation(result);
	} else if (!Adaptive::OneColumn() || !_history->isHidden()) {
		result.oldContentCache = _history->grabForShowAnimation(result);
	} else {
		result.oldContentCache = Ui::GrabWidget(this, QRect(
			0,
			sectionTop,
			_dialogsWidth,
			height() - sectionTop));
	}

	if (playerVolumeVisible) {
		_playerVolume->show();
	}
	if (playerPlaylistVisible) {
		_playerPlaylist->show();
	}
	if (_player) {
		_player->showShadow();
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
		Window::SectionMemento &&memento,
		const SectionShow &params) {
	using Column = Window::Column;

	auto saveInStack = (params.way == SectionShow::Way::Forward);
	auto thirdSectionTop = getThirdSectionTop();
	auto newThirdGeometry = QRect(
		width() - st::columnMinimalWidthThird,
		thirdSectionTop,
		st::columnMinimalWidthThird,
		height() - thirdSectionTop);
	auto newThirdSection = (Adaptive::ThreeColumn() && params.thirdColumn)
		? memento.createWidget(
			this,
			_controller,
			Column::Third,
			newThirdGeometry)
		: nullptr;
	if (newThirdSection) {
		saveInStack = false;
	} else if (auto layer = memento.createLayer(_controller, rect())) {
		if (params.activation != anim::activation::background) {
			Ui::hideLayer(anim::type::instant);
		}
		_controller->showSpecialLayer(std::move(layer));
		return;
	}

	if (params.activation != anim::activation::background) {
		Ui::hideSettingsAndLayer();
	}

	QPixmap animCache;

	_controller->dialogsListFocused().set(false, true);
	_a_dialogsWidth.stop();

	auto mainSectionTop = getMainSectionTop();
	auto newMainGeometry = QRect(
		_history->x(),
		mainSectionTop,
		_history->width(),
		height() - mainSectionTop);
	auto newMainSection = newThirdSection
		? nullptr
		: memento.createWidget(
			this,
			_controller,
			Adaptive::OneColumn() ? Column::First : Column::Second,
			newMainGeometry);
	Assert(newMainSection || newThirdSection);

	auto animatedShow = [&] {
		if (_a_show.animating()
			|| Core::App().locked()
			|| (params.animated == anim::type::instant)
			|| memento.instant()) {
			return false;
		}
		if (!Adaptive::OneColumn() && params.way == SectionShow::Way::ClearStack) {
			return false;
		} else if (Adaptive::OneColumn()
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
		saveSectionInStack();
	}
	auto &settingSection = newThirdSection
		? _thirdSection
		: _mainSection;
	if (newThirdSection) {
		_thirdSection = std::move(newThirdSection);
		if (!_thirdShadow) {
			_thirdShadow.create(this);
			_thirdShadow->show();
			orderWidgets();
		}
		updateControlsGeometry();
	} else {
		_mainSection = std::move(newMainSection);
		updateControlsGeometry();
		_history->finishAnimating();
		_history->showHistory(0, 0);
		_history->hide();
		if (Adaptive::OneColumn()) _dialogs->hide();
	}

	if (animationParams) {
		auto back = (params.way == SectionShow::Way::Backward);
		auto direction = (back || settingSection->forceAnimateBack())
			? Window::SlideDirection::FromLeft
			: Window::SlideDirection::FromRight;
		if (Adaptive::OneColumn()) {
			_controller->removeLayerBlackout();
		}
		settingSection->showAnimated(direction, animationParams);
	} else {
		settingSection->showFast();
	}

	if (settingSection.data() == _mainSection.data()) {
		if (const auto entry = _mainSection->activeChat(); entry.key) {
			_controller->setActiveChatEntry(entry);
		}
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

bool MainWidget::isMainSectionShown() const {
	return _mainSection || _history->peer();
}

bool MainWidget::isThirdSectionShown() const {
	return _thirdSection != nullptr;
}

bool MainWidget::stackIsEmpty() const {
	return _stack.empty();
}

void MainWidget::showBackFromStack(
		const SectionShow &params) {
	if (selectingPeer()) {
		return;
	}
	if (_stack.empty()) {
		_controller->clearSectionStack(params);
		crl::on_main(App::wnd(), [] {
			App::wnd()->setInnerFocus();
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
		_history->setReplyReturns(historyItem->peer()->id, historyItem->replyReturns);
	} else if (item->type() == SectionStackItem) {
		auto sectionItem = static_cast<StackItemSection*>(item.get());
		showNewSection(
			std::move(*sectionItem->memento()),
			params.withWay(SectionShow::Way::Backward));
	}
	if (_thirdSectionFromStack && _thirdSection) {
		_controller->showSection(
			std::move(*base::take(_thirdSectionFromStack)),
			SectionShow(
				SectionShow::Way::ClearStack,
				anim::type::instant,
				anim::activation::background));

	}
}

void MainWidget::orderWidgets() {
	_dialogs->raise();
	if (_player) {
		_player->raise();
	}
	if (_exportTopBar) {
		_exportTopBar->raise();
	}
	if (_callTopBar) {
		_callTopBar->raise();
	}
	if (_playerVolume) {
		_playerVolume->raise();
	}
	_sideShadow->raise();
	if (_thirdShadow) {
		_thirdShadow->raise();
	}
	if (_firstColumnResizeArea) {
		_firstColumnResizeArea->raise();
	}
	if (_thirdColumnResizeArea) {
		_thirdColumnResizeArea->raise();
	}
	_connecting->raise();
	_playerPlaylist->raise();
	floatPlayerRaiseAll();
	if (_hider) _hider->raise();
}

QRect MainWidget::historyRect() const {
	QRect r(_history->historyRect());
	r.moveLeft(r.left() + _history->x());
	r.moveTop(r.top() + _history->y());
	return r;
}

QPixmap MainWidget::grabForShowAnimation(const Window::SectionSlideParams &params) {
	QPixmap result;
	floatPlayerHideAll();
	if (_player) {
		_player->hideShadow();
	}
	auto playerVolumeVisible = _playerVolume && !_playerVolume->isHidden();
	if (playerVolumeVisible) {
		_playerVolume->hide();
	}
	auto playerPlaylistVisible = !_playerPlaylist->isHidden();
	if (playerPlaylistVisible) {
		_playerPlaylist->hide();
	}

	auto sectionTop = getMainSectionTop();
	if (Adaptive::OneColumn()) {
		result = Ui::GrabWidget(this, QRect(
			0,
			sectionTop,
			_dialogsWidth,
			height() - sectionTop));
	} else {
		_sideShadow->hide();
		if (_thirdShadow) {
			_thirdShadow->hide();
		}
		result = Ui::GrabWidget(this, QRect(
			_dialogsWidth,
			sectionTop,
			width() - _dialogsWidth,
			height() - sectionTop));
		_sideShadow->show();
		if (_thirdShadow) {
			_thirdShadow->show();
		}
	}
	if (playerVolumeVisible) {
		_playerVolume->show();
	}
	if (playerPlaylistVisible) {
		_playerPlaylist->show();
	}
	if (_player) {
		_player->showShadow();
	}
	floatPlayerShowVisible();
	return result;
}

void MainWidget::repaintDialogRow(
		Dialogs::Mode list,
		not_null<Dialogs::Row*> row) {
	_dialogs->repaintDialogRow(list, row);
}

void MainWidget::repaintDialogRow(Dialogs::RowDescriptor row) {
	_dialogs->repaintDialogRow(row);
}

void MainWidget::windowShown() {
	_history->windowShown();
}

void MainWidget::sentUpdatesReceived(uint64 randomId, const MTPUpdates &result) {
	feedUpdates(result, randomId);
}

bool MainWidget::deleteChannelFailed(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	//if (error.type() == qstr("CHANNEL_TOO_LARGE")) {
	//	Ui::show(Box<InformBox>(tr::lng_cant_delete_channel(tr::now)));
	//}

	return true;
}

void MainWidget::historyToDown(History *history) {
	_history->historyToDown(history);
}

void MainWidget::dialogsToUp() {
	_dialogs->jumpToTop();
}

void MainWidget::markActiveHistoryAsRead() {
	if (const auto activeHistory = _history->history()) {
		session().api().readServerHistory(activeHistory);
	}
}

void MainWidget::showAnimated(const QPixmap &bgAnimCache, bool back) {
	_showBack = back;
	(_showBack ? _cacheOver : _cacheUnder) = bgAnimCache;

	_a_show.stop();

	showAll();
	(_showBack ? _cacheUnder : _cacheOver) = Ui::GrabWidget(this);
	hideAll();

	_a_show.start(
		[this] { animationCallback(); },
		0.,
		1.,
		st::slideDuration,
		Window::SlideAnimation::transition());

	show();
}

void MainWidget::animationCallback() {
	update();
	if (!_a_show.animating()) {
		_cacheUnder = _cacheOver = QPixmap();

		showAll();
		activate();
	}
}

void MainWidget::paintEvent(QPaintEvent *e) {
	if (_background) {
		checkChatBackground();
	}

	Painter p(this);
	auto progress = _a_show.value(1.);
	if (_a_show.animating()) {
		auto coordUnder = _showBack ? anim::interpolate(-st::slideShift, 0, progress) : anim::interpolate(0, -st::slideShift, progress);
		auto coordOver = _showBack ? anim::interpolate(0, width(), progress) : anim::interpolate(width(), 0, progress);
		auto shadow = _showBack ? (1. - progress) : progress;
		if (coordOver > 0) {
			p.drawPixmap(QRect(0, 0, coordOver, height()), _cacheUnder, QRect(-coordUnder * cRetinaFactor(), 0, coordOver * cRetinaFactor(), height() * cRetinaFactor()));
			p.setOpacity(shadow);
			p.fillRect(0, 0, coordOver, height(), st::slideFadeOutBg);
			p.setOpacity(1);
		}
		p.drawPixmap(coordOver, 0, _cacheOver);
		p.setOpacity(shadow);
		st::slideShadow.fill(p, QRect(coordOver - st::slideShadow.width(), 0, st::slideShadow.width(), height()));
	}
}

int MainWidget::getMainSectionTop() const {
	return _callTopBarHeight + _exportTopBarHeight + _playerHeight;
}

int MainWidget::getThirdSectionTop() const {
	return 0;
}

void MainWidget::hideAll() {
	_dialogs->hide();
	_history->hide();
	if (_mainSection) {
		_mainSection->hide();
	}
	if (_thirdSection) {
		_thirdSection->hide();
	}
	_sideShadow->hide();
	if (_thirdShadow) {
		_thirdShadow->hide();
	}
	if (_player) {
		_player->setVisible(false);
		_playerHeight = 0;
	}
	floatPlayerHideAll();
}

void MainWidget::showAll() {
	if (cPasswordRecovered()) {
		cSetPasswordRecovered(false);
		Ui::show(Box<InformBox>(tr::lng_signin_password_removed(tr::now)));
	}
	if (Adaptive::OneColumn()) {
		_sideShadow->hide();
		if (_hider) {
			_hider->hide();
		}
		if (selectingPeer()) {
			_dialogs->showFast();
			_history->hide();
			if (_mainSection) _mainSection->hide();
		} else if (_mainSection) {
			_mainSection->show();
		} else if (_history->peer()) {
			_history->show();
			_history->updateControlsGeometry();
		} else {
			_dialogs->showFast();
			_history->hide();
		}
		if (!selectingPeer()) {
			if (_mainSection) {
				_dialogs->hide();
			} else if (isMainSectionShown()) {
				_dialogs->hide();
			}
		}
	} else {
		_sideShadow->show();
		if (_hider) {
			_hider->show();
		}
		_dialogs->showFast();
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
	updateControlsGeometry();
	floatPlayerCheckVisibility();
	floatPlayerShowVisible();

	App::wnd()->checkHistoryActivation();
}

void MainWidget::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void MainWidget::updateControlsGeometry() {
	updateWindowAdaptiveLayout();
	if (session().settings().dialogsWidthRatio() > 0) {
		_a_dialogsWidth.stop();
	}
	if (!_a_dialogsWidth.animating()) {
		_dialogs->stopWidthAnimation();
	}
	if (Adaptive::ThreeColumn()) {
		if (!_thirdSection
			&& !_controller->takeThirdSectionFromLayer()) {
			auto params = Window::SectionShow(
				Window::SectionShow::Way::ClearStack,
				anim::type::instant,
				anim::activation::background);
			if (session().settings().tabbedSelectorSectionEnabled()) {
				_history->pushTabbedSelectorToThirdSection(params);
			} else if (session().settings().thirdSectionInfoEnabled()) {
				const auto active = _controller->activeChatCurrent();
				if (const auto peer = active.peer()) {
					_controller->showSection(
						Info::Memento::Default(peer),
						params.withThirdColumn());
				}
			}
		}
	} else {
		_thirdSection.destroy();
		_thirdShadow.destroy();
	}
	auto mainSectionTop = getMainSectionTop();
	auto dialogsWidth = qRound(_a_dialogsWidth.value(_dialogsWidth));
	if (Adaptive::OneColumn()) {
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
		auto mainSectionGeometry = QRect(
			0,
			mainSectionTop,
			dialogsWidth,
			height() - mainSectionTop);
		_dialogs->setGeometry(mainSectionGeometry);
		_history->setGeometry(mainSectionGeometry);
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
		accumulate_min(dialogsWidth, width() - st::columnMinimalWidthMain);
		auto mainSectionWidth = width() - dialogsWidth - thirdSectionWidth;

		_dialogs->setGeometryToLeft(0, 0, dialogsWidth, height());
		_sideShadow->setGeometryToLeft(dialogsWidth, 0, st::lineWidth, height());
		if (_thirdShadow) {
			_thirdShadow->setGeometryToLeft(
				width() - thirdSectionWidth - st::lineWidth,
				0,
				st::lineWidth,
				height());
		}
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
		_history->setGeometryToLeft(dialogsWidth, mainSectionTop, mainSectionWidth, height() - mainSectionTop);
		if (_hider) {
			_hider->setGeometryToLeft(dialogsWidth, 0, mainSectionWidth, height());
		}
	}
	if (_mainSection) {
		auto mainSectionGeometry = QRect(_history->x(), mainSectionTop, _history->width(), height() - mainSectionTop);
		_mainSection->setGeometryWithTopMoved(mainSectionGeometry, _contentScrollAddToY);
	}
	refreshResizeAreas();
	updateMediaPlayerPosition();
	updateMediaPlaylistPosition(_playerPlaylist->x());
	_contentScrollAddToY = 0;

	floatPlayerUpdatePositions();
}

void MainWidget::refreshResizeAreas() {
	if (!Adaptive::OneColumn()) {
		ensureFirstColumnResizeAreaCreated();
		_firstColumnResizeArea->setGeometryToLeft(
			_history->x(),
			0,
			st::historyResizeWidth,
			height());
	} else if (_firstColumnResizeArea) {
		_firstColumnResizeArea.destroy();
	}

	if (Adaptive::ThreeColumn() && _thirdSection) {
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
	if (_firstColumnResizeArea) {
		return;
	}
	auto moveLeftCallback = [=](int globalLeft) {
		auto newWidth = globalLeft - mapToGlobal(QPoint(0, 0)).x();
		auto newRatio = (newWidth < st::columnMinimalWidthLeft / 2)
			? 0.
			: float64(newWidth) / width();
		session().settings().setDialogsWidthRatio(newRatio);
	};
	auto moveFinishedCallback = [=] {
		if (Adaptive::OneColumn()) {
			return;
		}
		if (session().settings().dialogsWidthRatio() > 0) {
			session().settings().setDialogsWidthRatio(
				float64(_dialogsWidth) / width());
		}
		Local::writeUserSettings();
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
		session().settings().setThirdColumnWidth(newWidth);
	};
	auto moveFinishedCallback = [=] {
		if (!Adaptive::ThreeColumn() || !_thirdSection) {
			return;
		}
		session().settings().setThirdColumnWidth(snap(
			session().settings().thirdColumnWidth(),
			st::columnMinimalWidthThird,
			st::columnMaximalWidthThird));
		Local::writeUserSettings();
	};
	createResizeArea(
		_thirdColumnResizeArea,
		std::move(moveLeftCallback),
		std::move(moveFinishedCallback));
}

void MainWidget::updateDialogsWidthAnimated() {
	if (session().settings().dialogsWidthRatio() > 0) {
		return;
	}
	auto dialogsWidth = _dialogsWidth;
	updateWindowAdaptiveLayout();
	if (!session().settings().dialogsWidthRatio()
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
-> std::unique_ptr<Window::SectionMemento> {
	if (_thirdSectionFromStack) {
		return std::move(_thirdSectionFromStack);
	} else if (const auto peer = key.peer()) {
		return std::make_unique<Info::Memento>(
			peer->id,
			Info::Memento::DefaultSection(peer));
	//} else if (const auto feed = key.feed()) { // #feed
	//	return std::make_unique<Info::Memento>(
	//		feed,
	//		Info::Memento::DefaultSection(key));
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
	auto params = Window::SectionShow(
		Window::SectionShow::Way::ClearStack,
		anim::type::instant,
		anim::activation::background);
	auto switchInfoFast = [&] {
		saveOldThirdSection();

		//
		// Like in _controller->showPeerInfo()
		//
		if (Adaptive::ThreeColumn()
			&& !session().settings().thirdSectionInfoEnabled()) {
			session().settings().setThirdSectionInfoEnabled(true);
			session().saveSettingsDelayed();
		}

		_controller->showSection(
			std::move(*thirdSectionForCurrentMainSection(key)),
			params.withThirdColumn());
	};
	auto switchTabbedFast = [&] {
		saveOldThirdSection();
		_history->pushTabbedSelectorToThirdSection(params);
	};
	if (Adaptive::ThreeColumn()
		&& session().settings().tabbedSelectorSectionEnabled()
		&& key) {
		if (!canWrite) {
			switchInfoFast();
			session().settings().setTabbedSelectorSectionEnabled(true);
			session().settings().setTabbedReplacedWithInfo(true);
		} else if (session().settings().tabbedReplacedWithInfo()) {
			session().settings().setTabbedReplacedWithInfo(false);
			switchTabbedFast();
		}
	} else {
		session().settings().setTabbedReplacedWithInfo(false);
		if (!key) {
			if (_thirdSection) {
				_thirdSection.destroy();
				_thirdShadow.destroy();
				updateControlsGeometry();
			}
		} else if (Adaptive::ThreeColumn()
			&& session().settings().thirdSectionInfoEnabled()) {
			switchInfoFast();
		}
	}
}

void MainWidget::updateMediaPlayerPosition() {
	if (_player && _playerVolume) {
		auto relativePosition = _player->entity()->getPositionForVolumeWidget();
		auto playerMargins = _playerVolume->getMargin();
		_playerVolume->moveToLeft(_player->x() + relativePosition.x() - playerMargins.left(), _player->y() + relativePosition.y() - playerMargins.top());
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

int MainWidget::contentScrollAddToY() const {
	return _contentScrollAddToY;
}

void MainWidget::returnTabbedSelector() {
	if (!_mainSection || !_mainSection->returnTabbedSelector()) {
		_history->returnTabbedSelector();
	}
}

void MainWidget::keyPressEvent(QKeyEvent *e) {
}

bool MainWidget::eventFilter(QObject *o, QEvent *e) {
	if (e->type() == QEvent::FocusIn) {
		if (const auto widget = qobject_cast<QWidget*>(o)) {
			if (_history == widget || _history->isAncestorOf(widget)
				|| (_mainSection && (_mainSection == widget || _mainSection->isAncestorOf(widget)))
				|| (_thirdSection && (_thirdSection == widget || _thirdSection->isAncestorOf(widget)))) {
				_controller->dialogsListFocused().set(false);
			} else if (_dialogs == widget || _dialogs->isAncestorOf(widget)) {
				_controller->dialogsListFocused().set(true);
			}
		}
	} else if (e->type() == QEvent::MouseButtonPress) {
		if (static_cast<QMouseEvent*>(e)->button() == Qt::BackButton) {
			if (!Core::App().hideMediaView()) {
				handleHistoryBack();
			}
			return true;
		}
	} else if (e->type() == QEvent::Wheel) {
		if (const auto result = floatPlayerFilterWheelEvent(o, e)) {
			return *result;
		}
	}
	return RpWidget::eventFilter(o, e);
}

void MainWidget::handleAdaptiveLayoutUpdate() {
	showAll();
	_sideShadow->setVisible(!Adaptive::OneColumn());
	if (_player) {
		_player->updateAdaptiveLayout();
	}
}

void MainWidget::handleHistoryBack() {
	const auto historyFromFolder = _history->history()
		? _history->history()->folder()
		: nullptr;
	const auto openedFolder = _controller->openedFolder().current();
	if (!openedFolder
		|| historyFromFolder == openedFolder
		|| _dialogs->isHidden()) {
		_controller->showBackFromStack();
		_dialogs->setInnerFocus();
	} else {
		_controller->closeFolder();
	}
}

void MainWidget::updateWindowAdaptiveLayout() {
	auto layout = _controller->computeColumnLayout();
	auto dialogsWidthRatio = session().settings().dialogsWidthRatio();

	// Check if we are in a single-column layout in a wide enough window
	// for the normal layout. If so, switch to the normal layout.
	if (layout.windowLayout == Adaptive::WindowLayout::OneColumn) {
		auto chatWidth = layout.chatWidth;
		//if (session().settings().tabbedSelectorSectionEnabled()
		//	&& chatWidth >= _history->minimalWidthForTabbedSelectorSection()) {
		//	chatWidth -= _history->tabbedSelectorSectionWidth();
		//}
		auto minimalNormalWidth = st::columnMinimalWidthLeft
			+ st::columnMinimalWidthMain;
		if (chatWidth >= minimalNormalWidth) {
			// Switch layout back to normal in a wide enough window.
			layout.windowLayout = Adaptive::WindowLayout::Normal;
			layout.dialogsWidth = st::columnMinimalWidthLeft;
			layout.chatWidth = layout.bodyWidth - layout.dialogsWidth;
			dialogsWidthRatio = float64(layout.dialogsWidth) / layout.bodyWidth;
		}
	}

	// Check if we are going to create the third column and shrink the
	// dialogs widget to provide a wide enough chat history column.
	// Don't shrink the column on the first call, when window is inited.
	if (layout.windowLayout == Adaptive::WindowLayout::ThreeColumn
		&& _started && _controller->window()->positionInited()) {
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

	session().settings().setDialogsWidthRatio(dialogsWidthRatio);

	auto useSmallColumnWidth = !Adaptive::OneColumn()
		&& !dialogsWidthRatio
		&& !_controller->forceWideDialogs();
	_dialogsWidth = useSmallColumnWidth
		? _controller->dialogsSmallColumnWidth()
		: layout.dialogsWidth;
	_thirdColumnWidth = layout.thirdWidth;
	if (layout.windowLayout != Global::AdaptiveWindowLayout()) {
		Global::SetAdaptiveWindowLayout(layout.windowLayout);
		Adaptive::Changed().notify(true);
	}
}

int MainWidget::backgroundFromY() const {
	return -getMainSectionTop();
}

void MainWidget::searchInChat(Dialogs::Key chat) {
	if (_controller->openedFolder().current()) {
		_controller->closeFolder();
	}
	_dialogs->searchInChat(chat);
	if (Adaptive::OneColumn()) {
		Ui::showChatsList();
	} else {
		_dialogs->setInnerFocus();
	}
}

void MainWidget::feedUpdateVector(
		const MTPVector<MTPUpdate> &updates,
		bool skipMessageIds) {
	for (const auto &update : updates.v) {
		if (skipMessageIds && update.type() == mtpc_updateMessageID) {
			continue;
		}
		feedUpdate(update);
	}
	session().data().sendHistoryChangeNotifications();
}

void MainWidget::feedMessageIds(const MTPVector<MTPUpdate> &updates) {
	for (const auto &update : updates.v) {
		if (update.type() == mtpc_updateMessageID) {
			feedUpdate(update);
		}
	}
}

void MainWidget::updSetState(int32 pts, int32 date, int32 qts, int32 seq) {
	if (pts) {
		_ptsWaiter.init(pts);
	}
	if (updDate < date && !_byMinChannelTimer.isActive()) {
		updDate = date;
	}
	if (qts && updQts < qts) {
		updQts = qts;
	}
	if (seq && seq != updSeq) {
		updSeq = seq;
		if (_bySeqTimer.isActive()) {
			_bySeqTimer.cancel();
		}
		for (QMap<int32, MTPUpdates>::iterator i = _bySeqUpdates.begin(); i != _bySeqUpdates.end();) {
			int32 s = i.key();
			if (s <= seq + 1) {
				MTPUpdates v = i.value();
				i = _bySeqUpdates.erase(i);
				if (s == seq + 1) {
					return feedUpdates(v);
				}
			} else {
				if (!_bySeqTimer.isActive()) {
					_bySeqTimer.callOnce(PtsWaiter::kWaitForSkippedTimeout);
				}
				break;
			}
		}
	}
}

void MainWidget::gotChannelDifference(
		ChannelData *channel,
		const MTPupdates_ChannelDifference &difference) {
	_channelFailDifferenceTimeout.remove(channel);

	const auto timeout = difference.match([&](const auto &data) {
		return data.vtimeout().value_or_empty();
	});
	const auto isFinal = difference.match([&](const auto &data) {
		return data.is_final();
	});
	difference.match([&](const MTPDupdates_channelDifferenceEmpty &data) {
		channel->ptsInit(data.vpts().v);
	}, [&](const MTPDupdates_channelDifferenceTooLong &data) {
		session().data().processUsers(data.vusers());
		session().data().processChats(data.vchats());
		const auto history = session().data().historyLoaded(channel->id);
		if (history) {
			history->setNotLoadedAtBottom();
			session().api().requestChannelRangeDifference(history);
		}
		data.vdialog().match([&](const MTPDdialog &data) {
			if (const auto pts = data.vpts()) {
				channel->ptsInit(pts->v);
			}
		}, [&](const MTPDdialogFolder &) {
		});
		session().data().applyDialogs(
			nullptr,
			data.vmessages().v,
			QVector<MTPDialog>(1, data.vdialog()));
		if (_history->peer() == channel) {
			_history->updateHistoryDownVisibility();
			_history->preloadHistoryIfNeeded();
		}
	}, [&](const MTPDupdates_channelDifference &data) {
		feedChannelDifference(data);
		channel->ptsInit(data.vpts().v);
	});

	channel->ptsSetRequesting(false);

	if (!isFinal) {
		MTP_LOG(0, ("getChannelDifference { good - after not final channelDifference was received }%1").arg(cTestMode() ? " TESTMODE" : ""));
		getChannelDifference(channel);
	} else if (_controller->activeChatCurrent().peer() == channel) {
		channel->ptsWaitingForShortPoll(timeout
			? (timeout * crl::time(1000))
			: kWaitForChannelGetDifference);
	}
}

void MainWidget::feedChannelDifference(
		const MTPDupdates_channelDifference &data) {
	session().data().processUsers(data.vusers());
	session().data().processChats(data.vchats());

	_handlingChannelDifference = true;
	feedMessageIds(data.vother_updates());
	session().data().processMessages(
		data.vnew_messages(),
		NewMessageType::Unread);
	feedUpdateVector(data.vother_updates(), true);
	_handlingChannelDifference = false;
}

bool MainWidget::failChannelDifference(ChannelData *channel, const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	LOG(("RPC Error in getChannelDifference: %1 %2: %3").arg(error.code()).arg(error.type()).arg(error.description()));
	failDifferenceStartTimerFor(channel);
	return true;
}

void MainWidget::gotState(const MTPupdates_State &state) {
	auto &d = state.c_updates_state();
	updSetState(d.vpts().v, d.vdate().v, d.vqts().v, d.vseq().v);

	_lastUpdateTime = crl::now();
	_noUpdatesTimer.callOnce(kNoUpdatesTimeout);
	_ptsWaiter.setRequesting(false);

	session().api().requestDialogs();
	updateOnline();
}

void MainWidget::gotDifference(const MTPupdates_Difference &difference) {
	_failDifferenceTimeout = 1;

	switch (difference.type()) {
	case mtpc_updates_differenceEmpty: {
		auto &d = difference.c_updates_differenceEmpty();
		updSetState(_ptsWaiter.current(), d.vdate().v, updQts, d.vseq().v);

		_lastUpdateTime = crl::now();
		_noUpdatesTimer.callOnce(kNoUpdatesTimeout);

		_ptsWaiter.setRequesting(false);
	} break;
	case mtpc_updates_differenceSlice: {
		auto &d = difference.c_updates_differenceSlice();
		feedDifference(d.vusers(), d.vchats(), d.vnew_messages(), d.vother_updates());

		auto &s = d.vintermediate_state().c_updates_state();
		updSetState(s.vpts().v, s.vdate().v, s.vqts().v, s.vseq().v);

		_ptsWaiter.setRequesting(false);

		MTP_LOG(0, ("getDifference { good - after a slice of difference was received }%1").arg(cTestMode() ? " TESTMODE" : ""));
		getDifference();
	} break;
	case mtpc_updates_difference: {
		auto &d = difference.c_updates_difference();
		feedDifference(d.vusers(), d.vchats(), d.vnew_messages(), d.vother_updates());

		gotState(d.vstate());
	} break;
	case mtpc_updates_differenceTooLong: {
		auto &d = difference.c_updates_differenceTooLong();
		LOG(("API Error: updates.differenceTooLong is not supported by Telegram Desktop!"));
	} break;
	};
}

bool MainWidget::getDifferenceTimeChanged(ChannelData *channel, int32 ms, ChannelGetDifferenceTime &channelCurTime, crl::time &curTime) {
	if (channel) {
		if (ms <= 0) {
			ChannelGetDifferenceTime::iterator i = channelCurTime.find(channel);
			if (i != channelCurTime.cend()) {
				channelCurTime.erase(i);
			} else {
				return false;
			}
		} else {
			auto when = crl::now() + ms;
			ChannelGetDifferenceTime::iterator i = channelCurTime.find(channel);
			if (i != channelCurTime.cend()) {
				if (i.value() > when) {
					i.value() = when;
				} else {
					return false;
				}
			} else {
				channelCurTime.insert(channel, when);
			}
		}
	} else {
		if (ms <= 0) {
			if (curTime) {
				curTime = 0;
			} else {
				return false;
			}
		} else {
			auto when = crl::now() + ms;
			if (!curTime || curTime > when) {
				curTime = when;
			} else {
				return false;
			}
		}
	}
	return true;
}

void MainWidget::ptsWaiterStartTimerFor(ChannelData *channel, int32 ms) {
	if (getDifferenceTimeChanged(channel, ms, _channelGetDifferenceTimeByPts, _getDifferenceTimeByPts)) {
		getDifferenceByPts();
	}
}

void MainWidget::failDifferenceStartTimerFor(ChannelData *channel) {
	auto &timeout = [&]() -> int32& {
		if (!channel) {
			return _failDifferenceTimeout;
		}
		const auto i = _channelFailDifferenceTimeout.find(channel);
		return (i == _channelFailDifferenceTimeout.end())
			? _channelFailDifferenceTimeout.insert(channel, 1).value()
			: i.value();
	}();
	if (getDifferenceTimeChanged(channel, timeout * 1000, _channelGetDifferenceTimeAfterFail, _getDifferenceTimeAfterFail)) {
		getDifferenceAfterFail();
	}
	if (timeout < 64) timeout *= 2;
}

bool MainWidget::ptsUpdateAndApply(int32 pts, int32 ptsCount, const MTPUpdates &updates) {
	return _ptsWaiter.updateAndApply(nullptr, pts, ptsCount, updates);
}

bool MainWidget::ptsUpdateAndApply(int32 pts, int32 ptsCount, const MTPUpdate &update) {
	return _ptsWaiter.updateAndApply(nullptr, pts, ptsCount, update);
}

bool MainWidget::ptsUpdateAndApply(int32 pts, int32 ptsCount) {
	return _ptsWaiter.updateAndApply(nullptr, pts, ptsCount);
}

void MainWidget::feedDifference(
		const MTPVector<MTPUser> &users,
		const MTPVector<MTPChat> &chats,
		const MTPVector<MTPMessage> &msgs,
		const MTPVector<MTPUpdate> &other) {
	session().checkAutoLock();
	session().data().processUsers(users);
	session().data().processChats(chats);
	feedMessageIds(other);
	session().data().processMessages(msgs, NewMessageType::Unread);
	feedUpdateVector(other, true);
}

bool MainWidget::failDifference(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	LOG(("RPC Error in getDifference: %1 %2: %3").arg(error.code()).arg(error.type()).arg(error.description()));
	failDifferenceStartTimerFor(nullptr);
	return true;
}

void MainWidget::getDifferenceByPts() {
	auto now = crl::now(), wait = crl::time(0);
	if (_getDifferenceTimeByPts) {
		if (_getDifferenceTimeByPts > now) {
			wait = _getDifferenceTimeByPts - now;
		} else {
			getDifference();
		}
	}
	for (ChannelGetDifferenceTime::iterator i = _channelGetDifferenceTimeByPts.begin(); i != _channelGetDifferenceTimeByPts.cend();) {
		if (i.value() > now) {
			wait = wait ? qMin(wait, i.value() - now) : (i.value() - now);
			++i;
		} else {
			getChannelDifference(i.key(), ChannelDifferenceRequest::PtsGapOrShortPoll);
			i = _channelGetDifferenceTimeByPts.erase(i);
		}
	}
	if (wait) {
		_byPtsTimer.callOnce(wait);
	} else {
		_byPtsTimer.cancel();
	}
}

void MainWidget::getDifferenceAfterFail() {
	auto now = crl::now(), wait = crl::time(0);
	if (_getDifferenceTimeAfterFail) {
		if (_getDifferenceTimeAfterFail > now) {
			wait = _getDifferenceTimeAfterFail - now;
		} else {
			_ptsWaiter.setRequesting(false);
			MTP_LOG(0, ("getDifference { force - after get difference failed }%1").arg(cTestMode() ? " TESTMODE" : ""));
			getDifference();
		}
	}
	for (auto i = _channelGetDifferenceTimeAfterFail.begin(); i != _channelGetDifferenceTimeAfterFail.cend();) {
		if (i.value() > now) {
			wait = wait ? qMin(wait, i.value() - now) : (i.value() - now);
			++i;
		} else {
			getChannelDifference(i.key(), ChannelDifferenceRequest::AfterFail);
			i = _channelGetDifferenceTimeAfterFail.erase(i);
		}
	}
	if (wait) {
		_failDifferenceTimer.callOnce(wait);
	} else {
		_failDifferenceTimer.cancel();
	}
}

void MainWidget::getDifference() {
	if (this != App::main()) return;

	_getDifferenceTimeByPts = 0;

	if (requestingDifference()) return;

	_bySeqUpdates.clear();
	_bySeqTimer.cancel();

	_noUpdatesTimer.cancel();
	_getDifferenceTimeAfterFail = 0;

	_ptsWaiter.setRequesting(true);

	MTP::send(
		MTPupdates_GetDifference(
			MTP_flags(0),
			MTP_int(_ptsWaiter.current()),
			MTPint(),
			MTP_int(updDate),
			MTP_int(updQts)),
		rpcDone(&MainWidget::gotDifference),
		rpcFail(&MainWidget::failDifference));
}

void MainWidget::getChannelDifference(ChannelData *channel, ChannelDifferenceRequest from) {
	if (this != App::main() || !channel) return;

	if (from != ChannelDifferenceRequest::PtsGapOrShortPoll) {
		_channelGetDifferenceTimeByPts.remove(channel);
	}

	if (!channel->ptsInited() || channel->ptsRequesting()) return;

	if (from != ChannelDifferenceRequest::AfterFail) {
		_channelGetDifferenceTimeAfterFail.remove(channel);
	}

	channel->ptsSetRequesting(true);

	auto filter = MTP_channelMessagesFilterEmpty();
	auto flags = MTPupdates_GetChannelDifference::Flag::f_force | 0;
	if (from != ChannelDifferenceRequest::PtsGapOrShortPoll) {
		if (!channel->ptsWaitingForSkipped()) {
			flags = 0; // No force flag when requesting for short poll.
		}
	}
	MTP::send(
		MTPupdates_GetChannelDifference(
			MTP_flags(flags),
			channel->inputChannel,
			filter,
			MTP_int(channel->pts()),
			MTP_int(kChannelGetDifferenceLimit)),
		rpcDone(&MainWidget::gotChannelDifference, channel),
		rpcFail(&MainWidget::failChannelDifference, channel));
}

void MainWidget::sendPing() {
	MTP::ping();
}

void MainWidget::start() {
	session().api().requestNotifySettings(MTP_inputNotifyUsers());
	session().api().requestNotifySettings(MTP_inputNotifyChats());
	session().api().requestNotifySettings(MTP_inputNotifyBroadcasts());

	cSetOtherOnline(0);
	session().user()->loadUserpic();

	MTP::send(MTPupdates_GetState(), rpcDone(&MainWidget::gotState));
	update();

	_started = true;
	Local::readInstalledStickers();
	Local::readFeaturedStickers();
	Local::readRecentStickers();
	Local::readFavedStickers();
	Local::readSavedGifs();
	if (const auto availableAt = Local::ReadExportSettings().availableAt) {
		session().data().suggestStartExport(availableAt);
	}

	_history->start();

	Core::App().checkStartUrl();
}

bool MainWidget::started() {
	return _started;
}

void MainWidget::openPeerByName(
		const QString &username,
		MsgId msgId,
		const QString &startToken,
		FullMsgId clickFromMessageId) {
	Core::App().hideMediaView();

	if (const auto peer = session().data().peerByUsername(username)) {
		if (msgId == ShowAtGameShareMsgId) {
			if (peer->isUser() && peer->asUser()->isBot() && !startToken.isEmpty()) {
				peer->asUser()->botInfo->shareGameShortName = startToken;
				AddBotToGroupBoxController::Start(
					_controller,
					peer->asUser());
			} else {
				InvokeQueued(this, [this, peer] {
					_controller->showPeerHistory(
						peer->id,
						SectionShow::Way::Forward);
				});
			}
		} else if (msgId == ShowAtProfileMsgId && !peer->isChannel()) {
			if (peer->isUser() && peer->asUser()->isBot() && !peer->asUser()->botInfo->cantJoinGroups && !startToken.isEmpty()) {
				peer->asUser()->botInfo->startGroupToken = startToken;
				AddBotToGroupBoxController::Start(
					_controller,
					peer->asUser());
			} else if (peer->isUser() && peer->asUser()->isBot()) {
				// Always open bot chats, even from mention links.
				InvokeQueued(this, [this, peer] {
					_controller->showPeerHistory(
						peer->id,
						SectionShow::Way::Forward);
				});
			} else {
				_controller->showPeerInfo(peer);
			}
		} else {
			if (msgId == ShowAtProfileMsgId || !peer->isChannel()) { // show specific posts only in channels / supergroups
				msgId = ShowAtUnreadMsgId;
			}
			if (peer->isUser() && peer->asUser()->isBot()) {
				peer->asUser()->botInfo->startToken = startToken;
				if (peer == _history->peer()) {
					_history->updateControlsVisibility();
					_history->updateControlsGeometry();
				}
			}
			const auto returnToId = clickFromMessageId;
			InvokeQueued(this, [=] {
				if (const auto returnTo = session().data().message(returnToId)) {
					if (returnTo->history()->peer == peer) {
						pushReplyReturn(returnTo);
					}
				}
				_controller->showPeerHistory(
					peer->id,
					SectionShow::Way::Forward,
					msgId);
			});
		}
	} else {
		MTP::send(MTPcontacts_ResolveUsername(MTP_string(username)), rpcDone(&MainWidget::usernameResolveDone, qMakePair(msgId, startToken)), rpcFail(&MainWidget::usernameResolveFail, username));
	}
}

bool MainWidget::contentOverlapped(const QRect &globalRect) {
	return (_history->contentOverlapped(globalRect)
			|| _playerPlaylist->overlaps(globalRect)
			|| (_playerVolume && _playerVolume->overlaps(globalRect)));
}

void MainWidget::usernameResolveDone(QPair<MsgId, QString> msgIdAndStartToken, const MTPcontacts_ResolvedPeer &result) {
	Ui::hideLayer();
	if (result.type() != mtpc_contacts_resolvedPeer) return;

	const auto &d(result.c_contacts_resolvedPeer());
	session().data().processUsers(d.vusers());
	session().data().processChats(d.vchats());
	PeerId peerId = peerFromMTP(d.vpeer());
	if (!peerId) return;

	PeerData *peer = session().data().peer(peerId);
	MsgId msgId = msgIdAndStartToken.first;
	QString startToken = msgIdAndStartToken.second;
	if (msgId == ShowAtProfileMsgId && !peer->isChannel()) {
		if (peer->isUser() && peer->asUser()->isBot() && !peer->asUser()->botInfo->cantJoinGroups && !startToken.isEmpty()) {
			peer->asUser()->botInfo->startGroupToken = startToken;
			AddBotToGroupBoxController::Start(
				_controller,
				peer->asUser());
		} else if (peer->isUser() && peer->asUser()->isBot()) {
			// Always open bot chats, even from mention links.
			InvokeQueued(this, [this, peer] {
				_controller->showPeerHistory(
					peer->id,
					SectionShow::Way::Forward);
			});
		} else {
			_controller->showPeerInfo(peer);
		}
	} else {
		if (msgId == ShowAtProfileMsgId || !peer->isChannel()) { // show specific posts only in channels / supergroups
			msgId = ShowAtUnreadMsgId;
		}
		if (peer->isUser() && peer->asUser()->isBot()) {
			peer->asUser()->botInfo->startToken = startToken;
			if (peer == _history->peer()) {
				_history->updateControlsVisibility();
				_history->updateControlsGeometry();
			}
		}
		InvokeQueued(this, [this, peer, msgId] {
			_controller->showPeerHistory(
				peer->id,
				SectionShow::Way::Forward,
				msgId);
		});
	}
}

bool MainWidget::usernameResolveFail(QString name, const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (error.code() == 400) {
		Ui::show(Box<InformBox>(tr::lng_username_not_found(tr::now, lt_user, name)));
	}
	return true;
}

void MainWidget::incrementSticker(DocumentData *sticker) {
	if (!sticker
		|| !sticker->sticker()
		|| sticker->sticker()->set.type() == mtpc_inputStickerSetEmpty) {
		return;
	}

	bool writeRecentStickers = false;
	auto &sets = session().data().stickerSetsRef();
	auto it = sets.find(Stickers::CloudRecentSetId);
	if (it == sets.cend()) {
		if (it == sets.cend()) {
			it = sets.insert(Stickers::CloudRecentSetId, Stickers::Set(
				Stickers::CloudRecentSetId,
				uint64(0),
				tr::lng_recent_stickers(tr::now),
				QString(),
				0, // count
				0, // hash
				MTPDstickerSet_ClientFlag::f_special | 0,
				TimeId(0),
				ImagePtr()));
		} else {
			it->title = tr::lng_recent_stickers(tr::now);
		}
	}
	auto removedFromEmoji = std::vector<not_null<EmojiPtr>>();
	auto index = it->stickers.indexOf(sticker);
	if (index > 0) {
		if (it->dates.empty()) {
			session().api().requestRecentStickersForce();
		} else {
			Assert(it->dates.size() == it->stickers.size());
			it->dates.erase(it->dates.begin() + index);
		}
		it->stickers.removeAt(index);
		for (auto i = it->emoji.begin(); i != it->emoji.end();) {
			if (const auto index = i->indexOf(sticker); index >= 0) {
				removedFromEmoji.emplace_back(i.key());
				i->removeAt(index);
				if (i->isEmpty()) {
					i = it->emoji.erase(i);
					continue;
				}
			}
			++i;
		}
	}
	if (index) {
		if (it->dates.size() == it->stickers.size()) {
			it->dates.insert(it->dates.begin(), base::unixtime::now());
		}
		it->stickers.push_front(sticker);
		if (const auto emojiList = Stickers::GetEmojiListFromSet(sticker)) {
			for (const auto emoji : *emojiList) {
				it->emoji[emoji].push_front(sticker);
			}
		} else if (!removedFromEmoji.empty()) {
			for (const auto emoji : removedFromEmoji) {
				it->emoji[emoji].push_front(sticker);
			}
		} else {
			session().api().requestRecentStickersForce();
		}

		writeRecentStickers = true;
	}

	// Remove that sticker from old recent, now it is in cloud recent stickers.
	bool writeOldRecent = false;
	auto &recent = Stickers::GetRecentPack();
	for (auto i = recent.begin(), e = recent.end(); i != e; ++i) {
		if (i->first == sticker) {
			writeOldRecent = true;
			recent.erase(i);
			break;
		}
	}
	while (!recent.isEmpty() && it->stickers.size() + recent.size() > Global::StickersRecentLimit()) {
		writeOldRecent = true;
		recent.pop_back();
	}

	if (writeOldRecent) {
		Local::writeUserSettings();
	}

	// Remove that sticker from custom stickers, now it is in cloud recent stickers.
	bool writeInstalledStickers = false;
	auto custom = sets.find(Stickers::CustomSetId);
	if (custom != sets.cend()) {
		int removeIndex = custom->stickers.indexOf(sticker);
		if (removeIndex >= 0) {
			custom->stickers.removeAt(removeIndex);
			if (custom->stickers.isEmpty()) {
				sets.erase(custom);
			}
			writeInstalledStickers = true;
		}
	}

	if (writeInstalledStickers) {
		Local::writeInstalledStickers();
	}
	if (writeRecentStickers) {
		Local::writeRecentStickers();
	}
	_controller->tabbedSelector()->refreshStickers();
}

void MainWidget::activate() {
	if (_a_show.animating()) return;
	if (!_mainSection) {
		if (_hider) {
			_dialogs->setInnerFocus();
        } else if (App::wnd() && !Ui::isLayerShown()) {
			if (!cSendPaths().isEmpty()) {
				const auto interpret = qstr("interpret://");
				const auto path = cSendPaths()[0];
				if (path.startsWith(interpret)) {
					cSetSendPaths(QStringList());
					const auto error = Support::InterpretSendPath(path.mid(interpret.size()));
					if (!error.isEmpty()) {
						Ui::show(Box<InformBox>(error));
					}
				} else {
					showSendPathsLayer();
				}
			} else if (_history->peer()) {
				_history->activate();
			} else {
				_dialogs->setInnerFocus();
			}
		}
	}
	App::wnd()->fixOrder();
}

bool MainWidget::isActive() const {
	return !_isIdle && isVisible() && !_a_show.animating();
}

bool MainWidget::doWeReadServerHistory() const {
	return isActive()
		&& !session().supportMode()
		&& !_mainSection
		&& _history->doWeReadServerHistory();
}

bool MainWidget::doWeReadMentions() const {
	return isActive() && !_mainSection && _history->doWeReadMentions();
}

bool MainWidget::lastWasOnline() const {
	return _lastWasOnline;
}

crl::time MainWidget::lastSetOnline() const {
	return _lastSetOnline;
}

int32 MainWidget::dlgsWidth() const {
	return _dialogs->width();
}

MainWidget::~MainWidget() {
	if (App::main() == this) {
		_history->showHistory(0, 0);
	}
}

void MainWidget::updateOnline(bool gotOtherOffline) {
	if (this != App::main()) return;
	InvokeQueued(this, [=] { session().checkAutoLock(); });

	bool isOnline = !App::quitting() && App::wnd()->isActive();
	int updateIn = Global::OnlineUpdatePeriod();
	Assert(updateIn >= 0);
	if (isOnline) {
		const auto idle = crl::now() - Core::App().lastNonIdleTime();
		if (idle >= Global::OfflineIdleTimeout()) {
			isOnline = false;
			if (!_isIdle) {
				_isIdle = true;
				_idleFinishTimer.callOnce(900);
			}
		} else {
			updateIn = qMin(updateIn, int(Global::OfflineIdleTimeout() - idle));
			Assert(updateIn >= 0);
		}
	}
	auto ms = crl::now();
	if (isOnline != _lastWasOnline
		|| (isOnline && _lastSetOnline + Global::OnlineUpdatePeriod() <= ms)
		|| (isOnline && gotOtherOffline)) {
		if (_onlineRequest) {
			MTP::cancel(_onlineRequest);
			_onlineRequest = 0;
		}

		_lastWasOnline = isOnline;
		_lastSetOnline = ms;
		if (!App::quitting()) {
			_onlineRequest = MTP::send(MTPaccount_UpdateStatus(MTP_bool(!isOnline)));
		} else {
			_onlineRequest = MTP::send(
				MTPaccount_UpdateStatus(MTP_bool(!isOnline)),
				rpcDone(&MainWidget::updateStatusDone),
				rpcFail(&MainWidget::updateStatusFail));
		}

		const auto self = session().user();
		self->onlineTill = base::unixtime::now() + (isOnline ? (Global::OnlineUpdatePeriod() / 1000) : -1);
		Notify::peerUpdatedDelayed(
			self,
			Notify::PeerUpdate::Flag::UserOnlineChanged);
		if (!isOnline) { // Went offline, so we need to save message draft to the cloud.
			saveDraftToCloud();
		}

		_lastSetOnline = ms;
	} else if (isOnline) {
		updateIn = qMin(updateIn, int(_lastSetOnline + Global::OnlineUpdatePeriod() - ms));
		Assert(updateIn >= 0);
	}
	_onlineTimer.callOnce(updateIn);
}

void MainWidget::updateStatusDone(const MTPBool &result) {
	Core::App().quitPreventFinished();
}

bool MainWidget::updateStatusFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) {
		return false;
	}
	Core::App().quitPreventFinished();
	return true;
}

bool MainWidget::isQuitPrevent() {
	if (!_lastWasOnline) {
		return false;
	}
	LOG(("MainWidget prevents quit, sending offline status..."));
	updateOnline();
	return true;
}

void MainWidget::saveDraftToCloud() {
	_history->saveFieldToHistoryLocalDraft();

	const auto peer = _history->peer();
	if (const auto history = session().data().historyLoaded(peer)) {
		writeDrafts(history);

		const auto localDraft = history->localDraft();
		const auto cloudDraft = history->cloudDraft();
		if (!Data::draftsAreEqual(localDraft, cloudDraft)
			&& !session().supportMode()) {
			session().api().saveDraftToCloudDelayed(history);
		}
	}
}

void MainWidget::applyCloudDraft(History *history) {
	_history->applyCloudDraft(history);
}

void MainWidget::writeDrafts(History *history) {
	Local::MessageDraft storedLocalDraft, storedEditDraft;
	MessageCursor localCursor, editCursor;
	if (const auto localDraft = history->localDraft()) {
		if (session().supportMode()
			|| !Data::draftsAreEqual(localDraft, history->cloudDraft())) {
			storedLocalDraft = Local::MessageDraft(
				localDraft->msgId,
				localDraft->textWithTags,
				localDraft->previewCancelled);
			localCursor = localDraft->cursor;
		}
	}
	if (const auto editDraft = history->editDraft()) {
		storedEditDraft = Local::MessageDraft(
			editDraft->msgId,
			editDraft->textWithTags,
			editDraft->previewCancelled);
		editCursor = editDraft->cursor;
	}
	Local::writeDrafts(
		history->peer->id,
		storedLocalDraft,
		storedEditDraft);
	Local::writeDraftCursors(history->peer->id, localCursor, editCursor);
}

void MainWidget::checkIdleFinish() {
	if (crl::now() - Core::App().lastNonIdleTime()
		< Global::OfflineIdleTimeout()) {
		_idleFinishTimer.cancel();
		_isIdle = false;
		updateOnline();
		App::wnd()->checkHistoryActivation();
	} else {
		_idleFinishTimer.callOnce(900);
	}
}

bool MainWidget::updateReceived(const mtpPrime *from, const mtpPrime *end) {
	if (end <= from) {
		return false;
	}

	session().checkAutoLock();

	if (mtpTypeId(*from) == mtpc_new_session_created) {
		MTPNewSession newSession;
		if (!newSession.read(from, end)) {
			return false;
		}
		updSeq = 0;
		MTP_LOG(0, ("getDifference { after new_session_created }%1").arg(cTestMode() ? " TESTMODE" : ""));
		getDifference();
		return true;
	}
	MTPUpdates updates;
	if (!updates.read(from, end)) {
		return false;
	}

	_lastUpdateTime = crl::now();
	_noUpdatesTimer.callOnce(kNoUpdatesTimeout);
	if (!requestingDifference()
		|| HasForceLogoutNotification(updates)) {
		feedUpdates(updates);
	}
	return true;
}

void MainWidget::feedUpdates(const MTPUpdates &updates, uint64 randomId) {
	switch (updates.type()) {
	case mtpc_updates: {
		auto &d = updates.c_updates();
		if (d.vseq().v) {
			if (d.vseq().v <= updSeq) {
				return;
			}
			if (d.vseq().v > updSeq + 1) {
				_bySeqUpdates.insert(d.vseq().v, updates);
				_bySeqTimer.callOnce(PtsWaiter::kWaitForSkippedTimeout);
				return;
			}
		}

		session().data().processUsers(d.vusers());
		session().data().processChats(d.vchats());
		feedUpdateVector(d.vupdates());

		updSetState(0, d.vdate().v, updQts, d.vseq().v);
	} break;

	case mtpc_updatesCombined: {
		auto &d = updates.c_updatesCombined();
		if (d.vseq_start().v) {
			if (d.vseq_start().v <= updSeq) {
				return;
			}
			if (d.vseq_start().v > updSeq + 1) {
				_bySeqUpdates.insert(d.vseq_start().v, updates);
				_bySeqTimer.callOnce(PtsWaiter::kWaitForSkippedTimeout);
				return;
			}
		}

		session().data().processUsers(d.vusers());
		session().data().processChats(d.vchats());
		feedUpdateVector(d.vupdates());

		updSetState(0, d.vdate().v, updQts, d.vseq().v);
	} break;

	case mtpc_updateShort: {
		auto &d = updates.c_updateShort();
		feedUpdate(d.vupdate());

		updSetState(0, d.vdate().v, updQts, updSeq);
	} break;

	case mtpc_updateShortMessage: {
		auto &d = updates.c_updateShortMessage();
		const auto viaBotId = d.vvia_bot_id();
		const auto entities = d.ventities();
		const auto fwd = d.vfwd_from();
		if (!session().data().userLoaded(d.vuser_id().v)
			|| (viaBotId && !session().data().userLoaded(viaBotId->v))
			|| (entities && !MentionUsersLoaded(&session(), *entities))
			|| (fwd && !ForwardedInfoDataLoaded(&session(), *fwd))) {
			MTP_LOG(0, ("getDifference { good - getting user for updateShortMessage }%1").arg(cTestMode() ? " TESTMODE" : ""));
			return getDifference();
		}
		if (ptsUpdateAndApply(d.vpts().v, d.vpts_count().v, updates)) {
			// Update date as well.
			updSetState(0, d.vdate().v, updQts, updSeq);
		}
	} break;

	case mtpc_updateShortChatMessage: {
		auto &d = updates.c_updateShortChatMessage();
		const auto noFrom = !session().data().userLoaded(d.vfrom_id().v);
		const auto chat = session().data().chatLoaded(d.vchat_id().v);
		const auto viaBotId = d.vvia_bot_id();
		const auto entities = d.ventities();
		const auto fwd = d.vfwd_from();
		if (!chat
			|| noFrom
			|| (viaBotId && !session().data().userLoaded(viaBotId->v))
			|| (entities && !MentionUsersLoaded(&session(), *entities))
			|| (fwd && !ForwardedInfoDataLoaded(&session(), *fwd))) {
			MTP_LOG(0, ("getDifference { good - getting user for updateShortChatMessage }%1").arg(cTestMode() ? " TESTMODE" : ""));
			if (chat && noFrom) {
				session().api().requestFullPeer(chat);
			}
			return getDifference();
		}
		if (ptsUpdateAndApply(d.vpts().v, d.vpts_count().v, updates)) {
			// Update date as well.
			updSetState(0, d.vdate().v, updQts, updSeq);
		}
	} break;

	case mtpc_updateShortSentMessage: {
		auto &d = updates.c_updateShortSentMessage();
		if (!IsServerMsgId(d.vid().v)) {
			LOG(("API Error: Bad msgId got from server: %1").arg(d.vid().v));
		} else if (randomId) {
			const auto sent = session().data().messageSentData(randomId);
			const auto lookupMessage = [&] {
				return sent.peerId
					? session().data().message(
						peerToChannel(sent.peerId),
						d.vid().v)
					: nullptr;
			};
			const auto wasAlready = (lookupMessage() != nullptr);
			feedUpdate(MTP_updateMessageID(d.vid(), MTP_long(randomId))); // ignore real date
			if (const auto item = lookupMessage()) {
				const auto list = d.ventities();
				if (list && !MentionUsersLoaded(&session(), *list)) {
					session().api().requestMessageData(
						item->history()->peer->asChannel(),
						item->id,
						ApiWrap::RequestMessageDataCallback());
				}
				item->updateSentContent({
					sent.text,
					TextUtilities::EntitiesFromMTP(list.value_or_empty())
				}, d.vmedia());
				item->contributeToSlowmode(d.vdate().v);
				if (!wasAlready) {
					item->indexAsNewItem();
				}
			}
		}

		if (ptsUpdateAndApply(d.vpts().v, d.vpts_count().v, updates)) {
			// Update date as well.
			updSetState(0, d.vdate().v, updQts, updSeq);
		}
	} break;

	case mtpc_updatesTooLong: {
		MTP_LOG(0, ("getDifference { good - updatesTooLong received }%1").arg(cTestMode() ? " TESTMODE" : ""));
		return getDifference();
	} break;
	}
	session().data().sendHistoryChangeNotifications();
}

void MainWidget::feedUpdate(const MTPUpdate &update) {
	switch (update.type()) {

	// New messages.
	case mtpc_updateNewMessage: {
		auto &d = update.c_updateNewMessage();

		const auto isDataLoaded = AllDataLoadedForMessage(&session(), d.vmessage());
		if (!requestingDifference() && isDataLoaded != DataIsLoadedResult::Ok) {
			MTP_LOG(0, ("getDifference { good - "
				"after not all data loaded in updateNewMessage }%1"
				).arg(cTestMode() ? " TESTMODE" : ""));

			// This can be if this update was created by grouping
			// some short message update into an updates vector.
			return getDifference();
		}

		ptsUpdateAndApply(d.vpts().v, d.vpts_count().v, update);
	} break;

	case mtpc_updateNewChannelMessage: {
		auto &d = update.c_updateNewChannelMessage();
		auto channel = session().data().channelLoaded(peerToChannel(PeerFromMessage(d.vmessage())));
		const auto isDataLoaded = AllDataLoadedForMessage(&session(), d.vmessage());
		if (!requestingDifference() && (!channel || isDataLoaded != DataIsLoadedResult::Ok)) {
			MTP_LOG(0, ("getDifference { good - "
				"after not all data loaded in updateNewChannelMessage }%1"
				).arg(cTestMode() ? " TESTMODE" : ""));

			// Request last active supergroup participants if the 'from' user was not loaded yet.
			// This will optimize similar getDifference() calls for almost all next messages.
			if (isDataLoaded == DataIsLoadedResult::FromNotLoaded && channel && channel->isMegagroup()) {
				if (channel->mgInfo->lastParticipants.size() < Global::ChatSizeMax() && (channel->mgInfo->lastParticipants.empty() || channel->mgInfo->lastParticipants.size() < channel->membersCount())) {
					session().api().requestLastParticipants(channel);
				}
			}

			if (!_byMinChannelTimer.isActive()) { // getDifference after timeout
				_byMinChannelTimer.callOnce(PtsWaiter::kWaitForSkippedTimeout);
			}
			return;
		}
		if (channel && !_handlingChannelDifference) {
			if (channel->ptsRequesting()) { // skip global updates while getting channel difference
				return;
			}
			channel->ptsUpdateAndApply(d.vpts().v, d.vpts_count().v, update);
		} else {
			session().api().applyUpdateNoPtsCheck(update);
		}
	} break;

	case mtpc_updateMessageID: {
		const auto &d = update.c_updateMessageID();
		const auto randomId = d.vrandom_id().v;
		if (const auto id = session().data().messageIdByRandomId(randomId)) {
			const auto newId = d.vid().v;
			if (const auto local = session().data().message(id)) {
				if (local->isScheduled()) {
					session().data().scheduledMessages().apply(d, local);
				} else {
					const auto channel = id.channel;
					const auto existing = session().data().message(
						channel,
						newId);
					if (existing && !local->mainView()) {
						const auto history = local->history();
						local->destroy();
						history->requestChatListMessage();
					} else {
						if (existing) {
							existing->destroy();
						}
						local->setRealId(d.vid().v);
					}
				}
			}
			session().data().unregisterMessageRandomId(randomId);
		}
		session().data().unregisterMessageSentData(randomId);
	} break;

	// Message contents being read.
	case mtpc_updateReadMessagesContents: {
		auto &d = update.c_updateReadMessagesContents();
		ptsUpdateAndApply(d.vpts().v, d.vpts_count().v, update);
	} break;

	case mtpc_updateChannelReadMessagesContents: {
		auto &d = update.c_updateChannelReadMessagesContents();
		auto channel = session().data().channelLoaded(d.vchannel_id().v);
		if (!channel) {
			if (!_byMinChannelTimer.isActive()) {
				// getDifference after timeout.
				_byMinChannelTimer.callOnce(PtsWaiter::kWaitForSkippedTimeout);
			}
			return;
		}
		auto possiblyReadMentions = base::flat_set<MsgId>();
		for_const (auto &msgId, d.vmessages().v) {
			if (auto item = session().data().message(channel, msgId.v)) {
				if (item->isUnreadMedia() || item->isUnreadMention()) {
					item->markMediaRead();
					session().data().requestItemRepaint(item);
				}
			} else {
				// Perhaps it was an unread mention!
				possiblyReadMentions.insert(msgId.v);
			}
		}
		session().api().checkForUnreadMentions(possiblyReadMentions, channel);
	} break;

	// Edited messages.
	case mtpc_updateEditMessage: {
		auto &d = update.c_updateEditMessage();
		ptsUpdateAndApply(d.vpts().v, d.vpts_count().v, update);
	} break;

	case mtpc_updateEditChannelMessage: {
		auto &d = update.c_updateEditChannelMessage();
		auto channel = session().data().channelLoaded(peerToChannel(PeerFromMessage(d.vmessage())));

		if (channel && !_handlingChannelDifference) {
			if (channel->ptsRequesting()) { // skip global updates while getting channel difference
				return;
			} else {
				channel->ptsUpdateAndApply(d.vpts().v, d.vpts_count().v, update);
			}
		} else {
			session().api().applyUpdateNoPtsCheck(update);
		}
	} break;

	// Messages being read.
	case mtpc_updateReadHistoryInbox: {
		auto &d = update.c_updateReadHistoryInbox();
		ptsUpdateAndApply(d.vpts().v, d.vpts_count().v, update);
	} break;

	case mtpc_updateReadHistoryOutbox: {
		auto &d = update.c_updateReadHistoryOutbox();
		if (ptsUpdateAndApply(d.vpts().v, d.vpts_count().v, update)) {
			// We could've updated the double checks.
			// Better would be for history to be subscribed to outbox read events.
			_history->update();
		}
	} break;

	case mtpc_updateReadChannelInbox: {
		const auto &d = update.c_updateReadChannelInbox();
		const auto peer = peerFromChannel(d.vchannel_id().v);
		if (const auto history = session().data().historyLoaded(peer)) {
			history->applyInboxReadUpdate(
				d.vfolder_id().value_or_empty(),
				d.vmax_id().v,
				d.vstill_unread_count().v,
				d.vpts().v);
		}
	} break;

	case mtpc_updateReadChannelOutbox: {
		const auto &d = update.c_updateReadChannelOutbox();
		const auto peer = peerFromChannel(d.vchannel_id().v);
		if (const auto history = session().data().historyLoaded(peer)) {
			history->outboxRead(d.vmax_id().v);
			if (!requestingDifference()) {
				if (const auto user = history->peer->asUser()) {
					user->madeAction(base::unixtime::now());
				}
			}
			if (_history->peer() && _history->peer()->id == peer) {
				_history->update();
			}
		}
	} break;

	//case mtpc_updateReadFeed: { // #feed
	//	const auto &d = update.c_updateReadFeed();
	//	const auto feedId = d.vfeed_id().v;
	//	if (const auto feed = session().data().feedLoaded(feedId)) {
	//		feed->setUnreadPosition(
	//			Data::FeedPositionFromMTP(d.vmax_position()));
	//		if (d.vunread_count() && d.vunread_muted_count()) {
	//			feed->setUnreadCounts(
	//				d.vunread_count()->v,
	//				d.vunread_muted_count()->v);
	//		} else {
	//			session().api().requestDialogEntry(feed);
	//		}
	//	}
	//} break;

	case mtpc_updateDialogUnreadMark: {
		const auto &data = update.c_updateDialogUnreadMark();
		data.vpeer().match(
		[&](const MTPDdialogPeer &dialog) {
			const auto id = peerFromMTP(dialog.vpeer());
			if (const auto history = session().data().historyLoaded(id)) {
				history->setUnreadMark(data.is_unread());
			}
		}, [&](const MTPDdialogPeerFolder &dialog) {
			const auto id = dialog.vfolder_id().v; // #TODO archive
			//if (const auto folder = session().data().folderLoaded(id)) {
			//	folder->setUnreadMark(data.is_unread());
			//}
		});
	} break;

	case mtpc_updateFolderPeers: {
		const auto &data = update.c_updateFolderPeers();

		ptsUpdateAndApply(data.vpts().v, data.vpts_count().v, update);
	} break;

	// Deleted messages.
	case mtpc_updateDeleteMessages: {
		auto &d = update.c_updateDeleteMessages();

		ptsUpdateAndApply(d.vpts().v, d.vpts_count().v, update);
	} break;

	case mtpc_updateDeleteChannelMessages: {
		auto &d = update.c_updateDeleteChannelMessages();
		auto channel = session().data().channelLoaded(d.vchannel_id().v);

		if (channel && !_handlingChannelDifference) {
			if (channel->ptsRequesting()) { // skip global updates while getting channel difference
				return;
			}
			channel->ptsUpdateAndApply(d.vpts().v, d.vpts_count().v, update);
		} else {
			session().api().applyUpdateNoPtsCheck(update);
		}
	} break;

	case mtpc_updateNewScheduledMessage: {
		const auto &d = update.c_updateNewScheduledMessage();
		session().data().scheduledMessages().apply(d);
	} break;

	case mtpc_updateDeleteScheduledMessages: {
		const auto &d = update.c_updateDeleteScheduledMessages();
		session().data().scheduledMessages().apply(d);
	} break;

	case mtpc_updateWebPage: {
		auto &d = update.c_updateWebPage();

		// Update web page anyway.
		session().data().processWebpage(d.vwebpage());
		_history->updatePreview();
		session().data().sendWebPageGamePollNotifications();

		ptsUpdateAndApply(d.vpts().v, d.vpts_count().v, update);
	} break;

	case mtpc_updateChannelWebPage: {
		auto &d = update.c_updateChannelWebPage();

		// Update web page anyway.
		session().data().processWebpage(d.vwebpage());
		_history->updatePreview();
		session().data().sendWebPageGamePollNotifications();

		auto channel = session().data().channelLoaded(d.vchannel_id().v);
		if (channel && !_handlingChannelDifference) {
			if (channel->ptsRequesting()) { // skip global updates while getting channel difference
				return;
			} else {
				channel->ptsUpdateAndApply(d.vpts().v, d.vpts_count().v, update);
			}
		} else {
			session().api().applyUpdateNoPtsCheck(update);
		}
	} break;

	case mtpc_updateMessagePoll: {
		session().data().applyUpdate(update.c_updateMessagePoll());
	} break;

	case mtpc_updateUserTyping: {
		auto &d = update.c_updateUserTyping();
		const auto userId = peerFromUser(d.vuser_id());
		const auto history = session().data().historyLoaded(userId);
		const auto user = session().data().userLoaded(d.vuser_id().v);
		if (history && user) {
			const auto when = requestingDifference() ? 0 : base::unixtime::now();
			session().data().registerSendAction(history, user, d.vaction(), when);
		}
	} break;

	case mtpc_updateChatUserTyping: {
		auto &d = update.c_updateChatUserTyping();
		const auto history = [&]() -> History* {
			if (const auto chat = session().data().chatLoaded(d.vchat_id().v)) {
				return session().data().historyLoaded(chat->id);
			} else if (const auto channel = session().data().channelLoaded(d.vchat_id().v)) {
				return session().data().historyLoaded(channel->id);
			}
			return nullptr;
		}();
		const auto user = (d.vuser_id().v == session().userId())
			? nullptr
			: session().data().userLoaded(d.vuser_id().v);
		if (history && user) {
			const auto when = requestingDifference() ? 0 : base::unixtime::now();
			session().data().registerSendAction(history, user, d.vaction(), when);
		}
	} break;

	case mtpc_updateChatParticipants: {
		session().data().applyUpdate(update.c_updateChatParticipants());
	} break;

	case mtpc_updateChatParticipantAdd: {
		session().data().applyUpdate(update.c_updateChatParticipantAdd());
	} break;

	case mtpc_updateChatParticipantDelete: {
		session().data().applyUpdate(update.c_updateChatParticipantDelete());
	} break;

	case mtpc_updateChatParticipantAdmin: {
		session().data().applyUpdate(update.c_updateChatParticipantAdmin());
	} break;

	case mtpc_updateChatDefaultBannedRights: {
		session().data().applyUpdate(update.c_updateChatDefaultBannedRights());
	} break;

	case mtpc_updateUserStatus: {
		auto &d = update.c_updateUserStatus();
		if (auto user = session().data().userLoaded(d.vuser_id().v)) {
			switch (d.vstatus().type()) {
			case mtpc_userStatusEmpty: user->onlineTill = 0; break;
			case mtpc_userStatusRecently:
				if (user->onlineTill > -10) { // don't modify pseudo-online
					user->onlineTill = -2;
				}
			break;
			case mtpc_userStatusLastWeek: user->onlineTill = -3; break;
			case mtpc_userStatusLastMonth: user->onlineTill = -4; break;
			case mtpc_userStatusOffline: user->onlineTill = d.vstatus().c_userStatusOffline().vwas_online().v; break;
			case mtpc_userStatusOnline: user->onlineTill = d.vstatus().c_userStatusOnline().vexpires().v; break;
			}
			Notify::peerUpdatedDelayed(
				user,
				Notify::PeerUpdate::Flag::UserOnlineChanged);
		}
		if (d.vuser_id().v == session().userId()) {
			if (d.vstatus().type() == mtpc_userStatusOffline || d.vstatus().type() == mtpc_userStatusEmpty) {
				updateOnline(true);
				if (d.vstatus().type() == mtpc_userStatusOffline) {
					cSetOtherOnline(d.vstatus().c_userStatusOffline().vwas_online().v);
				}
			} else if (d.vstatus().type() == mtpc_userStatusOnline) {
				cSetOtherOnline(d.vstatus().c_userStatusOnline().vexpires().v);
			}
		}
	} break;

	case mtpc_updateUserName: {
		auto &d = update.c_updateUserName();
		if (auto user = session().data().userLoaded(d.vuser_id().v)) {
			if (!user->isContact()) {
				user->setName(
					TextUtilities::SingleLine(qs(d.vfirst_name())),
					TextUtilities::SingleLine(qs(d.vlast_name())),
					user->nameOrPhone,
					TextUtilities::SingleLine(qs(d.vusername())));
			} else {
				user->setName(
					TextUtilities::SingleLine(user->firstName),
					TextUtilities::SingleLine(user->lastName),
					user->nameOrPhone,
					TextUtilities::SingleLine(qs(d.vusername())));
			}
		}
	} break;

	case mtpc_updateUserPhoto: {
		auto &d = update.c_updateUserPhoto();
		if (auto user = session().data().userLoaded(d.vuser_id().v)) {
			user->setPhoto(d.vphoto());
			user->loadUserpic();
			if (mtpIsTrue(d.vprevious()) || !user->userpicPhotoId()) {
				session().storage().remove(Storage::UserPhotosRemoveAfter(
					user->bareId(),
					user->userpicPhotoId()));
			} else {
				session().storage().add(Storage::UserPhotosAddNew(
					user->bareId(),
					user->userpicPhotoId()));
			}
		}
	} break;

	case mtpc_updatePeerSettings: {
		const auto &d = update.c_updatePeerSettings();
		const auto peerId = peerFromMTP(d.vpeer());
		if (const auto peer = session().data().peerLoaded(peerId)) {
			const auto settings = d.vsettings().match([](
					const MTPDpeerSettings &data) {
				return data.vflags().v;
			});
			peer->setSettings(settings);
		}
	} break;

	case mtpc_updateNotifySettings: {
		auto &d = update.c_updateNotifySettings();
		session().data().applyNotifySetting(d.vpeer(), d.vnotify_settings());
	} break;

	case mtpc_updateDcOptions: {
		auto &d = update.c_updateDcOptions();
		Core::App().dcOptions()->addFromList(d.vdc_options());
	} break;

	case mtpc_updateConfig: {
		session().mtp()->requestConfig();
	} break;

	case mtpc_updateUserPhone: {
		const auto &d = update.c_updateUserPhone();
		if (const auto user = session().data().userLoaded(d.vuser_id().v)) {
			const auto newPhone = qs(d.vphone());
			if (newPhone != user->phone()) {
				user->setPhone(newPhone);
				user->setName(
					user->firstName,
					user->lastName,
					((user->isContact()
						|| user->isServiceUser()
						|| user->isSelf()
						|| user->phone().isEmpty())
						? QString()
						: App::formatPhone(user->phone())),
					user->username);

				Notify::peerUpdatedDelayed(
					user,
					Notify::PeerUpdate::Flag::UserPhoneChanged);
			}
		}
	} break;

	case mtpc_updateNewEncryptedMessage: {
		auto &d = update.c_updateNewEncryptedMessage();
	} break;

	case mtpc_updateEncryptedChatTyping: {
		auto &d = update.c_updateEncryptedChatTyping();
	} break;

	case mtpc_updateEncryption: {
		auto &d = update.c_updateEncryption();
	} break;

	case mtpc_updateEncryptedMessagesRead: {
		auto &d = update.c_updateEncryptedMessagesRead();
	} break;

	case mtpc_updatePhoneCall: {
		session().calls().handleUpdate(update.c_updatePhoneCall());
	} break;

	case mtpc_updateUserBlocked: {
		const auto &d = update.c_updateUserBlocked();
		if (const auto user = session().data().userLoaded(d.vuser_id().v)) {
			user->setIsBlocked(mtpIsTrue(d.vblocked()));
		}
	} break;

	case mtpc_updateServiceNotification: {
		const auto &d = update.c_updateServiceNotification();
		const auto text = TextWithEntities {
			qs(d.vmessage()),
			TextUtilities::EntitiesFromMTP(d.ventities().v)
		};
		if (IsForceLogoutNotification(d)) {
			Core::App().forceLogOut(text);
		} else if (d.is_popup()) {
			Ui::show(Box<InformBox>(text));
		} else {
			session().data().serviceNotification(text, d.vmedia());
			session().data().checkNewAuthorization();
		}
	} break;

	case mtpc_updatePrivacy: {
		auto &d = update.c_updatePrivacy();
		const auto allChatsLoaded = [&](const MTPVector<MTPint> &ids) {
			for (const auto &chatId : ids.v) {
				if (!session().data().chatLoaded(chatId.v)
					&& !session().data().channelLoaded(chatId.v)) {
					return false;
				}
			}
			return true;
		};
		const auto allLoaded = [&] {
			for (const auto &rule : d.vrules().v) {
				const auto loaded = rule.match([&](
					const MTPDprivacyValueAllowChatParticipants & data) {
					return allChatsLoaded(data.vchats());
				}, [&](const MTPDprivacyValueDisallowChatParticipants & data) {
					return allChatsLoaded(data.vchats());
				}, [](auto &&) { return true; });
				if (!loaded) {
					return false;
				}
			}
			return true;
		};
		if (const auto key = ApiWrap::Privacy::KeyFromMTP(d.vkey().type())) {
			if (allLoaded()) {
				session().api().handlePrivacyChange(*key, d.vrules());
			} else {
				session().api().reloadPrivacy(*key);
			}
		}
	} break;

	case mtpc_updatePinnedDialogs: {
		const auto &d = update.c_updatePinnedDialogs();
		const auto folderId = d.vfolder_id().value_or_empty();
		const auto loaded = !folderId
			|| (session().data().folderLoaded(folderId) != nullptr);
		const auto folder = folderId
			? session().data().folder(folderId).get()
			: nullptr;
		const auto done = [&] {
			const auto list = d.vorder();
			if (!list) {
				return false;
			}
			const auto &order = list->v;
			const auto notLoaded = [&](const MTPDialogPeer &peer) {
				return peer.match([&](const MTPDdialogPeer &data) {
					return !session().data().historyLoaded(
						peerFromMTP(data.vpeer()));
				}, [&](const MTPDdialogPeerFolder &data) {
					if (folderId) {
						LOG(("API Error: "
							"updatePinnedDialogs has nested folders."));
						return true;
					}
					return !session().data().folderLoaded(data.vfolder_id().v);
				});
			};
			const auto allLoaded = ranges::find_if(order, notLoaded)
				== order.end();
			if (!allLoaded) {
				return false;
			}
			session().data().applyPinnedChats(folder, order);
			return true;
		}();
		if (!done) {
			session().api().requestPinnedDialogs(folder);
		}
		if (!loaded) {
			session().api().requestDialogEntry(folder);
		}
	} break;

	case mtpc_updateDialogPinned: {
		const auto &d = update.c_updateDialogPinned();
		const auto folderId = d.vfolder_id().value_or_empty();
		const auto folder = folderId
			? session().data().folder(folderId).get()
			: nullptr;
		const auto done = d.vpeer().match([&](const MTPDdialogPeer &data) {
			const auto id = peerFromMTP(data.vpeer());
			if (const auto history = session().data().historyLoaded(id)) {
				history->applyPinnedUpdate(d);
				return true;
			}
			DEBUG_LOG(("API Error: "
				"pinned chat not loaded for peer %1, folder: %2"
				).arg(id
				).arg(folderId
				));
			return false;
		}, [&](const MTPDdialogPeerFolder &data) {
			if (folderId != 0) {
				DEBUG_LOG(("API Error: Nested folders updateDialogPinned."));
				return false;
			}
			const auto id = data.vfolder_id().v;
			if (const auto folder = session().data().folderLoaded(id)) {
				folder->applyPinnedUpdate(d);
				return true;
			}
			DEBUG_LOG(("API Error: "
				"pinned folder not loaded for folderId %1, folder: %2"
				).arg(id
				).arg(folderId
				));
			return false;
		});
		if (!done) {
			session().api().requestPinnedDialogs(folder);
		}
	} break;

	case mtpc_updateChannel: {
		auto &d = update.c_updateChannel();
		if (const auto channel = session().data().channelLoaded(d.vchannel_id().v)) {
			channel->inviter = UserId(0);
			if (channel->amIn()) {
				if (channel->isMegagroup()
					&& !channel->amCreator()
					&& !channel->hasAdminRights()) {
					channel->updateFullForced();
				}
				const auto history = channel->owner().history(channel);
				//if (const auto feed = channel->feed()) { // #feed
				//	feed->requestChatListMessage();
				//	if (!feed->unreadCountKnown()) {
				//		feed->session().api().requestDialogEntry(feed);
				//	}
				//} else {
					history->requestChatListMessage();
					if (!history->unreadCountKnown()) {
						history->session().api().requestDialogEntry(history);
					}
				//}
				if (!channel->amCreator()) {
					session().api().requestSelfParticipant(channel);
				}
			}
		}
	} break;

	case mtpc_updateChannelTooLong: {
		const auto &d = update.c_updateChannelTooLong();
		if (const auto channel = session().data().channelLoaded(d.vchannel_id().v)) {
			const auto pts = d.vpts();
			if (!pts || channel->pts() < pts->v) {
				getChannelDifference(channel);
			}
		}
	} break;

	case mtpc_updateChannelMessageViews: {
		auto &d = update.c_updateChannelMessageViews();
		if (auto item = session().data().message(d.vchannel_id().v, d.vid().v)) {
			item->setViewsCount(d.vviews().v);
		}
	} break;

	case mtpc_updateChannelAvailableMessages: {
		auto &d = update.c_updateChannelAvailableMessages();
		if (const auto channel = session().data().channelLoaded(d.vchannel_id().v)) {
			channel->setAvailableMinId(d.vavailable_min_id().v);
			if (const auto history = session().data().historyLoaded(channel)) {
				history->clearUpTill(d.vavailable_min_id().v);
			}
		}
	} break;

	// Pinned message.
	case mtpc_updateUserPinnedMessage: {
		const auto &d = update.c_updateUserPinnedMessage();
		if (const auto user = session().data().userLoaded(d.vuser_id().v)) {
			user->setPinnedMessageId(d.vid().v);
		}
	} break;

	case mtpc_updateChatPinnedMessage: {
		const auto &d = update.c_updateChatPinnedMessage();
		if (const auto chat = session().data().chatLoaded(d.vchat_id().v)) {
			const auto status = chat->applyUpdateVersion(d.vversion().v);
			if (status == ChatData::UpdateStatus::Good) {
				chat->setPinnedMessageId(d.vid().v);
			}
		}
	} break;

	case mtpc_updateChannelPinnedMessage: {
		const auto &d = update.c_updateChannelPinnedMessage();
		if (const auto channel = session().data().channelLoaded(d.vchannel_id().v)) {
			channel->setPinnedMessageId(d.vid().v);
		}
	} break;

	////// Cloud sticker sets
	case mtpc_updateNewStickerSet: {
		const auto &d = update.c_updateNewStickerSet();
		Stickers::NewSetReceived(d.vstickerset());
	} break;

	case mtpc_updateStickerSetsOrder: {
		auto &d = update.c_updateStickerSetsOrder();
		if (!d.is_masks()) {
			auto &order = d.vorder().v;
			auto &sets = session().data().stickerSets();
			Stickers::Order result;
			for (const auto &item : order) {
				if (sets.constFind(item.v) == sets.cend()) {
					break;
				}
				result.push_back(item.v);
			}
			if (result.size() != session().data().stickerSetsOrder().size() || result.size() != order.size()) {
				session().data().setLastStickersUpdate(0);
				session().api().updateStickers();
			} else {
				session().data().stickerSetsOrderRef() = std::move(result);
				Local::writeInstalledStickers();
				session().data().notifyStickersUpdated();
			}
		}
	} break;

	case mtpc_updateStickerSets: {
		session().data().setLastStickersUpdate(0);
		session().api().updateStickers();
	} break;

	case mtpc_updateRecentStickers: {
		session().data().setLastRecentStickersUpdate(0);
		session().api().updateStickers();
	} break;

	case mtpc_updateFavedStickers: {
		session().data().setLastFavedStickersUpdate(0);
		session().api().updateStickers();
	} break;

	case mtpc_updateReadFeaturedStickers: {
		// We read some of the featured stickers, perhaps not all of them.
		// Here we don't know what featured sticker sets were read, so we
		// request all of them once again.
		session().data().setLastFeaturedStickersUpdate(0);
		session().api().updateStickers();
	} break;

	////// Cloud saved GIFs
	case mtpc_updateSavedGifs: {
		session().data().setLastSavedGifsUpdate(0);
		session().api().updateStickers();
	} break;

	////// Cloud drafts
	case mtpc_updateDraftMessage: {
		const auto &data = update.c_updateDraftMessage();
		const auto peerId = peerFromMTP(data.vpeer());
		data.vdraft().match([&](const MTPDdraftMessage &data) {
			Data::applyPeerCloudDraft(peerId, data);
		}, [&](const MTPDdraftMessageEmpty &data) {
			Data::clearPeerCloudDraft(
				peerId,
				data.vdate().value_or_empty());
		});
	} break;

	////// Cloud langpacks
	case mtpc_updateLangPack: {
		const auto &data = update.c_updateLangPack();
		Lang::CurrentCloudManager().applyLangPackDifference(data.vdifference());
	} break;

	case mtpc_updateLangPackTooLong: {
		const auto &data = update.c_updateLangPackTooLong();
		const auto code = qs(data.vlang_code());
		if (!code.isEmpty()) {
			Lang::CurrentCloudManager().requestLangPackDifference(code);
		}
	} break;

	}
}
