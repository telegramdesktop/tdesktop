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
#include "mainwidget.h"

#include "styles/style_dialogs.h"
#include "styles/style_history.h"
#include "ui/special_buttons.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "window/section_memento.h"
#include "window/section_widget.h"
#include "window/top_bar_widget.h"
#include "data/data_drafts.h"
#include "ui/widgets/dropdown_menu.h"
#include "observer_peer.h"
#include "apiwrap.h"
#include "dialogswidget.h"
#include "historywidget.h"
#include "overviewwidget.h"
#include "lang.h"
#include "boxes/addcontactbox.h"
#include "storage/file_upload.h"
#include "messenger.h"
#include "application.h"
#include "mainwindow.h"
#include "inline_bots/inline_bot_layout_item.h"
#include "boxes/confirmbox.h"
#include "boxes/stickersetbox.h"
#include "boxes/contactsbox.h"
#include "boxes/downloadpathbox.h"
#include "boxes/confirmphonebox.h"
#include "boxes/sharebox.h"
#include "storage/localstorage.h"
#include "shortcuts.h"
#include "media/media_audio.h"
#include "media/player/media_player_panel.h"
#include "media/player/media_player_widget.h"
#include "media/player/media_player_volume_controller.h"
#include "media/player/media_player_instance.h"
#include "core/qthelp_regex.h"
#include "core/qthelp_url.h"
#include "window/themes/window_theme.h"
#include "window/player_wrap_widget.h"
#include "styles/style_boxes.h"
#include "mtproto/dc_options.h"
#include "core/file_utilities.h"
#include "boxes/calendarbox.h"
#include "auth_session.h"
#include "window/notifications_manager.h"

StackItemSection::StackItemSection(std::unique_ptr<Window::SectionMemento> &&memento) : StackItem(nullptr)
, _memento(std::move(memento)) {
}

StackItemSection::~StackItemSection() {
}

