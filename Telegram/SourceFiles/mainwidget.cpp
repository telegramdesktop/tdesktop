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
#include "api/api_updates.h"
#include "data/data_photo.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_web_page.h"
#include "data/data_game.h"
#include "data/data_peer_values.h"
#include "data/data_drafts.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "data/data_media_types.h"
#include "data/data_folder.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_chat_filters.h"
#include "data/data_scheduled_messages.h"
#include "data/data_file_origin.h"
#include "data/data_histories.h"
#include "data/stickers/data_stickers.h"
#include "api/api_text_entities.h"
#include "ui/special_buttons.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "ui/toast/toast.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/image/image.h"
#include "ui/focus_persister.h"
#include "ui/resize_area.h"
#include "ui/text/text_options.h"
#include "ui/emoji_config.h"
#include "window/section_memento.h"
#include "window/section_widget.h"
#include "window/window_connecting_widget.h"
#include "window/window_top_bar_wrap.h"
#include "window/notifications_manager.h"
#include "window/window_slide_animation.h"
#include "window/window_session_controller.h"
#include "window/window_history_hider.h"
#include "window/window_controller.h"
#include "window/themes/window_theme.h"
#include "chat_helpers/tabbed_selector.h" // TabbedSelector::refreshStickers
#include "chat_helpers/message_field.h"
#include "info/info_memento.h"
#include "info/info_controller.h"
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
#include "storage/storage_account.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_panel.h"
#include "media/player/media_player_widget.h"
#include "media/player/media_player_volume_controller.h"
#include "media/player/media_player_instance.h"
#include "media/player/media_player_float.h"
#include "base/qthelp_regex.h"
#include "base/qthelp_url.h"
#include "base/flat_set.h"
#include "mtproto/mtproto_dc_options.h"
#include "core/file_utilities.h"
#include "core/update_checker.h"
#include "core/shortcuts.h"
#include "core/application.h"
#include "core/changelogs.h"
#include "base/unixtime.h"
#include "calls/calls_instance.h"
#include "calls/calls_top_bar.h"
#include "export/export_settings.h"
#include "export/export_manager.h"
#include "export/view/export_view_top_bar.h"
#include "export/view/export_view_panel_controller.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "main/main_account.h"
#include "support/support_helper.h"
#include "storage/storage_facade.h"
#include "storage/storage_shared_media.h"
#include "storage/storage_user_photos.h"
#include "app.h"
#include "facades.h"
#include "styles/style_dialogs.h"
#include "styles/style_chat.h"
#include "styles/style_boxes.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QMimeData>

namespace {

// Send channel views each second.
constexpr auto kSendViewsTimeout = crl::time(1000);

// Cache background scaled image after 3s.
constexpr auto kCacheBackgroundTimeout = 3000;

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
		std::shared_ptr<Window::SectionMemento> memento);
	std::shared_ptr<Window::SectionMemento> takeThirdSectionMemento() {
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
	std::shared_ptr<Window::SectionMemento> _thirdSectionMemento;

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
		std::shared_ptr<Window::SectionMemento> memento);

	StackItemType type() const override {
		return SectionStackItem;
	}
	std::shared_ptr<Window::SectionMemento> takeMemento() {
		return std::move(_memento);
	}

private:
	std::shared_ptr<Window::SectionMemento> _memento;

};

void StackItem::setThirdSectionMemento(
		std::shared_ptr<Window::SectionMemento> memento) {
	_thirdSectionMemento = std::move(memento);
}

StackItemSection::StackItemSection(
	std::shared_ptr<Window::SectionMemento> memento)