MainWidget::MainWidget(QWidget *parent) : TWidget(parent)
, _dialogsWidth(st::dialogsWidthMin)
, _sideShadow(this, st::shadowFg)
, _sideResizeArea(this)
, _dialogs(this)
, _history(this)
, _topBar(this)
, _playerPlaylist(this, Media::Player::Panel::Layout::OnlyPlaylist)
, _playerPanel(this, Media::Player::Panel::Layout::Full)
, _mediaType(this, st::defaultDropdownMenu)
, _api(new ApiWrap(this)) {
	Messenger::Instance().mtp()->setUpdatesHandler(rpcDone(&MainWidget::updateReceived));
	Messenger::Instance().mtp()->setGlobalFailHandler(rpcFail(&MainWidget::updateFail));

	_ptsWaiter.setRequesting(true);
	updateScrollColors();

	connect(_dialogs, SIGNAL(cancelled()), this, SLOT(dialogsCancelled()));
	connect(this, SIGNAL(dialogsUpdated()), _dialogs, SLOT(onListScroll()));
	connect(_history, SIGNAL(cancelled()), _dialogs, SLOT(activate()));
	connect(this, SIGNAL(peerPhotoChanged(PeerData*)), this, SIGNAL(dialogsUpdated()));
	connect(&noUpdatesTimer, SIGNAL(timeout()), this, SLOT(mtpPing()));
	connect(&_onlineTimer, SIGNAL(timeout()), this, SLOT(updateOnline()));
	connect(&_onlineUpdater, SIGNAL(timeout()), this, SLOT(updateOnlineDisplay()));
	connect(&_idleFinishTimer, SIGNAL(timeout()), this, SLOT(checkIdleFinish()));
	connect(&_bySeqTimer, SIGNAL(timeout()), this, SLOT(getDifference()));
	connect(&_byPtsTimer, SIGNAL(timeout()), this, SLOT(onGetDifferenceTimeByPts()));
	connect(&_byMinChannelTimer, SIGNAL(timeout()), this, SLOT(getDifference()));
	connect(&_failDifferenceTimer, SIGNAL(timeout()), this, SLOT(onGetDifferenceTimeAfterFail()));
	connect(_api.get(), SIGNAL(fullPeerUpdated(PeerData*)), this, SLOT(onFullPeerUpdated(PeerData*)));
	connect(this, SIGNAL(peerUpdated(PeerData*)), _history, SLOT(peerUpdated(PeerData*)));
	connect(_topBar, SIGNAL(clicked()), this, SLOT(onTopBarClick()));
	connect(_history, SIGNAL(historyShown(History*,MsgId)), this, SLOT(onHistoryShown(History*,MsgId)));
	connect(&updateNotifySettingTimer, SIGNAL(timeout()), this, SLOT(onUpdateNotifySettings()));
	subscribe(Media::Player::Updated(), [this](const AudioMsgId &audioId) {
		if (audioId.type() != AudioMsgId::Type::Video) {
			handleAudioUpdate(audioId);
		}
	});

	subscribe(Global::RefDialogsListFocused(), [this](bool) {
		updateDialogsWidthAnimated();
	});
	subscribe(Global::RefDialogsListDisplayForced(), [this](bool) {
		updateDialogsWidthAnimated();
	});

	QCoreApplication::instance()->installEventFilter(this);

	connect(&_updateMutedTimer, SIGNAL(timeout()), this, SLOT(onUpdateMuted()));
	connect(&_viewsIncrementTimer, SIGNAL(timeout()), this, SLOT(onViewsIncrement()));

	_webPageOrGameUpdater.setSingleShot(true);
	connect(&_webPageOrGameUpdater, SIGNAL(timeout()), this, SLOT(webPagesOrGamesUpdate()));

	_sideResizeArea->setCursor(style::cur_sizehor);

	using Update = Window::Theme::BackgroundUpdate;
	subscribe(Window::Theme::Background(), [this](const Update &update) {
		if (update.type == Update::Type::New || update.type == Update::Type::Changed) {
			clearCachedBackground();
		}
	});
	connect(&_cacheBackgroundTimer, SIGNAL(timeout()), this, SLOT(onCacheBackground()));

	_playerPanel->setPinCallback([this] { switchToFixedPlayer(); });
	_playerPanel->setCloseCallback([this] { closeBothPlayers(); });
	subscribe(Media::Player::instance()->titleButtonOver(), [this](bool over) {
		if (over) {
			_playerPanel->showFromOther();
		} else {
			_playerPanel->hideFromOther();
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

	subscribe(Adaptive::Changed(), [this]() { handleAdaptiveLayoutUpdate(); });

	auto observeEvents = Notify::PeerUpdate::Flag::SharedMediaChanged;
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(observeEvents, [this](const Notify::PeerUpdate &update) {
		mediaOverviewUpdated(update);
	}));

	_dialogs->show();
	if (Adaptive::OneColumn()) {
		_history->hide();
	} else {
		_history->show();
	}
	_topBar->hide();

	orderWidgets();

	_mediaType->hide();
	_mediaType->setOrigin(Ui::PanelAnimation::Origin::TopRight);
	_topBar->mediaTypeButton()->installEventFilter(_mediaType);
	_sideResizeArea->installEventFilter(this);

	_api->init();

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	Sandbox::startUpdateCheck();
#endif // !TDESKTOP_DISABLE_AUTOUPDATE
}

bool MainWidget::onForward(const PeerId &peer, ForwardWhatMessages what) {
	PeerData *p = App::peer(peer);
	if (!peer || (p->isChannel() && !p->asChannel()->canPublish() && p->asChannel()->isBroadcast()) || (p->isChat() && !p->asChat()->canWrite()) || (p->isUser() && p->asUser()->isInaccessible())) {
		Ui::show(Box<InformBox>(lang(lng_forward_cant)));
		return false;
	}
	_history->cancelReply();
	_toForward.clear();
	if (what == ForwardSelectedMessages) {
		if (_overview) {
			_overview->fillSelectedItems(_toForward, false);
		} else {
			_history->fillSelectedItems(_toForward, false);
		}
	} else {
		HistoryItem *item = 0;
		if (what == ForwardContextMessage) {
			item = App::contextItem();
		} else if (what == ForwardPressedMessage) {
			item = App::pressedItem();
		} else if (what == ForwardPressedLinkMessage) {
			item = App::pressedLinkItem();
		}
		if (item && item->toHistoryMessage() && item->id > 0) {
			_toForward.insert(item->id, item);
		}
	}
	updateForwardingItemRemovedSubscription();
	updateForwardingTexts();
	Ui::showPeerHistory(peer, ShowAtUnreadMsgId);
	_history->onClearSelected();
	_history->updateForwarding();
	return true;
}

bool MainWidget::onShareUrl(const PeerId &peer, const QString &url, const QString &text) {
	PeerData *p = App::peer(peer);
	if (!peer || (p->isChannel() && !p->asChannel()->canPublish() && p->asChannel()->isBroadcast()) || (p->isChat() && !p->asChat()->canWrite()) || (p->isUser() && p->asUser()->isInaccessible())) {
		Ui::show(Box<InformBox>(lang(lng_share_cant)));
		return false;
	}
	History *h = App::history(peer);
	TextWithTags textWithTags = { url + '\n' + text, TextWithTags::Tags() };
	MessageCursor cursor = { url.size() + 1, url.size() + 1 + text.size(), QFIXED_MAX };
	h->setLocalDraft(std::make_unique<Data::Draft>(textWithTags, 0, cursor, false));
	h->clearEditDraft();
	bool opened = _history->peer() && (_history->peer()->id == peer);
	if (opened) {
		_history->applyDraft();
	} else {
		Ui::showPeerHistory(peer, ShowAtUnreadMsgId);
	}
	return true;
}

bool MainWidget::onInlineSwitchChosen(const PeerId &peer, const QString &botAndQuery) {
	PeerData *p = App::peer(peer);
	if (!peer || (p->isChannel() && !p->asChannel()->canPublish() && p->asChannel()->isBroadcast()) || (p->isChat() && !p->asChat()->canWrite()) || (p->isUser() && p->asUser()->isInaccessible())) {
		Ui::show(Box<InformBox>(lang(lng_inline_switch_cant)));
		return false;
	}
	History *h = App::history(peer);
	TextWithTags textWithTags = { botAndQuery, TextWithTags::Tags() };
	MessageCursor cursor = { botAndQuery.size(), botAndQuery.size(), QFIXED_MAX };
	h->setLocalDraft(std::make_unique<Data::Draft>(textWithTags, 0, cursor, false));
	h->clearEditDraft();
	bool opened = _history->peer() && (_history->peer()->id == peer);
	if (opened) {
		_history->applyDraft();
	} else {
		Ui::showPeerHistory(peer, ShowAtUnreadMsgId);
	}
	return true;
}

bool MainWidget::hasForwardingItems() {
	return !_toForward.isEmpty();
}

void MainWidget::fillForwardingInfo(Text *&from, Text *&text, bool &serviceColor, ImagePtr &preview) {
	if (_toForward.isEmpty()) return;
	int32 version = 0;
	for (SelectedItemSet::const_iterator i = _toForward.cbegin(), e = _toForward.cend(); i != e; ++i) {
		version += i.value()->authorOriginal()->nameVersion;
	}
	if (version != _toForwardNameVersion) {
		updateForwardingTexts();
	}
	from = &_toForwardFrom;
	text = &_toForwardText;
	serviceColor = (_toForward.size() > 1) || _toForward.cbegin().value()->getMedia() || _toForward.cbegin().value()->serviceMsg();
	if (_toForward.size() < 2 && _toForward.cbegin().value()->getMedia() && _toForward.cbegin().value()->getMedia()->hasReplyPreview()) {
		preview = _toForward.cbegin().value()->getMedia()->replyPreview();
	}
}

void MainWidget::updateForwardingTexts() {
	int32 version = 0;
	QString from, text;
	if (!_toForward.isEmpty()) {
		QMap<PeerData*, bool> fromUsersMap;
		QVector<PeerData*> fromUsers;
		fromUsers.reserve(_toForward.size());
		for (SelectedItemSet::const_iterator i = _toForward.cbegin(), e = _toForward.cend(); i != e; ++i) {
			PeerData *from = i.value()->authorOriginal();
			if (!fromUsersMap.contains(from)) {
				fromUsersMap.insert(from, true);
				fromUsers.push_back(from);
			}
			version += from->nameVersion;
		}
		if (fromUsers.size() > 2) {
			from = lng_forwarding_from(lt_user, fromUsers.at(0)->shortName(), lt_count, fromUsers.size() - 1);
		} else if (fromUsers.size() < 2) {
			from = fromUsers.at(0)->name;
		} else {
			from = lng_forwarding_from_two(lt_user, fromUsers.at(0)->shortName(), lt_second_user, fromUsers.at(1)->shortName());
		}

		if (_toForward.size() < 2) {
			text = _toForward.cbegin().value()->inReplyText();
		} else {
			text = lng_forward_messages(lt_count, _toForward.size());
		}
	}
	_toForwardFrom.setText(st::msgNameStyle, from, _textNameOptions);
	_toForwardText.setText(st::messageTextStyle, textClean(text), _textDlgOptions);
	_toForwardNameVersion = version;
}

void MainWidget::updateForwardingItemRemovedSubscription() {
	if (_toForward.isEmpty()) {
		unsubscribe(_forwardingItemRemovedSubscription);
		_forwardingItemRemovedSubscription = 0;
	} else if (!_forwardingItemRemovedSubscription) {
		_forwardingItemRemovedSubscription = subscribe(Global::RefItemRemoved(), [this](HistoryItem *item) {
			auto i = _toForward.find(item->id);
			if (i == _toForward.cend() || i.value() != item) {
				i = _toForward.find(item->id - ServerMaxMsgId);
			}
			if (i != _toForward.cend() && i.value() == item) {
				_toForward.erase(i);
				updateForwardingItemRemovedSubscription();
				updateForwardingTexts();
			}
		});
	}
}

void MainWidget::cancelForwarding() {
	if (_toForward.isEmpty()) return;

	_toForward.clear();
	_history->cancelForwarding();
	updateForwardingItemRemovedSubscription();
}

void MainWidget::finishForwarding(History *history, bool silent) {
	if (!history) return;

	if (!_toForward.isEmpty()) {
		bool genClientSideMessage = (_toForward.size() < 2);
		PeerData *forwardFrom = 0;
		App::main()->readServerHistory(history);

		MTPDmessage::Flags flags = 0;
		MTPmessages_ForwardMessages::Flags sendFlags = 0;
		bool channelPost = history->peer->isChannel() && !history->peer->isMegagroup();
		bool showFromName = !channelPost || history->peer->asChannel()->addsSignature();
		bool silentPost = channelPost && silent;
		if (channelPost) {
			flags |= MTPDmessage::Flag::f_views;
			flags |= MTPDmessage::Flag::f_post;
		}
		if (showFromName) {
			flags |= MTPDmessage::Flag::f_from_id;
		}
		if (silentPost) {
			sendFlags |= MTPmessages_ForwardMessages::Flag::f_silent;
		}

		QVector<MTPint> ids;
		QVector<MTPlong> randomIds;
		ids.reserve(_toForward.size());
		randomIds.reserve(_toForward.size());
		for (SelectedItemSet::const_iterator i = _toForward.cbegin(), e = _toForward.cend(); i != e; ++i) {
			uint64 randomId = rand_value<uint64>();
			if (genClientSideMessage) {
				FullMsgId newId(peerToChannel(history->peer->id), clientMsgId());
				auto msg = static_cast<HistoryMessage*>(_toForward.cbegin().value());
				auto messageFromId = showFromName ? AuthSession::CurrentUserId() : 0;
				history->addNewForwarded(newId.msg, flags, date(MTP_int(unixtime())), messageFromId, msg);
				App::historyRegRandom(randomId, newId);
			}
			if (forwardFrom != i.value()->history()->peer) {
				if (forwardFrom) {
					history->sendRequestId = MTP::send(MTPmessages_ForwardMessages(MTP_flags(sendFlags), forwardFrom->input, MTP_vector<MTPint>(ids), MTP_vector<MTPlong>(randomIds), history->peer->input), rpcDone(&MainWidget::sentUpdatesReceived), RPCFailHandlerPtr(), 0, 0, history->sendRequestId);
					ids.resize(0);
					randomIds.resize(0);
				}
				forwardFrom = i.value()->history()->peer;
			}
			ids.push_back(MTP_int(i.value()->id));
			randomIds.push_back(MTP_long(randomId));
		}
		history->sendRequestId = MTP::send(MTPmessages_ForwardMessages(MTP_flags(sendFlags), forwardFrom->input, MTP_vector<MTPint>(ids), MTP_vector<MTPlong>(randomIds), history->peer->input), rpcDone(&MainWidget::sentUpdatesReceived), RPCFailHandlerPtr(), 0, 0, history->sendRequestId);

		if (_history->peer() == history->peer) {
			_history->peerMessagesUpdated();
		}

		cancelForwarding();
	}

	historyToDown(history);
	dialogsToUp();
	_history->peerMessagesUpdated(history->peer->id);
}

void MainWidget::webPageUpdated(WebPageData *data) {
	_webPagesUpdated.insert(data->id);
	_webPageOrGameUpdater.start(0);
}

void MainWidget::gameUpdated(GameData *data) {
	_gamesUpdated.insert(data->id);
	_webPageOrGameUpdater.start(0);
}

void MainWidget::webPagesOrGamesUpdate() {
	_webPageOrGameUpdater.stop();
	if (!_webPagesUpdated.isEmpty()) {
		auto &items = App::webPageItems();
		for_const (auto webPageId, _webPagesUpdated) {
			auto j = items.constFind(App::webPage(webPageId));
			if (j != items.cend()) {
				for_const (auto item, j.value()) {
					item->setPendingInitDimensions();
				}
			}
		}
		_webPagesUpdated.clear();
	}
	if (!_gamesUpdated.isEmpty()) {
		auto &items = App::gameItems();
		for_const (auto gameId, _gamesUpdated) {
			auto j = items.constFind(App::game(gameId));
			if (j != items.cend()) {
				for_const (auto item, j.value()) {
					item->setPendingInitDimensions();
				}
			}
		}
		_gamesUpdated.clear();
	}
}

void MainWidget::updateMutedIn(int32 seconds) {
	if (seconds > 86400) seconds = 86400;
	int32 ms = seconds * 1000;
	if (_updateMutedTimer.isActive() && _updateMutedTimer.remainingTime() <= ms) return;
	_updateMutedTimer.start(ms);
}

void MainWidget::updateStickers() {
	_history->updateStickers();
}

void MainWidget::onUpdateMuted() {
	App::updateMuted();
}

void MainWidget::onShareContact(const PeerId &peer, UserData *contact) {
	_history->onShareContact(peer, contact);
}

bool MainWidget::onSendPaths(const PeerId &peer) {
	Ui::showPeerHistory(peer, ShowAtTheEndMsgId);
	return _history->confirmSendingFiles(cSendPaths());
}

void MainWidget::onFilesOrForwardDrop(const PeerId &peer, const QMimeData *data) {
	if (data->hasFormat(qsl("application/x-td-forward-selected"))) {
		onForward(peer, ForwardSelectedMessages);
	} else if (data->hasFormat(qsl("application/x-td-forward-pressed-link"))) {
		onForward(peer, ForwardPressedLinkMessage);
	} else if (data->hasFormat(qsl("application/x-td-forward-pressed"))) {
		onForward(peer, ForwardPressedMessage);
	} else {
		Ui::showPeerHistory(peer, ShowAtTheEndMsgId);
		_history->confirmSendingFiles(data);
	}
}

void MainWidget::rpcClear() {
	_history->rpcClear();
	_dialogs->rpcClear();
	if (_overview) _overview->rpcClear();
	if (_api) _api->rpcClear();
	RPCSender::rpcClear();
}

bool MainWidget::isItemVisible(HistoryItem *item) {
	if (isHidden() || _a_show.animating()) {
		return false;
	}
	return _history->isItemVisible(item);
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

void MainWidget::notify_userIsContactChanged(UserData *user, bool fromThisApp) {
	if (!user) return;

	_dialogs->notify_userIsContactChanged(user, fromThisApp);

	const SharedContactItems &items(App::sharedContactItems());
	SharedContactItems::const_iterator i = items.constFind(peerToUser(user->id));
	if (i != items.cend()) {
		for_const (auto item, i.value()) {
			item->setPendingInitDimensions();
		}
	}

	if (user->contact > 0 && fromThisApp) {
		Ui::showPeerHistory(user->id, ShowAtTheEndMsgId);
	}
}

void MainWidget::notify_migrateUpdated(PeerData *peer) {
	_history->notify_migrateUpdated(peer);
}

void MainWidget::notify_clipStopperHidden(ClipStopperType type) {
	_history->notify_clipStopperHidden(type);
}

void MainWidget::ui_repaintHistoryItem(const HistoryItem *item) {
	_history->ui_repaintHistoryItem(item);
	if (item->history()->lastMsg == item) {
		item->history()->updateChatListEntry();
	}
	_playerPlaylist->ui_repaintHistoryItem(item);
	_playerPanel->ui_repaintHistoryItem(item);
	if (_overview) _overview->ui_repaintHistoryItem(item);
}

void MainWidget::ui_repaintInlineItem(const InlineBots::Layout::ItemBase *layout) {
	_history->ui_repaintInlineItem(layout);
}

bool MainWidget::ui_isInlineItemVisible(const InlineBots::Layout::ItemBase *layout) {
	return _history->ui_isInlineItemVisible(layout);
}

bool MainWidget::ui_isInlineItemBeingChosen() {
	return _history->ui_isInlineItemBeingChosen();
}

void MainWidget::notify_historyItemLayoutChanged(const HistoryItem *item) {
	_history->notify_historyItemLayoutChanged(item);
	if (_overview) _overview->notify_historyItemLayoutChanged(item);
}

void MainWidget::notify_inlineItemLayoutChanged(const InlineBots::Layout::ItemBase *layout) {
	_history->notify_inlineItemLayoutChanged(layout);
}

void MainWidget::notify_historyMuteUpdated(History *history) {
	_dialogs->notify_historyMuteUpdated(history);
}

void MainWidget::notify_handlePendingHistoryUpdate() {
	_history->notify_handlePendingHistoryUpdate();
}

bool MainWidget::cmd_search() {
	if (Ui::isLayerShown() || Ui::isMediaViewShown()) return false;
	return _history->cmd_search();
}

bool MainWidget::cmd_next_chat() {
	if (Ui::isLayerShown() || Ui::isMediaViewShown()) return false;
	return _history->cmd_next_chat();
}

bool MainWidget::cmd_previous_chat() {
	if (Ui::isLayerShown() || Ui::isMediaViewShown()) return false;
	return _history->cmd_previous_chat();
}

void MainWidget::noHider(HistoryHider *destroyed) {
	if (_hider == destroyed) {
		_hider = nullptr;
		if (Adaptive::OneColumn()) {
			if (_forwardConfirm) {
				_forwardConfirm->closeBox();
				_forwardConfirm = nullptr;
			}
			onHistoryShown(_history->history(), _history->msgId());
			if (_wideSection || _overview || (_history->peer() && _history->peer()->id)) {
				auto animationParams = ([this] {
					if (_overview) {
						return prepareOverviewAnimation();
					} else if (_wideSection) {
						return prepareWideSectionAnimation(_wideSection);
					}
					return prepareHistoryAnimation(_history->peer() ? _history->peer()->id : 0);
				})();
				_dialogs->hide();
				if (_overview) {
					_overview->showAnimated(Window::SlideDirection::FromRight, animationParams);
				} else if (_wideSection) {
					_wideSection->showAnimated(Window::SlideDirection::FromRight, animationParams);
				} else {
					_history->showAnimated(Window::SlideDirection::FromRight, animationParams);
				}
			}
		} else {
			if (_forwardConfirm) {
				_forwardConfirm->deleteLater();
				_forwardConfirm = nullptr;
			}
		}
	}
}

void MainWidget::hiderLayer(object_ptr<HistoryHider> h) {
	if (App::passcoded()) {
		return;
	}

	_hider = std::move(h);
	connect(_hider, SIGNAL(forwarded()), _dialogs, SLOT(onCancelSearch()));
	if (Adaptive::OneColumn()) {
		dialogsToUp();

		_hider->hide();
		auto animationParams = prepareDialogsAnimation();

		onHistoryShown(0, 0);
		if (_overview) {
			_overview->hide();
		} else if (_wideSection) {
			_wideSection->hide();
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
		_dialogs->activate();
	}
}

void MainWidget::forwardLayer(int forwardSelected) {
	hiderLayer((forwardSelected < 0) ? object_ptr<HistoryHider>(this) : object_ptr<HistoryHider>(this, forwardSelected > 0));
}

void MainWidget::deleteLayer(int selectedCount) {
	if (selectedCount) {
		auto forDelete = true;
		SelectedItemSet selected;
		if (_overview) {
			_overview->fillSelectedItems(selected, forDelete);
		} else {
			_history->fillSelectedItems(selected, forDelete);
		}
		if (!selected.isEmpty()) {
			Ui::show(Box<DeleteMessagesBox>(selected));
		}
	} else if (auto item = App::contextItem()) {
		auto suggestModerateActions = !_overview;
		Ui::show(Box<DeleteMessagesBox>(item, suggestModerateActions));
	}
}

void MainWidget::cancelUploadLayer() {
	auto item = App::contextItem();
	if (!item) {
		return;
	}

	App::uploader()->pause(item->fullId());
	Ui::show(Box<ConfirmBox>(lang(lng_selected_cancel_sure_this), lang(lng_selected_upload_stop), lang(lng_continue), base::lambda_guarded(this, [this] {
		if (_overview) {
			_overview->deleteContextItem(false);
		} else {
			_history->deleteContextItem(false);
		}
		App::uploader()->unpause();
	}), base::lambda_guarded(this, [] {
		App::uploader()->unpause();
	})));
}

void MainWidget::deletePhotoLayer(PhotoData *photo) {
	if (!photo) return;
	Ui::show(Box<ConfirmBox>(lang(lng_delete_photo_sure), lang(lng_box_delete), base::lambda_guarded(this, [this, photo] {
		Ui::hideLayer();

		auto me = App::self();
		if (!me) return;

		if (me->photoId == photo->id) {
			App::app()->peerClearPhoto(me->id);
		} else if (photo->peer && !photo->peer->isUser() && photo->peer->photoId == photo->id) {
			App::app()->peerClearPhoto(photo->peer->id);
		} else {
			for (int i = 0, l = me->photos.size(); i != l; ++i) {
				if (me->photos.at(i) == photo) {
					me->photos.removeAt(i);
					MTP::send(MTPphotos_DeletePhotos(MTP_vector<MTPInputPhoto>(1, MTP_inputPhoto(MTP_long(photo->id), MTP_long(photo->access)))));
					break;
				}
			}
		}
	})));
}

void MainWidget::shareContactLayer(UserData *contact) {
	hiderLayer(object_ptr<HistoryHider>(this, contact));
}

void MainWidget::shareUrlLayer(const QString &url, const QString &text) {
	// Don't allow to insert an inline bot query by share url link.
	if (url.trimmed().startsWith('@')) {
		return;
	}
	hiderLayer(object_ptr<HistoryHider>(this, url, text));
}

void MainWidget::inlineSwitchLayer(const QString &botAndQuery) {
	hiderLayer(object_ptr<HistoryHider>(this, botAndQuery));
}

bool MainWidget::selectingPeer(bool withConfirm) {
	return _hider ? (withConfirm ? _hider->withConfirm() : true) : false;
}

bool MainWidget::selectingPeerForInlineSwitch() {
	return selectingPeer() ? !_hider->botAndQuery().isEmpty() : false;
}

void MainWidget::offerPeer(PeerId peer) {
	Ui::hideLayer();
	if (_hider->offerPeer(peer) && Adaptive::OneColumn()) {
		_forwardConfirm = Ui::show(Box<ConfirmBox>(_hider->offeredText(), lang(lng_forward_send), base::lambda_guarded(this, [this] {
			_hider->forward();
			if (_forwardConfirm) _forwardConfirm->closeBox();
			if (_hider) _hider->offerPeer(0);
		}), base::lambda_guarded(this, [this] {
			if (_hider && _forwardConfirm) _hider->offerPeer(0);
		})));
	}
}

void MainWidget::dialogsActivate() {
	_dialogs->activate();
}

DragState MainWidget::getDragState(const QMimeData *mime) {
	return _history->getDragState(mime);
}

bool MainWidget::leaveChatFailed(PeerData *peer, const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (error.type() == qstr("USER_NOT_PARTICIPANT") || error.type() == qstr("CHAT_ID_INVALID") || error.type() == qstr("PEER_ID_INVALID")) { // left this chat already
		deleteConversation(peer);
		return true;
	}
	return false;
}

void MainWidget::deleteHistoryAfterLeave(PeerData *peer, const MTPUpdates &updates) {
	sentUpdatesReceived(updates);
	deleteConversation(peer);
}

void MainWidget::deleteHistoryPart(DeleteHistoryRequest request, const MTPmessages_AffectedHistory &result) {
	auto peer = request.peer;

	const auto &d(result.c_messages_affectedHistory());
	if (peer && peer->isChannel()) {
		if (peer->asChannel()->ptsUpdated(d.vpts.v, d.vpts_count.v)) {
			peer->asChannel()->ptsApplySkippedUpdates();
		}
	} else {
		if (ptsUpdated(d.vpts.v, d.vpts_count.v)) {
			ptsApplySkippedUpdates();
		}
	}

	int32 offset = d.voffset.v;
	if (offset <= 0) {
		cRefReportSpamStatuses().remove(peer->id);
		Local::writeReportSpamStatuses();
		return;
	}

	MTPmessages_DeleteHistory::Flags flags = 0;
	if (request.justClearHistory) {
		flags |= MTPmessages_DeleteHistory::Flag::f_just_clear;
	}
	MTP::send(MTPmessages_DeleteHistory(MTP_flags(flags), peer->input, MTP_int(0)), rpcDone(&MainWidget::deleteHistoryPart, request));
}

void MainWidget::deleteMessages(PeerData *peer, const QVector<MTPint> &ids, bool forEveryone) {
	if (peer->isChannel()) {
		MTP::send(MTPchannels_DeleteMessages(peer->asChannel()->inputChannel, MTP_vector<MTPint>(ids)), rpcDone(&MainWidget::messagesAffected, peer));
	} else {
		auto flags = MTPmessages_DeleteMessages::Flags(0);
		if (forEveryone) {
			flags |= MTPmessages_DeleteMessages::Flag::f_revoke;
		}
		MTP::send(MTPmessages_DeleteMessages(MTP_flags(flags), MTP_vector<MTPint>(ids)), rpcDone(&MainWidget::messagesAffected, peer));
	}
}

void MainWidget::deletedContact(UserData *user, const MTPcontacts_Link &result) {
	auto &d(result.c_contacts_link());
	App::feedUsers(MTP_vector<MTPUser>(1, d.vuser));
	App::feedUserLink(MTP_int(peerToUser(user->id)), d.vmy_link, d.vforeign_link);
}

void MainWidget::removeDialog(History *history) {
	_dialogs->removeDialog(history);
}

void MainWidget::deleteConversation(PeerData *peer, bool deleteHistory) {
	if (activePeer() == peer) {
		Ui::showChatsList();
	}
	if (auto history = App::historyLoaded(peer->id)) {
		history->setPinnedDialog(false);
		removeDialog(history);
		if (peer->isMegagroup() && peer->asChannel()->mgInfo->migrateFromPtr) {
			if (auto migrated = App::historyLoaded(peer->asChannel()->mgInfo->migrateFromPtr->id)) {
				if (migrated->lastMsg) { // return initial dialog
					migrated->setLastMessage(migrated->lastMsg);
				} else {
					checkPeerHistory(migrated->peer);
				}
			}
		}
		history->clear();
		history->newLoaded = true;
		history->oldLoaded = deleteHistory;
	}
	if (peer->isChannel()) {
		peer->asChannel()->ptsWaitingForShortPoll(-1);
	}
	if (deleteHistory) {
		DeleteHistoryRequest request = { peer, false };
		MTPmessages_DeleteHistory::Flags flags = 0;
		MTP::send(MTPmessages_DeleteHistory(MTP_flags(flags), peer->input, MTP_int(0)), rpcDone(&MainWidget::deleteHistoryPart, request));
	}
}

void MainWidget::deleteAndExit(ChatData *chat) {
	PeerData *peer = chat;
	MTP::send(MTPmessages_DeleteChatUser(chat->inputChat, App::self()->inputUser), rpcDone(&MainWidget::deleteHistoryAfterLeave, peer), rpcFail(&MainWidget::leaveChatFailed, peer));
}

void MainWidget::deleteAllFromUser(ChannelData *channel, UserData *from) {
	t_assert(channel != nullptr && from != nullptr);

	QVector<MsgId> toDestroy;
	if (auto history = App::historyLoaded(channel->id)) {
		for_const (auto block, history->blocks) {
			for_const (auto item, block->items) {
				if (item->from() == from && item->canDelete()) {
					toDestroy.push_back(item->id);
				}
			}
		}
		for_const (auto &msgId, toDestroy) {
			if (auto item = App::histItemById(peerToChannel(channel->id), msgId)) {
				item->destroy();
			}
		}
	}
	MTP::send(MTPchannels_DeleteUserHistory(channel->inputChannel, from->inputUser), rpcDone(&MainWidget::deleteAllFromUserPart, { channel, from }));
}

void MainWidget::deleteAllFromUserPart(DeleteAllFromUserParams params, const MTPmessages_AffectedHistory &result) {
	const auto &d(result.c_messages_affectedHistory());
	if (params.channel->ptsUpdated(d.vpts.v, d.vpts_count.v)) {
		params.channel->ptsApplySkippedUpdates();
	}

	int32 offset = d.voffset.v;
	if (offset > 0) {
		MTP::send(MTPchannels_DeleteUserHistory(params.channel->inputChannel, params.from->inputUser), rpcDone(&MainWidget::deleteAllFromUserPart, params));
	} else if (History *h = App::historyLoaded(params.channel)) {
		if (!h->lastMsg) {
			checkPeerHistory(params.channel);
		}
	}
}

void MainWidget::clearHistory(PeerData *peer) {
	if (History *h = App::historyLoaded(peer->id)) {
		if (h->lastMsg) {
			Local::addSavedPeer(h->peer, h->lastMsg->date);
		}
		h->clear();
		h->newLoaded = h->oldLoaded = true;
	}
	MTPmessages_DeleteHistory::Flags flags = MTPmessages_DeleteHistory::Flag::f_just_clear;
	DeleteHistoryRequest request = { peer, true };
	MTP::send(MTPmessages_DeleteHistory(MTP_flags(flags), peer->input, MTP_int(0)), rpcDone(&MainWidget::deleteHistoryPart, request));
}

void MainWidget::addParticipants(PeerData *chatOrChannel, const QVector<UserData*> &users) {
	if (chatOrChannel->isChat()) {
		auto chat = chatOrChannel->asChat();
		for_const (auto user, users) {
			MTP::send(MTPmessages_AddChatUser(chat->inputChat, user->inputUser, MTP_int(ForwardOnAdd)), rpcDone(&MainWidget::sentUpdatesReceived), rpcFail(&MainWidget::addParticipantFail, { user, chat }), 0, 5);
		}
	} else if (chatOrChannel->isChannel()) {
		QVector<MTPInputUser> inputUsers;
		inputUsers.reserve(qMin(users.size(), int(MaxUsersPerInvite)));
		for (QVector<UserData*>::const_iterator i = users.cbegin(), e = users.cend(); i != e; ++i) {
			inputUsers.push_back((*i)->inputUser);
			if (inputUsers.size() == MaxUsersPerInvite) {
				MTP::send(MTPchannels_InviteToChannel(chatOrChannel->asChannel()->inputChannel, MTP_vector<MTPInputUser>(inputUsers)), rpcDone(&MainWidget::inviteToChannelDone, chatOrChannel->asChannel()), rpcFail(&MainWidget::addParticipantsFail, chatOrChannel->asChannel()), 0, 5);
				inputUsers.clear();
			}
		}
		if (!inputUsers.isEmpty()) {
			MTP::send(MTPchannels_InviteToChannel(chatOrChannel->asChannel()->inputChannel, MTP_vector<MTPInputUser>(inputUsers)), rpcDone(&MainWidget::inviteToChannelDone, chatOrChannel->asChannel()), rpcFail(&MainWidget::addParticipantsFail, chatOrChannel->asChannel()), 0, 5);
		}
	}
}

bool MainWidget::addParticipantFail(UserAndPeer data, const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	QString text = lang(lng_failed_add_participant);
	if (error.type() == qstr("USER_LEFT_CHAT")) { // trying to return a user who has left
	} else if (error.type() == qstr("USER_KICKED")) { // trying to return a user who was kicked by admin
		text = lang(lng_cant_invite_banned);
	} else if (error.type() == qstr("USER_PRIVACY_RESTRICTED")) {
		text = lang(lng_cant_invite_privacy);
	} else if (error.type() == qstr("USER_NOT_MUTUAL_CONTACT")) { // trying to return user who does not have me in contacts
		text = lang(lng_failed_add_not_mutual);
	} else if (error.type() == qstr("USER_ALREADY_PARTICIPANT") && data.user->botInfo) {
		text = lang(lng_bot_already_in_group);
	} else if (error.type() == qstr("PEER_FLOOD")) {
		text = PeerFloodErrorText((data.peer->isChat() || data.peer->isMegagroup()) ? PeerFloodType::InviteGroup : PeerFloodType::InviteChannel);
	}
	Ui::show(Box<InformBox>(text));
	return false;
}

bool MainWidget::addParticipantsFail(ChannelData *channel, const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	QString text = lang(lng_failed_add_participant);
	if (error.type() == qstr("USER_LEFT_CHAT")) { // trying to return banned user to his group
	} else if (error.type() == qstr("USER_KICKED")) { // trying to return a user who was kicked by admin
		text = lang(lng_cant_invite_banned);
	} else if (error.type() == qstr("USER_PRIVACY_RESTRICTED")) {
		text = lang(channel->isMegagroup() ? lng_cant_invite_privacy : lng_cant_invite_privacy_channel);
	} else if (error.type() == qstr("USER_NOT_MUTUAL_CONTACT")) { // trying to return user who does not have me in contacts
		text = lang(channel->isMegagroup() ? lng_failed_add_not_mutual : lng_failed_add_not_mutual_channel);
	} else if (error.type() == qstr("PEER_FLOOD")) {
		text = PeerFloodErrorText(PeerFloodType::InviteGroup);
	}
	Ui::show(Box<InformBox>(text));
	return false;
}

void MainWidget::kickParticipant(ChatData *chat, UserData *user) {
	MTP::send(MTPmessages_DeleteChatUser(chat->inputChat, user->inputUser), rpcDone(&MainWidget::sentUpdatesReceived), rpcFail(&MainWidget::kickParticipantFail, chat));
	Ui::showPeerHistory(chat->id, ShowAtTheEndMsgId);
}

bool MainWidget::kickParticipantFail(ChatData *chat, const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	error.type();
	return false;
}

void MainWidget::checkPeerHistory(PeerData *peer) {
	MTP::send(MTPmessages_GetHistory(peer->input, MTP_int(0), MTP_int(0), MTP_int(0), MTP_int(1), MTP_int(0), MTP_int(0)), rpcDone(&MainWidget::checkedHistory, peer));
}

void MainWidget::checkedHistory(PeerData *peer, const MTPmessages_Messages &result) {
	const QVector<MTPMessage> *v = 0;
	switch (result.type()) {
	case mtpc_messages_messages: {
		auto &d(result.c_messages_messages());
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		v = &d.vmessages.c_vector().v;
	} break;

	case mtpc_messages_messagesSlice: {
		auto &d(result.c_messages_messagesSlice());
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		v = &d.vmessages.c_vector().v;
	} break;

	case mtpc_messages_channelMessages: {
		auto &d(result.c_messages_channelMessages());
		if (peer && peer->isChannel()) {
			peer->asChannel()->ptsReceived(d.vpts.v);
		} else {
			LOG(("API Error: received messages.channelMessages when no channel was passed! (MainWidget::checkedHistory)"));
		}
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		v = &d.vmessages.c_vector().v;
	} break;
	}
	if (!v) return;

	if (v->isEmpty()) {
		if (peer->isChat() && !peer->asChat()->haveLeft()) {
			History *h = App::historyLoaded(peer->id);
			if (h) Local::addSavedPeer(peer, h->lastMsgDate);
		} else if (peer->isChannel()) {
			if (peer->asChannel()->inviter > 0 && peer->asChannel()->amIn()) {
				if (UserData *from = App::userLoaded(peer->asChannel()->inviter)) {
					History *h = App::history(peer->id);
					h->clear(true);
					h->addNewerSlice(QVector<MTPMessage>());
					h->asChannelHistory()->insertJoinedMessage(true);
					_history->peerMessagesUpdated(h->peer->id);
				}
			}
		} else {
			deleteConversation(peer, false);
		}
	} else {
		History *h = App::history(peer->id);
		if (!h->lastMsg) {
			h->addNewMessage((*v)[0], NewMessageLast);
		}
		if (!h->lastMsgDate.isNull() && h->loadedAtBottom()) {
			if (peer->isChannel() && peer->asChannel()->inviter > 0 && h->lastMsgDate <= peer->asChannel()->inviteDate && peer->asChannel()->amIn()) {
				if (UserData *from = App::userLoaded(peer->asChannel()->inviter)) {
					h->asChannelHistory()->insertJoinedMessage(true);
					_history->peerMessagesUpdated(h->peer->id);
				}
			}
		}
	}
}

bool MainWidget::sendMessageFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (error.type() == qstr("PEER_FLOOD")) {
		Ui::show(Box<InformBox>(PeerFloodErrorText(PeerFloodType::Send)));
		return true;
	}
	return false;
}

void MainWidget::onCacheBackground() {
	if (Window::Theme::Background()->tile()) {
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

void MainWidget::forwardSelectedItems() {
	if (_overview) {
		_overview->onForwardSelected();
	} else {
		_history->onForwardSelected();
	}
}

void MainWidget::confirmDeleteSelectedItems() {
	if (_overview) {
		_overview->confirmDeleteSelectedItems();
	} else {
		_history->confirmDeleteSelectedItems();
	}
}

void MainWidget::clearSelectedItems() {
	if (_overview) {
		_overview->onClearSelected();
	} else {
		_history->onClearSelected();
	}
}

Dialogs::IndexedList *MainWidget::contactsList() {
	return _dialogs->contactsList();
}

Dialogs::IndexedList *MainWidget::dialogsList() {
	return _dialogs->dialogsList();
}

namespace {
QString parseCommandFromMessage(History *history, const QString &message) {
	if (history->peer->id != peerFromUser(ServiceUserId)) {
		return QString();
	}
	if (message.size() < 3 || message.at(0) != '*' || message.at(message.size() - 1) != '*') {
		return QString();
	}
	QString command = message.mid(1, message.size() - 2);
	QStringList commands;
	commands.push_back(qsl("new_version_text"));
	commands.push_back(qsl("all_new_version_texts"));
	if (commands.indexOf(command) < 0) {
		return QString();
	}
	return command;
}

void executeParsedCommand(const QString &command) {
	if (command.isEmpty() || !App::wnd()) {
		return;
	}
	if (command == qsl("new_version_text")) {
		App::wnd()->serviceNotificationLocal(langNewVersionText());
	} else if (command == qsl("all_new_version_texts")) {
		for (int i = 0; i < languageCount; ++i) {
			App::wnd()->serviceNotificationLocal(langNewVersionTextForLang(i));
		}
	}
}
} // namespace

void MainWidget::sendMessage(const MessageToSend &message) {
	auto history = message.history;
	auto &textWithTags = message.textWithTags;

	readServerHistory(history);
	_history->fastShowAtEnd(history);

	if (!history || !_history->canSendMessages(history->peer)) {
		return;
	}

	saveRecentHashtags(textWithTags.text);

	EntitiesInText sendingEntities, leftEntities = entitiesFromTextTags(textWithTags.tags);
	auto prepareFlags = itemTextOptions(history, App::self()).flags;
	QString sendingText, leftText = prepareTextWithEntities(textWithTags.text, prepareFlags, &leftEntities);

	QString command = parseCommandFromMessage(history, textWithTags.text);
	HistoryItem *lastMessage = nullptr;

	MsgId replyTo = (message.replyTo < 0) ? _history->replyToId() : message.replyTo;
	while (command.isEmpty() && textSplit(sendingText, sendingEntities, leftText, leftEntities, MaxMessageSize)) {
		FullMsgId newId(peerToChannel(history->peer->id), clientMsgId());
		uint64 randomId = rand_value<uint64>();

		trimTextWithEntities(sendingText, &sendingEntities);

		App::historyRegRandom(randomId, newId);
		App::historyRegSentData(randomId, history->peer->id, sendingText);

		MTPstring msgText(MTP_string(sendingText));
		MTPDmessage::Flags flags = newMessageFlags(history->peer) | MTPDmessage::Flag::f_entities; // unread, out
		MTPmessages_SendMessage::Flags sendFlags = 0;
		if (replyTo) {
			flags |= MTPDmessage::Flag::f_reply_to_msg_id;
			sendFlags |= MTPmessages_SendMessage::Flag::f_reply_to_msg_id;
		}
		MTPMessageMedia media = MTP_messageMediaEmpty();
		if (message.webPageId == CancelledWebPageId) {
			sendFlags |= MTPmessages_SendMessage::Flag::f_no_webpage;
		} else if (message.webPageId) {
			auto page = App::webPage(message.webPageId);
			media = MTP_messageMediaWebPage(MTP_webPagePending(MTP_long(page->id), MTP_int(page->pendingTill)));
			flags |= MTPDmessage::Flag::f_media;
		}
		bool channelPost = history->peer->isChannel() && !history->peer->isMegagroup();
		bool showFromName = !channelPost || history->peer->asChannel()->addsSignature();
		bool silentPost = channelPost && message.silent;
		if (channelPost) {
			flags |= MTPDmessage::Flag::f_views;
			flags |= MTPDmessage::Flag::f_post;
		}
		if (showFromName) {
			flags |= MTPDmessage::Flag::f_from_id;
		}
		if (silentPost) {
			sendFlags |= MTPmessages_SendMessage::Flag::f_silent;
		}
		MTPVector<MTPMessageEntity> localEntities = linksToMTP(sendingEntities), sentEntities = linksToMTP(sendingEntities, true);
		if (!sentEntities.c_vector().v.isEmpty()) {
			sendFlags |= MTPmessages_SendMessage::Flag::f_entities;
		}
		if (message.clearDraft) {
			sendFlags |= MTPmessages_SendMessage::Flag::f_clear_draft;
			history->clearCloudDraft();
		}
		auto messageFromId = showFromName ? AuthSession::CurrentUserId() : 0;
		lastMessage = history->addNewMessage(MTP_message(MTP_flags(flags), MTP_int(newId.msg), MTP_int(messageFromId), peerToMTP(history->peer->id), MTPnullFwdHeader, MTPint(), MTP_int(replyTo), MTP_int(unixtime()), msgText, media, MTPnullMarkup, localEntities, MTP_int(1), MTPint()), NewMessageUnread);
		history->sendRequestId = MTP::send(MTPmessages_SendMessage(MTP_flags(sendFlags), history->peer->input, MTP_int(replyTo), msgText, MTP_long(randomId), MTPnullMarkup, sentEntities), rpcDone(&MainWidget::sentUpdatesReceived, randomId), rpcFail(&MainWidget::sendMessageFail), 0, 0, history->sendRequestId);
	}

	history->lastSentMsg = lastMessage;

	finishForwarding(history, message.silent);

	executeParsedCommand(command);
}

void MainWidget::saveRecentHashtags(const QString &text) {
	bool found = false;
	QRegularExpressionMatch m;
	RecentHashtagPack recent(cRecentWriteHashtags());
	for (int32 i = 0, next = 0; (m = reHashtag().match(text, i)).hasMatch(); i = next) {
		i = m.capturedStart();
		next = m.capturedEnd();
		if (m.hasMatch()) {
			if (!m.capturedRef(1).isEmpty()) {
				++i;
			}
			if (!m.capturedRef(2).isEmpty()) {
				--next;
			}
		}
		if (!found && cRecentWriteHashtags().isEmpty() && cRecentSearchHashtags().isEmpty()) {
			Local::readRecentHashtagsAndBots();
			recent = cRecentWriteHashtags();
		}
		found = true;
		incrementRecentHashtag(recent, text.mid(i + 1, next - i - 1));
	}
	if (found) {
		cSetRecentWriteHashtags(recent);
		Local::writeRecentHashtagsAndBots();
	}
}

void MainWidget::readServerHistory(History *history, ReadServerHistoryChecks checks) {
	if (!history) return;
	if (checks == ReadServerHistoryChecks::OnlyIfUnread && !history->unreadCount()) return;

	auto peer = history->peer;
	MsgId upTo = history->inboxRead(0);
	if (auto channel = peer->asChannel()) {
		if (!channel->amIn()) {
			return; // no read request for channels that I didn't koin
		}
	}

	if (_readRequests.contains(peer)) {
		auto i = _readRequestsPending.find(peer);
		if (i == _readRequestsPending.cend()) {
			_readRequestsPending.insert(peer, upTo);
		} else if (i.value() < upTo) {
			i.value() = upTo;
		}
	} else {
		sendReadRequest(peer, upTo);
	}
}

void MainWidget::unreadCountChanged(History *history) {
	_history->unreadCountChanged(history);
}

TimeMs MainWidget::animActiveTimeStart(const HistoryItem *msg) const {
	return _history->animActiveTimeStart(msg);
}

void MainWidget::stopAnimActive() {
	_history->stopAnimActive();
}

void MainWidget::sendBotCommand(PeerData *peer, UserData *bot, const QString &cmd, MsgId replyTo) {
	_history->sendBotCommand(peer, bot, cmd, replyTo);
}

void MainWidget::hideSingleUseKeyboard(PeerData *peer, MsgId replyTo) {
	_history->hideSingleUseKeyboard(peer, replyTo);
}

void MainWidget::app_sendBotCallback(const HistoryMessageReplyMarkup::Button *button, const HistoryItem *msg, int row, int col) {
	_history->app_sendBotCallback(button, msg, row, col);
}

bool MainWidget::insertBotCommand(const QString &cmd, bool specialGif) {
	return _history->insertBotCommand(cmd, specialGif);
}

void MainWidget::searchMessages(const QString &query, PeerData *inPeer) {
	App::wnd()->hideMediaview();
	_dialogs->searchMessages(query, inPeer);
	if (Adaptive::OneColumn()) {
		Ui::showChatsList();
	} else {
		_dialogs->activate();
	}
}

bool MainWidget::preloadOverview(PeerData *peer, MediaOverviewType type) {
	MTPMessagesFilter filter = typeToMediaFilter(type);
	if (type == OverviewCount) return false;

	History *h = App::history(peer->id);
	if (h->overviewCountLoaded(type) || _overviewPreload[type].contains(peer)) {
		return false;
	}

	MTPmessages_Search::Flags flags = 0;
	_overviewPreload[type].insert(peer, MTP::send(MTPmessages_Search(MTP_flags(flags), peer->input, MTP_string(""), filter, MTP_int(0), MTP_int(0), MTP_int(0), MTP_int(0), MTP_int(0)), rpcDone(&MainWidget::overviewPreloaded, peer), rpcFail(&MainWidget::overviewFailed, peer), 0, 10));
	return true;
}

void MainWidget::preloadOverviews(PeerData *peer) {
	History *h = App::history(peer->id);
	bool sending = false;
	for (int32 i = 0; i < OverviewCount; ++i) {
		auto type = MediaOverviewType(i);
		if (type != OverviewChatPhotos && preloadOverview(peer, type)) {
			sending = true;
		}
	}
	if (sending) {
		MTP::sendAnything();
	}
}

void MainWidget::overviewPreloaded(PeerData *peer, const MTPmessages_Messages &result, mtpRequestId req) {
	MediaOverviewType type = OverviewCount;
	for (int32 i = 0; i < OverviewCount; ++i) {
		OverviewsPreload::iterator j = _overviewPreload[i].find(peer);
		if (j != _overviewPreload[i].end() && j.value() == req) {
			type = MediaOverviewType(i);
			_overviewPreload[i].erase(j);
			break;
		}
	}

	if (type == OverviewCount) return;

	App::history(peer->id)->overviewSliceDone(type, result, true);

	Notify::mediaOverviewUpdated(peer, type);
}

void MainWidget::mediaOverviewUpdated(const Notify::PeerUpdate &update) {
	auto peer = update.peer;
	if (_overview && (_overview->peer() == peer || _overview->peer()->migrateFrom() == peer)) {
		_overview->mediaOverviewUpdated(update);

		int32 mask = 0;
		History *h = peer ? App::historyLoaded((peer->migrateTo() ? peer->migrateTo() : peer)->id) : 0;
		History *m = (peer && peer->migrateFrom()) ? App::historyLoaded(peer->migrateFrom()->id) : 0;
		if (h) {
			for (int32 i = 0; i < OverviewCount; ++i) {
				if (!h->overview[i].isEmpty() || h->overviewCount(i) > 0 || i == _overview->type()) {
					mask |= (1 << i);
				} else if (m && (!m->overview[i].isEmpty() || m->overviewCount(i) > 0)) {
					mask |= (1 << i);
				}
			}
		}
		if (mask != _mediaTypeMask) {
			_mediaType->clearActions();
			for (int32 i = 0; i < OverviewCount; ++i) {
				if (mask & (1 << i)) {
					switch (i) {
					case OverviewPhotos: _mediaType->addAction(lang(lng_media_type_photos), this, SLOT(onPhotosSelect())); break;
					case OverviewVideos: _mediaType->addAction(lang(lng_media_type_videos), this, SLOT(onVideosSelect())); break;
					case OverviewMusicFiles: _mediaType->addAction(lang(lng_media_type_songs), this, SLOT(onSongsSelect())); break;
					case OverviewFiles: _mediaType->addAction(lang(lng_media_type_files), this, SLOT(onDocumentsSelect())); break;
					case OverviewVoiceFiles: _mediaType->addAction(lang(lng_media_type_audios), this, SLOT(onAudiosSelect())); break;
					case OverviewLinks: _mediaType->addAction(lang(lng_media_type_links), this, SLOT(onLinksSelect())); break;
					}
				}
			}
			_mediaTypeMask = mask;
			_mediaType->move(width() - _mediaType->width(), st::topBarHeight);
			_overview->updateTopBarSelection();
		}
	}
}

void MainWidget::changingMsgId(HistoryItem *row, MsgId newId) {
	if (_overview) _overview->changingMsgId(row, newId);
}

void MainWidget::itemEdited(HistoryItem *item) {
	if (_history->peer() == item->history()->peer || (_history->peer() && _history->peer() == item->history()->peer->migrateTo())) {
		_history->itemEdited(item);
	}
}

bool MainWidget::overviewFailed(PeerData *peer, const RPCError &error, mtpRequestId req) {
	if (MTP::isDefaultHandledError(error)) return false;

	MediaOverviewType type = OverviewCount;
	for (int32 i = 0; i < OverviewCount; ++i) {
		OverviewsPreload::iterator j = _overviewPreload[i].find(peer);
		if (j != _overviewPreload[i].end() && j.value() == req) {
			_overviewPreload[i].erase(j);
			break;
		}
	}
	return true;
}

void MainWidget::loadMediaBack(PeerData *peer, MediaOverviewType type, bool many) {
	if (_overviewLoad[type].constFind(peer) != _overviewLoad[type].cend()) return;

	History *history = App::history(peer->id);
	if (history->overviewLoaded(type)) return;

	MsgId minId = history->overviewMinId(type);
	int32 limit = (many || history->overview[type].size() > MediaOverviewStartPerPage) ? SearchPerPage : MediaOverviewStartPerPage;
	MTPMessagesFilter filter = typeToMediaFilter(type);
	if (type == OverviewCount) return;

	MTPmessages_Search::Flags flags = 0;
	_overviewLoad[type].insert(peer, MTP::send(MTPmessages_Search(MTP_flags(flags), peer->input, MTPstring(), filter, MTP_int(0), MTP_int(0), MTP_int(0), MTP_int(minId), MTP_int(limit)), rpcDone(&MainWidget::overviewLoaded, history)));
}

void MainWidget::checkLastUpdate(bool afterSleep) {
	auto n = getms(true);
	if (_lastUpdateTime && n > _lastUpdateTime + (afterSleep ? NoUpdatesAfterSleepTimeout : NoUpdatesTimeout)) {
		_lastUpdateTime = n;
		MTP::ping();
	}
}

void MainWidget::overviewLoaded(History *history, const MTPmessages_Messages &result, mtpRequestId req) {
	OverviewsPreload::iterator it;
	MediaOverviewType type = OverviewCount;
	for (int32 i = 0; i < OverviewCount; ++i) {
		it = _overviewLoad[i].find(history->peer);
		if (it != _overviewLoad[i].cend()) {
			type = MediaOverviewType(i);
			_overviewLoad[i].erase(it);
			break;
		}
	}
	if (type == OverviewCount) return;

	history->overviewSliceDone(type, result);

	Notify::mediaOverviewUpdated(history->peer, type);
}

void MainWidget::sendReadRequest(PeerData *peer, MsgId upTo) {
	if (peer->isChannel()) {
		_readRequests.insert(peer, qMakePair(MTP::send(MTPchannels_ReadHistory(peer->asChannel()->inputChannel, MTP_int(upTo)), rpcDone(&MainWidget::channelReadDone, peer), rpcFail(&MainWidget::readRequestFail, peer)), upTo));
	} else {
		_readRequests.insert(peer, qMakePair(MTP::send(MTPmessages_ReadHistory(peer->input, MTP_int(upTo)), rpcDone(&MainWidget::historyReadDone, peer), rpcFail(&MainWidget::readRequestFail, peer)), upTo));
	}
}

void MainWidget::channelReadDone(PeerData *peer, const MTPBool &result) {
	readRequestDone(peer);
}

void MainWidget::historyReadDone(PeerData *peer, const MTPmessages_AffectedMessages &result) {
	messagesAffected(peer, result);
	readRequestDone(peer);
}

bool MainWidget::readRequestFail(PeerData *peer, const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	readRequestDone(peer);
	return false;
}

void MainWidget::readRequestDone(PeerData *peer) {
	_readRequests.remove(peer);
	ReadRequestsPending::iterator i = _readRequestsPending.find(peer);
	if (i != _readRequestsPending.cend()) {
		sendReadRequest(peer, i.value());
		_readRequestsPending.erase(i);
	}
}

void MainWidget::messagesAffected(PeerData *peer, const MTPmessages_AffectedMessages &result) {
	const auto &d(result.c_messages_affectedMessages());
	if (peer && peer->isChannel()) {
		if (peer->asChannel()->ptsUpdated(d.vpts.v, d.vpts_count.v)) {
			peer->asChannel()->ptsApplySkippedUpdates();
		}
	} else {
		if (ptsUpdated(d.vpts.v, d.vpts_count.v)) {
			ptsApplySkippedUpdates();
		}
	}
	if (History *h = App::historyLoaded(peer ? peer->id : 0)) {
		if (!h->lastMsg) {
			checkPeerHistory(peer);
		}
	}
}

void MainWidget::ui_showPeerHistoryAsync(quint64 peerId, qint32 showAtMsgId, Ui::ShowWay way) {
	Ui::showPeerHistory(peerId, showAtMsgId, way);
}

void MainWidget::ui_autoplayMediaInlineAsync(qint32 channelId, qint32 msgId) {
	if (HistoryItem *item = App::histItemById(channelId, msgId)) {
		if (HistoryMedia *media = item->getMedia()) {
			media->playInline(true);
		}
	}
}

void MainWidget::handleAudioUpdate(const AudioMsgId &audioId) {
	using State = Media::Player::State;
	auto state = Media::Player::mixer()->currentState(audioId.type());
	if (state.id == audioId && state.state == State::StoppedAtStart) {
		state.state = State::Stopped;
		Media::Player::mixer()->clearStoppedAtStart(audioId);

		auto document = audioId.audio();
		auto filepath = document->filepath(DocumentData::FilePathResolveSaveFromData);
		if (!filepath.isEmpty()) {
			if (documentIsValidMediaFile(filepath)) {
				File::Launch(filepath);
			}
		}
	}

	if (state.id == audioId && audioId.type() == AudioMsgId::Type::Song) {
		if (!Media::Player::IsStopped(state.state) && state.state != State::Finishing) {
			if (!_playerUsingPanel && !_player) {
				createPlayer();
			}
		} else if (_player && _player->isHidden() && !_playerUsingPanel) {
			_player.destroyDelayed();
			_playerVolume.destroyDelayed();
		}
	}

	if (auto item = App::histItemById(audioId.contextId())) {
		Ui::repaintHistoryItem(item);
	}
	if (auto items = InlineBots::Layout::documentItems()) {
		for (auto item : items->value(audioId.audio())) {
			Ui::repaintInlineItem(item);
		}
	}
}

void MainWidget::switchToPanelPlayer() {
	if (_playerUsingPanel) return;
	_playerUsingPanel = true;

	_player->slideUp();
	_playerVolume.destroyDelayed();
	_playerPlaylist->hideIgnoringEnterEvents();

	Media::Player::instance()->usePanelPlayer().notify(true, true);
}

void MainWidget::switchToFixedPlayer() {
	if (!_playerUsingPanel) return;
	_playerUsingPanel = false;

	if (!_player) {
		createPlayer();
	} else {
		_player->slideDown();
		if (!_playerVolume) {
			_playerVolume.create(this);
			_player->entity()->volumeWidgetCreated(_playerVolume);
			updateMediaPlayerPosition();
		}
	}

	Media::Player::instance()->usePanelPlayer().notify(false, true);
	_playerPanel->hideIgnoringEnterEvents();
}

void MainWidget::closeBothPlayers() {
	if (_playerUsingPanel) {
		_playerUsingPanel = false;
		_player.destroyDelayed();
	} else {
		_player->slideUp();
	}
	_playerVolume.destroyDelayed();

	Media::Player::instance()->usePanelPlayer().notify(false, true);
	_playerPanel->hideIgnoringEnterEvents();
	_playerPlaylist->hideIgnoringEnterEvents();
	Media::Player::instance()->stop();

	Shortcuts::disableMediaShortcuts();
}

void MainWidget::createPlayer() {
	_player.create(this, [this] { playerHeightUpdated(); });
	_player->entity()->setCloseCallback([this] { closeBothPlayers(); });
	_playerVolume.create(this);
	_player->entity()->volumeWidgetCreated(_playerVolume);
	orderWidgets();
	if (_a_show.animating()) {
		_player->showFast();
		_player->hide();
	} else {
		_player->hideFast();
		if (_player) {
			_player->slideDown();
			_playerHeight = _contentScrollAddToY = _player->contentHeight();
			updateControlsGeometry();
		}
	}

	Shortcuts::enableMediaShortcuts();
}

void MainWidget::playerHeightUpdated() {
	auto playerHeight = _player->contentHeight();
	if (playerHeight != _playerHeight) {
		_contentScrollAddToY += playerHeight - _playerHeight;
		_playerHeight = playerHeight;
		updateControlsGeometry();
	}
	if (!_playerHeight && _player->isHidden()) {
		auto state = Media::Player::mixer()->currentState(AudioMsgId::Type::Song);
		if (state.id && Media::Player::IsStopped(state.state)) {
			_playerVolume.destroyDelayed();
			_player.destroyDelayed();
		}
	}
}

void MainWidget::documentLoadProgress(FileLoader *loader) {
	if (auto documentId = loader ? loader->objId() : 0) {
		documentLoadProgress(App::document(documentId));
	}
}

void MainWidget::documentLoadProgress(DocumentData *document) {
	if (document->loaded()) {
		document->performActionOnLoad();
	}

	auto &items = App::documentItems();
	auto i = items.constFind(document);
	if (i != items.cend()) {
		for_const (auto item, i.value()) {
			Ui::repaintHistoryItem(item);
		}
	}
	App::wnd()->documentUpdated(document);

	if (!document->loaded() && document->song()) {
		Media::Player::instance()->documentLoadProgress(document);
	}
}

void MainWidget::documentLoadFailed(FileLoader *loader, bool started) {
	auto documentId = loader ? loader->objId() : 0;
	if (!documentId) return;

	auto document = App::document(documentId);
	if (started) {
		auto failedFileName = loader->fileName();
		Ui::show(Box<ConfirmBox>(lang(lng_download_finish_failed), base::lambda_guarded(this, [this, document, failedFileName] {
			Ui::hideLayer();
			if (document) document->save(failedFileName);
		})));
	} else {
		Ui::show(Box<ConfirmBox>(lang(lng_download_path_failed), lang(lng_download_path_settings), base::lambda_guarded(this, [this] {
			Global::SetDownloadPath(QString());
			Global::SetDownloadPathBookmark(QByteArray());
			Ui::show(Box<DownloadPathBox>());
			Global::RefDownloadPathChanged().notify();
		})));
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

void MainWidget::mediaMarkRead(DocumentData *data) {
	const DocumentItems &items(App::documentItems());
	DocumentItems::const_iterator i = items.constFind(data);
	if (i != items.cend()) {
		mediaMarkRead(i.value());
	}
}

void MainWidget::mediaMarkRead(const HistoryItemsMap &items) {
	QVector<MTPint> markedIds;
	markedIds.reserve(items.size());
	for_const (auto item, items) {
		if (!item->out() && item->isMediaUnread()) {
			item->markMediaRead();
			if (item->id > 0) {
				markedIds.push_back(MTP_int(item->id));
			}
		}
	}
	if (!markedIds.isEmpty()) {
		MTP::send(MTPmessages_ReadMessageContents(MTP_vector<MTPint>(markedIds)), rpcDone(&MainWidget::messagesAffected, (PeerData*)0));
	}
}

void MainWidget::updateOnlineDisplay() {
	if (this != App::main()) return;
	_history->updateOnlineDisplay();
}

void MainWidget::onSendFileConfirm(const FileLoadResultPtr &file) {
	_history->sendFileConfirmed(file);
}

bool MainWidget::onSendSticker(DocumentData *document) {
	return _history->onStickerSend(document);
}

void MainWidget::dialogsCancelled() {
	if (_hider) {
		_hider->startHide();
		noHider(_hider);
	}
	_history->activate();
}

void MainWidget::serviceNotification(const TextWithEntities &message, const MTPMessageMedia &media, int32 date) {
	MTPDmessage::Flags flags = MTPDmessage::Flag::f_entities | MTPDmessage::Flag::f_from_id | MTPDmessage_ClientFlag::f_clientside_unread;
	QString sendingText, leftText = message.text;
	EntitiesInText sendingEntities, leftEntities = message.entities;
	HistoryItem *item = nullptr;
	while (textSplit(sendingText, sendingEntities, leftText, leftEntities, MaxMessageSize)) {
		MTPVector<MTPMessageEntity> localEntities = linksToMTP(sendingEntities);
		item = App::histories().addNewMessage(MTP_message(MTP_flags(flags), MTP_int(clientMsgId()), MTP_int(ServiceUserId), MTP_peerUser(MTP_int(AuthSession::CurrentUserId())), MTPnullFwdHeader, MTPint(), MTPint(), MTP_int(date), MTP_string(sendingText), media, MTPnullMarkup, localEntities, MTPint(), MTPint()), NewMessageUnread);
	}
	if (item) {
		_history->peerMessagesUpdated(item->history()->peer->id);
	}
}

void MainWidget::serviceHistoryDone(const MTPmessages_Messages &msgs) {
	switch (msgs.type()) {
	case mtpc_messages_messages: {
		auto &d(msgs.c_messages_messages());
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		App::feedMsgs(d.vmessages, NewMessageLast);
	} break;

	case mtpc_messages_messagesSlice: {
		auto &d(msgs.c_messages_messagesSlice());
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		App::feedMsgs(d.vmessages, NewMessageLast);
	} break;

	case mtpc_messages_channelMessages: {
		auto &d(msgs.c_messages_channelMessages());
		LOG(("API Error: received messages.channelMessages! (MainWidget::serviceHistoryDone)"));
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		App::feedMsgs(d.vmessages, NewMessageLast);
	} break;
	}

	App::wnd()->showDelayedServiceMsgs();
}

bool MainWidget::serviceHistoryFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	App::wnd()->showDelayedServiceMsgs();
	return false;
}

bool MainWidget::isIdle() const {
	return _isIdle;
}

void MainWidget::clearCachedBackground() {
	_cachedBackground = QPixmap();
	_cacheBackgroundTimer.stop();
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
		_cacheBackgroundTimer.start(CacheBackgroundTimeout);
	}
	return QPixmap();
}

void MainWidget::updateScrollColors() {
	_history->updateScrollColors();
}

void MainWidget::setChatBackground(const App::WallPaper &wp) {
	_background = std::make_unique<App::WallPaper>(wp);
	_background->full->loadEvenCancelled();
	checkChatBackground();
}

bool MainWidget::chatBackgroundLoading() {
	return (_background != nullptr);
}

float64 MainWidget::chatBackgroundProgress() const {
	if (_background) {
		return _background->full->progress();
	}
	return 1.;
}

void MainWidget::checkChatBackground() {
	if (_background) {
		if (_background->full->loaded()) {
			if (_background->full->isNull()) {
				Window::Theme::Background()->setImage(Window::Theme::kDefaultBackground);
			} else if (false
				|| _background->id == Window::Theme::kInitialBackground
				|| _background->id == Window::Theme::kDefaultBackground) {
				Window::Theme::Background()->setImage(_background->id);
			} else {
				Window::Theme::Background()->setImage(_background->id, _background->full->pix().toImage());
			}
			_background = nullptr;
			QTimer::singleShot(0, this, SLOT(update()));
		}
	}
}

ImagePtr MainWidget::newBackgroundThumb() {
	return _background ? _background->thumb : ImagePtr();
}

ApiWrap *MainWidget::api() {
	return _api.get();
}

void MainWidget::messageDataReceived(ChannelData *channel, MsgId msgId) {
	_history->messageDataReceived(channel, msgId);
}

void MainWidget::updateBotKeyboard(History *h) {
	_history->updateBotKeyboard(h);
}

void MainWidget::pushReplyReturn(HistoryItem *item) {
	_history->pushReplyReturn(item);
}

void MainWidget::setInnerFocus() {
	if (_hider || !_history->peer()) {
		if (_hider && _hider->wasOffered()) {
			_hider->setFocus();
		} else if (!_hider && _overview) {
			_overview->activate();
		} else if (!_hider && _wideSection) {
			_wideSection->setInnerFocus();
		} else {
			dialogsActivate();
		}
	} else if (_overview) {
		_overview->activate();
	} else if (_wideSection) {
		_wideSection->setInnerFocus();
	} else {
		_history->setInnerFocus();
	}
}

void MainWidget::scheduleViewIncrement(HistoryItem *item) {
	PeerData *peer = item->history()->peer;
	ViewsIncrement::iterator i = _viewsIncremented.find(peer);
	if (i != _viewsIncremented.cend()) {
		if (i.value().contains(item->id)) return;
	} else {
		i = _viewsIncremented.insert(peer, ViewsIncrementMap());
	}
	i.value().insert(item->id, true);
	ViewsIncrement::iterator j = _viewsToIncrement.find(peer);
	if (j == _viewsToIncrement.cend()) {
		j = _viewsToIncrement.insert(peer, ViewsIncrementMap());
		_viewsIncrementTimer.start(SendViewsTimeout);
	}
	j.value().insert(item->id, true);
}

void MainWidget::fillPeerMenu(PeerData *peer, base::lambda<QAction*(const QString &text, base::lambda<void()> handler)> callback, bool pinToggle) {
	if (pinToggle) {
		auto isPinned = false;
		if (auto history = App::historyLoaded(peer)) {
			isPinned = history->isPinnedDialog();
		}
		auto pinSubscription = MakeShared<base::Subscription>();
		auto pinAction = callback(lang(isPinned ? lng_context_unpin_from_top : lng_context_pin_to_top), [peer, pinSubscription] {
			auto history = App::history(peer);
			auto isPinned = !history->isPinnedDialog();
			if (isPinned && App::histories().pinnedCount() >= Global::PinnedDialogsCountMax()) {
				// Some old chat, that was converted to supergroup, maybe is still pinned.
				auto findWastedPin = []() -> History* {
					auto order = App::histories().getPinnedOrder();
					for_const (auto pinned, order) {
						if (pinned->peer->isChat()
							&& pinned->peer->asChat()->isDeactivated()
							&& !pinned->inChatList(Dialogs::Mode::All)) {
							return pinned;
						}
					}
					return nullptr;
				};
				if (auto wasted = findWastedPin()) {
					wasted->setPinnedDialog(false);
					history->setPinnedDialog(isPinned);
					App::histories().savePinnedToServer();
				} else {
					Ui::show(Box<InformBox>(lng_error_pinned_max(lt_count, Global::PinnedDialogsCountMax())));
				}
				return;
			}

			history->setPinnedDialog(isPinned);
			auto flags = MTPmessages_ToggleDialogPin::Flags(0);
			if (isPinned) {
				flags |= MTPmessages_ToggleDialogPin::Flag::f_pinned;
			}
			MTP::send(MTPmessages_ToggleDialogPin(MTP_flags(flags), peer->input));
			if (isPinned) {
				if (auto main = App::main()) {
					main->dialogsToUp();
				}
			}
		});
		auto pinChangedHandler = Notify::PeerUpdatedHandler(Notify::PeerUpdate::Flag::PinnedChanged, [pinAction, peer](const Notify::PeerUpdate &update) {
			if (update.peer != peer) return;
			pinAction->setText(lang(App::history(peer)->isPinnedDialog() ? lng_context_unpin_from_top : lng_context_pin_to_top));
		});
		*pinSubscription = Notify::PeerUpdated().add_subscription(std::move(pinChangedHandler));
	}
	callback(lang((peer->isChat() || peer->isMegagroup()) ? lng_context_view_group : (peer->isUser() ? lng_context_view_profile : lng_context_view_channel)), [peer] {
		Ui::showPeerProfile(peer);
	});
	auto muteSubscription = MakeShared<base::Subscription>();
	auto muteAction = callback(lang(peer->isMuted() ? lng_enable_notifications_from_tray : lng_disable_notifications_from_tray), [peer, muteSubscription] {
		App::main()->updateNotifySetting(peer, peer->isMuted() ? NotifySettingSetNotify : NotifySettingSetMuted);
	});
	auto muteChangedHandler = Notify::PeerUpdatedHandler(Notify::PeerUpdate::Flag::NotificationsEnabled, [muteAction, peer](const Notify::PeerUpdate &update) {
		if (update.peer != peer) return;
		muteAction->setText(lang(peer->isMuted() ? lng_enable_notifications_from_tray : lng_disable_notifications_from_tray));
	});
	*muteSubscription = Notify::PeerUpdated().add_subscription(std::move(muteChangedHandler));

	callback(lang(lng_profile_search_messages), [peer] {
		App::main()->searchInPeer(peer);
	});

	auto clearHistoryHandler = [peer] {
		auto text = peer->isUser() ? lng_sure_delete_history(lt_contact, peer->name) : lng_sure_delete_group_history(lt_group, peer->name);
		Ui::show(Box<ConfirmBox>(text, lang(lng_box_delete), st::attentionBoxButton, [peer] {
			if (!App::main()) return;

			Ui::hideLayer();
			App::main()->clearHistory(peer);
		}));
	};
	auto deleteAndLeaveHandler = [peer] {
		auto warningText = peer->isUser() ? lng_sure_delete_history(lt_contact, peer->name) :
			peer->isChat() ? lng_sure_delete_and_exit(lt_group, peer->name) :
			lang(peer->isMegagroup() ? lng_sure_leave_group : lng_sure_leave_channel);
		auto confirmText = lang(peer->isUser() ? lng_box_delete : lng_box_leave);
		auto &confirmStyle = peer->isChannel() ? st::defaultBoxButton : st::attentionBoxButton;
		Ui::show(Box<ConfirmBox>(warningText, confirmText, confirmStyle, [peer] {
			if (!App::main()) return;

			Ui::hideLayer();
			Ui::showChatsList();
			if (peer->isUser()) {
				App::main()->deleteConversation(peer);
			} else if (peer->isChat()) {
				MTP::send(MTPmessages_DeleteChatUser(peer->asChat()->inputChat, App::self()->inputUser), App::main()->rpcDone(&MainWidget::deleteHistoryAfterLeave, peer), App::main()->rpcFail(&MainWidget::leaveChatFailed, peer));
			} else if (peer->isChannel()) {
				if (peer->migrateFrom()) {
					App::main()->deleteConversation(peer->migrateFrom());
				}
				MTP::send(MTPchannels_LeaveChannel(peer->asChannel()->inputChannel), App::main()->rpcDone(&MainWidget::sentUpdatesReceived));
			}
		}));
	};
	if (auto user = peer->asUser()) {
		callback(lang(lng_profile_delete_conversation), std::move(deleteAndLeaveHandler));
		callback(lang(lng_profile_clear_history), std::move(clearHistoryHandler));
		if (!user->isInaccessible() && user != App::self()) {
			auto blockSubscription = MakeShared<base::Subscription>();
			auto blockAction = callback(lang(user->isBlocked() ? (user->botInfo ? lng_profile_unblock_bot : lng_profile_unblock_user) : (user->botInfo ? lng_profile_block_bot : lng_profile_block_user)), [user, blockSubscription] {
				auto willBeBlocked = !user->isBlocked();
				auto handler = ::rpcDone([user, willBeBlocked](const MTPBool &result) {
					user->setBlockStatus(willBeBlocked ? UserData::BlockStatus::Blocked : UserData::BlockStatus::NotBlocked);
					emit App::main()->peerUpdated(user);
				});
				if (willBeBlocked) {
					MTP::send(MTPcontacts_Block(user->inputUser), std::move(handler));
				} else {
					MTP::send(MTPcontacts_Unblock(user->inputUser), std::move(handler));
				}
			});
			auto blockChangedHandler = Notify::PeerUpdatedHandler(Notify::PeerUpdate::Flag::UserIsBlocked, [blockAction, peer](const Notify::PeerUpdate &update) {
				if (update.peer != peer) return;
				blockAction->setText(lang(peer->asUser()->isBlocked() ? (peer->asUser()->botInfo ? lng_profile_unblock_bot : lng_profile_unblock_user) : (peer->asUser()->botInfo ? lng_profile_block_bot : lng_profile_block_user)));
			});
			*blockSubscription = Notify::PeerUpdated().add_subscription(std::move(blockChangedHandler));

			if (user->blockStatus() == UserData::BlockStatus::Unknown) {
				App::api()->requestFullPeer(user);
			}
		}
	} else if (peer->isChat()) {
		callback(lang(lng_profile_clear_and_exit), std::move(deleteAndLeaveHandler));
		callback(lang(lng_profile_clear_history), std::move(clearHistoryHandler));
	} else if (peer->isChannel() && peer->asChannel()->amIn() && !peer->asChannel()->amCreator()) {
		callback(lang(peer->isMegagroup() ? lng_profile_leave_group : lng_profile_leave_channel), std::move(deleteAndLeaveHandler));
	}
}

void MainWidget::onViewsIncrement() {
	for (ViewsIncrement::iterator i = _viewsToIncrement.begin(); i != _viewsToIncrement.cend();) {
		if (_viewsIncrementRequests.contains(i.key())) {
			++i;
			continue;
		}

		QVector<MTPint> ids;
		ids.reserve(i.value().size());
		for (ViewsIncrementMap::const_iterator j = i.value().cbegin(), end = i.value().cend(); j != end; ++j) {
			ids.push_back(MTP_int(j.key()));
		}
		mtpRequestId req = MTP::send(MTPmessages_GetMessagesViews(i.key()->input, MTP_vector<MTPint>(ids), MTP_bool(true)), rpcDone(&MainWidget::viewsIncrementDone, ids), rpcFail(&MainWidget::viewsIncrementFail), 0, 5);
		_viewsIncrementRequests.insert(i.key(), req);
		i = _viewsToIncrement.erase(i);
	}
}

void MainWidget::viewsIncrementDone(QVector<MTPint> ids, const MTPVector<MTPint> &result, mtpRequestId req) {
	const auto &v(result.c_vector().v);
	if (ids.size() == v.size()) {
		for (ViewsIncrementRequests::iterator i = _viewsIncrementRequests.begin(); i != _viewsIncrementRequests.cend(); ++i) {
			if (i.value() == req) {
				PeerData *peer = i.key();
				ChannelId channel = peerToChannel(peer->id);
				for (int32 j = 0, l = ids.size(); j < l; ++j) {
					if (HistoryItem *item = App::histItemById(channel, ids.at(j).v)) {
						item->setViewsCount(v.at(j).v);
					}
				}
				_viewsIncrementRequests.erase(i);
				break;
			}
		}
	}
	if (!_viewsToIncrement.isEmpty() && !_viewsIncrementTimer.isActive()) {
		_viewsIncrementTimer.start(SendViewsTimeout);
	}
}

bool MainWidget::viewsIncrementFail(const RPCError &error, mtpRequestId req) {
	if (MTP::isDefaultHandledError(error)) return false;

	for (ViewsIncrementRequests::iterator i = _viewsIncrementRequests.begin(); i != _viewsIncrementRequests.cend(); ++i) {
		if (i.value() == req) {
			_viewsIncrementRequests.erase(i);
			break;
		}
	}
	if (!_viewsToIncrement.isEmpty() && !_viewsIncrementTimer.isActive()) {
		_viewsIncrementTimer.start(SendViewsTimeout);
	}
	return false;
}

void MainWidget::createDialog(History *history) {
	_dialogs->createDialog(history);
}

void MainWidget::choosePeer(PeerId peerId, MsgId showAtMsgId) {
	if (selectingPeer()) {
		offerPeer(peerId);
	} else {
		Ui::showPeerHistory(peerId, showAtMsgId);
	}
}

void MainWidget::clearBotStartToken(PeerData *peer) {
	if (peer && peer->isUser() && peer->asUser()->botInfo) {
		peer->asUser()->botInfo->startToken = QString();
	}
}

void MainWidget::contactsReceived() {
	_history->contactsReceived();
}

void MainWidget::updateAfterDrag() {
	if (_overview) {
		_overview->updateAfterDrag();
	} else {
		_history->updateAfterDrag();
	}
}

void MainWidget::ctrlEnterSubmitUpdated() {
	_history->updateFieldSubmitSettings();
}

void MainWidget::ui_showPeerHistory(quint64 peerId, qint32 showAtMsgId, Ui::ShowWay way) {
	if (auto peer = App::peerLoaded(peerId)) {
		if (peer->migrateTo()) {
			peer = peer->migrateTo();
			peerId = peer->id;
			if (showAtMsgId > 0) showAtMsgId = -showAtMsgId;
		}
		QString restriction = peer->restrictionReason();
		if (!restriction.isEmpty()) {
			Ui::showChatsList();
			Ui::show(Box<InformBox>(restriction));
			return;
		}
	}

	Global::RefDialogsListFocused().set(false, true);
	_a_dialogsWidth.finish();

	bool back = (way == Ui::ShowWay::Backward || !peerId);
	bool foundInStack = !peerId;
	if (foundInStack || (way == Ui::ShowWay::ClearStack)) {
		for_const (auto &item, _stack) {
			clearBotStartToken(item->peer);
		}
		_stack.clear();
	} else {
		for (auto i = 0, s = int(_stack.size()); i < s; ++i) {
			if (_stack.at(i)->type() == HistoryStackItem && _stack.at(i)->peer->id == peerId) {
				foundInStack = true;
				while (int(_stack.size()) > i + 1) {
					clearBotStartToken(_stack.back()->peer);
					_stack.pop_back();
				}
				_stack.pop_back();
				if (!back) {
					back = true;
				}
				break;
			}
		}
		if (auto historyPeer = _history->peer()) {
			if (way == Ui::ShowWay::Forward && historyPeer->id == peerId) {
				way = Ui::ShowWay::ClearStack;
			}
		}
	}

	dlgUpdated();
	if (back || (way == Ui::ShowWay::ClearStack)) {
		_peerInStack = nullptr;
		_msgIdInStack = 0;
	} else {
		saveSectionInStack();
	}
	dlgUpdated();

	auto wasActivePeer = activePeer();

	Ui::hideSettingsAndLayer();
	if (_hider) {
		_hider->startHide();
		_hider = nullptr;
	}

	auto animatedShow = [this, peerId, back, way] {
		if (_a_show.animating() || App::passcoded()) {
			return false;
		}
		if (!peerId) {
			if (Adaptive::OneColumn()) {
				return true;
			} else {
				return false;
			}
		}
		if (back || way == Ui::ShowWay::Forward) {
			return true;
		}
		if (_history->isHidden() && (_wideSection || _overview || Adaptive::OneColumn())) {
			return true;
		}
		return false;
	};
	auto animationParams = animatedShow() ? prepareHistoryAnimation(peerId) : Window::SectionSlideParams();

	if (_history->peer() && _history->peer()->id != peerId && way != Ui::ShowWay::Forward) {
		clearBotStartToken(_history->peer());
	}
	_history->showHistory(peerId, showAtMsgId);

	auto noPeer = !_history->peer();
	auto onlyDialogs = noPeer && Adaptive::OneColumn();
	if (_wideSection || _overview) {
		if (_wideSection) {
			_wideSection->hide();
			_wideSection->deleteLater();
			_wideSection = nullptr;
		}
		if (_overview) {
			_overview->hide();
			_overview->clear();
			_overview->deleteLater();
			_overview->rpcClear();
			_overview = nullptr;
		}
	}
	if (onlyDialogs) {
		_topBar->hide();
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
		if (noPeer) {
			_topBar->hide();
			updateControlsGeometry();
		} else if (wasActivePeer != activePeer()) {
			if (activePeer()->isChannel()) {
				activePeer()->asChannel()->ptsWaitingForShortPoll(WaitForChannelGetDifference);
			}
			_viewsIncremented.remove(activePeer());
		}
		if (Adaptive::OneColumn() && !_dialogs->isHidden()) _dialogs->hide();
		if (!_a_show.animating()) {
			if (!animationParams.oldContentCache.isNull()) {
				_history->showAnimated(back ? Window::SlideDirection::FromLeft : Window::SlideDirection::FromRight, animationParams);
			} else {
				_history->show();
				if (App::wnd()) {
					QTimer::singleShot(0, App::wnd(), SLOT(setInnerFocus()));
				}
			}
		}
	}
	//if (wasActivePeer && wasActivePeer->isChannel() && activePeer() != wasActivePeer) {
	//	wasActivePeer->asChannel()->ptsWaitingForShortPoll(false);
	//}

	if (!_dialogs->isHidden()) {
		if (!back) {
			_dialogs->scrollToPeer(peerId, showAtMsgId);
		}
		_dialogs->update();
	}
	topBar()->showAll();
}

PeerData *MainWidget::ui_getPeerForMouseAction() {
	return _history->ui_getPeerForMouseAction();
}

void MainWidget::peerBefore(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg) {
	if (selectingPeer()) {
		outPeer = 0;
		outMsg = 0;
		return;
	}
	_dialogs->peerBefore(inPeer, inMsg, outPeer, outMsg);
}

void MainWidget::peerAfter(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg) {
	if (selectingPeer()) {
		outPeer = 0;
		outMsg = 0;
		return;
	}
	_dialogs->peerAfter(inPeer, inMsg, outPeer, outMsg);
}

PeerData *MainWidget::historyPeer() {
	return _history->peer();
}

PeerData *MainWidget::peer() {
	return _overview ? _overview->peer() : _history->peer();
}

PeerData *MainWidget::activePeer() {
	return _history->peer() ? _history->peer() : _peerInStack;
}

MsgId MainWidget::activeMsgId() {
	return _history->peer() ? _history->msgId() : _msgIdInStack;
}

PeerData *MainWidget::overviewPeer() {
	return _overview ? _overview->peer() : 0;
}

bool MainWidget::mediaTypeSwitch() {
	if (!_overview) return false;

	for (int32 i = 0; i < OverviewCount; ++i) {
		if (!(_mediaTypeMask & ~(1 << i))) {
			return false;
		}
	}
	return true;
}

void MainWidget::saveSectionInStack() {
	if (_overview) {
		_stack.push_back(std::make_unique<StackItemOverview>(_overview->peer(), _overview->type(), _overview->lastWidth(), _overview->lastScrollTop()));
	} else if (_wideSection) {
		_stack.push_back(std::make_unique<StackItemSection>(_wideSection->createMemento()));
	} else if (_history->peer()) {
		_peerInStack = _history->peer();
		_msgIdInStack = _history->msgId();
		_stack.push_back(std::make_unique<StackItemHistory>(_peerInStack, _msgIdInStack, _history->replyReturns()));
	}
}

void MainWidget::showMediaOverview(PeerData *peer, MediaOverviewType type, bool back, int32 lastScrollTop) {
	if (peer->migrateTo()) {
		peer = peer->migrateTo();
	}

	Ui::hideSettingsAndLayer();
	if (_overview && _overview->peer() == peer) {
		if (_overview->type() != type) {
			_overview->switchType(type);
		} else if (type == OverviewMusicFiles) { // hack for player
			showBackFromStack();
		}
		return;
	}

	Global::RefDialogsListFocused().set(false, true);
	_a_dialogsWidth.finish();

	auto animatedShow = [this] {
		if (_a_show.animating() || App::passcoded()) {
			return false;
		}
		if (Adaptive::OneColumn() || isSectionShown()) {
			return true;
		}
		return false;
	};
	auto animationParams = animatedShow() ? prepareOverviewAnimation() : Window::SectionSlideParams();
	if (!back) {
		saveSectionInStack();
	}

	setFocus(); // otherwise dialogs widget could be focused.
	if (_overview) {
		_overview->hide();
		_overview->clear();
		_overview->deleteLater();
		_overview->rpcClear();
	}
	if (_wideSection) {
		_wideSection->hide();
		_wideSection->deleteLater();
		_wideSection = nullptr;
	}
	_overview.create(this, peer, type);
	_mediaTypeMask = 0;
	_topBar->show();
	updateControlsGeometry();

	// Send a fake update.
	Notify::PeerUpdate update(peer);
	update.flags |= Notify::PeerUpdate::Flag::SharedMediaChanged;
	update.mediaTypesMask |= (1 << type);
	mediaOverviewUpdated(update);

	_overview->setLastScrollTop(lastScrollTop);
	if (!animationParams.oldContentCache.isNull()) {
		_overview->showAnimated(back ? Window::SlideDirection::FromLeft : Window::SlideDirection::FromRight, animationParams);
	} else {
		_overview->fastShow();
	}
	_history->finishAnimation();
	if (back) {
		clearBotStartToken(_history->peer());
	}
	_history->showHistory(0, 0);
	_history->hide();
	if (Adaptive::OneColumn()) _dialogs->hide();

	orderWidgets();
}

void MainWidget::showWideSection(const Window::SectionMemento &memento) {
	Ui::hideSettingsAndLayer();
	if (_wideSection && _wideSection->showInternal(&memento)) {
		return;
	}
	showNewWideSection(&memento, false, true);
}

Window::SectionSlideParams MainWidget::prepareShowAnimation(bool willHaveTopBarShadow) {
	Window::SectionSlideParams result;
	result.withTopBarShadow = willHaveTopBarShadow;
	if (selectingPeer() && Adaptive::OneColumn()) {
		result.withTopBarShadow = false;
	} else if (_wideSection) {
		if (!_wideSection->hasTopBarShadow()) {
			result.withTopBarShadow = false;
		}
	} else if (!_overview && !_history->peer()) {
		result.withTopBarShadow = false;
	}

	if (_player) {
		_player->hideShadow();
	}
	auto playerVolumeVisible = _playerVolume && !_playerVolume->isHidden();
	if (playerVolumeVisible) {
		_playerVolume->hide();
	}
	auto playerPanelVisible = !_playerPanel->isHidden();
	if (playerPanelVisible) {
		_playerPanel->hide();
	}
	auto playerPlaylistVisible = !_playerPlaylist->isHidden();
	if (playerPlaylistVisible) {
		_playerPlaylist->hide();
	}

	if (selectingPeer() && Adaptive::OneColumn()) {
		result.oldContentCache = myGrab(this, QRect(0, _playerHeight, _dialogsWidth, height() - _playerHeight));
	} else if (_wideSection) {
		result.oldContentCache = _wideSection->grabForShowAnimation(result);
	} else {
		if (result.withTopBarShadow) {
			if (_overview) _overview->grapWithoutTopBarShadow();
			_history->grapWithoutTopBarShadow();
		} else {
			if (_overview) _overview->grabStart();
			_history->grabStart();
		}
		if (Adaptive::OneColumn()) {
			result.oldContentCache = myGrab(this, QRect(0, _playerHeight, _dialogsWidth, height() - _playerHeight));
		} else {
			_sideShadow->hide();
			result.oldContentCache = myGrab(this, QRect(_dialogsWidth, _playerHeight, width() - _dialogsWidth, height() - _playerHeight));
			_sideShadow->show();
		}
		if (_overview) _overview->grabFinish();
		_history->grabFinish();
	}

	if (playerVolumeVisible) {
		_playerVolume->show();
	}
	if (playerPanelVisible) {
		_playerPanel->show();
	}
	if (playerPlaylistVisible) {
		_playerPlaylist->show();
	}
	if (_player) {
		_player->showShadow();
	}

	return result;
}

Window::SectionSlideParams MainWidget::prepareWideSectionAnimation(Window::SectionWidget *section) {
	return prepareShowAnimation(section->hasTopBarShadow());
}

Window::SectionSlideParams MainWidget::prepareHistoryAnimation(PeerId historyPeerId) {
	return prepareShowAnimation(historyPeerId != 0);
}

Window::SectionSlideParams MainWidget::prepareOverviewAnimation() {
	return prepareShowAnimation(true);
}

Window::SectionSlideParams MainWidget::prepareDialogsAnimation() {
	return prepareShowAnimation(false);
}

void MainWidget::showNewWideSection(const Window::SectionMemento *memento, bool back, bool saveInStack) {
	QPixmap animCache;

	Global::RefDialogsListFocused().set(false, true);
	_a_dialogsWidth.finish();

	auto newWideGeometry = QRect(_history->x(), _playerHeight, _history->width(), height() - _playerHeight);
	auto newWideSection = memento->createWidget(this, newWideGeometry);
	auto animatedShow = [this] {
		if (_a_show.animating() || App::passcoded()) {
			return false;
		}
		if (Adaptive::OneColumn() || isSectionShown()) {
			return true;
		}
		return false;
	};
	auto animationParams = animatedShow() ? prepareWideSectionAnimation(newWideSection) : Window::SectionSlideParams();

	if (saveInStack) {
		saveSectionInStack();
	}

	setFocus(); // otherwise dialogs widget could be focused.
	if (_overview) {
		_overview->hide();
		_overview->clear();
		_overview->deleteLater();
		_overview->rpcClear();
		_overview = nullptr;
	}
	if (_wideSection) {
		_wideSection->hide();
		_wideSection->deleteLater();
		_wideSection = nullptr;
	}
	_wideSection = std::move(newWideSection);
	_topBar->hide();
	updateControlsGeometry();
	_history->finishAnimation();
	_history->showHistory(0, 0);
	_history->hide();
	if (Adaptive::OneColumn()) _dialogs->hide();

	if (animationParams) {
		auto direction = back ? Window::SlideDirection::FromLeft : Window::SlideDirection::FromRight;
		_wideSection->showAnimated(direction, animationParams);
	} else {
		_wideSection->showFast();
	}

	orderWidgets();
}

bool MainWidget::isSectionShown() const {
	return _wideSection || _overview || _history->peer();
}

bool MainWidget::stackIsEmpty() const {
	return _stack.empty();
}

void MainWidget::showBackFromStack() {
	if (selectingPeer()) return;
	if (_stack.empty()) {
		Ui::showChatsList();
		if (App::wnd()) QTimer::singleShot(0, App::wnd(), SLOT(setInnerFocus()));
		return;
	}
	auto item = std::move(_stack.back());
	_stack.pop_back();
	if (auto currentHistoryPeer = _history->peer()) {
		clearBotStartToken(currentHistoryPeer);
	}
	if (item->type() == HistoryStackItem) {
		dlgUpdated();
		_peerInStack = nullptr;
		_msgIdInStack = 0;
		for (auto i = _stack.size(); i > 0;) {
			if (_stack[--i]->type() == HistoryStackItem) {
				auto historyItem = static_cast<StackItemHistory*>(_stack[i].get());
				_peerInStack = historyItem->peer;
				_msgIdInStack = historyItem->msgId;
				dlgUpdated();
				break;
			}
		}
		auto historyItem = static_cast<StackItemHistory*>(item.get());
		Ui::showPeerHistory(historyItem->peer->id, ShowAtUnreadMsgId, Ui::ShowWay::Backward);
		_history->setReplyReturns(historyItem->peer->id, historyItem->replyReturns);
	} else if (item->type() == SectionStackItem) {
		auto sectionItem = static_cast<StackItemSection*>(item.get());
		showNewWideSection(sectionItem->memento(), true, false);
	} else if (item->type() == OverviewStackItem) {
		auto overviewItem = static_cast<StackItemOverview*>(item.get());
		showMediaOverview(overviewItem->peer, overviewItem->mediaType, true, overviewItem->lastScrollTop);
	}
}

void MainWidget::orderWidgets() {
	_topBar->raise();
	_dialogs->raise();
	if (_player) {
		_player->raise();
	}
	if (_playerVolume) {
		_playerVolume->raise();
	}
	_mediaType->raise();
	_sideShadow->raise();
	_sideResizeArea->raise();
	_playerPlaylist->raise();
	_playerPanel->raise();
	if (_hider) _hider->raise();
}

QRect MainWidget::historyRect() const {
	QRect r(_history->historyRect());
	r.moveLeft(r.left() + _history->x());
	r.moveTop(r.top() + _history->y());
	return r;
}

QPixmap MainWidget::grabForShowAnimation(const Window::SectionSlideParams &params) {
	_topBar->stopAnim();
	QPixmap result;
	if (_player) {
		_player->hideShadow();
	}
	auto playerVolumeVisible = _playerVolume && !_playerVolume->isHidden();
	if (playerVolumeVisible) {
		_playerVolume->hide();
	}
	auto playerPanelVisible = !_playerPanel->isHidden();
	if (playerPanelVisible) {
		_playerPanel->hide();
	}
	auto playerPlaylistVisible = !_playerPlaylist->isHidden();
	if (playerPlaylistVisible) {
		_playerPlaylist->hide();
	}

	if (Adaptive::OneColumn()) {
		result = myGrab(this, QRect(0, _playerHeight, _dialogsWidth, height() - _playerHeight));
	} else {
		_sideShadow->hide();
		result = myGrab(this, QRect(_dialogsWidth, _playerHeight, width() - _dialogsWidth, height() - _playerHeight));
		_sideShadow->show();
	}
	if (playerVolumeVisible) {
		_playerVolume->show();
	}
	if (playerPanelVisible) {
		_playerPanel->show();
	}
	if (playerPlaylistVisible) {
		_playerPlaylist->show();
	}
	if (_player) {
		_player->showShadow();
	}
	return result;
}

void MainWidget::dlgUpdated() {
	if (_peerInStack) {
		_dialogs->dlgUpdated(_peerInStack, _msgIdInStack);
	}
}

void MainWidget::dlgUpdated(Dialogs::Mode list, Dialogs::Row *row) {
	if (row) {
		_dialogs->dlgUpdated(list, row);
	}
}

void MainWidget::dlgUpdated(PeerData *peer, MsgId msgId) {
	if (!peer) return;
	if (msgId < 0 && -msgId < ServerMaxMsgId && peer->migrateFrom()) {
		_dialogs->dlgUpdated(peer->migrateFrom(), -msgId);
	} else {
		_dialogs->dlgUpdated(peer, msgId);
	}
}

void MainWidget::showJumpToDate(PeerData *peer, QDate requestedDate) {
	t_assert(peer != nullptr);
	auto currentPeerDate = [peer] {
		if (auto history = App::historyLoaded(peer)) {
			if (history->scrollTopItem) {
				return history->scrollTopItem->date.date();
			} else if (history->loadedAtTop() && !history->isEmpty() && history->peer->migrateFrom()) {
				if (auto migrated = App::historyLoaded(history->peer->migrateFrom())) {
					if (migrated->scrollTopItem) {
						// We're up in the migrated history.
						// So current date is the date of first message here.
						return history->blocks.front()->items.front()->date.date();
					}
				}
			} else if (!history->lastMsgDate.isNull()) {
				return history->lastMsgDate.date();
			}
		}
		return QDate::currentDate();
	};
	auto maxPeerDate = [peer] {
		if (auto history = App::historyLoaded(peer)) {
			if (!history->lastMsgDate.isNull()) {
				return history->lastMsgDate.date();
			}
		}
		return QDate::currentDate();
	};
	auto minPeerDate = [peer] {
		if (auto history = App::historyLoaded(peer)) {
			if (history->loadedAtTop()) {
				if (history->isEmpty()) {
					return QDate::currentDate();
				}
				return history->blocks.front()->items.front()->date.date();
			}
		}
		return QDate(2013, 8, 1); // Telegram was launched in August 2013 :)
	};
	auto highlighted = requestedDate.isNull() ? currentPeerDate() : requestedDate;
	auto month = highlighted;
	auto box = Box<CalendarBox>(month, highlighted, [this, peer](const QDate &date) { jumpToDate(peer, date); });
	box->setMinDate(minPeerDate());
	box->setMaxDate(maxPeerDate());
	Ui::show(std::move(box));
}

void MainWidget::jumpToDate(PeerData *peer, const QDate &date) {
	// API returns a message with date <= offset_date.
	// So we request a message with offset_date = desired_date - 1 and add_offset = -1.
	// This should give us the first message with date >= desired_date.
	auto offset_date = static_cast<int>(QDateTime(date).toTime_t()) - 1;
	auto add_offset = -1;
	auto limit = 1;
	auto flags = MTPmessages_Search::Flags(0);
	auto request = MTPmessages_GetHistory(peer->input, MTP_int(0), MTP_int(offset_date), MTP_int(add_offset), MTP_int(limit), MTP_int(0), MTP_int(0));
	MTP::send(request, ::rpcDone([peer](const MTPmessages_Messages &result) {
		auto getMessagesList = [&result, peer]() -> const QVector<MTPMessage>* {
			auto handleMessages = [](auto &messages) {
				App::feedUsers(messages.vusers);
				App::feedChats(messages.vchats);
				return &messages.vmessages.c_vector().v;
			};
			switch (result.type()) {
			case mtpc_messages_messages: return handleMessages(result.c_messages_messages());
			case mtpc_messages_messagesSlice: return handleMessages(result.c_messages_messagesSlice());
			case mtpc_messages_channelMessages: {
				auto &messages = result.c_messages_channelMessages();
				if (peer && peer->isChannel()) {
					peer->asChannel()->ptsReceived(messages.vpts.v);
				} else {
					LOG(("API Error: received messages.channelMessages when no channel was passed! (MainWidget::showJumpToDate)"));
				}
				return handleMessages(messages);
			} break;
			}
			return nullptr;
		};

		if (auto list = getMessagesList()) {
			App::feedMsgs(*list, NewMessageExisting);
			for (auto &message : *list) {
				if (auto id = idFromMessage(message)) {
					Ui::showPeerHistory(peer, id);
					return;
				}
			}
		}
		Ui::showPeerHistory(peer, ShowAtUnreadMsgId);
	}));
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
	//	Ui::show(Box<InformBox>(lang(lng_cant_delete_channel)));
	//}

	return true;
}

void MainWidget::inviteToChannelDone(ChannelData *channel, const MTPUpdates &updates) {
	sentUpdatesReceived(updates);
	QTimer::singleShot(ReloadChannelMembersTimeout, this, SLOT(onActiveChannelUpdateFull()));
}

void MainWidget::onActiveChannelUpdateFull() {
	if (activePeer() && activePeer()->isChannel()) {
		activePeer()->asChannel()->updateFull(true);
	}
}

void MainWidget::historyToDown(History *history) {
	_history->historyToDown(history);
}

void MainWidget::dialogsToUp() {
	_dialogs->dialogsToUp();
}

void MainWidget::newUnreadMsg(History *history, HistoryItem *item) {
	_history->newUnreadMsg(history, item);
}

void MainWidget::markActiveHistoryAsRead() {
	_history->historyWasRead(ReadServerHistoryChecks::OnlyIfUnread);
}

void MainWidget::historyCleared(History *history) {
	_history->historyCleared(history);
}

void MainWidget::showAnimated(const QPixmap &bgAnimCache, bool back) {
	_showBack = back;
	(_showBack ? _cacheOver : _cacheUnder) = bgAnimCache;

	_a_show.finish();

	showAll();
	(_showBack ? _cacheUnder : _cacheOver) = myGrab(this);
	hideAll();

	_a_show.start([this] { animationCallback(); }, 0., 1., st::slideDuration, Window::SlideAnimation::transition());

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
	if (_background) checkChatBackground();

	Painter p(this);
	auto progress = _a_show.current(getms(), 1.);
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

void MainWidget::hideAll() {
	_dialogs->hide();
	_history->hide();
	if (_wideSection) {
		_wideSection->hide();
	}
	if (_overview) {
		_overview->hide();
	}
	_sideShadow->hide();
	_topBar->hide();
	_mediaType->hide();
	if (_player) {
		_player->hide();
		_playerHeight = 0;
	}
}

void MainWidget::showAll() {
	if (cPasswordRecovered()) {
		cSetPasswordRecovered(false);
		Ui::show(Box<InformBox>(lang(lng_signin_password_removed)));
	}
	if (Adaptive::OneColumn()) {
		_sideShadow->hide();
		if (_hider) {
			_hider->hide();
			if (!_forwardConfirm && _hider->wasOffered()) {
				_forwardConfirm = Ui::show(Box<ConfirmBox>(_hider->offeredText(), lang(lng_forward_send), base::lambda_guarded(this, [this] {
					_hider->forward();
					if (_forwardConfirm) _forwardConfirm->closeBox();
					if (_hider) _hider->offerPeer(0);
				}), base::lambda_guarded(this, [this] {
					if (_hider && _forwardConfirm) _hider->offerPeer(0);
				})), ForceFastShowLayer);
			}
		}
		if (selectingPeer()) {
			_dialogs->showFast();
			_history->hide();
			if (_overview) _overview->hide();
			if (_wideSection) _wideSection->hide();
			_topBar->hide();
		} else if (_overview) {
			_overview->show();
		} else if (_wideSection) {
			_wideSection->show();
		} else if (_history->peer()) {
			_history->show();
			_history->updateControlsGeometry();
		} else {
			_dialogs->showFast();
			_history->hide();
		}
		if (!selectingPeer()) {
			if (_wideSection) {
				_topBar->hide();
				_dialogs->hide();
			} else if (isSectionShown()) {
				_topBar->show();
				_dialogs->hide();
			}
		}
	} else {
		_sideShadow->show();
		if (_hider) {
			_hider->show();
			if (_forwardConfirm) {
				_forwardConfirm = nullptr;
				Ui::hideLayer(true);
				if (_hider->wasOffered()) {
					_hider->setFocus();
				}
			}
		}
		_dialogs->showFast();
		if (_overview) {
			_overview->show();
		} else if (_wideSection) {
			_wideSection->show();
		} else {
			_history->show();
			_history->updateControlsGeometry();
		}
		if (_wideSection) {
			_topBar->hide();
		} else if (isSectionShown()) {
			_topBar->show();
		}
	}
	if (_player) {
		_player->show();
		_playerHeight = _player->contentHeight();
	}
	updateControlsGeometry();

	App::wnd()->checkHistoryActivation();
}

void MainWidget::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void MainWidget::updateControlsGeometry() {
	updateWindowAdaptiveLayout();
	auto topBarHeight = _topBar->isHidden() ? 0 : st::topBarHeight;
	if (!Adaptive::SmallColumn()) {
		_a_dialogsWidth.finish();
	}
	if (!_a_dialogsWidth.animating()) {
		_dialogs->stopWidthAnimation();
	}
	auto dialogsWidth = qRound(_a_dialogsWidth.current(_dialogsWidth));
	if (Adaptive::OneColumn()) {
		if (_player) {
			_player->resizeToWidth(dialogsWidth);
			_player->moveToLeft(0, 0);
		}
		_dialogs->setGeometry(0, _playerHeight, dialogsWidth, height() - _playerHeight);
		_topBar->setGeometry(0, _playerHeight, dialogsWidth, st::topBarHeight);
		_history->setGeometry(0, _playerHeight + topBarHeight, dialogsWidth, height() - _playerHeight - topBarHeight);
		if (_hider) _hider->setGeometry(0, 0, dialogsWidth, height());
	} else {
		accumulate_min(dialogsWidth, width() - st::windowMinWidth);
		auto sectionWidth = width() - dialogsWidth;

		_dialogs->setGeometryToLeft(0, 0, dialogsWidth, height());
		_sideShadow->setGeometryToLeft(dialogsWidth, 0, st::lineWidth, height());
		if (_player) {
			_player->resizeToWidth(sectionWidth);
			_player->moveToLeft(dialogsWidth, 0);
		}
		_topBar->setGeometryToLeft(dialogsWidth, _playerHeight, sectionWidth, st::topBarHeight);
		_history->setGeometryToLeft(dialogsWidth, _playerHeight + topBarHeight, sectionWidth, height() - _playerHeight - topBarHeight);
		if (_hider) {
			_hider->setGeometryToLeft(dialogsWidth, 0, sectionWidth, height());
		}
	}
	_sideResizeArea->setGeometryToLeft(_history->x(), 0, st::historyResizeWidth, height());
	auto isSideResizeAreaVisible = [this] {
		if (width() < st::windowMinWidth + st::dialogsWidthMin) {
			return false;
		}
		if (Adaptive::OneColumn() && !isSectionShown()) {
			return false;
		}
		return true;
	};
	_sideResizeArea->setVisible(isSideResizeAreaVisible());
	_mediaType->moveToLeft(width() - _mediaType->width(), _playerHeight + st::topBarHeight);
	if (_wideSection) {
		QRect wideSectionGeometry(_history->x(), _playerHeight, _history->width(), height() - _playerHeight);
		_wideSection->setGeometryWithTopMoved(wideSectionGeometry, _contentScrollAddToY);
	}
	if (_overview) _overview->setGeometry(_history->geometry());
	updateMediaPlayerPosition();
	updateMediaPlaylistPosition(_playerPlaylist->x());
	_contentScrollAddToY = 0;
}

void MainWidget::updateDialogsWidthAnimated() {
	if (!Adaptive::SmallColumn()) {
		return;
	}
	auto dialogsWidth = _dialogsWidth;
	updateWindowAdaptiveLayout();
	if (Adaptive::SmallColumn() && (_dialogsWidth != dialogsWidth || _a_dialogsWidth.animating())) {
		_dialogs->startWidthAnimation();
		_a_dialogsWidth.start([this] { updateControlsGeometry(); }, dialogsWidth, _dialogsWidth, st::dialogsWidthDuration, anim::easeOutCirc);
		updateControlsGeometry();
	}
}

void MainWidget::updateMediaPlayerPosition() {
	_playerPanel->moveToRight(0, 0);
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

void MainWidget::keyPressEvent(QKeyEvent *e) {
}

bool MainWidget::eventFilter(QObject *o, QEvent *e) {
	if (o == _sideResizeArea) {
		auto mouseLeft = [this, e] {
			return mapFromGlobal(static_cast<QMouseEvent*>(e)->globalPos()).x();
		};
		if (e->type() == QEvent::MouseButtonPress && static_cast<QMouseEvent*>(e)->button() == Qt::LeftButton) {
			_resizingSide = true;
			_resizingSideShift = mouseLeft() - (Adaptive::OneColumn() ? 0 : _dialogsWidth);
		} else if (e->type() == QEvent::MouseButtonRelease) {
			_resizingSide = false;
			if (!Adaptive::OneColumn()) {
				Global::SetDialogsWidthRatio(float64(_dialogsWidth) / width());
			}
			Local::writeUserSettings();
		} else if (e->type() == QEvent::MouseMove && _resizingSide) {
			auto newWidth = mouseLeft() - _resizingSideShift;
			Global::SetDialogsWidthRatio(float64(newWidth) / width());
			updateControlsGeometry();
		}
	} else if (e->type() == QEvent::FocusIn) {
		if (auto widget = qobject_cast<QWidget*>(o)) {
			if (_history == widget || _history->isAncestorOf(widget)
				|| (_overview && (_overview == widget || _overview->isAncestorOf(widget)))
				|| (_wideSection && (_wideSection == widget || _wideSection->isAncestorOf(widget)))) {
				Global::RefDialogsListFocused().set(false, false);
			} else if (_dialogs == widget || _dialogs->isAncestorOf(widget)) {
				Global::RefDialogsListFocused().set(true, false);
			}
		}
	} else if (e->type() == QEvent::MouseButtonPress) {
		if (static_cast<QMouseEvent*>(e)->button() == Qt::BackButton) {
			showBackFromStack();
			return true;
		}
	}
	return TWidget::eventFilter(o, e);
}

void MainWidget::handleAdaptiveLayoutUpdate() {
	showAll();
	_sideShadow->setVisible(!Adaptive::OneColumn());
	if (_player) {
		_player->updateAdaptiveLayout();
	}
}

void MainWidget::updateWindowAdaptiveLayout() {
	auto layout = Adaptive::WindowLayout::OneColumn;

	auto dialogsWidth = qRound(width() * Global::DialogsWidthRatio());
	auto historyWidth = width() - dialogsWidth;
	accumulate_max(historyWidth, st::windowMinWidth);
	dialogsWidth = width() - historyWidth;

	auto useOneColumnLayout = [this, dialogsWidth] {
		auto someSectionShown = !selectingPeer() && isSectionShown();
		if (dialogsWidth < st::dialogsPadding.x() && (Adaptive::OneColumn() || someSectionShown)) {
			return true;
		}
		if (width() < st::windowMinWidth + st::dialogsWidthMin) {
			return true;
		}
		return false;
	};
	auto useSmallColumnLayout = [this, dialogsWidth] {
		// used if useOneColumnLayout() == false.
		if (dialogsWidth < st::dialogsWidthMin / 2) {
			return true;
		}
		return false;
	};
	if (useOneColumnLayout()) {
		dialogsWidth = width();
	} else if (useSmallColumnLayout()) {
		layout = Adaptive::WindowLayout::SmallColumn;
		auto forceWideDialogs = [this] {
			if (Global::DialogsListDisplayForced().value()) {
				return true;
			} else if (Global::DialogsListFocused().value()) {
				return true;
			}
			return !isSectionShown();
		};
		if (forceWideDialogs()) {
			dialogsWidth = st::dialogsWidthMin;
		} else {
			dialogsWidth = st::dialogsPadding.x() + st::dialogsPhotoSize + st::dialogsPadding.x();
		}
	} else {
		layout = Adaptive::WindowLayout::Normal;
		accumulate_max(dialogsWidth, st::dialogsWidthMin);
	}
	_dialogsWidth = dialogsWidth;
	if (layout != Global::AdaptiveWindowLayout()) {
		Global::SetAdaptiveWindowLayout(layout);
		Adaptive::Changed().notify(true);
	}
}

bool MainWidget::needBackButton() {
	return isSectionShown();
}

bool MainWidget::paintTopBar(Painter &p, int decreaseWidth, TimeMs ms) {
	if (_overview) {
		return _overview->paintTopBar(p, decreaseWidth);
	} else if (!_wideSection) {
		return _history->paintTopBar(p, decreaseWidth, ms);
	}
	return false;
}

QRect MainWidget::getMembersShowAreaGeometry() const {
	if (!_overview && !_wideSection) {
		return _history->getMembersShowAreaGeometry();
	}
	return QRect();
}

void MainWidget::setMembersShowAreaActive(bool active) {
	if (!active || (!_overview && !_wideSection)) {
		_history->setMembersShowAreaActive(active);
	}
}

void MainWidget::onPhotosSelect() {
	if (_overview) _overview->switchType(OverviewPhotos);
	_mediaType->hideAnimated();
}

void MainWidget::onVideosSelect() {
	if (_overview) _overview->switchType(OverviewVideos);
	_mediaType->hideAnimated();
}

void MainWidget::onSongsSelect() {
	if (_overview) _overview->switchType(OverviewMusicFiles);
	_mediaType->hideAnimated();
}

void MainWidget::onDocumentsSelect() {
	if (_overview) _overview->switchType(OverviewFiles);
	_mediaType->hideAnimated();
}

void MainWidget::onAudiosSelect() {
	if (_overview) _overview->switchType(OverviewVoiceFiles);
	_mediaType->hideAnimated();
}

void MainWidget::onLinksSelect() {
	if (_overview) _overview->switchType(OverviewLinks);
	_mediaType->hideAnimated();
}

Window::TopBarWidget *MainWidget::topBar() {
	return _topBar;
}

int MainWidget::backgroundFromY() const {
	return (_topBar->isHidden() ? 0 : (-st::topBarHeight)) - _playerHeight;
}

void MainWidget::onTopBarClick() {
	if (_overview) {
		_overview->topBarClick();
	} else if (!_wideSection) {
		_history->topBarClick();
	}
}

void MainWidget::onHistoryShown(History *history, MsgId atMsgId) {
	if ((!Adaptive::OneColumn() || !selectingPeer()) && (_overview || history)) {
		_topBar->show();
	} else {
		_topBar->hide();
	}
	updateControlsGeometry();
	if (_a_show.animating()) {
		_topBar->hide();
	}

	dlgUpdated(history ? history->peer : nullptr, atMsgId);
}

void MainWidget::searchInPeer(PeerData *peer) {
	_dialogs->searchInPeer(peer);
	if (Adaptive::OneColumn()) {
		dialogsToUp();
		Ui::showChatsList();
	} else {
		_dialogs->activate();
	}
}

void MainWidget::onUpdateNotifySettings() {
	if (this != App::main()) return;
	while (!updateNotifySettingPeers.isEmpty()) {
		PeerData *peer = *updateNotifySettingPeers.begin();
		updateNotifySettingPeers.erase(updateNotifySettingPeers.begin());

		if (peer->notify == UnknownNotifySettings || peer->notify == EmptyNotifySettings) {
			peer->notify = new NotifySettings();
		}
		MTP::send(MTPaccount_UpdateNotifySettings(MTP_inputNotifyPeer(peer->input), MTP_inputPeerNotifySettings(MTP_flags(mtpCastFlags(peer->notify->flags)), MTP_int(peer->notify->mute), MTP_string(peer->notify->sound))), RPCResponseHandler(), 0, updateNotifySettingPeers.isEmpty() ? 0 : 10);
	}
}

void MainWidget::feedUpdateVector(const MTPVector<MTPUpdate> &updates, bool skipMessageIds) {
	const auto &v(updates.c_vector().v);
	for (QVector<MTPUpdate>::const_iterator i = v.cbegin(), e = v.cend(); i != e; ++i) {
		if (skipMessageIds && i->type() == mtpc_updateMessageID) continue;
		feedUpdate(*i);
	}
}

void MainWidget::feedMessageIds(const MTPVector<MTPUpdate> &updates) {
	const auto &v(updates.c_vector().v);
	for (QVector<MTPUpdate>::const_iterator i = v.cbegin(), e = v.cend(); i != e; ++i) {
		if (i->type() == mtpc_updateMessageID) {
			feedUpdate(*i);
		}
	}
}

bool MainWidget::updateFail(const RPCError &e) {
	App::logOutDelayed();
	return true;
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
		if (_bySeqTimer.isActive()) _bySeqTimer.stop();
		for (QMap<int32, MTPUpdates>::iterator i = _bySeqUpdates.begin(); i != _bySeqUpdates.end();) {
			int32 s = i.key();
			if (s <= seq + 1) {
				MTPUpdates v = i.value();
				i = _bySeqUpdates.erase(i);
				if (s == seq + 1) {
					return feedUpdates(v);
				}
			} else {
				if (!_bySeqTimer.isActive()) _bySeqTimer.start(WaitForSkippedTimeout);
				break;
			}
		}
	}
}

void MainWidget::gotChannelDifference(ChannelData *channel, const MTPupdates_ChannelDifference &diff) {
	_channelFailDifferenceTimeout.remove(channel);

	int32 timeout = 0;
	bool isFinal = true;
	switch (diff.type()) {
	case mtpc_updates_channelDifferenceEmpty: {
		auto &d = diff.c_updates_channelDifferenceEmpty();
		if (d.has_timeout()) timeout = d.vtimeout.v;
		isFinal = d.is_final();
		channel->ptsInit(d.vpts.v);
	} break;

	case mtpc_updates_channelDifferenceTooLong: {
		auto &d = diff.c_updates_channelDifferenceTooLong();

		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		auto h = App::historyLoaded(channel->id);
		if (h) {
			h->setNotLoadedAtBottom();
		}
		App::feedMsgs(d.vmessages, NewMessageLast);
		if (h) {
			if (auto item = App::histItemById(peerToChannel(channel->id), d.vtop_message.v)) {
				h->setLastMessage(item);
			}
			if (d.vunread_count.v >= h->unreadCount()) {
				h->setUnreadCount(d.vunread_count.v);
				h->inboxReadBefore = d.vread_inbox_max_id.v + 1;
			}
			if (_history->peer() == channel) {
				_history->updateHistoryDownVisibility();
				_history->preloadHistoryIfNeeded();
			}
			h->asChannelHistory()->getRangeDifference();
		}

		if (d.has_timeout()) timeout = d.vtimeout.v;
		isFinal = d.is_final();
		channel->ptsInit(d.vpts.v);
	} break;

	case mtpc_updates_channelDifference: {
		auto &d = diff.c_updates_channelDifference();

		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);

		_handlingChannelDifference = true;
		feedMessageIds(d.vother_updates);

		// feed messages and groups, copy from App::feedMsgs
		auto h = App::history(channel->id);
		auto &vmsgs = d.vnew_messages.c_vector().v;
		QMap<uint64, int> msgsIds;
		for (int i = 0, l = vmsgs.size(); i < l; ++i) {
			auto &msg = vmsgs[i];
			switch (msg.type()) {
			case mtpc_message: {
				const auto &d(msg.c_message());
				if (App::checkEntitiesAndViewsUpdate(d)) { // new message, index my forwarded messages to links _overview, already in blocks
					LOG(("Skipping message, because it is already in blocks!"));
				} else {
					msgsIds.insert((uint64(uint32(d.vid.v)) << 32) | uint64(i), i + 1);
				}
			} break;
			case mtpc_messageEmpty: msgsIds.insert((uint64(uint32(msg.c_messageEmpty().vid.v)) << 32) | uint64(i), i + 1); break;
			case mtpc_messageService: msgsIds.insert((uint64(uint32(msg.c_messageService().vid.v)) << 32) | uint64(i), i + 1); break;
			}
		}
		for_const (auto msgIndex, msgsIds) {
			if (msgIndex > 0) { // add message
				auto &msg = vmsgs.at(msgIndex - 1);
				if (channel->id != peerFromMessage(msg)) {
					LOG(("API Error: message with invalid peer returned in channelDifference, channelId: %1, peer: %2").arg(peerToChannel(channel->id)).arg(peerFromMessage(msg)));
					continue; // wtf
				}
				h->addNewMessage(msg, NewMessageUnread);
			}
		}

		feedUpdateVector(d.vother_updates, true);
		_handlingChannelDifference = false;

		if (d.has_timeout()) timeout = d.vtimeout.v;
		isFinal = d.is_final();
		channel->ptsInit(d.vpts.v);
	} break;
	}

	channel->ptsSetRequesting(false);

	if (!isFinal) {
		MTP_LOG(0, ("getChannelDifference { good - after not final channelDifference was received }%1").arg(cTestMode() ? " TESTMODE" : ""));
		getChannelDifference(channel);
	} else if (activePeer() == channel) {
		channel->ptsWaitingForShortPoll(timeout ? (timeout * 1000) : WaitForChannelGetDifference);
	}
}

void MainWidget::gotRangeDifference(ChannelData *channel, const MTPupdates_ChannelDifference &diff) {
	int32 nextRequestPts = 0;
	bool isFinal = true;
	switch (diff.type()) {
	case mtpc_updates_channelDifferenceEmpty: {
		const auto &d(diff.c_updates_channelDifferenceEmpty());
		nextRequestPts = d.vpts.v;
		isFinal = d.is_final();
	} break;

	case mtpc_updates_channelDifferenceTooLong: {
		const auto &d(diff.c_updates_channelDifferenceTooLong());

		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);

		nextRequestPts = d.vpts.v;
		isFinal = d.is_final();
	} break;

	case mtpc_updates_channelDifference: {
		const auto &d(diff.c_updates_channelDifference());

		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);

		_handlingChannelDifference = true;
		feedMessageIds(d.vother_updates);
		App::feedMsgs(d.vnew_messages, NewMessageUnread);
		feedUpdateVector(d.vother_updates, true);
		_handlingChannelDifference = false;

		nextRequestPts = d.vpts.v;
		isFinal = d.is_final();
	} break;
	}

	if (!isFinal) {
		if (History *h = App::historyLoaded(channel->id)) {
			MTP_LOG(0, ("getChannelDifference { good - after not final channelDifference was received, validating history part }%1").arg(cTestMode() ? " TESTMODE" : ""));
			h->asChannelHistory()->getRangeDifferenceNext(nextRequestPts);
		}
	}
}

bool MainWidget::failChannelDifference(ChannelData *channel, const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	LOG(("RPC Error in getChannelDifference: %1 %2: %3").arg(error.code()).arg(error.type()).arg(error.description()));
	failDifferenceStartTimerFor(channel);
	return true;
}

void MainWidget::gotState(const MTPupdates_State &state) {
	const auto &d(state.c_updates_state());
	updSetState(d.vpts.v, d.vdate.v, d.vqts.v, d.vseq.v);

	_lastUpdateTime = getms(true);
	noUpdatesTimer.start(NoUpdatesTimeout);
	_ptsWaiter.setRequesting(false);

	_dialogs->loadDialogs();
	updateOnline();
}

void MainWidget::gotDifference(const MTPupdates_Difference &difference) {
	_failDifferenceTimeout = 1;

	switch (difference.type()) {
	case mtpc_updates_differenceEmpty: {
		auto &d = difference.c_updates_differenceEmpty();
		updSetState(_ptsWaiter.current(), d.vdate.v, updQts, d.vseq.v);

		_lastUpdateTime = getms(true);
		noUpdatesTimer.start(NoUpdatesTimeout);

		_ptsWaiter.setRequesting(false);
	} break;
	case mtpc_updates_differenceSlice: {
		auto &d = difference.c_updates_differenceSlice();
		feedDifference(d.vusers, d.vchats, d.vnew_messages, d.vother_updates);

		auto &s = d.vintermediate_state.c_updates_state();
		updSetState(s.vpts.v, s.vdate.v, s.vqts.v, s.vseq.v);

		_ptsWaiter.setRequesting(false);

		MTP_LOG(0, ("getDifference { good - after a slice of difference was received }%1").arg(cTestMode() ? " TESTMODE" : ""));
		getDifference();
	} break;
	case mtpc_updates_difference: {
		auto &d = difference.c_updates_difference();
		feedDifference(d.vusers, d.vchats, d.vnew_messages, d.vother_updates);

		gotState(d.vstate);
	} break;
	case mtpc_updates_differenceTooLong: {
		auto &d = difference.c_updates_differenceTooLong();
		LOG(("API Error: updates.differenceTooLong is not supported by Telegram Desktop!"));
	} break;
	};
}

bool MainWidget::getDifferenceTimeChanged(ChannelData *channel, int32 ms, ChannelGetDifferenceTime &channelCurTime, TimeMs &curTime) {
	if (channel) {
		if (ms <= 0) {
			ChannelGetDifferenceTime::iterator i = channelCurTime.find(channel);
			if (i != channelCurTime.cend()) {
				channelCurTime.erase(i);
			} else {
				return false;
			}
		} else {
			auto when = getms(true) + ms;
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
			auto when = getms(true) + ms;
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
		onGetDifferenceTimeByPts();
	}
}

void MainWidget::failDifferenceStartTimerFor(ChannelData *channel) {
	int32 ms = 0;
	ChannelFailDifferenceTimeout::iterator i;
	if (channel) {
		i = _channelFailDifferenceTimeout.find(channel);
		if (i == _channelFailDifferenceTimeout.cend()) {
			i = _channelFailDifferenceTimeout.insert(channel, 1);
		}
		ms = i.value() * 1000;
	} else {
		ms = _failDifferenceTimeout * 1000;
	}
	if (getDifferenceTimeChanged(channel, ms, _channelGetDifferenceTimeAfterFail, _getDifferenceTimeAfterFail)) {
		onGetDifferenceTimeAfterFail();
	}
	if (channel) {
		if (i.value() < 64) i.value() *= 2;
	} else {
		if (_failDifferenceTimeout < 64) _failDifferenceTimeout *= 2;
	}
}

bool MainWidget::ptsUpdated(int32 pts, int32 ptsCount) { // return false if need to save that update and apply later
	return _ptsWaiter.updated(0, pts, ptsCount);
}

bool MainWidget::ptsUpdated(int32 pts, int32 ptsCount, const MTPUpdates &updates) {
	return _ptsWaiter.updated(0, pts, ptsCount, updates);
}

bool MainWidget::ptsUpdated(int32 pts, int32 ptsCount, const MTPUpdate &update) {
	return _ptsWaiter.updated(0, pts, ptsCount, update);
}

void MainWidget::ptsApplySkippedUpdates() {
	return _ptsWaiter.applySkippedUpdates(0);
}

void MainWidget::feedDifference(const MTPVector<MTPUser> &users, const MTPVector<MTPChat> &chats, const MTPVector<MTPMessage> &msgs, const MTPVector<MTPUpdate> &other) {
	App::wnd()->checkAutoLock();
	App::feedUsers(users);
	App::feedChats(chats);
	feedMessageIds(other);
	App::feedMsgs(msgs, NewMessageUnread);
	feedUpdateVector(other, true);
	_history->peerMessagesUpdated();
}

bool MainWidget::failDifference(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	LOG(("RPC Error in getDifference: %1 %2: %3").arg(error.code()).arg(error.type()).arg(error.description()));
	failDifferenceStartTimerFor(0);
	return true;
}

void MainWidget::onGetDifferenceTimeByPts() {
	auto now = getms(true), wait = 0LL;
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
		_byPtsTimer.start(wait);
	} else {
		_byPtsTimer.stop();
	}
}

void MainWidget::onGetDifferenceTimeAfterFail() {
	auto now = getms(true), wait = 0LL;
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
		_failDifferenceTimer.start(wait);
	} else {
		_failDifferenceTimer.stop();
	}
}

void MainWidget::getDifference() {
	if (this != App::main()) return;

	_getDifferenceTimeByPts = 0;

	if (requestingDifference()) return;

	_bySeqUpdates.clear();
	_bySeqTimer.stop();

	noUpdatesTimer.stop();
	_getDifferenceTimeAfterFail = 0;

	_ptsWaiter.setRequesting(true);

	MTPupdates_GetDifference::Flags flags = 0;
	MTP::send(MTPupdates_GetDifference(MTP_flags(flags), MTP_int(_ptsWaiter.current()), MTPint(), MTP_int(updDate), MTP_int(updQts)), rpcDone(&MainWidget::gotDifference), rpcFail(&MainWidget::failDifference));
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
	MTPupdates_GetChannelDifference::Flags flags = MTPupdates_GetChannelDifference::Flag::f_force;
	if (from != ChannelDifferenceRequest::PtsGapOrShortPoll) {
		if (!channel->ptsWaitingForSkipped()) {
			flags = 0; // No force flag when requesting for short poll.
		}
	}
	MTP::send(MTPupdates_GetChannelDifference(MTP_flags(flags), channel->inputChannel, filter, MTP_int(channel->pts()), MTP_int(MTPChannelGetDifferenceLimit)), rpcDone(&MainWidget::gotChannelDifference, channel), rpcFail(&MainWidget::failChannelDifference, channel));
}

void MainWidget::mtpPing() {
	MTP::ping();
}

void MainWidget::start(const MTPUser *self) {
	if (!self) {
		MTP::send(MTPusers_GetUsers(MTP_vector<MTPInputUser>(1, MTP_inputUserSelf())), rpcDone(&MainWidget::startWithSelf));
		return;
	}
	if (!AuthSession::Current().validateSelf(*self)) {
		return;
	}

	Local::readSavedPeers();
	cSetOtherOnline(0);
	if (auto user = App::feedUsers(MTP_vector<MTPUser>(1, *self))) {
		user->loadUserpic();
	}

	MTP::send(MTPupdates_GetState(), rpcDone(&MainWidget::gotState));
	update();

	_started = true;
	App::wnd()->sendServiceHistoryRequest();
	Local::readInstalledStickers();
	Local::readFeaturedStickers();
	Local::readRecentStickers();
	Local::readSavedGifs();
	_history->start();

	checkStartUrl();
}

bool MainWidget::started() {
	return _started;
}

void MainWidget::checkStartUrl() {
	if (!cStartUrl().isEmpty() && App::self() && !App::passcoded()) {
		auto url = cStartUrl();
		cSetStartUrl(QString());

		openLocalUrl(url);
	}
}

void MainWidget::openLocalUrl(const QString &url) {
	auto urlTrimmed = url.trimmed();
	if (urlTrimmed.size() > 8192) urlTrimmed = urlTrimmed.mid(0, 8192);

	if (!urlTrimmed.startsWith(qstr("tg://"), Qt::CaseInsensitive)) {
		return;
	}
	auto command = urlTrimmed.midRef(qstr("tg://").size());

	using namespace qthelp;
	auto matchOptions = RegExOption::CaseInsensitive;
	if (auto joinChatMatch = regex_match(qsl("^join/?\\?invite=([a-zA-Z0-9\\.\\_\\-]+)(&|$)"), command, matchOptions)) {
		joinGroupByHash(joinChatMatch->captured(1));
	} else if (auto stickerSetMatch = regex_match(qsl("^addstickers/?\\?set=([a-zA-Z0-9\\.\\_]+)(&|$)"), command, matchOptions)) {
		stickersBox(MTP_inputStickerSetShortName(MTP_string(stickerSetMatch->captured(1))));
	} else if (auto shareUrlMatch = regex_match(qsl("^msg_url/?\\?(.+)(#|$)"), command, matchOptions)) {
		auto params = url_parse_params(shareUrlMatch->captured(1), UrlParamNameTransform::ToLower);
		auto url = params.value(qsl("url"));
		if (!url.isEmpty()) {
			shareUrlLayer(url, params.value("text"));
		}
	} else if (auto confirmPhoneMatch = regex_match(qsl("^confirmphone/?\\?(.+)(#|$)"), command, matchOptions)) {
		auto params = url_parse_params(confirmPhoneMatch->captured(1), UrlParamNameTransform::ToLower);
		auto phone = params.value(qsl("phone"));
		auto hash = params.value(qsl("hash"));
		if (!phone.isEmpty() && !hash.isEmpty()) {
			ConfirmPhoneBox::start(phone, hash);
		}
	} else if (auto usernameMatch = regex_match(qsl("^resolve/?\\?(.+)(#|$)"), command, matchOptions)) {
		auto params = url_parse_params(usernameMatch->captured(1), UrlParamNameTransform::ToLower);
		auto domain = params.value(qsl("domain"));
		if (regex_match(qsl("^[a-zA-Z0-9\\.\\_]+$"), domain, matchOptions)) {
			auto start = qsl("start");
			auto startToken = params.value(start);
			if (startToken.isEmpty()) {
				start = qsl("startgroup");
				startToken = params.value(start);
				if (startToken.isEmpty()) {
					start = QString();
				}
			}
			auto post = (start == qsl("startgroup")) ? ShowAtProfileMsgId : ShowAtUnreadMsgId;
			auto postParam = params.value(qsl("post"));
			if (auto postId = postParam.toInt()) {
				post = postId;
			}
			auto gameParam = params.value(qsl("game"));
			if (!gameParam.isEmpty() && regex_match(qsl("^[a-zA-Z0-9\\.\\_]+$"), gameParam, matchOptions)) {
				startToken = gameParam;
				post = ShowAtGameShareMsgId;
			}
			openPeerByName(domain, post, startToken);
		}
	} else if (auto shareGameScoreMatch = regex_match(qsl("^share_game_score/?\\?(.+)(#|$)"), command, matchOptions)) {
		auto params = url_parse_params(shareGameScoreMatch->captured(1), UrlParamNameTransform::ToLower);
		shareGameScoreByHash(params.value(qsl("hash")));
	}
}

void MainWidget::openPeerByName(const QString &username, MsgId msgId, const QString &startToken) {
	App::wnd()->hideMediaview();

	PeerData *peer = App::peerByName(username);
	if (peer) {
		if (msgId == ShowAtGameShareMsgId) {
			if (peer->isUser() && peer->asUser()->botInfo && !startToken.isEmpty()) {
				peer->asUser()->botInfo->shareGameShortName = startToken;
				Ui::show(Box<ContactsBox>(peer->asUser()));
			} else {
				Ui::showPeerHistoryAsync(peer->id, ShowAtUnreadMsgId, Ui::ShowWay::Forward);
			}
		} else if (msgId == ShowAtProfileMsgId && !peer->isChannel()) {
			if (peer->isUser() && peer->asUser()->botInfo && !peer->asUser()->botInfo->cantJoinGroups && !startToken.isEmpty()) {
				peer->asUser()->botInfo->startGroupToken = startToken;
				Ui::show(Box<ContactsBox>(peer->asUser()));
			} else if (peer->isUser() && peer->asUser()->botInfo) {
				// Always open bot chats, even from mention links.
				Ui::showPeerHistoryAsync(peer->id, ShowAtUnreadMsgId, Ui::ShowWay::Forward);
			} else {
				Ui::showPeerProfile(peer);
			}
		} else {
			if (msgId == ShowAtProfileMsgId || !peer->isChannel()) { // show specific posts only in channels / supergroups
				msgId = ShowAtUnreadMsgId;
			}
			if (peer->isUser() && peer->asUser()->botInfo) {
				peer->asUser()->botInfo->startToken = startToken;
				if (peer == _history->peer()) {
					_history->updateControlsVisibility();
					_history->updateControlsGeometry();
				}
			}
			Ui::showPeerHistoryAsync(peer->id, msgId, Ui::ShowWay::Forward);
		}
	} else {
		MTP::send(MTPcontacts_ResolveUsername(MTP_string(username)), rpcDone(&MainWidget::usernameResolveDone, qMakePair(msgId, startToken)), rpcFail(&MainWidget::usernameResolveFail, username));
	}
}

void MainWidget::joinGroupByHash(const QString &hash) {
	App::wnd()->hideMediaview();
	MTP::send(MTPmessages_CheckChatInvite(MTP_string(hash)), rpcDone(&MainWidget::inviteCheckDone, hash), rpcFail(&MainWidget::inviteCheckFail));
}

void MainWidget::stickersBox(const MTPInputStickerSet &set) {
	App::wnd()->hideMediaview();
	auto box = Ui::show(Box<StickerSetBox>(set));
	connect(box, SIGNAL(installed(uint64)), this, SLOT(onStickersInstalled(uint64)));
}

void MainWidget::onStickersInstalled(uint64 setId) {
	_history->stickersInstalled(setId);
}

void MainWidget::onFullPeerUpdated(PeerData *peer) {
	emit peerUpdated(peer);
}

void MainWidget::onSelfParticipantUpdated(ChannelData *channel) {
	auto history = App::historyLoaded(channel->id);
	if (_updatedChannels.contains(channel)) {
		_updatedChannels.remove(channel);
		if ((history ? history : App::history(channel->id))->isEmpty()) {
			checkPeerHistory(channel);
		} else {
			history->asChannelHistory()->checkJoinedMessage(true);
			_history->peerMessagesUpdated(channel->id);
		}
	} else if (history) {
		history->asChannelHistory()->checkJoinedMessage();
		_history->peerMessagesUpdated(channel->id);
	}
}

bool MainWidget::contentOverlapped(const QRect &globalRect) {
	return (_history->contentOverlapped(globalRect) ||
			_playerPanel->overlaps(globalRect) ||
			_playerPlaylist->overlaps(globalRect) ||
			(_playerVolume && _playerVolume->overlaps(globalRect)) ||
			_mediaType->overlaps(globalRect));
}

void MainWidget::usernameResolveDone(QPair<MsgId, QString> msgIdAndStartToken, const MTPcontacts_ResolvedPeer &result) {
	Ui::hideLayer();
	if (result.type() != mtpc_contacts_resolvedPeer) return;

	const auto &d(result.c_contacts_resolvedPeer());
	App::feedUsers(d.vusers);
	App::feedChats(d.vchats);
	PeerId peerId = peerFromMTP(d.vpeer);
	if (!peerId) return;

	PeerData *peer = App::peer(peerId);
	MsgId msgId = msgIdAndStartToken.first;
	QString startToken = msgIdAndStartToken.second;
	if (msgId == ShowAtProfileMsgId && !peer->isChannel()) {
		if (peer->isUser() && peer->asUser()->botInfo && !peer->asUser()->botInfo->cantJoinGroups && !startToken.isEmpty()) {
			peer->asUser()->botInfo->startGroupToken = startToken;
			Ui::show(Box<ContactsBox>(peer->asUser()));
		} else if (peer->isUser() && peer->asUser()->botInfo) {
			// Always open bot chats, even from mention links.
			Ui::showPeerHistoryAsync(peer->id, ShowAtUnreadMsgId, Ui::ShowWay::Forward);
		} else {
			Ui::showPeerProfile(peer);
		}
	} else {
		if (msgId == ShowAtProfileMsgId || !peer->isChannel()) { // show specific posts only in channels / supergroups
			msgId = ShowAtUnreadMsgId;
		}
		if (peer->isUser() && peer->asUser()->botInfo) {
			peer->asUser()->botInfo->startToken = startToken;
			if (peer == _history->peer()) {
				_history->updateControlsVisibility();
				_history->updateControlsGeometry();
			}
		}
		Ui::showPeerHistory(peer->id, msgId, Ui::ShowWay::Forward);
	}
}

bool MainWidget::usernameResolveFail(QString name, const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (error.code() == 400) {
		Ui::show(Box<InformBox>(lng_username_not_found(lt_user, name)));
	}
	return true;
}

void MainWidget::inviteCheckDone(QString hash, const MTPChatInvite &invite) {
	switch (invite.type()) {
	case mtpc_chatInvite: {
		auto &d(invite.c_chatInvite());

		QVector<UserData*> participants;
		if (d.has_participants()) {
			auto &v = d.vparticipants.c_vector().v;
			participants.reserve(v.size());
			for_const (auto &user, v) {
				if (auto feededUser = App::feedUser(user)) {
					participants.push_back(feededUser);
				}
			}
		}
		_inviteHash = hash;
		Ui::show(Box<ConfirmInviteBox>(qs(d.vtitle), d.vphoto, d.vparticipants_count.v, participants));
	} break;

	case mtpc_chatInviteAlready: {
		const auto &d(invite.c_chatInviteAlready());
		PeerData *chat = App::feedChats(MTP_vector<MTPChat>(1, d.vchat));
		if (chat) {
			Ui::showPeerHistory(chat->id, ShowAtUnreadMsgId);
		}
	} break;
	}
}

bool MainWidget::inviteCheckFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (error.code() == 400) {
		Ui::show(Box<InformBox>(lang(lng_group_invite_bad_link)));
	}
	return true;
}

void MainWidget::onInviteImport() {
	if (_inviteHash.isEmpty()) return;
	MTP::send(MTPmessages_ImportChatInvite(MTP_string(_inviteHash)), rpcDone(&MainWidget::inviteImportDone), rpcFail(&MainWidget::inviteImportFail));
}

void MainWidget::inviteImportDone(const MTPUpdates &updates) {
	App::main()->sentUpdatesReceived(updates);

	Ui::hideLayer();
	const QVector<MTPChat> *v = 0;
	switch (updates.type()) {
	case mtpc_updates: v = &updates.c_updates().vchats.c_vector().v; break;
	case mtpc_updatesCombined: v = &updates.c_updatesCombined().vchats.c_vector().v; break;
	default: LOG(("API Error: unexpected update cons %1 (MainWidget::inviteImportDone)").arg(updates.type())); break;
	}
	if (v && !v->isEmpty()) {
		if (v->front().type() == mtpc_chat) {
			Ui::showPeerHistory(peerFromChat(v->front().c_chat().vid.v), ShowAtTheEndMsgId);
		} else if (v->front().type() == mtpc_channel) {
			Ui::showPeerHistory(peerFromChannel(v->front().c_channel().vid.v), ShowAtTheEndMsgId);
		}
	}
}

bool MainWidget::inviteImportFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (error.type() == qstr("CHANNELS_TOO_MUCH")) {
		Ui::show(Box<InformBox>(lang(lng_join_channel_error)));
	} else if (error.code() == 400) {
		Ui::show(Box<InformBox>(lang(error.type() == qstr("USERS_TOO_MUCH") ? lng_group_invite_no_room : lng_group_invite_bad_link)));
	}

	return true;
}

void MainWidget::startWithSelf(const MTPVector<MTPUser> &users) {
	auto &v = users.c_vector().v;
	if (v.isEmpty()) {
		LOG(("Auth Error: self user not received."));
		return App::logOutDelayed();
	}
	start(&v[0]);
}

void MainWidget::applyNotifySetting(const MTPNotifyPeer &peer, const MTPPeerNotifySettings &settings, History *h) {
	PeerData *updatePeer = nullptr;
	bool changed = false;
	switch (settings.type()) {
	case mtpc_peerNotifySettingsEmpty:
		switch (peer.type()) {
		case mtpc_notifyAll: globalNotifyAllPtr = EmptyNotifySettings; break;
		case mtpc_notifyUsers: globalNotifyUsersPtr = EmptyNotifySettings; break;
		case mtpc_notifyChats: globalNotifyChatsPtr = EmptyNotifySettings; break;
		case mtpc_notifyPeer: {
			if ((updatePeer = App::peerLoaded(peerFromMTP(peer.c_notifyPeer().vpeer)))) {
				changed = (updatePeer->notify != EmptyNotifySettings);
				if (changed) {
					if (updatePeer->notify != UnknownNotifySettings) {
						delete updatePeer->notify;
					}
					updatePeer->notify = EmptyNotifySettings;
					App::unregMuted(updatePeer);
					if (!h) h = App::history(updatePeer->id);
					h->setMute(false);
				}
			}
		} break;
		}
	break;
	case mtpc_peerNotifySettings: {
		const auto &d(settings.c_peerNotifySettings());
		NotifySettingsPtr setTo = UnknownNotifySettings;
		switch (peer.type()) {
		case mtpc_notifyAll: setTo = globalNotifyAllPtr = &globalNotifyAll; break;
		case mtpc_notifyUsers: setTo = globalNotifyUsersPtr = &globalNotifyUsers; break;
		case mtpc_notifyChats: setTo = globalNotifyChatsPtr = &globalNotifyChats; break;
		case mtpc_notifyPeer: {
			if ((updatePeer = App::peerLoaded(peerFromMTP(peer.c_notifyPeer().vpeer)))) {
				if (updatePeer->notify == UnknownNotifySettings || updatePeer->notify == EmptyNotifySettings) {
					changed = true;
					updatePeer->notify = new NotifySettings();
				}
				setTo = updatePeer->notify;
			}
		} break;
		}
		if (setTo == UnknownNotifySettings) break;

		changed = (setTo->flags != d.vflags.v) || (setTo->mute != d.vmute_until.v) || (setTo->sound != d.vsound.c_string().v);
		if (changed) {
			setTo->flags = d.vflags.v;
			setTo->mute = d.vmute_until.v;
			setTo->sound = d.vsound.c_string().v;
			if (updatePeer) {
				if (!h) h = App::history(updatePeer->id);
				int32 changeIn = 0;
				if (isNotifyMuted(setTo, &changeIn)) {
					AuthSession::Current().notifications()->clearFromHistory(h);
					h->setMute(true);
					App::regMuted(updatePeer, changeIn);
				} else {
					h->setMute(false);
				}
			}
		}
	} break;
	}

	if (updatePeer) {
		if (_history->peer() == updatePeer) {
			_history->updateNotifySettings();
		}
		if (changed) {
			Notify::peerUpdatedDelayed(updatePeer, Notify::PeerUpdate::Flag::NotificationsEnabled);
		}
	}
}