: StackItem(nullptr)
, _memento(std::move(memento)) {
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
, _api(&controller->session().mtp())
, _dialogsWidth(st::columnMinimalWidthLeft)
, _thirdColumnWidth(st::columnMinimalWidthThird)
, _sideShadow(this)
, _dialogs(this, _controller)
, _history(this, _controller)
, _playerPlaylist(this, _controller)
, _cacheBackgroundTimer([=] { cacheBackground(); })
, _viewsIncrementTimer([=] { viewsIncrement(); })
, _changelogs(Core::Changelogs::Create(&controller->session())) {
	updateScrollColors();
	setupConnectingWidget();

	connect(_dialogs, SIGNAL(cancelled()), this, SLOT(dialogsCancelled()));

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

	Core::App().setDefaultFloatPlayerDelegate(floatPlayerDelegate());
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

	Media::Player::instance()->updatedNotifier(
	) | rpl::start_with_next([=](const Media::Player::TrackState &state) {
		handleAudioUpdate(state);
	}, lifetime());
	handleAudioUpdate(Media::Player::instance()->getState(AudioMsgId::Type::Song));
	handleAudioUpdate(Media::Player::instance()->getState(AudioMsgId::Type::Voice));
	if (_player) {
		_player->finishAnimating();
	}

	subscribe(_controller->dialogsListFocused(), [this](bool) {
		updateDialogsWidthAnimated();
	});
	subscribe(_controller->dialogsListDisplayForced(), [this](bool) {
		updateDialogsWidthAnimated();
	});
	rpl::merge(
		Core::App().settings().dialogsWidthRatioChanges() | rpl::to_empty,
		Core::App().settings().thirdColumnWidthChanges() | rpl::to_empty
	) | rpl::start_with_next([=] {
		updateControlsGeometry();
	}, lifetime());

	session().changes().historyUpdates(
		Data::HistoryUpdate::Flag::MessageSent
		| Data::HistoryUpdate::Flag::LocalDraftSet
	) | rpl::start_with_next([=](const Data::HistoryUpdate &update) {
		const auto history = update.history;
		if (update.flags & Data::HistoryUpdate::Flag::MessageSent) {
			history->forgetScrollState();
			if (const auto from = history->peer->migrateFrom()) {
				auto &owner = history->owner();
				if (const auto migrated = owner.historyLoaded(from)) {
					migrated->forgetScrollState();
				}
			}
		}
		if (update.flags & Data::HistoryUpdate::Flag::LocalDraftSet) {
			const auto opened = (_history->peer() == history->peer.get());
			if (opened) {
				_history->applyDraft();
			} else {
				Ui::showPeerHistory(history, ShowAtUnreadMsgId);
			}
			Ui::hideLayer();
		}
	}, lifetime());

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
		} else if (type == AudioMsgId::Type::Song) {
			const auto songState = Media::Player::instance()->getState(AudioMsgId::Type::Song);
			if (!songState.id) {
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
		Window::AdaptiveIsOneColumn() | rpl::map(!_1));
}

not_null<Media::Player::FloatDelegate*> MainWidget::floatPlayerDelegate() {
	return static_cast<Media::Player::FloatDelegate*>(this);
}

not_null<Ui::RpWidget*> MainWidget::floatPlayerWidget() {
	return this;
}

auto MainWidget::floatPlayerGetSection(Window::Column column)
-> not_null<Media::Player::FloatSectionDelegate*> {
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
		not_null<Media::Player::FloatSectionDelegate*> widget,
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
			stopAndClosePlayer();
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
		Ui::show(Box<InformBox>(error), Ui::LayerOption::KeepOther);
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
	history->setLocalDraft(std::make_unique<Data::Draft>(
		textWithTags,
		0,
		cursor,
		Data::PreviewState::Allowed));
	history->clearLocalEditDraft();
	history->session().changes().historyUpdated(
		history,
		Data::HistoryUpdate::Flag::LocalDraftSet);
	return true;
}

void MainWidget::replyToItem(not_null<HistoryItem*> item) {
	if ((!_mainSection || !_mainSection->replyToMessage(item))
		&& (_history->peer() == item->history()->peer
			|| _history->peer() == item->history()->peer->migrateTo())) {
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
	h->setLocalDraft(std::make_unique<Data::Draft>(
		textWithTags,
		0,
		cursor,
		Data::PreviewState::Allowed));
	h->clearLocalEditDraft();
	h->session().changes().historyUpdated(
		h,
		Data::HistoryUpdate::Flag::LocalDraftSet);
	return true;
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

bool MainWidget::notify_switchInlineBotButtonReceived(const QString &query, UserData *samePeerBot, MsgId samePeerReplyTo) {
	return _history->notify_switchInlineBotButtonReceived(query, samePeerBot, samePeerReplyTo);
}

void MainWidget::clearHider(not_null<Window::HistoryHider*> instance) {
	if (_hider != instance) {
		return;
	}
	_hider.release();
	controller()->setSelectingPeer(false);

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
	if (controller()->window().locked()) {
		return;
	}

	_hider = std::move(hider);
	controller()->setSelectingPeer(true);

	_hider->setParent(this);

	_hider->hidden(
	) | rpl::start_with_next([=, instance = _hider.get()] {
		clearHider(instance);
		instance->hide();
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
		auto &data = session().data();
		if (const auto item = data.message(itemId)) {
			if (!item->isEditingMedia()) {
				const auto history = item->history();
				item->destroy();
				history->requestChatListMessage();
			} else {
				item->returnSavedMedia();
				session().uploader().cancel(item->fullId());
			}
			data.sendHistoryChangeNotifications();
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

void MainWidget::sendBotCommand(
		not_null<PeerData*> peer,
		UserData *bot,
		const QString &cmd,
		MsgId replyTo) {
	_history->sendBotCommand(peer, bot, cmd, replyTo);
}

void MainWidget::hideSingleUseKeyboard(PeerData *peer, MsgId replyTo) {
	_history->hideSingleUseKeyboard(peer, replyTo);
}

bool MainWidget::insertBotCommand(const QString &cmd) {
	return _history->insertBotCommand(cmd);
}

void MainWidget::searchMessages(const QString &query, Dialogs::Key inChat) {
	_dialogs->searchMessages(query, inChat);
	if (Adaptive::OneColumn()) {
		Ui::showChatsList(&session());
	} else {
		_dialogs->setInnerFocus();
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

void MainWidget::stopAndClosePlayer() {
	if (_player) {
		_player->entity()->stopAndClose();
	}
}

void MainWidget::createPlayer() {
	if (!_player) {
		_player.create(
			this,
			object_ptr<Media::Player::Widget>(this, &session()));
		rpl::merge(
			_player->heightValue() | rpl::map_to(true),
			_player->shownValue()
		) | rpl::start_with_next(
			[this] { playerHeightUpdated(); },
			_player->lifetime());
		_player->entity()->setCloseCallback([=] { closeBothPlayers(); });
		_playerVolume.create(this, _controller);
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

	_callTopBar.create(
		this,
		(_currentCall
			? object_ptr<Calls::TopBar>(this, _currentCall)
			: object_ptr<Calls::TopBar>(this, _currentGroupCall)));
	_callTopBar->entity()->initBlobsUnder(this, _callTopBar->geometryValue());
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

bool MainWidget::sendExistingDocument(not_null<DocumentData*> document) {
	return _history->sendExistingDocument(document, Api::SendOptions());
}

void MainWidget::dialogsCancelled() {
	if (_hider) {
		_hider->startHide();
		clearHider(_hider);
	}
	_history->activate();
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
	if (const auto document = _background->data.document()) {
		_background->dataMedia = document->createMediaView();
		_background->dataMedia->thumbnailWanted(
			_background->data.fileOrigin());
	}
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
		&& background.localThumbnail()) {
		image = background.localThumbnail()->original();
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
	_background->generating = Data::ReadImageAsync(
		media.get(),
		Window::Theme::ProcessBackgroundImage,
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
		if (i->second.contains(item->id)) return;
	} else {
		i = _viewsIncremented.emplace(peer).first;
	}
	i->second.emplace(item->id);
	auto j = _viewsToIncrement.find(peer);
	if (j == _viewsToIncrement.cend()) {
		j = _viewsToIncrement.emplace(peer).first;
		_viewsIncrementTimer.callOnce(kSendViewsTimeout);
	}
	j->second.emplace(item->id);
}

void MainWidget::viewsIncrement() {
	for (auto i = _viewsToIncrement.begin(); i != _viewsToIncrement.cend();) {
		if (_viewsIncrementRequests.contains(i->first)) {
			++i;
			continue;
		}

		QVector<MTPint> ids;
		ids.reserve(i->second.size());
		for (const auto msgId : i->second) {
			ids.push_back(MTP_int(msgId));
		}
		const auto requestId = _api.request(MTPmessages_GetMessagesViews(
			i->first->input,
			MTP_vector<MTPint>(ids),
			MTP_bool(true)
		)).done([=](const MTPmessages_MessageViews &result, mtpRequestId requestId) {
			viewsIncrementDone(ids, result, requestId);
		}).fail([=](const MTP::Error &error, mtpRequestId requestId) {
			viewsIncrementFail(error, requestId);
		}).afterDelay(5).send();

		_viewsIncrementRequests.emplace(i->first, requestId);
		i = _viewsToIncrement.erase(i);
	}
}

void MainWidget::viewsIncrementDone(
		QVector<MTPint> ids,
		const MTPmessages_MessageViews &result,
		mtpRequestId requestId) {
	const auto &data = result.c_messages_messageViews();
	session().data().processUsers(data.vusers());
	session().data().processChats(data.vchats());
	auto &v = data.vviews().v;
	if (ids.size() == v.size()) {
		for (auto i = _viewsIncrementRequests.begin(); i != _viewsIncrementRequests.cend(); ++i) {
			if (i->second == requestId) {
				const auto peer = i->first;
				const auto channel = peerToChannel(peer->id);
				for (int32 j = 0, l = ids.size(); j < l; ++j) {
					if (const auto item = session().data().message(channel, ids[j].v)) {
						v[j].match([&](const MTPDmessageViews &data) {
							if (const auto views = data.vviews()) {
								item->setViewsCount(views->v);
							}
							if (const auto forwards = data.vforwards()) {
								item->setForwardsCount(forwards->v);
							}
							if (const auto replies = data.vreplies()) {
								item->setReplies(*replies);
							}
						});
					}
				}
				_viewsIncrementRequests.erase(i);
				break;
			}
		}
	}
	if (!_viewsToIncrement.empty() && !_viewsIncrementTimer.isActive()) {
		_viewsIncrementTimer.callOnce(kSendViewsTimeout);
	}
}

void MainWidget::viewsIncrementFail(const MTP::Error &error, mtpRequestId requestId) {
	for (auto i = _viewsIncrementRequests.begin(); i != _viewsIncrementRequests.cend(); ++i) {
		if (i->second == requestId) {
			_viewsIncrementRequests.erase(i);
			break;
		}
	}
	if (!_viewsToIncrement.empty() && !_viewsIncrementTimer.isActive()) {
		_viewsIncrementTimer.callOnce(kSendViewsTimeout);
	}
}

void MainWidget::choosePeer(PeerId peerId, MsgId showAtMsgId) {
	if (selectingPeer()) {
		_hider->offerPeer(peerId);
	} else if (peerId) {
		Ui::showPeerHistory(session().data().peer(peerId), showAtMsgId);
	} else {
		Ui::showChatsList(&session());
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
	ui_showPeerHistory(
		peer->id,
		SectionShow::Way::Forward,
		ShowForChooseMessagesMsgId);
}

void MainWidget::clearChooseReportMessages() {
	_history->setChooseReportMessagesDetails({}, nullptr);
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
		const auto unavailable = peer->computeUnavailableReason();
		if (!unavailable.isEmpty()) {
			if (params.activation != anim::activation::background) {
				Ui::show(Box<InformBox>(unavailable));
			}
			return;
		}
	}
	if (IsServerMsgId(showAtMsgId)
		&& _mainSection
		&& _mainSection->showMessage(peerId, params, showAtMsgId)) {
		return;
	}

	if (!(_history->peer() && _history->peer()->id == peerId)
		&& preventsCloseSection(
			[=] { ui_showPeerHistory(peerId, params, showAtMsgId); },
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
					way = _mainSection ? Way::Backward : Way::ClearStack;
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
		controller()->setSelectingPeer(false);
	}

	auto animatedShow = [&] {
		if (_a_show.animating()
			|| Core::App().passcodeLocked()
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
				crl::on_main(this, [=] {
					_controller->widget()->setInnerFocus();
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
		std::shared_ptr<Window::SectionMemento> memento,
		const SectionShow &params) {
	if (_mainSection && _mainSection->showInternal(
			memento.get(),
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
		std::shared_ptr<Window::SectionMemento> memento,
		const SectionShow &params) {
	using Column = Window::Column;

	auto saveInStack = (params.way == SectionShow::Way::Forward);
	const auto thirdSectionTop = getThirdSectionTop();
	const auto newThirdGeometry = QRect(
		width() - st::columnMinimalWidthThird,
		thirdSectionTop,
		st::columnMinimalWidthThird,
		height() - thirdSectionTop);
	auto newThirdSection = (Adaptive::ThreeColumn() && params.thirdColumn)
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
			Ui::hideLayer(anim::type::instant);
		}
		_controller->showSpecialLayer(std::move(layer));
		return;
	}

	if (params.activation != anim::activation::background) {
		Ui::hideSettingsAndLayer();
	}

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
		: memento->createWidget(
			this,
			_controller,
			Adaptive::OneColumn() ? Column::First : Column::Second,
			newMainGeometry);
	Assert(newMainSection || newThirdSection);

	auto animatedShow = [&] {
		if (_a_show.animating()
			|| Core::App().passcodeLocked()
			|| (params.animated == anim::type::instant)
			|| memento->instant()) {
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
	return params.thirdColumn
		? false
		: preventsCloseSection(std::move(callback));
}

void MainWidget::showBackFromStack(
		const SectionShow &params) {

	if (preventsCloseSection([=] { showBackFromStack(params); }, params)) {
		return;
	}

	if (selectingPeer()) {
		return;
	} else if (_stack.empty()) {
		_controller->clearSectionStack(params);
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
		_history->setReplyReturns(historyItem->peer()->id, historyItem->replyReturns);
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

void MainWidget::windowShown() {
	_history->windowShown();
}

void MainWidget::dialogsToUp() {
	_dialogs->jumpToTop();
}

void MainWidget::checkHistoryActivation() {
	_history->checkHistoryActivation();
}

void MainWidget::showAnimated(const QPixmap &bgAnimCache, bool back) {
	_showBack = back;
	(_showBack ? _cacheOver : _cacheUnder) = bgAnimCache;

	_a_show.stop();

	showAll();
	floatPlayerHideAll();
	(_showBack ? _cacheUnder : _cacheOver) = Ui::GrabWidget(this);
	hideAll();
	floatPlayerShowVisible();

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
	if (_callTopBar) {
		_callTopBar->setVisible(false);
		_callTopBarHeight = 0;
	}
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

	_controller->widget()->checkHistoryActivation();
}

void MainWidget::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void MainWidget::updateControlsGeometry() {
	updateWindowAdaptiveLayout();
	if (Core::App().settings().dialogsWidthRatio() > 0) {
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
			const auto active = _controller->activeChatCurrent();
			if (const auto peer = active.peer()) {
				if (Core::App().settings().tabbedSelectorSectionEnabled()) {
					if (_mainSection) {
						_mainSection->pushTabbedSelectorToThirdSection(peer, params);
					} else {
						_history->pushTabbedSelectorToThirdSection(peer, params);
					}
				} else if (Core::App().settings().thirdSectionInfoEnabled()) {
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
		_dialogs->setGeometryWithTopMoved(mainSectionGeometry, _contentScrollAddToY);
		_history->setGeometryWithTopMoved(mainSectionGeometry, _contentScrollAddToY);
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
		const auto shadowTop = _controller->window().verticalShadowTop();
		const auto shadowHeight = height() - shadowTop;
		_sideShadow->setGeometryToLeft(
			dialogsWidth,
			shadowTop,
			st::lineWidth,
			shadowHeight);
		if (_thirdShadow) {
			_thirdShadow->setGeometryToLeft(
				width() - thirdSectionWidth - st::lineWidth,
				shadowTop,
				st::lineWidth,
				shadowHeight);
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
		_history->setGeometryWithTopMoved({ dialogsWidth, mainSectionTop, mainSectionWidth, height() - mainSectionTop }, _contentScrollAddToY);
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
		Core::App().settings().setDialogsWidthRatio(newRatio);
	};
	auto moveFinishedCallback = [=] {
		if (Adaptive::OneColumn()) {
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
		if (!Adaptive::ThreeColumn() || !_thirdSection) {
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
	if (Core::App().settings().dialogsWidthRatio() > 0) {
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
	} else if (const auto peer = key.peer()) {
		return std::make_shared<Info::Memento>(
			peer,
			Info::Memento::DefaultSection(peer));
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
		if (Adaptive::ThreeColumn()
			&& !settings.thirdSectionInfoEnabled()) {
			settings.setThirdSectionInfoEnabled(true);
			Core::App().saveSettingsDelayed();
		}

		_controller->showSection(
			thirdSectionForCurrentMainSection(key),
			params.withThirdColumn());
	};
	auto switchTabbedFast = [&](not_null<PeerData*> peer) {
		saveOldThirdSection();
		return _mainSection
			? _mainSection->pushTabbedSelectorToThirdSection(peer, params)
			: _history->pushTabbedSelectorToThirdSection(peer, params);
	};
	if (Adaptive::ThreeColumn()
		&& settings.tabbedSelectorSectionEnabled()
		&& key) {
		if (!canWrite) {
			switchInfoFast();
			settings.setTabbedSelectorSectionEnabled(true);
			settings.setTabbedReplacedWithInfo(true);
		} else if (settings.tabbedReplacedWithInfo()
			&& key.history()
			&& switchTabbedFast(key.history()->peer)) {
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
		} else if (Adaptive::ThreeColumn()
			&& settings.thirdSectionInfoEnabled()) {
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
	auto dialogsWidthRatio = Core::App().settings().dialogsWidthRatio();

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
		Ui::showChatsList(&session());
	} else {
		_dialogs->setInnerFocus();
	}
}

bool MainWidget::contentOverlapped(const QRect &globalRect) {
	return (_history->contentOverlapped(globalRect)
			|| _playerPlaylist->overlaps(globalRect)
			|| (_playerVolume && _playerVolume->overlaps(globalRect)));
}

void MainWidget::activate() {
	if (_a_show.animating()) {
		return;
	} else if (!_mainSection) {
		if (_hider) {
			_dialogs->setInnerFocus();
		} else if (!Ui::isLayerShown()) {
			if (!cSendPaths().isEmpty()) {
				const auto interpret = qstr("interpret://");
				const auto path = cSendPaths()[0];
				if (path.startsWith(interpret)) {
					cSetSendPaths(QStringList());
					const auto error = Support::InterpretSendPath(
						_controller,
						path.mid(interpret.size()));
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
	_controller->widget()->fixOrder();
}

bool MainWidget::isActive() const {
	return isVisible()
		&& !_a_show.animating()
		&& !session().updates().isIdle();
}

bool MainWidget::doWeMarkAsRead() const {
	return isActive() && !_mainSection;
}

int32 MainWidget::dlgsWidth() const {
	return _dialogs->width();
}

void MainWidget::saveFieldToHistoryLocalDraft() {
	_history->saveFieldToHistoryLocalDraft();
}

namespace App {

MainWidget *main() {
	if (const auto window = wnd()) {
		return window->sessionContent();
	}
	return nullptr;
}

} // namespace App