void MainWidget::updateNotifySetting(PeerData *peer, NotifySettingStatus notify, SilentNotifiesStatus silent) {
	if (notify == NotifySettingDontChange && silent == SilentNotifiesDontChange) return;

	updateNotifySettingPeers.insert(peer);
	int32 muteFor = 86400 * 365;
	if (peer->notify == EmptyNotifySettings) {
		if (notify == NotifySettingSetMuted || silent == SilentNotifiesSetSilent) {
			peer->notify = new NotifySettings();
		}
	} else if (peer->notify == UnknownNotifySettings) {
		peer->notify = new NotifySettings();
	}
	if (peer->notify != EmptyNotifySettings && peer->notify != UnknownNotifySettings) {
		if (notify != NotifySettingDontChange) {
			peer->notify->sound = (notify == NotifySettingSetMuted) ? "" : "default";
			peer->notify->mute = (notify == NotifySettingSetMuted) ? (unixtime() + muteFor) : 0;
		}
		if (silent == SilentNotifiesSetSilent) {
			peer->notify->flags |= MTPDpeerNotifySettings::Flag::f_silent;
		} else if (silent == SilentNotifiesSetNotify) {
			peer->notify->flags &= ~MTPDpeerNotifySettings::Flag::f_silent;
		}
	}
	if (notify != NotifySettingDontChange) {
		if (notify == NotifySettingSetMuted) {
			App::regMuted(peer, muteFor + 1);
		} else {
			App::unregMuted(peer);
		}
		App::history(peer->id)->setMute(notify == NotifySettingSetMuted);
	}
	if (_history->peer() == peer) _history->updateNotifySettings();
	updateNotifySettingTimer.start(NotifySettingSaveTimeout);
}

void MainWidget::incrementSticker(DocumentData *sticker) {
	if (!sticker || !sticker->sticker()) return;
	if (sticker->sticker()->set.type() == mtpc_inputStickerSetEmpty) return;

	bool writeRecentStickers = false;
	auto &sets = Global::RefStickerSets();
	auto it = sets.find(Stickers::CloudRecentSetId);
	if (it == sets.cend()) {
		if (it == sets.cend()) {
			it = sets.insert(Stickers::CloudRecentSetId, Stickers::Set(Stickers::CloudRecentSetId, 0, lang(lng_recent_stickers), QString(), 0, 0, qFlags(MTPDstickerSet_ClientFlag::f_special)));
		} else {
			it->title = lang(lng_recent_stickers);
		}
	}
	auto index = it->stickers.indexOf(sticker);
	if (index > 0) {
		it->stickers.removeAt(index);
	}
	if (index) {
		it->stickers.push_front(sticker);
		writeRecentStickers = true;
	}

	// Remove that sticker from old recent, now it is in cloud recent stickers.
	bool writeOldRecent = false;
	auto &recent = cGetRecentStickers();
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
	_history->updateRecentStickers();
}

void MainWidget::activate() {
	if (_a_show.animating()) return;
	if (!_wideSection && !_overview) {
		if (_hider) {
			if (_hider->wasOffered()) {
				_hider->setFocus();
			} else {
				_dialogs->activate();
			}
        } else if (App::wnd() && !Ui::isLayerShown()) {
			if (!cSendPaths().isEmpty()) {
				forwardLayer(-1);
			} else if (_history->peer()) {
				_history->activate();
			} else {
				_dialogs->activate();
			}
		}
	}
	App::wnd()->fixOrder();
}

void MainWidget::destroyData() {
	_history->destroyData();
	_dialogs->destroyData();
}

void MainWidget::updateOnlineDisplayIn(int32 msecs) {
	_onlineUpdater.start(msecs);
}

bool MainWidget::isActive() const {
	return !_isIdle && isVisible() && !_a_show.animating();
}

bool MainWidget::doWeReadServerHistory() const {
	return isActive() && !_wideSection && !_overview && _history->doWeReadServerHistory();
}

bool MainWidget::lastWasOnline() const {
	return _lastWasOnline;
}

TimeMs MainWidget::lastSetOnline() const {
	return _lastSetOnline;
}

int32 MainWidget::dlgsWidth() const {
	return _dialogs->width();
}

MainWidget::~MainWidget() {
	if (App::main() == this) _history->showHistory(0, 0);

	if (HistoryHider *hider = _hider) {
		_hider = nullptr;
		delete hider;
	}
	Messenger::Instance().mtp()->clearGlobalHandlers();

	if (App::wnd()) App::wnd()->noMain(this);
}

void MainWidget::updateOnline(bool gotOtherOffline) {
	if (this != App::main()) return;
	App::wnd()->checkAutoLock();

	bool isOnline = App::wnd()->isActive();
	int updateIn = Global::OnlineUpdatePeriod();
	if (isOnline) {
		auto idle = psIdleTime();
		if (idle >= Global::OfflineIdleTimeout()) {
			isOnline = false;
			if (!_isIdle) {
				_isIdle = true;
				_idleFinishTimer.start(900);
			}
		} else {
			updateIn = qMin(updateIn, int(Global::OfflineIdleTimeout() - idle));
		}
	}
	auto ms = getms(true);
	if (isOnline != _lastWasOnline || (isOnline && _lastSetOnline + Global::OnlineUpdatePeriod() <= ms) || (isOnline && gotOtherOffline)) {
		if (_onlineRequest) {
			MTP::cancel(_onlineRequest);
			_onlineRequest = 0;
		}

		_lastWasOnline = isOnline;
		_lastSetOnline = ms;
		_onlineRequest = MTP::send(MTPaccount_UpdateStatus(MTP_bool(!isOnline)));

		if (App::self()) {
			App::self()->onlineTill = unixtime() + (isOnline ? (Global::OnlineUpdatePeriod() / 1000) : -1);
			Notify::peerUpdatedDelayed(App::self(), Notify::PeerUpdate::Flag::UserOnlineChanged);
		}
		if (!isOnline) { // Went offline, so we need to save message draft to the cloud.
			saveDraftToCloud();
		}

		_lastSetOnline = ms;

		updateOnlineDisplay();
	} else if (isOnline) {
		updateIn = qMin(updateIn, int(_lastSetOnline + Global::OnlineUpdatePeriod() - ms));
	}
	_onlineTimer.start(updateIn);
}

void MainWidget::saveDraftToCloud() {
	_history->saveFieldToHistoryLocalDraft();

	auto peer = _history->peer();
	if (auto history = App::historyLoaded(peer)) {
		writeDrafts(history);

		auto localDraft = history->localDraft();
		auto cloudDraft = history->cloudDraft();
		if (!Data::draftsAreEqual(localDraft, cloudDraft)) {
			App::api()->saveDraftToCloudDelayed(history);
		}
	}
}

void MainWidget::applyCloudDraft(History *history) {
	_history->applyCloudDraft(history);
}

void MainWidget::writeDrafts(History *history) {
	Local::MessageDraft storedLocalDraft, storedEditDraft;
	MessageCursor localCursor, editCursor;
	if (auto localDraft = history->localDraft()) {
		if (!Data::draftsAreEqual(localDraft, history->cloudDraft())) {
			storedLocalDraft = Local::MessageDraft(localDraft->msgId, localDraft->textWithTags, localDraft->previewCancelled);
			localCursor = localDraft->cursor;
		}
	}
	if (auto editDraft = history->editDraft()) {
		storedEditDraft = Local::MessageDraft(editDraft->msgId, editDraft->textWithTags, editDraft->previewCancelled);
		editCursor = editDraft->cursor;
	}
	Local::writeDrafts(history->peer->id, storedLocalDraft, storedEditDraft);
	Local::writeDraftCursors(history->peer->id, localCursor, editCursor);
}

void MainWidget::checkIdleFinish() {
	if (this != App::main()) return;
	if (psIdleTime() < Global::OfflineIdleTimeout()) {
		_idleFinishTimer.stop();
		_isIdle = false;
		updateOnline();
		if (App::wnd()) App::wnd()->checkHistoryActivation();
	} else {
		_idleFinishTimer.start(900);
	}
}

void MainWidget::updateReceived(const mtpPrime *from, const mtpPrime *end) {
	if (end <= from) return;

	App::wnd()->checkAutoLock();

	if (mtpTypeId(*from) == mtpc_new_session_created) {
		MTPNewSession newSession(from, end);
		updSeq = 0;
		MTP_LOG(0, ("getDifference { after new_session_created }%1").arg(cTestMode() ? " TESTMODE" : ""));
		return getDifference();
	} else {
		try {
			MTPUpdates updates(from, end);

			_lastUpdateTime = getms(true);
			noUpdatesTimer.start(NoUpdatesTimeout);
			if (!requestingDifference()) {
				feedUpdates(updates);
			}
		} catch (mtpErrorUnexpected &) { // just some other type
		}
	}
	update();
}

namespace {

bool fwdInfoDataLoaded(const MTPMessageFwdHeader &header) {
	if (header.type() != mtpc_messageFwdHeader) {
		return true;
	}
	auto &info = header.c_messageFwdHeader();
	if (info.has_channel_id()) {
		if (!App::channelLoaded(peerFromChannel(info.vchannel_id))) {
			return false;
		}
		if (info.has_from_id() && !App::user(peerFromUser(info.vfrom_id), PeerData::MinimalLoaded)) {
			return false;
		}
	} else {
		if (info.has_from_id() && !App::userLoaded(peerFromUser(info.vfrom_id))) {
			return false;
		}
	}
	return true;
}

bool mentionUsersLoaded(const MTPVector<MTPMessageEntity> &entities) {
	for_const (auto &entity, entities.c_vector().v) {
		auto type = entity.type();
		if (type == mtpc_messageEntityMentionName) {
			if (!App::userLoaded(peerFromUser(entity.c_messageEntityMentionName().vuser_id))) {
				return false;
			}
		} else if (type == mtpc_inputMessageEntityMentionName) {
			auto &inputUser = entity.c_inputMessageEntityMentionName().vuser_id;
			if (inputUser.type() == mtpc_inputUser) {
				if (!App::userLoaded(peerFromUser(inputUser.c_inputUser().vuser_id))) {
					return false;
				}
			}
		}
	}
	return true;
}

enum class DataIsLoadedResult {
	NotLoaded = 0,
	FromNotLoaded = 1,
	MentionNotLoaded = 2,
	Ok = 3,
};
DataIsLoadedResult allDataLoadedForMessage(const MTPMessage &msg) {
	switch (msg.type()) {
	case mtpc_message: {
		const MTPDmessage &d(msg.c_message());
		if (!d.is_post() && d.has_from_id()) {
			if (!App::userLoaded(peerFromUser(d.vfrom_id))) {
				return DataIsLoadedResult::FromNotLoaded;
			}
		}
		if (d.has_via_bot_id()) {
			if (!App::userLoaded(peerFromUser(d.vvia_bot_id))) {
				return DataIsLoadedResult::NotLoaded;
			}
		}
		if (d.has_fwd_from() && !fwdInfoDataLoaded(d.vfwd_from)) {
			return DataIsLoadedResult::NotLoaded;
		}
		if (d.has_entities() && !mentionUsersLoaded(d.ventities)) {
			return DataIsLoadedResult::MentionNotLoaded;
		}
	} break;
	case mtpc_messageService: {
		const MTPDmessageService &d(msg.c_messageService());
		if (!d.is_post() && d.has_from_id()) {
			if (!App::userLoaded(peerFromUser(d.vfrom_id))) {
				return DataIsLoadedResult::FromNotLoaded;
			}
		}
		switch (d.vaction.type()) {
		case mtpc_messageActionChatAddUser: {
			for_const (const MTPint &userId, d.vaction.c_messageActionChatAddUser().vusers.c_vector().v) {
				if (!App::userLoaded(peerFromUser(userId))) {
					return DataIsLoadedResult::NotLoaded;
				}
			}
		} break;
		case mtpc_messageActionChatJoinedByLink: {
			if (!App::userLoaded(peerFromUser(d.vaction.c_messageActionChatJoinedByLink().vinviter_id))) {
				return DataIsLoadedResult::NotLoaded;
			}
		} break;
		case mtpc_messageActionChatDeleteUser: {
			if (!App::userLoaded(peerFromUser(d.vaction.c_messageActionChatDeleteUser().vuser_id))) {
				return DataIsLoadedResult::NotLoaded;
			}
		} break;
		}
	} break;
	}
	return DataIsLoadedResult::Ok;
}

} // namespace

void MainWidget::feedUpdates(const MTPUpdates &updates, uint64 randomId) {
	switch (updates.type()) {
	case mtpc_updates: {
		auto &d = updates.c_updates();
		if (d.vseq.v) {
			if (d.vseq.v <= updSeq) return;
			if (d.vseq.v > updSeq + 1) {
				_bySeqUpdates.insert(d.vseq.v, updates);
				return _bySeqTimer.start(WaitForSkippedTimeout);
			}
		}

		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		feedUpdateVector(d.vupdates);

		updSetState(0, d.vdate.v, updQts, d.vseq.v);
	} break;

	case mtpc_updatesCombined: {
		auto &d = updates.c_updatesCombined();
		if (d.vseq_start.v) {
			if (d.vseq_start.v <= updSeq) return;
			if (d.vseq_start.v > updSeq + 1) {
				_bySeqUpdates.insert(d.vseq_start.v, updates);
				return _bySeqTimer.start(WaitForSkippedTimeout);
			}
		}

		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		feedUpdateVector(d.vupdates);

		updSetState(0, d.vdate.v, updQts, d.vseq.v);
	} break;

	case mtpc_updateShort: {
		auto &d = updates.c_updateShort();
		feedUpdate(d.vupdate);

		updSetState(0, d.vdate.v, updQts, updSeq);
	} break;

	case mtpc_updateShortMessage: {
		auto &d = updates.c_updateShortMessage();
		if (!App::userLoaded(d.vuser_id.v)
			|| (d.has_via_bot_id() && !App::userLoaded(d.vvia_bot_id.v))
			|| (d.has_entities() && !mentionUsersLoaded(d.ventities))
			|| (d.has_fwd_from() && !fwdInfoDataLoaded(d.vfwd_from))) {
			MTP_LOG(0, ("getDifference { good - getting user for updateShortMessage }%1").arg(cTestMode() ? " TESTMODE" : ""));
			return getDifference();
		}
		if (!ptsUpdated(d.vpts.v, d.vpts_count.v, updates)) {
			return;
		}

		// update before applying skipped
		MTPDmessage::Flags flags = mtpCastFlags(d.vflags.v) | MTPDmessage::Flag::f_from_id;
		auto item = App::histories().addNewMessage(MTP_message(MTP_flags(flags), d.vid, d.is_out() ? MTP_int(AuthSession::CurrentUserId()) : d.vuser_id, MTP_peerUser(d.is_out() ? d.vuser_id : MTP_int(AuthSession::CurrentUserId())), d.vfwd_from, d.vvia_bot_id, d.vreply_to_msg_id, d.vdate, d.vmessage, MTP_messageMediaEmpty(), MTPnullMarkup, d.has_entities() ? d.ventities : MTPnullEntities, MTPint(), MTPint()), NewMessageUnread);
		if (item) {
			_history->peerMessagesUpdated(item->history()->peer->id);
		}

		ptsApplySkippedUpdates();

		updSetState(0, d.vdate.v, updQts, updSeq);
	} break;

	case mtpc_updateShortChatMessage: {
		auto &d = updates.c_updateShortChatMessage();
		bool noFrom = !App::userLoaded(d.vfrom_id.v);
		if (!App::chatLoaded(d.vchat_id.v)
			|| noFrom
			|| (d.has_via_bot_id() && !App::userLoaded(d.vvia_bot_id.v))
			|| (d.has_entities() && !mentionUsersLoaded(d.ventities))
			|| (d.has_fwd_from() && !fwdInfoDataLoaded(d.vfwd_from))) {
			MTP_LOG(0, ("getDifference { good - getting user for updateShortChatMessage }%1").arg(cTestMode() ? " TESTMODE" : ""));
			if (noFrom && App::api()) App::api()->requestFullPeer(App::chatLoaded(d.vchat_id.v));
			return getDifference();
		}
		if (!ptsUpdated(d.vpts.v, d.vpts_count.v, updates)) {
			return;
		}

		// update before applying skipped
		MTPDmessage::Flags flags = mtpCastFlags(d.vflags.v) | MTPDmessage::Flag::f_from_id;
		auto item = App::histories().addNewMessage(MTP_message(MTP_flags(flags), d.vid, d.vfrom_id, MTP_peerChat(d.vchat_id), d.vfwd_from, d.vvia_bot_id, d.vreply_to_msg_id, d.vdate, d.vmessage, MTP_messageMediaEmpty(), MTPnullMarkup, d.has_entities() ? d.ventities : MTPnullEntities, MTPint(), MTPint()), NewMessageUnread);
		if (item) {
			_history->peerMessagesUpdated(item->history()->peer->id);
		}

		ptsApplySkippedUpdates();

		updSetState(0, d.vdate.v, updQts, updSeq);
	} break;

	case mtpc_updateShortSentMessage: {
		auto &d = updates.c_updateShortSentMessage();
		if (randomId) {
			PeerId peerId = 0;
			QString text;
			App::histSentDataByItem(randomId, peerId, text);

			feedUpdate(MTP_updateMessageID(d.vid, MTP_long(randomId))); // ignore real date
			if (peerId) {
				if (auto item = App::histItemById(peerToChannel(peerId), d.vid.v)) {
					if (d.has_entities() && !mentionUsersLoaded(d.ventities)) {
						api()->requestMessageData(item->history()->peer->asChannel(), item->id, ApiWrap::RequestMessageDataCallback());
					}
					auto entities = d.has_entities() ? entitiesFromMTP(d.ventities.c_vector().v) : EntitiesInText();
					item->setText({ text, entities });
					item->updateMedia(d.has_media() ? (&d.vmedia) : nullptr);
					item->addToOverview(AddToOverviewNew);
				}
			}
		}

		if (!ptsUpdated(d.vpts.v, d.vpts_count.v, updates)) {
			return;
		}
		// update before applying skipped
		ptsApplySkippedUpdates();

		updSetState(0, d.vdate.v, updQts, updSeq);
	} break;

	case mtpc_updatesTooLong: {
		MTP_LOG(0, ("getDifference { good - updatesTooLong received }%1").arg(cTestMode() ? " TESTMODE" : ""));
		return getDifference();
	} break;
	}
}

void MainWidget::feedUpdate(const MTPUpdate &update) {
	switch (update.type()) {
	case mtpc_updateNewMessage: {
		auto &d = update.c_updateNewMessage();

		DataIsLoadedResult isDataLoaded = allDataLoadedForMessage(d.vmessage);
		if (!requestingDifference() && isDataLoaded != DataIsLoadedResult::Ok) {
			MTP_LOG(0, ("getDifference { good - after not all data loaded in updateNewMessage }%1").arg(cTestMode() ? " TESTMODE" : ""));

			// This can be if this update was created by grouping
			// some short message update into an updates vector.
			return getDifference();
		}

		if (!ptsUpdated(d.vpts.v, d.vpts_count.v, update)) {
			return;
		}

		// update before applying skipped
		bool needToAdd = true;
		if (d.vmessage.type() == mtpc_message) { // index forwarded messages to links _overview
			if (App::checkEntitiesAndViewsUpdate(d.vmessage.c_message())) { // already in blocks
				LOG(("Skipping message, because it is already in blocks!"));
				needToAdd = false;
			}
		}
		if (needToAdd) {
			if (auto item = App::histories().addNewMessage(d.vmessage, NewMessageUnread)) {
				_history->peerMessagesUpdated(item->history()->peer->id);
			}
		}
		ptsApplySkippedUpdates();
	} break;

	case mtpc_updateMessageID: {
		auto &d = update.c_updateMessageID();
		auto msg = App::histItemByRandom(d.vrandom_id.v);
		if (msg.msg) {
			if (auto msgRow = App::histItemById(msg)) {
				if (App::histItemById(msg.channel, d.vid.v)) {
					auto history = msgRow->history();
					auto wasLast = (history->lastMsg == msgRow);
					msgRow->destroy();
					if (wasLast && !history->lastMsg) {
						checkPeerHistory(history->peer);
					}
					_history->peerMessagesUpdated();
				} else {
					App::historyUnregItem(msgRow);
					if (App::wnd()) App::wnd()->changingMsgId(msgRow, d.vid.v);
					msgRow->setId(d.vid.v);
					if (msgRow->history()->peer->isSelf()) {
						msgRow->history()->unregSendAction(App::self());
					}
					App::historyRegItem(msgRow);
					Ui::repaintHistoryItem(msgRow);
				}
			}
			App::historyUnregRandom(d.vrandom_id.v);
		}
		App::historyUnregSentData(d.vrandom_id.v);
	} break;

	case mtpc_updateReadMessagesContents: {
		auto &d = update.c_updateReadMessagesContents();

		if (!ptsUpdated(d.vpts.v, d.vpts_count.v, update)) {
			return;
		}

		// update before applying skipped
		auto &v = d.vmessages.c_vector().v;
		for (int32 i = 0, l = v.size(); i < l; ++i) {
			if (HistoryItem *item = App::histItemById(NoChannel, v.at(i).v)) {
				if (item->isMediaUnread()) {
					item->markMediaRead();
					Ui::repaintHistoryItem(item);

					if (item->out() && item->history()->peer->isUser()) {
						auto when = requestingDifference() ? 0 : unixtime();
						item->history()->peer->asUser()->madeAction(when);
					}
				}
			}
		}

		ptsApplySkippedUpdates();
	} break;

	case mtpc_updateReadHistoryInbox: {
		auto &d = update.c_updateReadHistoryInbox();

		if (!ptsUpdated(d.vpts.v, d.vpts_count.v, update)) {
			return;
		}

		// update before applying skipped
		App::feedInboxRead(peerFromMTP(d.vpeer), d.vmax_id.v);

		ptsApplySkippedUpdates();
	} break;

	case mtpc_updateReadHistoryOutbox: {
		auto &d = update.c_updateReadHistoryOutbox();

		if (!ptsUpdated(d.vpts.v, d.vpts_count.v, update)) {
			return;
		}

		// update before applying skipped
		auto peerId = peerFromMTP(d.vpeer);
		auto when = requestingDifference() ? 0 : unixtime();
		App::feedOutboxRead(peerId, d.vmax_id.v, when);
		if (_history->peer() && _history->peer()->id == peerId) {
			_history->update();
		}

		ptsApplySkippedUpdates();
	} break;

	case mtpc_updateWebPage: {
		auto &d = update.c_updateWebPage();

		// update web page anyway
		App::feedWebPage(d.vwebpage);
		_history->updatePreview();
		webPagesOrGamesUpdate();

		if (!ptsUpdated(d.vpts.v, d.vpts_count.v, update)) {
			return;
		}
		ptsApplySkippedUpdates();
	} break;

	case mtpc_updateDeleteMessages: {
		auto &d = update.c_updateDeleteMessages();

		if (!ptsUpdated(d.vpts.v, d.vpts_count.v, update)) {
			return;
		}

		// update before applying skipped
		App::feedWereDeleted(NoChannel, d.vmessages.c_vector().v);
		_history->peerMessagesUpdated();

		ptsApplySkippedUpdates();
	} break;

	case mtpc_updateUserTyping: {
		auto &d = update.c_updateUserTyping();
		auto history = App::historyLoaded(peerFromUser(d.vuser_id));
		auto user = App::userLoaded(d.vuser_id.v);
		if (history && user) {
			auto when = requestingDifference() ? 0 : unixtime();
			App::histories().regSendAction(history, user, d.vaction, when);
		}
	} break;

	case mtpc_updateChatUserTyping: {
		auto &d = update.c_updateChatUserTyping();
		History *history = 0;
		if (auto chat = App::chatLoaded(d.vchat_id.v)) {
			history = App::historyLoaded(chat->id);
		} else if (auto channel = App::channelLoaded(d.vchat_id.v)) {
			history = App::historyLoaded(channel->id);
		}
		auto user = (d.vuser_id.v == AuthSession::CurrentUserId()) ? nullptr : App::userLoaded(d.vuser_id.v);
		if (history && user) {
			auto when = requestingDifference() ? 0 : unixtime();
			App::histories().regSendAction(history, user, d.vaction, when);
		}
	} break;

	case mtpc_updateChatParticipants: {
		App::feedParticipants(update.c_updateChatParticipants().vparticipants, true, false);
	} break;

	case mtpc_updateChatParticipantAdd: {
		App::feedParticipantAdd(update.c_updateChatParticipantAdd(), false);
	} break;

	case mtpc_updateChatParticipantDelete: {
		App::feedParticipantDelete(update.c_updateChatParticipantDelete(), false);
	} break;

	case mtpc_updateChatAdmins: {
		App::feedChatAdmins(update.c_updateChatAdmins(), false);
	} break;

	case mtpc_updateChatParticipantAdmin: {
		App::feedParticipantAdmin(update.c_updateChatParticipantAdmin(), false);
	} break;

	case mtpc_updateUserStatus: {
		auto &d = update.c_updateUserStatus();
		if (auto user = App::userLoaded(d.vuser_id.v)) {
			switch (d.vstatus.type()) {
			case mtpc_userStatusEmpty: user->onlineTill = 0; break;
			case mtpc_userStatusRecently:
				if (user->onlineTill > -10) { // don't modify pseudo-online
					user->onlineTill = -2;
				}
			break;
			case mtpc_userStatusLastWeek: user->onlineTill = -3; break;
			case mtpc_userStatusLastMonth: user->onlineTill = -4; break;
			case mtpc_userStatusOffline: user->onlineTill = d.vstatus.c_userStatusOffline().vwas_online.v; break;
			case mtpc_userStatusOnline: user->onlineTill = d.vstatus.c_userStatusOnline().vexpires.v; break;
			}
			App::markPeerUpdated(user);
			Notify::peerUpdatedDelayed(user, Notify::PeerUpdate::Flag::UserOnlineChanged);
		}
		if (d.vuser_id.v == AuthSession::CurrentUserId()) {
			if (d.vstatus.type() == mtpc_userStatusOffline || d.vstatus.type() == mtpc_userStatusEmpty) {
				updateOnline(true);
				if (d.vstatus.type() == mtpc_userStatusOffline) {
					cSetOtherOnline(d.vstatus.c_userStatusOffline().vwas_online.v);
				}
			} else if (d.vstatus.type() == mtpc_userStatusOnline) {
				cSetOtherOnline(d.vstatus.c_userStatusOnline().vexpires.v);
			}
		}
	} break;

	case mtpc_updateUserName: {
		auto &d = update.c_updateUserName();
		if (auto user = App::userLoaded(d.vuser_id.v)) {
			if (user->contact <= 0) {
				user->setName(textOneLine(qs(d.vfirst_name)), textOneLine(qs(d.vlast_name)), user->nameOrPhone, textOneLine(qs(d.vusername)));
			} else {
				user->setName(textOneLine(user->firstName), textOneLine(user->lastName), user->nameOrPhone, textOneLine(qs(d.vusername)));
			}
			App::markPeerUpdated(user);
		}
	} break;

	case mtpc_updateUserPhoto: {
		auto &d = update.c_updateUserPhoto();
		if (auto user = App::userLoaded(d.vuser_id.v)) {
			user->setPhoto(d.vphoto);
			user->loadUserpic();
			if (mtpIsTrue(d.vprevious)) {
				user->photosCount = -1;
				user->photos.clear();
			} else {
				if (user->photoId && user->photoId != UnknownPeerPhotoId) {
					if (user->photosCount > 0) ++user->photosCount;
					user->photos.push_front(App::photo(user->photoId));
				} else {
					user->photosCount = -1;
					user->photos.clear();
				}
			}
			App::markPeerUpdated(user);
			Notify::mediaOverviewUpdated(user, OverviewCount);
		}
	} break;

	case mtpc_updateContactRegistered: {
		auto &d = update.c_updateContactRegistered();
		if (auto user = App::userLoaded(d.vuser_id.v)) {
			if (App::history(user->id)->loadedAtBottom()) {
				App::history(user->id)->addNewService(clientMsgId(), date(d.vdate), lng_action_user_registered(lt_from, user->name), 0);
			}
		}
	} break;

	case mtpc_updateContactLink: {
		auto &d = update.c_updateContactLink();
		App::feedUserLink(d.vuser_id, d.vmy_link, d.vforeign_link);
	} break;

	case mtpc_updateNotifySettings: {
		auto &d = update.c_updateNotifySettings();
		applyNotifySetting(d.vpeer, d.vnotify_settings);
	} break;

	case mtpc_updateDcOptions: {
		auto &d = update.c_updateDcOptions();
		Messenger::Instance().dcOptions()->addFromList(d.vdc_options);
	} break;

	case mtpc_updateUserPhone: {
		auto &d = update.c_updateUserPhone();
		if (auto user = App::userLoaded(d.vuser_id.v)) {
			auto newPhone = qs(d.vphone);
			if (newPhone != user->phone()) {
				user->setPhone(newPhone);
				user->setName(user->firstName, user->lastName, (user->contact || isServiceUser(user->id) || user->isSelf() || user->phone().isEmpty()) ? QString() : App::formatPhone(user->phone()), user->username);
				App::markPeerUpdated(user);

				Notify::peerUpdatedDelayed(user, Notify::PeerUpdate::Flag::UserPhoneChanged);
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
		auto &d = update.c_updatePhoneCall();
	} break;

	case mtpc_updateUserBlocked: {
		auto &d = update.c_updateUserBlocked();
		if (auto user = App::userLoaded(d.vuser_id.v)) {
			user->setBlockStatus(mtpIsTrue(d.vblocked) ? UserData::BlockStatus::Blocked : UserData::BlockStatus::NotBlocked);
			App::markPeerUpdated(user);
		}
	} break;

	case mtpc_updateServiceNotification: {
		auto &d = update.c_updateServiceNotification();
		if (d.is_popup()) {
			Ui::show(Box<InformBox>(qs(d.vmessage)));
		} else {
			App::wnd()->serviceNotification({ qs(d.vmessage), entitiesFromMTP(d.ventities.c_vector().v) }, d.vmedia);
			emit App::wnd()->checkNewAuthorization();
		}
	} break;

	case mtpc_updatePrivacy: {
		auto &d = update.c_updatePrivacy();
	} break;

	case mtpc_updatePinnedDialogs: {
		auto &d = update.c_updatePinnedDialogs();
		if (d.has_order()) {
			auto allLoaded = true;
			auto &order = d.vorder.c_vector().v;
			for_const (auto &peer, order) {
				auto peerId = peerFromMTP(peer);
				if (!App::historyLoaded(peerId)) {
					allLoaded = false;
					DEBUG_LOG(("API Error: pinned chat not loaded for peer %1").arg(peerId));
					break;
				}
			}
			if (allLoaded) {
				App::histories().clearPinned();
				for (auto i = order.size(); i != 0;) {
					auto history = App::historyLoaded(peerFromMTP(order[--i]));
					t_assert(history != nullptr);
					history->setPinnedDialog(true);
				}
			} else {
				_dialogs->loadPinnedDialogs();
			}
		} else {
			_dialogs->loadPinnedDialogs();
		}
	} break;

	case mtpc_updateDialogPinned: {
		auto &d = update.c_updateDialogPinned();
		auto peerId = peerFromMTP(d.vpeer);
		if (auto history = App::historyLoaded(peerId)) {
			history->setPinnedDialog(d.is_pinned());
		} else {
			DEBUG_LOG(("API Error: pinned chat not loaded for peer %1").arg(peerId));
			_dialogs->loadPinnedDialogs();
		}
	} break;

	/////// Channel updates
	case mtpc_updateChannel: {
		auto &d = update.c_updateChannel();
		if (auto channel = App::channelLoaded(d.vchannel_id.v)) {
			App::markPeerUpdated(channel);
			channel->inviter = 0;
			if (!channel->amIn()) {
				deleteConversation(channel, false);
			} else if (!channel->amCreator() && App::history(channel->id)) { // create history
				_updatedChannels.insert(channel, true);
				if (App::api()) App::api()->requestSelfParticipant(channel);
			}
		}
	} break;

	case mtpc_updateNewChannelMessage: {
		auto &d = update.c_updateNewChannelMessage();
		auto channel = App::channelLoaded(peerToChannel(peerFromMessage(d.vmessage)));
		auto isDataLoaded = allDataLoadedForMessage(d.vmessage);
		if (!requestingDifference() && (!channel || isDataLoaded != DataIsLoadedResult::Ok)) {
			MTP_LOG(0, ("getDifference { good - after not all data loaded in updateNewChannelMessage }%1").arg(cTestMode() ? " TESTMODE" : ""));

			// Request last active supergroup participants if the 'from' user was not loaded yet.
			// This will optimize similar getDifference() calls for almost all next messages.
			if (isDataLoaded == DataIsLoadedResult::FromNotLoaded && channel && channel->isMegagroup() && App::api()) {
				if (channel->mgInfo->lastParticipants.size() < Global::ChatSizeMax() && (channel->mgInfo->lastParticipants.isEmpty() || channel->mgInfo->lastParticipants.size() < channel->membersCount())) {
					App::api()->requestLastParticipants(channel);
				}
			}

			if (!_byMinChannelTimer.isActive()) { // getDifference after timeout
				_byMinChannelTimer.start(WaitForSkippedTimeout);
			}
			return;
		}
		if (channel && !_handlingChannelDifference) {
			if (channel->ptsRequesting()) { // skip global updates while getting channel difference
				return;
			} else if (!channel->ptsUpdated(d.vpts.v, d.vpts_count.v, update)) {
				return;
			}
		}

		// update before applying skipped
		bool needToAdd = true;
		if (d.vmessage.type() == mtpc_message) { // index forwarded messages to links _overview
			if (App::checkEntitiesAndViewsUpdate(d.vmessage.c_message())) { // already in blocks
				LOG(("Skipping message, because it is already in blocks!"));
				needToAdd = false;
			}
		}
		if (needToAdd) {
			if (auto item = App::histories().addNewMessage(d.vmessage, NewMessageUnread)) {
				_history->peerMessagesUpdated(item->history()->peer->id);
			}
		}
		if (channel && !_handlingChannelDifference) {
			channel->ptsApplySkippedUpdates();
		}
	} break;

	case mtpc_updateEditChannelMessage: {
		auto &d = update.c_updateEditChannelMessage();
		auto channel = App::channelLoaded(peerToChannel(peerFromMessage(d.vmessage)));

		if (channel && !_handlingChannelDifference) {
			if (channel->ptsRequesting()) { // skip global updates while getting channel difference
				return;
			} else if (!channel->ptsUpdated(d.vpts.v, d.vpts_count.v, update)) {
				return;
			}
		}

		// update before applying skipped
		App::updateEditedMessage(d.vmessage);

		if (channel && !_handlingChannelDifference) {
			channel->ptsApplySkippedUpdates();
		}
	} break;

	case mtpc_updateEditMessage: {
		auto &d = update.c_updateEditMessage();

		if (!ptsUpdated(d.vpts.v, d.vpts_count.v, update)) {
			return;
		}

		// update before applying skipped
		App::updateEditedMessage(d.vmessage);

		ptsApplySkippedUpdates();
	} break;

	case mtpc_updateChannelPinnedMessage: {
		auto &d = update.c_updateChannelPinnedMessage();

		if (auto channel = App::channelLoaded(d.vchannel_id.v)) {
			if (channel->isMegagroup()) {
				channel->mgInfo->pinnedMsgId = d.vid.v;
				if (App::api()) {
					emit App::api()->fullPeerUpdated(channel);
				}
			}
		}
	} break;

	case mtpc_updateReadChannelInbox: {
		auto &d = update.c_updateReadChannelInbox();
		App::feedInboxRead(peerFromChannel(d.vchannel_id.v), d.vmax_id.v);
	} break;

	case mtpc_updateReadChannelOutbox: {
		auto &d = update.c_updateReadChannelOutbox();
		auto peerId = peerFromChannel(d.vchannel_id.v);
		auto when = requestingDifference() ? 0 : unixtime();
		App::feedOutboxRead(peerId, d.vmax_id.v, when);
		if (_history->peer() && _history->peer()->id == peerId) {
			_history->update();
		}
	} break;

	case mtpc_updateChannelWebPage: {
		auto &d = update.c_updateChannelWebPage();

		// update web page anyway
		App::feedWebPage(d.vwebpage);
		_history->updatePreview();
		webPagesOrGamesUpdate();

		auto channel = App::channelLoaded(d.vchannel_id.v);
		if (channel && !_handlingChannelDifference) {
			if (channel->ptsRequesting()) { // skip global updates while getting channel difference
				return;
			} else if (!channel->ptsUpdated(d.vpts.v, d.vpts_count.v, update)) {
				return;
			}
		}

		if (channel && !_handlingChannelDifference) {
			channel->ptsApplySkippedUpdates();
		}
	} break;

	case mtpc_updateDeleteChannelMessages: {
		auto &d = update.c_updateDeleteChannelMessages();
		auto channel = App::channelLoaded(d.vchannel_id.v);

		if (channel && !_handlingChannelDifference) {
			if (channel->ptsRequesting()) { // skip global updates while getting channel difference
				return;
			} else if (!channel->ptsUpdated(d.vpts.v, d.vpts_count.v, update)) {
				return;
			}
		}

		// update before applying skipped
		App::feedWereDeleted(d.vchannel_id.v, d.vmessages.c_vector().v);
		_history->peerMessagesUpdated();

		if (channel && !_handlingChannelDifference) {
			channel->ptsApplySkippedUpdates();
		}
	} break;

	case mtpc_updateChannelTooLong: {
		auto &d = update.c_updateChannelTooLong();
		if (auto channel = App::channelLoaded(d.vchannel_id.v)) {
			if (!d.has_pts() || channel->pts() < d.vpts.v) {
				getChannelDifference(channel);
			}
		}
	} break;

	case mtpc_updateChannelMessageViews: {
		auto &d = update.c_updateChannelMessageViews();
		if (auto item = App::histItemById(d.vchannel_id.v, d.vid.v)) {
			item->setViewsCount(d.vviews.v);
		}
	} break;

	////// Cloud sticker sets
	case mtpc_updateNewStickerSet: {
		auto &d = update.c_updateNewStickerSet();
		bool writeArchived = false;
		if (d.vstickerset.type() == mtpc_messages_stickerSet) {
			auto &set = d.vstickerset.c_messages_stickerSet();
			if (set.vset.type() == mtpc_stickerSet) {
				auto &s = set.vset.c_stickerSet();
				if (!s.is_masks()) {
					auto &sets = Global::RefStickerSets();
					auto it = sets.find(s.vid.v);
					if (it == sets.cend()) {
						it = sets.insert(s.vid.v, Stickers::Set(s.vid.v, s.vaccess_hash.v, stickerSetTitle(s), qs(s.vshort_name), s.vcount.v, s.vhash.v, s.vflags.v | MTPDstickerSet::Flag::f_installed));
					} else {
						it->flags |= MTPDstickerSet::Flag::f_installed;
						if (it->flags & MTPDstickerSet::Flag::f_archived) {
							it->flags &= ~MTPDstickerSet::Flag::f_archived;
							writeArchived = true;
						}
					}
					auto inputSet = MTP_inputStickerSetID(MTP_long(it->id), MTP_long(it->access));
					auto &v = set.vdocuments.c_vector().v;
					it->stickers.clear();
					it->stickers.reserve(v.size());
					for (int i = 0, l = v.size(); i < l; ++i) {
						auto doc = App::feedDocument(v.at(i));
						if (!doc || !doc->sticker()) continue;

						it->stickers.push_back(doc);
						if (doc->sticker()->set.type() != mtpc_inputStickerSetID) {
							doc->sticker()->set = inputSet;
						}
					}
					it->emoji.clear();
					auto &packs = set.vpacks.c_vector().v;
					for (auto i = 0, l = packs.size(); i != l; ++i) {
						if (packs[i].type() != mtpc_stickerPack) continue;
						auto &pack = packs.at(i).c_stickerPack();
						if (auto emoji = Ui::Emoji::Find(qs(pack.vemoticon))) {
							emoji = emoji->original();
							auto &stickers = pack.vdocuments.c_vector().v;

							StickerPack p;
							p.reserve(stickers.size());
							for (auto j = 0, c = stickers.size(); j != c; ++j) {
								auto doc = App::document(stickers[j].v);
								if (!doc || !doc->sticker()) continue;

								p.push_back(doc);
							}
							it->emoji.insert(emoji, p);
						}
					}

					auto &order(Global::RefStickerSetsOrder());
					int32 insertAtIndex = 0, currentIndex = order.indexOf(s.vid.v);
					if (currentIndex != insertAtIndex) {
						if (currentIndex > 0) {
							order.removeAt(currentIndex);
						}
						order.insert(insertAtIndex, s.vid.v);
					}

					auto custom = sets.find(Stickers::CustomSetId);
					if (custom != sets.cend()) {
						for (int32 i = 0, l = it->stickers.size(); i < l; ++i) {
							int32 removeIndex = custom->stickers.indexOf(it->stickers.at(i));
							if (removeIndex >= 0) custom->stickers.removeAt(removeIndex);
						}
						if (custom->stickers.isEmpty()) {
							sets.erase(custom);
						}
					}
					Local::writeInstalledStickers();
					if (writeArchived) Local::writeArchivedStickers();
					emit stickersUpdated();
				}
			}
		}
	} break;

	case mtpc_updateStickerSetsOrder: {
		auto &d = update.c_updateStickerSetsOrder();
		if (!d.is_masks()) {
			auto &order = d.vorder.c_vector().v;
			auto &sets = Global::StickerSets();
			Stickers::Order result;
			for (int i = 0, l = order.size(); i < l; ++i) {
				if (sets.constFind(order.at(i).v) == sets.cend()) {
					break;
				}
				result.push_back(order.at(i).v);
			}
			if (result.size() != Global::StickerSetsOrder().size() || result.size() != order.size()) {
				Global::SetLastStickersUpdate(0);
				App::main()->updateStickers();
			} else {
				Global::SetStickerSetsOrder(result);
				Local::writeInstalledStickers();
				emit stickersUpdated();
			}
		}
	} break;

	case mtpc_updateStickerSets: {
		Global::SetLastStickersUpdate(0);
		App::main()->updateStickers();
	} break;

	case mtpc_updateRecentStickers: {
		Global::SetLastStickersUpdate(0);
		App::main()->updateStickers();
	} break;

	case mtpc_updateReadFeaturedStickers: {
		// We read some of the featured stickers, perhaps not all of them.
		// Here we don't know what featured sticker sets were read, so we
		// request all of them once again.
		Global::SetLastFeaturedStickersUpdate(0);
		App::main()->updateStickers();
	} break;

	////// Cloud saved GIFs
	case mtpc_updateSavedGifs: {
		cSetLastSavedGifsUpdate(0);
		App::main()->updateStickers();
	} break;

	////// Cloud drafts
	case mtpc_updateDraftMessage: {
		auto &peerDraft = update.c_updateDraftMessage();
		auto peerId = peerFromMTP(peerDraft.vpeer);

		auto &draftMessage = peerDraft.vdraft;
		if (draftMessage.type() == mtpc_draftMessage) {
			auto &draft = draftMessage.c_draftMessage();
			Data::applyPeerCloudDraft(peerId, draft);
		} else {
			Data::clearPeerCloudDraft(peerId);
		}
	} break;

	}
}
