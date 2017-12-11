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

#include <rpl/combine.h>
#include <rpl/merge.h>
#include <rpl/flatten_latest.h>
#include "data/data_photo.h"
#include "data/data_document.h"
#include "data/data_web_page.h"
#include "data/data_game.h"
#include "data/data_peer_values.h"
#include "styles/style_dialogs.h"
#include "styles/style_history.h"
#include "ui/special_buttons.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/shadow.h"
#include "window/section_memento.h"
#include "window/section_widget.h"
#include "data/data_drafts.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/focus_persister.h"
#include "ui/resize_area.h"
#include "ui/toast/toast.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/stickers.h"
#include "info/info_memento.h"
#include "info/info_controller.h"
#include "observer_peer.h"
#include "apiwrap.h"
#include "dialogs/dialogs_widget.h"
#include "history/history_widget.h"
#include "history/history_message.h"
#include "history/history_media.h"
#include "history/history_service_layout.h"
#include "lang/lang_keys.h"
#include "lang/lang_cloud_manager.h"
#include "boxes/add_contact_box.h"
#include "storage/file_upload.h"
#include "messenger.h"
#include "application.h"
#include "mainwindow.h"
#include "inline_bots/inline_bot_layout_item.h"
#include "boxes/confirm_box.h"
#include "boxes/sticker_set_box.h"
#include "boxes/mute_settings_box.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/download_path_box.h"
#include "storage/localstorage.h"
#include "shortcuts.h"
#include "media/media_audio.h"
#include "media/player/media_player_panel.h"
#include "media/player/media_player_widget.h"
#include "media/player/media_player_volume_controller.h"
#include "media/player/media_player_instance.h"
#include "media/player/media_player_float.h"
#include "base/qthelp_regex.h"
#include "base/qthelp_url.h"
#include "base/flat_set.h"
#include "window/player_wrap_widget.h"
#include "window/notifications_manager.h"
#include "window/window_slide_animation.h"
#include "window/window_controller.h"
#include "window/themes/window_theme.h"
#include "styles/style_boxes.h"
#include "mtproto/dc_options.h"
#include "core/file_utilities.h"
#include "auth_session.h"
#include "calls/calls_instance.h"
#include "calls/calls_top_bar.h"
#include "auth_session.h"
#include "storage/storage_facade.h"
#include "storage/storage_shared_media.h"
#include "storage/storage_user_photos.h"

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
		PeerData *peer,
		MsgId msgId,
		QList<MsgId> replyReturns)
	: StackItem(peer)
	, msgId(msgId)
	, replyReturns(replyReturns) {
	}

	StackItemType type() const override {
		return HistoryStackItem;
	}

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

template <typename ToggleCallback, typename DraggedCallback>
MainWidget::Float::Float(QWidget *parent, HistoryItem *item, ToggleCallback toggle, DraggedCallback dragged)
: animationSide(RectPart::Right)
, column(Window::Column::Second)
, corner(RectPart::TopRight)
, widget(parent, item, [this, toggle = std::move(toggle)](bool visible) {
	toggle(this, visible);
}, [this, dragged = std::move(dragged)](bool closed) {
	dragged(this, closed);
}) {
}

MainWidget::MainWidget(
	QWidget *parent,
	not_null<Window::Controller*> controller)
: RpWidget(parent)
, _controller(controller)
, _dialogsWidth(st::columnMinimalWidthLeft)
, _thirdColumnWidth(st::columnMinimalWidthThird)
, _sideShadow(this)
, _dialogs(this, _controller)
, _history(this, _controller)
, _playerPlaylist(
	this,
	_controller,
	Media::Player::Panel::Layout::OnlyPlaylist)
, _playerPanel(this, _controller, Media::Player::Panel::Layout::Full) {
	Messenger::Instance().mtp()->setUpdatesHandler(rpcDone(&MainWidget::updateReceived));
	Messenger::Instance().mtp()->setGlobalFailHandler(rpcFail(&MainWidget::updateFail));

	_ptsWaiter.setRequesting(true);
	updateScrollColors();

	connect(_dialogs, SIGNAL(cancelled()), this, SLOT(dialogsCancelled()));
	connect(this, SIGNAL(dialogsUpdated()), _dialogs, SLOT(onListScroll()));
	connect(_history, SIGNAL(cancelled()), _dialogs, SLOT(activate()));
	connect(&noUpdatesTimer, SIGNAL(timeout()), this, SLOT(mtpPing()));
	connect(&_onlineTimer, SIGNAL(timeout()), this, SLOT(updateOnline()));
	connect(&_idleFinishTimer, SIGNAL(timeout()), this, SLOT(checkIdleFinish()));
	connect(&_bySeqTimer, SIGNAL(timeout()), this, SLOT(getDifference()));
	connect(&_byPtsTimer, SIGNAL(timeout()), this, SLOT(onGetDifferenceTimeByPts()));
	connect(&_byMinChannelTimer, SIGNAL(timeout()), this, SLOT(getDifference()));
	connect(&_failDifferenceTimer, SIGNAL(timeout()), this, SLOT(onGetDifferenceTimeAfterFail()));
	connect(_history, SIGNAL(historyShown(History*,MsgId)), this, SLOT(onHistoryShown(History*,MsgId)));
	connect(&updateNotifySettingTimer, SIGNAL(timeout()), this, SLOT(onUpdateNotifySettings()));
	subscribe(Media::Player::Updated(), [this](const AudioMsgId &audioId) {
		if (audioId.type() != AudioMsgId::Type::Video) {
			handleAudioUpdate(audioId);
		}
	});
	subscribe(Auth().calls().currentCallChanged(), [this](Calls::Call *call) { setCurrentCall(call); });
	subscribe(_controller->dialogsListFocused(), [this](bool) {
		updateDialogsWidthAnimated();
	});
	subscribe(_controller->dialogsListDisplayForced(), [this](bool) {
		updateDialogsWidthAnimated();
	});
	rpl::merge(
		Auth().data().dialogsWidthRatioChanges()
			| rpl::map([] { return rpl::empty_value(); }),
		Auth().data().thirdColumnWidthChanges()
			| rpl::map([] { return rpl::empty_value(); }))
		| rpl::start_with_next(
			[this] { updateControlsGeometry(); },
			lifetime());
	subscribe(_controller->floatPlayerAreaUpdated(), [this] {
		checkFloatPlayerVisibility();
	});

	using namespace rpl::mappers;
	_controller->activePeer.value()
		| rpl::map([](PeerData *peer) {
			auto canWrite = peer
				? Data::CanWriteValue(peer)
				: rpl::single(false);
			return std::move(canWrite)
					| rpl::map(tuple(peer, _1));
		})
		| rpl::flatten_latest()
		| rpl::start_with_next([this](PeerData *peer, bool canWrite) {
			updateThirdColumnToCurrentPeer(peer, canWrite);
		}, lifetime());

	QCoreApplication::instance()->installEventFilter(this);

	connect(&_updateMutedTimer, SIGNAL(timeout()), this, SLOT(onUpdateMuted()));
	connect(&_viewsIncrementTimer, SIGNAL(timeout()), this, SLOT(onViewsIncrement()));

	_webPageOrGameUpdater.setSingleShot(true);
	connect(&_webPageOrGameUpdater, SIGNAL(timeout()), this, SLOT(webPagesOrGamesUpdate()));

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
	subscribe(Media::Player::instance()->tracksFinishedNotifier(), [this](AudioMsgId::Type type) {
		if (type == AudioMsgId::Type::Voice) {
			auto songState = Media::Player::mixer()->currentState(AudioMsgId::Type::Song);
			if (!songState.id || IsStoppedOrStopping(songState.state)) {
				closeBothPlayers();
			}
		}
	});
	subscribe(Media::Player::instance()->trackChangedNotifier(), [this](AudioMsgId::Type type) {
		if (type == AudioMsgId::Type::Voice) {
			checkCurrentFloatPlayer();
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

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	Sandbox::startUpdateCheck();
#endif // !TDESKTOP_DISABLE_AUTOUPDATE
}

void MainWidget::checkCurrentFloatPlayer() {
	auto state = Media::Player::instance()->current(AudioMsgId::Type::Voice);
	auto fullId = state.contextId();
	auto last = currentFloatPlayer();
	if (!last || last->widget->detached() || last->widget->item()->fullId() != fullId) {
		if (last) {
			last->widget->detach();
		}
		if (auto item = App::histItemById(fullId)) {
			if (auto media = item->getMedia()) {
				if (auto document = media->getDocument()) {
					if (document->isVideoMessage()) {
						_playerFloats.push_back(std::make_unique<Float>(this, item, [this](not_null<Float*> instance, bool visible) {
							instance->hiddenByWidget = !visible;
							toggleFloatPlayer(instance);
						}, [this](not_null<Float*> instance, bool closed) {
							finishFloatPlayerDrag(instance, closed);
						}));
						currentFloatPlayer()->column = Auth().data().floatPlayerColumn();
						currentFloatPlayer()->corner = Auth().data().floatPlayerCorner();
						checkFloatPlayerVisibility();
					}
				}
			}
		}
	}
}

void MainWidget::toggleFloatPlayer(not_null<Float*> instance) {
	auto visible = !instance->hiddenByHistory && !instance->hiddenByWidget && instance->widget->isReady();
	if (instance->visible != visible) {
		instance->widget->resetMouseState();
		instance->visible = visible;
		if (!instance->visibleAnimation.animating() && !instance->hiddenByDrag) {
			auto finalRect = QRect(getFloatPlayerPosition(instance), instance->widget->size());
			instance->animationSide = getFloatPlayerSide(finalRect.center());
		}
		instance->visibleAnimation.start([this, instance] {
			updateFloatPlayerPosition(instance);
		}, visible ? 0. : 1., visible ? 1. : 0., st::slideDuration, visible ? anim::easeOutCirc : anim::linear);
		updateFloatPlayerPosition(instance);
	}
}

void MainWidget::checkFloatPlayerVisibility() {
	auto instance = currentFloatPlayer();
	if (!instance) {
		return;
	}

	auto amVisible = false;
	if (auto item = instance->widget->item()) {
		Auth().data().queryItemVisibility().notify({ item, &amVisible }, true);
	}
	instance->hiddenByHistory = amVisible;
	toggleFloatPlayer(instance);
	updateFloatPlayerPosition(instance);
}

void MainWidget::updateFloatPlayerPosition(not_null<Float*> instance) {
	auto visible = instance->visibleAnimation.current(instance->visible ? 1. : 0.);
	if (visible == 0. && !instance->visible) {
		instance->widget->hide();
		if (instance->widget->detached()) {
			InvokeQueued(instance->widget, [this, instance] {
				removeFloatPlayer(instance);
			});
		}
		return;
	}

	if (!instance->widget->dragged()) {
		if (instance->widget->isHidden()) {
			instance->widget->show();
		}

		auto dragged = instance->draggedAnimation.current(1.);
		auto position = QPoint();
		if (instance->hiddenByDrag) {
			instance->widget->setOpacity(instance->widget->countOpacityByParent());
			position = getFloatPlayerHiddenPosition(instance->dragFrom, instance->widget->size(), instance->animationSide);
		} else {
			instance->widget->setOpacity(visible * visible);
			position = getFloatPlayerPosition(instance);
			if (visible < 1.) {
				auto hiddenPosition = getFloatPlayerHiddenPosition(position, instance->widget->size(), instance->animationSide);
				position.setX(anim::interpolate(hiddenPosition.x(), position.x(), visible));
				position.setY(anim::interpolate(hiddenPosition.y(), position.y(), visible));
			}
		}
		if (dragged < 1.) {
			position.setX(anim::interpolate(instance->dragFrom.x(), position.x(), dragged));
			position.setY(anim::interpolate(instance->dragFrom.y(), position.y(), dragged));
		}
		instance->widget->move(position);
	}
}

QPoint MainWidget::getFloatPlayerHiddenPosition(QPoint position, QSize size, RectPart side) const {
	switch (side) {
	case RectPart::Left: return QPoint(-size.width(), position.y());
	case RectPart::Top: return QPoint(position.x(), -size.height());
	case RectPart::Right: return QPoint(width(), position.y());
	case RectPart::Bottom: return QPoint(position.x(), height());
	}
	Unexpected("Bad side in MainWidget::getFloatPlayerHiddenPosition().");
}

QPoint MainWidget::getFloatPlayerPosition(not_null<Float*> instance) const {
	auto section = getFloatPlayerSection(instance->column);
	auto rect = section->rectForFloatPlayer();
	auto position = rect.topLeft();
	if (IsBottomCorner(instance->corner)) {
		position.setY(position.y() + rect.height() - instance->widget->height());
	}
	if (IsRightCorner(instance->corner)) {
		position.setX(position.x() + rect.width() - instance->widget->width());
	}
	return mapFromGlobal(position);
}

RectPart MainWidget::getFloatPlayerSide(QPoint center) const {
	auto left = qAbs(center.x());
	auto right = qAbs(width() - center.x());
	auto top = qAbs(center.y());
	auto bottom = qAbs(height() - center.y());
	if (left < right && left < top && left < bottom) {
		return RectPart::Left;
	} else if (right < top && right < bottom) {
		return RectPart::Right;
	} else if (top < bottom) {
		return RectPart::Top;
	}
	return RectPart::Bottom;
}

void MainWidget::removeFloatPlayer(not_null<Float*> instance) {
	auto widget = std::move(instance->widget);
	auto i = std::find_if(_playerFloats.begin(), _playerFloats.end(), [instance](auto &item) {
		return (item.get() == instance);
	});
	Assert(i != _playerFloats.end());
	_playerFloats.erase(i);

	// ~QWidget() can call HistoryInner::enterEvent() which can
	// lead to repaintHistoryItem() and we'll have an instance
	// in _playerFloats with destroyed widget. So we destroy the
	// instance first and only after that destroy the widget.
	widget.destroy();
}

Window::AbstractSectionWidget *MainWidget::getFloatPlayerSection(Window::Column column) const {
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

void MainWidget::updateFloatPlayerColumnCorner(QPoint center) {
	Expects(!_playerFloats.empty());
	auto size = _playerFloats.back()->widget->size();
	auto min = INT_MAX;
	auto column = Auth().data().floatPlayerColumn();
	auto corner = Auth().data().floatPlayerCorner();
	auto checkSection = [this, center, size, &min, &column, &corner](
			Window::AbstractSectionWidget *widget,
			Window::Column widgetColumn) {
		auto rect = mapFromGlobal(widget->rectForFloatPlayer());
		auto left = rect.x() + (size.width() / 2);
		auto right = rect.x() + rect.width() - (size.width() / 2);
		auto top = rect.y() + (size.height() / 2);
		auto bottom = rect.y() + rect.height() - (size.height() / 2);
		auto checkCorner = [&](QPoint point, RectPart checked) {
			auto distance = (point - center).manhattanLength();
			if (min > distance) {
				min = distance;
				column = widgetColumn;
				corner = checked;
			}
		};
		checkCorner({ left, top }, RectPart::TopLeft);
		checkCorner({ right, top }, RectPart::TopRight);
		checkCorner({ left, bottom }, RectPart::BottomLeft);
		checkCorner({ right, bottom }, RectPart::BottomRight);
	};

	if (Adaptive::ThreeColumn()) {
		checkSection(_dialogs, Window::Column::First);
		if (_mainSection) {
			checkSection(_mainSection, Window::Column::Second);
		} else {
			checkSection(_history, Window::Column::Second);
		}
		if (_thirdSection) {
			checkSection(_thirdSection, Window::Column::Third);
		}
	} else if (Adaptive::Normal()) {
		checkSection(_dialogs, Window::Column::First);
		if (_mainSection) {
			checkSection(_mainSection, Window::Column::Second);
		} else {
			checkSection(_history, Window::Column::Second);
		}
	} else {
		if (Adaptive::OneColumn() && selectingPeer()) {
			checkSection(_dialogs, Window::Column::First);
		} else if (_mainSection) {
			checkSection(_mainSection, Window::Column::Second);
		} else if (!Adaptive::OneColumn() || _history->peer()) {
			checkSection(_history, Window::Column::Second);
		} else {
			checkSection(_dialogs, Window::Column::First);
		}
	}
	if (Auth().data().floatPlayerColumn() != column) {
		Auth().data().setFloatPlayerColumn(column);
		Auth().saveDataDelayed();
	}
	if (Auth().data().floatPlayerCorner() != corner) {
		Auth().data().setFloatPlayerCorner(corner);
		Auth().saveDataDelayed();
	}
}

void MainWidget::finishFloatPlayerDrag(not_null<Float*> instance, bool closed) {
	instance->dragFrom = instance->widget->pos();
	auto center = instance->widget->geometry().center();
	if (closed) {
		instance->hiddenByDrag = true;
		instance->animationSide = getFloatPlayerSide(center);
	}
	updateFloatPlayerColumnCorner(center);
	instance->column = Auth().data().floatPlayerColumn();
	instance->corner = Auth().data().floatPlayerCorner();

	instance->draggedAnimation.finish();
	instance->draggedAnimation.start([this, instance] { updateFloatPlayerPosition(instance); }, 0., 1., st::slideDuration, anim::sineInOut);
	updateFloatPlayerPosition(instance);

	if (closed) {
		if (auto item = instance->widget->item()) {
			auto voiceData = Media::Player::instance()->current(AudioMsgId::Type::Voice);
			if (_player && voiceData.contextId() == item->fullId()) {
				_player->entity()->stopAndClose();
			}
		}
		instance->widget->detach();
	}
}

bool MainWidget::setForwardDraft(PeerId peerId, ForwardWhatMessages what) {
	const auto collect = [&]() -> MessageIdsList {
		if (what == ForwardSelectedMessages) {
			return _history->getSelectedItems();
		}
		auto item = (HistoryItem*)nullptr;
		if (what == ForwardContextMessage) {
			item = App::contextItem();
		} else if (what == ForwardPressedMessage) {
			item = App::pressedItem();
		} else if (what == ForwardPressedLinkMessage) {
			item = App::pressedLinkItem();
		}
		if (item && item->toHistoryMessage() && item->id > 0) {
			return { 1, item->fullId() };
		}
		return {};
	};
	const auto result = setForwardDraft(peerId, collect());
	if (!result) {
		if (what == ForwardPressedMessage || what == ForwardPressedLinkMessage) {
			// We've already released the mouse button, so the forwarding is cancelled.
			if (_hider) {
				_hider->startHide();
				noHider(_hider);
			}
		}
	}
	return result;
}

bool MainWidget::setForwardDraft(PeerId peerId, MessageIdsList &&items) {
	Expects(peerId != 0);
	const auto peer = App::peer(peerId);
	const auto error = GetErrorTextForForward(
		peer,
		Auth().data().idsToItems(items));
	if (!error.isEmpty()) {
		Ui::show(Box<InformBox>(error), LayerOption::KeepOther);
		return false;
	}

	App::history(peer)->setForwardDraft(std::move(items));
	if (_history->peer() == peer) {
		_history->cancelReply();
	}
	Ui::showPeerHistory(peer, ShowAtUnreadMsgId);
	return true;
}

bool MainWidget::shareUrl(
		not_null<PeerData*> peer,
		const QString &url,
		const QString &text) {
	if (!peer->canWrite()) {
		Ui::show(Box<InformBox>(lang(lng_share_cant)));
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
	auto history = App::history(peer->id);
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

bool MainWidget::onInlineSwitchChosen(const PeerId &peer, const QString &botAndQuery) {
	PeerData *p = App::peer(peer);
	if (!peer || !p->canWrite()) {
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

void MainWidget::cancelForwarding(not_null<History*> history) {
	history->setForwardDraft({});
	_history->updateForwarding();
}

void MainWidget::finishForwarding(not_null<History*> history) {
	auto toForward = history->validateForwardDraft();
	if (!toForward.empty()) {
		auto options = ApiWrap::SendOptions(history);
		Auth().api().forwardMessages(std::move(toForward), options);

		if (_history->peer() == history->peer) {
			_history->peerMessagesUpdated();
		}
		cancelForwarding(history);
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

void MainWidget::updateMutedIn(TimeMs delay) {
	accumulate_max(delay, 24 * 3600 * 1000LL);
	if (!_updateMutedTimer.isActive()
		|| _updateMutedTimer.remainingTime() > delay) {
		_updateMutedTimer.start(delay);
	}
}

void MainWidget::onUpdateMuted() {
	App::updateMuted();
}

bool MainWidget::onSendPaths(const PeerId &peerId) {
	Expects(peerId != 0);
	auto peer = App::peer(peerId);
	if (!peer->canWrite()) {
		Ui::show(Box<InformBox>(lang(lng_forward_send_files_cant)));
		return false;
	} else if (auto megagroup = peer->asMegagroup()) {
		if (megagroup->restricted(ChannelRestriction::f_send_media)) {
			Ui::show(Box<InformBox>(lang(lng_restricted_send_media)));
			return false;
		}
	}
	Ui::showPeerHistory(peer, ShowAtTheEndMsgId);
	return _history->confirmSendingFiles(cSendPaths());
}

void MainWidget::onFilesOrForwardDrop(const PeerId &peerId, const QMimeData *data) {
	Expects(peerId != 0);
	if (data->hasFormat(qsl("application/x-td-forward-selected"))) {
		setForwardDraft(peerId, ForwardSelectedMessages);
	} else if (data->hasFormat(qsl("application/x-td-forward-pressed-link"))) {
		setForwardDraft(peerId, ForwardPressedLinkMessage);
	} else if (data->hasFormat(qsl("application/x-td-forward-pressed"))) {
		setForwardDraft(peerId, ForwardPressedMessage);
	} else {
		auto peer = App::peer(peerId);
		if (!peer->canWrite()) {
			Ui::show(Box<InformBox>(lang(lng_forward_send_files_cant)));
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

void MainWidget::notify_historyMuteUpdated(History *history) {
	_dialogs->notify_historyMuteUpdated(history);
}

bool MainWidget::cmd_search() {
	if (Ui::isLayerShown() || !isActiveWindow()) return false;
	if (_mainSection) {
		return _mainSection->cmd_search();
	}
	return _history->cmd_search();
}

bool MainWidget::cmd_next_chat() {
	if (Ui::isLayerShown() || !isActiveWindow()) return false;
	return _history->cmd_next_chat();
}

bool MainWidget::cmd_previous_chat() {
	if (Ui::isLayerShown() || !isActiveWindow()) return false;
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
			if (_mainSection || (_history->peer() && _history->peer()->id)) {
				auto animationParams = ([this] {
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
				checkFloatPlayerVisibility();
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
		_dialogs->activate();
	}
	checkFloatPlayerVisibility();
}

void MainWidget::showForwardLayer(MessageIdsList &&items) {
	hiderLayer(object_ptr<HistoryHider>(this, std::move(items)));
}

void MainWidget::showSendPathsLayer() {
	hiderLayer(object_ptr<HistoryHider>(this));
}

void MainWidget::deleteLayer(int selectedCount) {
	if (selectedCount) {
		auto selected = _history->getSelectedItems();
		if (!selected.empty()) {
			Ui::show(Box<DeleteMessagesBox>(std::move(selected)));
		}
	} else if (const auto item = App::contextItem()) {
		const auto suggestModerateActions = true;
		Ui::show(Box<DeleteMessagesBox>(item, suggestModerateActions));
	}
}

void MainWidget::cancelUploadLayer() {
	auto item = App::contextItem();
	if (!item) {
		return;
	}

	Auth().uploader().pause(item->fullId());
	Ui::show(Box<ConfirmBox>(lang(lng_selected_cancel_sure_this), lang(lng_selected_upload_stop), lang(lng_continue), base::lambda_guarded(this, [this] {
		_history->deleteContextItem(false);
		Auth().uploader().unpause();
	}), base::lambda_guarded(this, [] {
		Auth().uploader().unpause();
	})));
}

void MainWidget::deletePhotoLayer(PhotoData *photo) {
	if (!photo) return;
	Ui::show(Box<ConfirmBox>(lang(lng_delete_photo_sure), lang(lng_box_delete), base::lambda_guarded(this, [this, photo] {
		Ui::hideLayer();

		auto me = App::self();
		if (!me) return;

		if (me->userpicPhotoId() == photo->id) {
			Messenger::Instance().peerClearPhoto(me->id);
		} else if (photo->peer && !photo->peer->isUser() && photo->peer->userpicPhotoId() == photo->id) {
			Messenger::Instance().peerClearPhoto(photo->peer->id);
		} else {
			MTP::send(MTPphotos_DeletePhotos(MTP_vector<MTPInputPhoto>(1, MTP_inputPhoto(MTP_long(photo->id), MTP_long(photo->access)))));
			Auth().storage().remove(Storage::UserPhotosRemoveOne(me->bareId(), photo->id));
		}
	})));
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

bool MainWidget::selectingPeer(bool withConfirm) const {
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

	auto &d = result.c_messages_affectedHistory();
	if (peer && peer->isChannel()) {
		peer->asChannel()->ptsUpdateAndApply(d.vpts.v, d.vpts_count.v);
	} else {
		ptsUpdateAndApply(d.vpts.v, d.vpts_count.v);
	}

	auto offset = d.voffset.v;
	if (offset <= 0) {
		cRefReportSpamStatuses().remove(peer->id);
		Local::writeReportSpamStatuses();
		return;
	}

	auto flags = MTPmessages_DeleteHistory::Flags(0);
	if (request.justClearHistory) {
		flags |= MTPmessages_DeleteHistory::Flag::f_just_clear;
	}
	MTP::send(MTPmessages_DeleteHistory(MTP_flags(flags), peer->input, MTP_int(0)), rpcDone(&MainWidget::deleteHistoryPart, request));
}

void MainWidget::deleteMessages(
		not_null<PeerData*> peer,
		const QVector<MTPint> &ids,
		bool forEveryone) {
	if (const auto channel = peer->asChannel()) {
		MTP::send(
			MTPchannels_DeleteMessages(
				channel->inputChannel,
				MTP_vector<MTPint>(ids)),
			rpcDone(&MainWidget::messagesAffected, peer));
	} else {
		auto flags = MTPmessages_DeleteMessages::Flags(0);
		if (forEveryone) {
			flags |= MTPmessages_DeleteMessages::Flag::f_revoke;
		}
		MTP::send(
			MTPmessages_DeleteMessages(
				MTP_flags(flags),
				MTP_vector<MTPint>(ids)),
			rpcDone(&MainWidget::messagesAffected, peer));
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
		MTP::send(MTPmessages_DeleteHistory(MTP_flags(0), peer->input, MTP_int(0)), rpcDone(&MainWidget::deleteHistoryPart, request));
	}
}

void MainWidget::deleteAndExit(ChatData *chat) {
	PeerData *peer = chat;
	MTP::send(MTPmessages_DeleteChatUser(chat->inputChat, App::self()->inputUser), rpcDone(&MainWidget::deleteHistoryAfterLeave, peer), rpcFail(&MainWidget::leaveChatFailed, peer));
}

void MainWidget::deleteAllFromUser(ChannelData *channel, UserData *from) {
	Assert(channel != nullptr && from != nullptr);

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
	auto &d = result.c_messages_affectedHistory();
	params.channel->ptsUpdateAndApply(d.vpts.v, d.vpts_count.v);

	auto offset = d.voffset.v;
	if (offset > 0) {
		MTP::send(MTPchannels_DeleteUserHistory(params.channel->inputChannel, params.from->inputUser), rpcDone(&MainWidget::deleteAllFromUserPart, params));
	} else if (auto h = App::historyLoaded(params.channel)) {
		if (!h->lastMsg) {
			checkPeerHistory(params.channel);
		}
	}
}

void MainWidget::addParticipants(
		not_null<PeerData*> chatOrChannel,
		const std::vector<not_null<UserData*>> &users) {
	if (auto chat = chatOrChannel->asChat()) {
		for_const (auto user, users) {
			MTP::send(
				MTPmessages_AddChatUser(
					chat->inputChat,
					user->inputUser,
					MTP_int(ForwardOnAdd)),
				rpcDone(&MainWidget::sentUpdatesReceived),
				rpcFail(&MainWidget::addParticipantFail, { user, chat }),
				0,
				5);
		}
	} else if (auto channel = chatOrChannel->asChannel()) {
		QVector<MTPInputUser> inputUsers;
		inputUsers.reserve(qMin(int(users.size()), int(MaxUsersPerInvite)));
		for (auto i = users.cbegin(), e = users.cend(); i != e; ++i) {
			inputUsers.push_back((*i)->inputUser);
			if (inputUsers.size() == MaxUsersPerInvite) {
				MTP::send(
					MTPchannels_InviteToChannel(
						channel->inputChannel,
						MTP_vector<MTPInputUser>(inputUsers)),
					rpcDone(&MainWidget::inviteToChannelDone, { channel }),
					rpcFail(&MainWidget::addParticipantsFail, { channel }),
					0,
					5);
				inputUsers.clear();
			}
		}
		if (!inputUsers.isEmpty()) {
			MTP::send(
				MTPchannels_InviteToChannel(
					channel->inputChannel,
					MTP_vector<MTPInputUser>(inputUsers)),
				rpcDone(&MainWidget::inviteToChannelDone, { channel }),
				rpcFail(&MainWidget::addParticipantsFail, { channel }),
				0,
				5);
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

bool MainWidget::addParticipantsFail(
		not_null<ChannelData*> channel,
		const RPCError &error) {
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
	MTP::send(
		MTPmessages_DeleteChatUser(chat->inputChat, user->inputUser),
		rpcDone(&MainWidget::sentUpdatesReceived),
		rpcFail(&MainWidget::kickParticipantFail, chat));
	Ui::showPeerHistory(chat->id, ShowAtTheEndMsgId);
}

bool MainWidget::kickParticipantFail(ChatData *chat, const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	error.type();
	return false;
}

void MainWidget::checkPeerHistory(PeerData *peer) {
	auto offsetId = 0;
	auto offsetDate = 0;
	auto addOffset = 0;
	auto limit = 1;
	auto maxId = 0;
	auto minId = 0;
	auto historyHash = 0;
	MTP::send(
		MTPmessages_GetHistory(
			peer->input,
			MTP_int(offsetId),
			MTP_int(offsetDate),
			MTP_int(addOffset),
			MTP_int(limit),
			MTP_int(maxId),
			MTP_int(minId),
			MTP_int(historyHash)),
		rpcDone(&MainWidget::checkedHistory, peer));
}

void MainWidget::checkedHistory(PeerData *peer, const MTPmessages_Messages &result) {
	const QVector<MTPMessage> *v = 0;
	switch (result.type()) {
	case mtpc_messages_messages: {
		auto &d(result.c_messages_messages());
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		v = &d.vmessages.v;
	} break;

	case mtpc_messages_messagesSlice: {
		auto &d(result.c_messages_messagesSlice());
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		v = &d.vmessages.v;
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
		v = &d.vmessages.v;
	} break;

	case mtpc_messages_messagesNotModified: {
		LOG(("API Error: received messages.messagesNotModified! (MainWidget::checkedHistory)"));
	} break;
	}

	if (!v || v->isEmpty()) {
		if (peer->isChat() && !peer->asChat()->haveLeft()) {
			auto h = App::historyLoaded(peer->id);
			if (h) Local::addSavedPeer(peer, h->lastMsgDate);
		} else if (peer->isChannel()) {
			if (peer->asChannel()->inviter > 0 && peer->asChannel()->amIn()) {
				if (auto from = App::userLoaded(peer->asChannel()->inviter)) {
					auto h = App::history(peer->id);
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
		auto h = App::history(peer->id);
		if (!h->lastMsg) {
			h->addNewMessage((*v)[0], NewMessageLast);
		}
		if (!h->lastMsgDate.isNull() && h->loadedAtBottom()) {
			if (peer->isChannel() && peer->asChannel()->inviter > 0 && h->lastMsgDate <= peer->asChannel()->inviteDate && peer->asChannel()->amIn()) {
				if (auto from = App::userLoaded(peer->asChannel()->inviter)) {
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
	} else if (error.type() == qstr("USER_BANNED_IN_CHANNEL")) {
		auto link = textcmdLink(Messenger::Instance().createInternalLinkFull(qsl("spambot")), lang(lng_cant_more_info));
		Ui::show(Box<InformBox>(lng_error_public_groups_denied(lt_more_info, link)));
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
	_history->onForwardSelected();
}

void MainWidget::confirmDeleteSelectedItems() {
	_history->confirmDeleteSelectedItems();
}

void MainWidget::clearSelectedItems() {
	_history->onClearSelected();
}

Dialogs::IndexedList *MainWidget::contactsList() {
	return _dialogs->contactsList();
}

Dialogs::IndexedList *MainWidget::dialogsList() {
	return _dialogs->dialogsList();
}

Dialogs::IndexedList *MainWidget::contactsNoDialogsList() {
	return _dialogs->contactsNoDialogsList();
}

void MainWidget::sendMessage(const MessageToSend &message) {
	const auto history = message.history;
	const auto peer = history->peer;
	auto &textWithTags = message.textWithTags;

	auto options = ApiWrap::SendOptions(history);
	options.clearDraft = message.clearDraft;
	options.replyTo = message.replyTo;
	options.generateLocal = true;
	options.webPageId = message.webPageId;
	Auth().api().sendAction(options);

	if (!peer->canWrite()) {
		return;
	}
	saveRecentHashtags(textWithTags.text);

	auto sending = TextWithEntities();
	auto left = TextWithEntities { textWithTags.text, ConvertTextTagsToEntities(textWithTags.tags) };
	auto prepareFlags = itemTextOptions(history, App::self()).flags;
	TextUtilities::PrepareForSending(left, prepareFlags);

	HistoryItem *lastMessage = nullptr;

	while (TextUtilities::CutPart(sending, left, MaxMessageSize)) {
		auto newId = FullMsgId(peerToChannel(peer->id), clientMsgId());
		auto randomId = rand_value<uint64>();

		TextUtilities::Trim(sending);

		App::historyRegRandom(randomId, newId);
		App::historyRegSentData(randomId, peer->id, sending.text);

		MTPstring msgText(MTP_string(sending.text));
		auto flags = NewMessageFlags(peer) | MTPDmessage::Flag::f_entities;
		auto sendFlags = MTPmessages_SendMessage::Flags(0);
		if (message.replyTo) {
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
		bool channelPost = peer->isChannel() && !peer->isMegagroup();
		bool silentPost = channelPost && peer->notifySilentPosts();
		if (channelPost) {
			flags |= MTPDmessage::Flag::f_views;
			flags |= MTPDmessage::Flag::f_post;
		}
		if (!channelPost) {
			flags |= MTPDmessage::Flag::f_from_id;
		} else if (peer->asChannel()->addsSignature()) {
			flags |= MTPDmessage::Flag::f_post_author;
		}
		if (silentPost) {
			sendFlags |= MTPmessages_SendMessage::Flag::f_silent;
		}
		auto localEntities = TextUtilities::EntitiesToMTP(sending.entities);
		auto sentEntities = TextUtilities::EntitiesToMTP(sending.entities, TextUtilities::ConvertOption::SkipLocal);
		if (!sentEntities.v.isEmpty()) {
			sendFlags |= MTPmessages_SendMessage::Flag::f_entities;
		}
		if (message.clearDraft) {
			sendFlags |= MTPmessages_SendMessage::Flag::f_clear_draft;
			history->clearCloudDraft();
		}
		auto messageFromId = channelPost ? 0 : Auth().userId();
		auto messagePostAuthor = channelPost ? (Auth().user()->firstName + ' ' + Auth().user()->lastName) : QString();
		lastMessage = history->addNewMessage(
			MTP_message(
				MTP_flags(flags),
				MTP_int(newId.msg),
				MTP_int(messageFromId),
				peerToMTP(peer->id),
				MTPnullFwdHeader,
				MTPint(),
				MTP_int(message.replyTo),
				MTP_int(unixtime()),
				msgText,
				media,
				MTPnullMarkup,
				localEntities,
				MTP_int(1),
				MTPint(),
				MTP_string(messagePostAuthor),
				MTPlong()),
			NewMessageUnread);
		history->sendRequestId = MTP::send(
			MTPmessages_SendMessage(
				MTP_flags(sendFlags),
				peer->input,
				MTP_int(message.replyTo),
				msgText,
				MTP_long(randomId),
				MTPnullMarkup,
				sentEntities),
			rpcDone(&MainWidget::sentUpdatesReceived, randomId),
			rpcFail(&MainWidget::sendMessageFail),
			0,
			0,
			history->sendRequestId);
	}

	history->lastSentMsg = lastMessage;

	finishForwarding(history);
}

void MainWidget::saveRecentHashtags(const QString &text) {
	bool found = false;
	QRegularExpressionMatch m;
	RecentHashtagPack recent(cRecentWriteHashtags());
	for (int32 i = 0, next = 0; (m = TextUtilities::RegExpHashtag().match(text, i)).hasMatch(); i = next) {
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
		Stickers::IncrementRecentHashtag(recent, text.mid(i + 1, next - i - 1));
	}
	if (found) {
		cSetRecentWriteHashtags(recent);
		Local::writeRecentHashtagsAndBots();
	}
}

void MainWidget::unreadCountChanged(History *history) {
	_history->unreadCountChanged(history);
}

TimeMs MainWidget::highlightStartTime(not_null<const HistoryItem*> item) const {
	return _history->highlightStartTime(item);
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

bool MainWidget::insertBotCommand(const QString &cmd) {
	return _history->insertBotCommand(cmd);
}

void MainWidget::searchMessages(const QString &query, PeerData *inPeer) {
	_dialogs->searchMessages(query, inPeer);
	if (Adaptive::OneColumn()) {
		Ui::showChatsList();
	} else {
		_dialogs->activate();
	}
}

void MainWidget::itemEdited(HistoryItem *item) {
	if (_history->peer() == item->history()->peer || (_history->peer() && _history->peer() == item->history()->peer->migrateTo())) {
		_history->itemEdited(item);
	}
}

void MainWidget::checkLastUpdate(bool afterSleep) {
	auto n = getms(true);
	if (_lastUpdateTime && n > _lastUpdateTime + (afterSleep ? NoUpdatesAfterSleepTimeout : NoUpdatesTimeout)) {
		_lastUpdateTime = n;
		MTP::ping();
	}
}

void MainWidget::messagesAffected(
		not_null<PeerData*> peer,
		const MTPmessages_AffectedMessages &result) {
	const auto &data = result.c_messages_affectedMessages();
	if (const auto channel = peer->asChannel()) {
		channel->ptsUpdateAndApply(data.vpts.v, data.vpts_count.v);
	} else {
		ptsUpdateAndApply(data.vpts.v, data.vpts_count.v);
	}

	if (auto h = App::historyLoaded(peer ? peer->id : 0)) {
		if (!h->lastMsg) {
			checkPeerHistory(peer);
		}
	}
}

void MainWidget::messagesContentsRead(
		const MTPmessages_AffectedMessages &result) {
	const auto &data = result.c_messages_affectedMessages();
	ptsUpdateAndApply(data.vpts.v, data.vpts_count.v);
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

	if (state.id == audioId && (audioId.type() == AudioMsgId::Type::Song || audioId.type() == AudioMsgId::Type::Voice)) {
		if (!Media::Player::IsStoppedOrStopping(state.state)) {
			createPlayer();
		}
	}

	if (auto item = App::histItemById(audioId.contextId())) {
		Auth().data().requestItemRepaint(item);
		item->audioTrackUpdated();
	}
	if (auto items = InlineBots::Layout::documentItems()) {
		for (auto item : items->value(audioId.audio())) {
			item->update();
		}
	}
}

void MainWidget::switchToPanelPlayer() {
	if (_playerUsingPanel) return;
	_playerUsingPanel = true;

	_player->hide(anim::type::normal);
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
		_player->show(anim::type::normal);
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
		_player->hide(anim::type::normal);
	}
	_playerVolume.destroyDelayed();

	Media::Player::instance()->usePanelPlayer().notify(false, true);
	_playerPanel->hideIgnoringEnterEvents();
	_playerPlaylist->hideIgnoringEnterEvents();
	Media::Player::instance()->stop(AudioMsgId::Type::Voice);
	Media::Player::instance()->stop(AudioMsgId::Type::Song);

	Shortcuts::disableMediaShortcuts();
}

void MainWidget::createPlayer() {
	if (_playerUsingPanel) {
		return;
	}
	if (!_player) {
		_player.create(this);
		_player->heightValue()
			| rpl::start_with_next(
				[this] { playerHeightUpdated(); },
				lifetime());
		_player->entity()->setCloseCallback([this] { closeBothPlayers(); });
		_playerVolume.create(this);
		_player->entity()->volumeWidgetCreated(_playerVolume);
		orderWidgets();
		if (_a_show.animating()) {
			_player->show(anim::type::instant);
			_player->setVisible(false);
			Shortcuts::enableMediaShortcuts();
		} else {
			_player->hide(anim::type::instant);
		}
	}
	if (_player && !_player->toggled()) {
		if (!_a_show.animating()) {
			_player->show(anim::type::normal);
			_playerHeight = _contentScrollAddToY = _player->contentHeight();
			updateControlsGeometry();
			Shortcuts::enableMediaShortcuts();
		}
	}
}

void MainWidget::playerHeightUpdated() {
	auto playerHeight = _player->contentHeight();
	if (playerHeight != _playerHeight) {
		_contentScrollAddToY += playerHeight - _playerHeight;
		_playerHeight = playerHeight;
		updateControlsGeometry();
	}
	if (!_playerHeight && _player->isHidden()) {
		auto state = Media::Player::mixer()->currentState(Media::Player::instance()->getActiveType());
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
	_callTopBar->heightValue()
		| rpl::start_with_next([this](int value) {
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
			Auth().data().requestItemRepaint(item);
		}
	}
	Auth().documentUpdated.notify(document, true);

	if (!document->loaded() && document->isAudioFile()) {
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

void MainWidget::mediaMarkRead(not_null<DocumentData*> data) {
	auto &items = App::documentItems();
	auto i = items.constFind(data);
	if (i != items.cend()) {
		mediaMarkRead({ i.value().begin(), i.value().end() });
	}
}

void MainWidget::mediaMarkRead(
		const base::flat_set<not_null<HistoryItem*>> &items) {
	QVector<MTPint> markedIds;
	base::flat_map<not_null<ChannelData*>, QVector<MTPint>> channelMarkedIds;
	markedIds.reserve(items.size());
	for (const auto item : items) {
		if (!item->isMediaUnread() || (item->out() && !item->mentionsMe())) {
			continue;
		}
		item->markMediaRead();
		if (item->id > 0) {
			if (const auto channel = item->history()->peer->asChannel()) {
				channelMarkedIds[channel].push_back(MTP_int(item->id));
			} else {
				markedIds.push_back(MTP_int(item->id));
			}
		}
	}
	if (!markedIds.isEmpty()) {
		MTP::send(
			MTPmessages_ReadMessageContents(MTP_vector<MTPint>(markedIds)),
			rpcDone(&MainWidget::messagesContentsRead));
	}
	for (const auto &channelIds : channelMarkedIds) {
		MTP::send(MTPchannels_ReadMessageContents(
			channelIds.first->inputChannel,
			MTP_vector<MTPint>(channelIds.second)));
	}
}

void MainWidget::mediaMarkRead(not_null<HistoryItem*> item) {
	if ((!item->out() || item->mentionsMe()) && item->isMediaUnread()) {
		item->markMediaRead();
		if (item->id > 0) {
			const auto ids = MTP_vector<MTPint>(1, MTP_int(item->id));
			if (const auto channel = item->history()->peer->asChannel()) {
				MTP::send(
					MTPchannels_ReadMessageContents(
						channel->inputChannel,
						ids));
			} else {
				MTP::send(
					MTPmessages_ReadMessageContents(ids),
					rpcDone(&MainWidget::messagesContentsRead));
			}
		}
	}
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

void MainWidget::insertCheckedServiceNotification(const TextWithEntities &message, const MTPMessageMedia &media, int32 date) {
	auto flags = MTPDmessage::Flag::f_entities | MTPDmessage::Flag::f_from_id | MTPDmessage_ClientFlag::f_clientside_unread;
	auto sending = TextWithEntities(), left = message;
	HistoryItem *item = nullptr;
	while (TextUtilities::CutPart(sending, left, MaxMessageSize)) {
		auto localEntities = TextUtilities::EntitiesToMTP(sending.entities);
		item = App::histories().addNewMessage(
			MTP_message(
				MTP_flags(flags),
				MTP_int(clientMsgId()),
				MTP_int(ServiceUserId),
				MTP_peerUser(MTP_int(Auth().userId())),
				MTPnullFwdHeader,
				MTPint(),
				MTPint(),
				MTP_int(date),
				MTP_string(sending.text),
				media,
				MTPnullMarkup,
				localEntities,
				MTPint(),
				MTPint(),
				MTPstring(),
				MTPlong()), NewMessageUnread);
	}
	if (item) {
		_history->peerMessagesUpdated(item->history()->peer->id);
	}
}

void MainWidget::serviceHistoryDone(const MTPmessages_Messages &msgs) {
	auto handleResult = [&](auto &&result) {
		App::feedUsers(result.vusers);
		App::feedChats(result.vchats);
		App::feedMsgs(result.vmessages, NewMessageLast);
	};

	switch (msgs.type()) {
	case mtpc_messages_messages:
		handleResult(msgs.c_messages_messages());
		break;

	case mtpc_messages_messagesSlice:
		handleResult(msgs.c_messages_messagesSlice());
		break;

	case mtpc_messages_channelMessages:
		LOG(("API Error: received messages.channelMessages! (MainWidget::serviceHistoryDone)"));
		handleResult(msgs.c_messages_channelMessages());
		break;

	case mtpc_messages_messagesNotModified:
		LOG(("API Error: received messages.messagesNotModified! (MainWidget::serviceHistoryDone)"));
		break;
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
		} else if (!_hider && _mainSection) {
			_mainSection->setInnerFocus();
		} else if (!_hider && _thirdSection) {
			_thirdSection->setInnerFocus();
		} else {
			dialogsActivate();
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
		auto req = MTP::send(MTPmessages_GetMessagesViews(i.key()->input, MTP_vector<MTPint>(ids), MTP_bool(true)), rpcDone(&MainWidget::viewsIncrementDone, ids), rpcFail(&MainWidget::viewsIncrementFail), 0, 5);
		_viewsIncrementRequests.insert(i.key(), req);
		i = _viewsToIncrement.erase(i);
	}
}

void MainWidget::viewsIncrementDone(QVector<MTPint> ids, const MTPVector<MTPint> &result, mtpRequestId req) {
	auto &v = result.v;
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

void MainWidget::ctrlEnterSubmitUpdated() {
	_history->updateFieldSubmitSettings();
}

void MainWidget::ui_showPeerHistory(
		PeerId peerId,
		const SectionShow &params,
		MsgId showAtMsgId) {
	if (auto peer = App::peerLoaded(peerId)) {
		if (peer->migrateTo()) {
			peer = peer->migrateTo();
			peerId = peer->id;
			if (showAtMsgId > 0) showAtMsgId = -showAtMsgId;
		}
		auto restriction = peer->restrictionReason();
		if (!restriction.isEmpty()) {
			if (params.activation != anim::activation::background) {
				Ui::show(Box<InformBox>(restriction));
			}
			return;
		}
	}

	_controller->dialogsListFocused().set(false, true);
	_a_dialogsWidth.finish();

	using Way = SectionShow::Way;
	auto way = params.way;
	bool back = (way == Way::Backward || !peerId);
	bool foundInStack = !peerId;
	if (foundInStack || (way == Way::ClearStack)) {
		for_const (auto &item, _stack) {
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
		if (auto historyPeer = _controller->historyPeer.current()) {
			if (way == Way::Forward && historyPeer->id == peerId) {
				way = Way::ClearStack;
			}
		}
	}

	auto wasActivePeer = activePeer();
	if (params.activation != anim::activation::background) {
		Ui::hideSettingsAndLayer();
	}
	if (_hider) {
		_hider->startHide();
		_hider = nullptr;
	}

	auto animatedShow = [&] {
		if (_a_show.animating()
			|| App::passcoded()
			|| (params.animated == anim::type::instant)) {
			return false;
		}
		if (!peerId) {
			if (Adaptive::OneColumn()) {
				return true;
			} else {
				return false;
			}
		}
		if (_history->isHidden()) {
			return (_mainSection != nullptr)
				|| (Adaptive::OneColumn() && !_dialogs->isHidden());
		}
		if (back || way == Way::Forward) {
			return true;
		}
		return false;
	};

	auto animationParams = animatedShow() ? prepareHistoryAnimation(peerId) : Window::SectionSlideParams();

	dlgUpdated();
	if (back || (way == Way::ClearStack)) {
		_peerInStack = nullptr;
		_msgIdInStack = 0;
	} else {
		// This may modify the current section, for example remove its contents.
		saveSectionInStack();
	}
	dlgUpdated();

	if (_history->peer() && _history->peer()->id != peerId && way != Way::Forward) {
		clearBotStartToken(_history->peer());
	}
	_history->showHistory(peerId, showAtMsgId);

	auto noPeer = !_history->peer();
	auto onlyDialogs = noPeer && Adaptive::OneColumn();
	if (_mainSection) {
		_mainSection->hide();
		_mainSection->deleteLater();
		_mainSection = nullptr;
	}

	updateControlsGeometry();
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
		if (!noPeer && wasActivePeer != activePeer()) {
			if (activePeer()->isChannel()) {
				activePeer()->asChannel()->ptsWaitingForShortPoll(
					WaitForChannelGetDifference);
			}
			_viewsIncremented.remove(activePeer());
		}
		if (Adaptive::OneColumn() && !_dialogs->isHidden()) _dialogs->hide();
		if (!_a_show.animating()) {
			if (!animationParams.oldContentCache.isNull()) {
				_history->showAnimated(
					back
						? Window::SlideDirection::FromLeft
						: Window::SlideDirection::FromRight,
					animationParams);
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

	if (!peerId) {
		_controller->activePeer = nullptr;
	}

	checkFloatPlayerVisibility();
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

PeerData *MainWidget::peer() {
	return _history->peer();
}

PeerData *MainWidget::activePeer() {
	return _history->peer() ? _history->peer() : _peerInStack;
}

MsgId MainWidget::activeMsgId() {
	return _history->peer() ? _history->msgId() : _msgIdInStack;
}

void MainWidget::saveSectionInStack() {
	if (_mainSection) {
		if (auto memento = _mainSection->createMemento()) {
			_stack.push_back(std::make_unique<StackItemSection>(
				std::move(memento)));
			_stack.back()->setThirdSectionWeak(_thirdSection.data());
		}
	} else if (_history->peer()) {
		_peerInStack = _history->peer();
		_msgIdInStack = _history->msgId();
		_stack.push_back(std::make_unique<StackItemHistory>(
			_peerInStack,
			_msgIdInStack,
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
	for (auto &instance : _playerFloats) {
		instance->widget->hide();
	}
	auto sectionTop = getThirdSectionTop();
	result.oldContentCache = _thirdSection->grabForShowAnimation(result);
	for (auto &instance : _playerFloats) {
		if (instance->visible) {
			instance->widget->show();
		}
	}
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

	for (auto &instance : _playerFloats) {
		instance->widget->hide();
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

	auto sectionTop = getMainSectionTop();
	if (selectingPeer() && Adaptive::OneColumn()) {
		result.oldContentCache = myGrab(this, QRect(
			0,
			sectionTop,
			_dialogsWidth,
			height() - sectionTop));
	} else if (_mainSection) {
		result.oldContentCache = _mainSection->grabForShowAnimation(result);
	} else {
		if (result.withTopBarShadow) {
			_history->grapWithoutTopBarShadow();
		} else {
			_history->grabStart();
		}
		if (Adaptive::OneColumn()) {
			result.oldContentCache = myGrab(this, QRect(
				0,
				sectionTop,
				_dialogsWidth,
				height() - sectionTop));
		} else {
			_sideShadow->hide();
			if (_thirdShadow) {
				_thirdShadow->hide();
			}
			result.oldContentCache = myGrab(this, QRect(
				_dialogsWidth,
				sectionTop,
				width() - _dialogsWidth,
				height() - sectionTop));
			_sideShadow->show();
			if (_thirdShadow) {
				_thirdShadow->show();
			}
		}
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
	for (auto &instance : _playerFloats) {
		if (instance->visible) {
			instance->widget->show();
		}
	}

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
	} else {
		if (auto layer = memento.createLayer(_controller, rect())) {
			_controller->showSpecialLayer(std::move(layer));
			return;
		}
	}

	if (params.activation != anim::activation::background) {
		Ui::hideSettingsAndLayer();
	}

	QPixmap animCache;

	_controller->dialogsListFocused().set(false, true);
	_a_dialogsWidth.finish();

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
			|| App::passcoded()
			|| (params.animated == anim::type::instant)
			|| memento.instant()) {
			return false;
		}
		if (Adaptive::OneColumn()
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
		if (_mainSection) {
			_mainSection->hide();
			_mainSection->deleteLater();
			_mainSection = nullptr;
		}
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
		settingSection->showAnimated(direction, animationParams);
	} else {
		settingSection->showFast();
	}

	if (settingSection.data() == _mainSection.data()) {
		_controller->activePeer = _mainSection->activePeer();
	}

	checkFloatPlayerVisibility();
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
	if (selectingPeer()) return;
	if (_stack.empty()) {
		_controller->clearSectionStack(params);
		if (App::wnd()) QTimer::singleShot(0, App::wnd(), SLOT(setInnerFocus()));
		return;
	}
	auto item = std::move(_stack.back());
	_stack.pop_back();
	if (auto currentHistoryPeer = _history->peer()) {
		clearBotStartToken(currentHistoryPeer);
	}
	_thirdSectionFromStack = item->takeThirdSectionMemento();
	if (item->type() == HistoryStackItem) {
		dlgUpdated();
		_peerInStack = nullptr;
		_msgIdInStack = 0;
		for (auto i = _stack.size(); i > 0;) {
			if (_stack[--i]->type() == HistoryStackItem) {
				auto historyItem = static_cast<StackItemHistory*>(_stack[i].get());
				_peerInStack = historyItem->peer();
				_msgIdInStack = historyItem->msgId;
				dlgUpdated();
				break;
			}
		}
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
	if (_callTopBar) {
		_callTopBar->raise();
	}
	if (_player) {
		_player->raise();
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
	_playerPlaylist->raise();
	_playerPanel->raise();
	for (auto &instance : _playerFloats) {
		instance->widget->raise();
	}
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
	for (auto &instance : _playerFloats) {
		instance->widget->hide();
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

	auto sectionTop = getMainSectionTop();
	if (Adaptive::OneColumn()) {
		result = myGrab(this, QRect(
			0,
			sectionTop,
			_dialogsWidth,
			height() - sectionTop));
	} else {
		_sideShadow->hide();
		if (_thirdShadow) {
			_thirdShadow->hide();
		}
		result = myGrab(this, QRect(
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
	if (playerPanelVisible) {
		_playerPanel->show();
	}
	if (playerPlaylistVisible) {
		_playerPlaylist->show();
	}
	if (_player) {
		_player->showShadow();
	}
	for (auto &instance : _playerFloats) {
		if (instance->visible) {
			instance->widget->show();
		}
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

void MainWidget::inviteToChannelDone(
		not_null<ChannelData*> channel,
		const MTPUpdates &updates) {
	sentUpdatesReceived(updates);
	Auth().api().requestParticipantsCountDelayed(channel);
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
	if (const auto activeHistory = _history->history()) {
		Auth().api().readServerHistory(activeHistory);
	}
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

int MainWidget::getMainSectionTop() const {
	return _callTopBarHeight + _playerHeight;
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
	for (auto &instance : _playerFloats) {
		instance->widget->hide();
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
				})), LayerOption::CloseOther, anim::type::instant);
			}
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
			if (_forwardConfirm) {
				_forwardConfirm = nullptr;
				Ui::hideLayer(anim::type::instant);
				if (_hider->wasOffered()) {
					_hider->setFocus();
				}
			}
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
	if (auto instance = currentFloatPlayer()) {
		checkFloatPlayerVisibility();
		if (instance->visible) {
			instance->widget->show();
		}
	}

	App::wnd()->checkHistoryActivation();
}

void MainWidget::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void MainWidget::updateControlsGeometry() {
	updateWindowAdaptiveLayout();
	if (Auth().data().dialogsWidthRatio() > 0) {
		_a_dialogsWidth.finish();
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
			if (Auth().data().tabbedSelectorSectionEnabled()) {
				_history->pushTabbedSelectorToThirdSection(params);
			} else if (Auth().data().thirdSectionInfoEnabled()) {
				_history->pushInfoToThirdSection(params);
			}
		}
	} else {
		_thirdSection.destroy();
		_thirdShadow.destroy();
	}
	auto mainSectionTop = getMainSectionTop();
	auto dialogsWidth = qRound(_a_dialogsWidth.current(_dialogsWidth));
	if (Adaptive::OneColumn()) {
		if (_callTopBar) {
			_callTopBar->resizeToWidth(dialogsWidth);
			_callTopBar->moveToLeft(0, 0);
		}
		if (_player) {
			_player->resizeToWidth(dialogsWidth);
			_player->moveToLeft(0, _callTopBarHeight);
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
		if (_player) {
			_player->resizeToWidth(mainSectionWidth);
			_player->moveToLeft(dialogsWidth, _callTopBarHeight);
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
	for (auto &instance : _playerFloats) {
		updateFloatPlayerPosition(instance.get());
	}
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
		Auth().data().setDialogsWidthRatio(newRatio);
	};
	auto moveFinishedCallback = [=] {
		if (Adaptive::OneColumn()) {
			return;
		}
		if (Auth().data().dialogsWidthRatio() > 0) {
			Auth().data().setDialogsWidthRatio(
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
		Auth().data().setThirdColumnWidth(newWidth);
	};
	auto moveFinishedCallback = [=] {
		if (!Adaptive::ThreeColumn() || !_thirdSection) {
			return;
		}
		Auth().data().setThirdColumnWidth(snap(
			Auth().data().thirdColumnWidth(),
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
	if (Auth().data().dialogsWidthRatio() > 0) {
		return;
	}
	auto dialogsWidth = _dialogsWidth;
	updateWindowAdaptiveLayout();
	if (!Auth().data().dialogsWidthRatio()
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
	not_null<PeerData*> peer)
-> std::unique_ptr<Window::SectionMemento> {
	if (_thirdSectionFromStack) {
		return std::move(_thirdSectionFromStack);
	}
	return std::make_unique<Info::Memento>(
		peer->id,
		Info::Memento::DefaultSection(peer));
}

void MainWidget::updateThirdColumnToCurrentPeer(
		PeerData *peer,
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
			&& !Auth().data().thirdSectionInfoEnabled()) {
			Auth().data().setThirdSectionInfoEnabled(true);
			Auth().saveDataDelayed();
		}

		_controller->showSection(
			std::move(*thirdSectionForCurrentMainSection(peer)),
			params.withThirdColumn());
	};
	auto switchTabbedFast = [&] {
		saveOldThirdSection();
		_history->pushTabbedSelectorToThirdSection(params);
	};
	if (Adaptive::ThreeColumn()
		&& Auth().data().tabbedSelectorSectionEnabled()
		&& peer) {
		if (!canWrite) {
			switchInfoFast();
			Auth().data().setTabbedSelectorSectionEnabled(true);
			Auth().data().setTabbedReplacedWithInfo(true);
		} else if (Auth().data().tabbedReplacedWithInfo()) {
			Auth().data().setTabbedReplacedWithInfo(false);
			switchTabbedFast();
		}
	} else {
		Auth().data().setTabbedReplacedWithInfo(false);
		if (!peer) {
			if (_thirdSection) {
				_thirdSection.destroy();
				_thirdShadow.destroy();
				updateControlsGeometry();
			}
		} else if (Adaptive::ThreeColumn()
			&& Auth().data().thirdSectionInfoEnabled()) {
			switchInfoFast();
		}
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
	if (e->type() == QEvent::FocusIn) {
		if (auto widget = qobject_cast<QWidget*>(o)) {
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
			_controller->showBackFromStack();
			return true;
		}
	} else if (e->type() == QEvent::Wheel && !_playerFloats.empty()) {
		for (auto &instance : _playerFloats) {
			if (instance->widget == o) {
				auto section = getFloatPlayerSection(
					instance->column);
				return section->wheelEventFromFloatPlayer(e);
			}
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
	auto layout = _controller->computeColumnLayout();
	auto dialogsWidthRatio = Auth().data().dialogsWidthRatio();

	// Check if we are in a single-column layout in a wide enough window
	// for the normal layout. If so, switch to the normal layout.
	if (layout.windowLayout == Adaptive::WindowLayout::OneColumn) {
		auto chatWidth = layout.chatWidth;
		//if (Auth().data().tabbedSelectorSectionEnabled()
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
		//	auto desiredChatWidth = qMax(sameRatioChatWidth, HistoryLayout::WideChatWidth());
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

	Auth().data().setDialogsWidthRatio(dialogsWidthRatio);

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

void MainWidget::onHistoryShown(History *history, MsgId atMsgId) {
//	updateControlsGeometry();
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

	while (!updateNotifySettingPeers.empty()) {
		auto peer = *updateNotifySettingPeers.begin();
		updateNotifySettingPeers.erase(updateNotifySettingPeers.begin());
		MTP::send(
			MTPaccount_UpdateNotifySettings(
				MTP_inputNotifyPeer(peer->input),
				peer->notifySerialize()),
			RPCResponseHandler(),
			0,
			updateNotifySettingPeers.empty() ? 0 : 10);
	}
}

void MainWidget::feedUpdateVector(const MTPVector<MTPUpdate> &updates, bool skipMessageIds) {
	for_const (auto &update, updates.v) {
		if (skipMessageIds && update.type() == mtpc_updateMessageID) continue;
		feedUpdate(update);
	}
}

void MainWidget::feedMessageIds(const MTPVector<MTPUpdate> &updates) {
	for_const (auto &update, updates.v) {
		if (update.type() == mtpc_updateMessageID) {
			feedUpdate(update);
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
			h->setUnreadMentionsCount(d.vunread_mentions_count.v);
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
		auto &vmsgs = d.vnew_messages.v;
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
		auto &d = diff.c_updates_channelDifferenceEmpty();
		nextRequestPts = d.vpts.v;
		isFinal = d.is_final();
	} break;

	case mtpc_updates_channelDifferenceTooLong: {
		auto &d = diff.c_updates_channelDifferenceTooLong();

		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);

		nextRequestPts = d.vpts.v;
		isFinal = d.is_final();
	} break;

	case mtpc_updates_channelDifference: {
		auto &d = diff.c_updates_channelDifference();

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
	auto &d = state.c_updates_state();
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

bool MainWidget::ptsUpdateAndApply(int32 pts, int32 ptsCount, const MTPUpdates &updates) {
	return _ptsWaiter.updateAndApply(nullptr, pts, ptsCount, updates);
}

bool MainWidget::ptsUpdateAndApply(int32 pts, int32 ptsCount, const MTPUpdate &update) {
	return _ptsWaiter.updateAndApply(nullptr, pts, ptsCount, update);
}

bool MainWidget::ptsUpdateAndApply(int32 pts, int32 ptsCount) {
	return _ptsWaiter.updateAndApply(nullptr, pts, ptsCount);
}

void MainWidget::feedDifference(const MTPVector<MTPUser> &users, const MTPVector<MTPChat> &chats, const MTPVector<MTPMessage> &msgs, const MTPVector<MTPUpdate> &other) {
	Auth().checkAutoLock();
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

	MTP::send(MTPupdates_GetDifference(MTP_flags(0), MTP_int(_ptsWaiter.current()), MTPint(), MTP_int(updDate), MTP_int(updQts)), rpcDone(&MainWidget::gotDifference), rpcFail(&MainWidget::failDifference));
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
	MTP::send(MTPupdates_GetChannelDifference(MTP_flags(flags), channel->inputChannel, filter, MTP_int(channel->pts()), MTP_int(MTPChannelGetDifferenceLimit)), rpcDone(&MainWidget::gotChannelDifference, channel), rpcFail(&MainWidget::failChannelDifference, channel));
}

void MainWidget::mtpPing() {
	MTP::ping();
}

void MainWidget::start(const MTPUser *self) {
	if (!self) {
		MTP::send(MTPusers_GetFullUser(MTP_inputUserSelf()), rpcDone(&MainWidget::startWithSelf));
		return;
	}
	if (!Auth().validateSelf(*self)) {
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
	Local::readFavedStickers();
	Local::readSavedGifs();
	_history->start();

	Messenger::Instance().checkStartUrl();
}

bool MainWidget::started() {
	return _started;
}

void MainWidget::openPeerByName(const QString &username, MsgId msgId, const QString &startToken) {
	Messenger::Instance().hideMediaView();

	PeerData *peer = App::peerByName(username);
	if (peer) {
		if (msgId == ShowAtGameShareMsgId) {
			if (peer->isUser() && peer->asUser()->botInfo && !startToken.isEmpty()) {
				peer->asUser()->botInfo->shareGameShortName = startToken;
				AddBotToGroupBoxController::Start(peer->asUser());
			} else {
				InvokeQueued(this, [this, peer] {
					_controller->showPeerHistory(
						peer->id,
						SectionShow::Way::Forward);
				});
			}
		} else if (msgId == ShowAtProfileMsgId && !peer->isChannel()) {
			if (peer->isUser() && peer->asUser()->botInfo && !peer->asUser()->botInfo->cantJoinGroups && !startToken.isEmpty()) {
				peer->asUser()->botInfo->startGroupToken = startToken;
				AddBotToGroupBoxController::Start(peer->asUser());
			} else if (peer->isUser() && peer->asUser()->botInfo) {
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
			if (peer->isUser() && peer->asUser()->botInfo) {
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
	} else {
		MTP::send(MTPcontacts_ResolveUsername(MTP_string(username)), rpcDone(&MainWidget::usernameResolveDone, qMakePair(msgId, startToken)), rpcFail(&MainWidget::usernameResolveFail, username));
	}
}

void MainWidget::joinGroupByHash(const QString &hash) {
	Messenger::Instance().hideMediaView();
	MTP::send(MTPmessages_CheckChatInvite(MTP_string(hash)), rpcDone(&MainWidget::inviteCheckDone, hash), rpcFail(&MainWidget::inviteCheckFail));
}

void MainWidget::stickersBox(const MTPInputStickerSet &set) {
	Messenger::Instance().hideMediaView();
	Ui::show(Box<StickerSetBox>(set));
}

void MainWidget::onSelfParticipantUpdated(ChannelData *channel) {
	auto history = App::historyLoaded(channel->id);
	if (_updatedChannels.contains(channel)) {
		_updatedChannels.remove(channel);
		if (!history) {
			history = App::history(channel);
		}
		if (history->isEmpty()) {
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
	return (_history->contentOverlapped(globalRect)
			|| _playerPanel->overlaps(globalRect)
			|| _playerPlaylist->overlaps(globalRect)
			|| (_playerVolume && _playerVolume->overlaps(globalRect)));
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
			AddBotToGroupBoxController::Start(peer->asUser());
		} else if (peer->isUser() && peer->asUser()->botInfo) {
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
		if (peer->isUser() && peer->asUser()->botInfo) {
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
		Ui::show(Box<InformBox>(lng_username_not_found(lt_user, name)));
	}
	return true;
}

void MainWidget::inviteCheckDone(QString hash, const MTPChatInvite &invite) {
	switch (invite.type()) {
	case mtpc_chatInvite: {
		auto &d = invite.c_chatInvite();

		auto participants = QVector<UserData*>();
		if (d.has_participants()) {
			auto &v = d.vparticipants.v;
			participants.reserve(v.size());
			for_const (auto &user, v) {
				if (auto feededUser = App::feedUser(user)) {
					participants.push_back(feededUser);
				}
			}
		}
		_inviteHash = hash;
		auto box = Box<ConfirmInviteBox>(
			qs(d.vtitle),
			d.is_channel() && !d.is_megagroup(),
			d.vphoto,
			d.vparticipants_count.v,
			participants);
		Ui::show(std::move(box));
	} break;

	case mtpc_chatInviteAlready: {
		auto &d = invite.c_chatInviteAlready();
		if (auto chat = App::feedChat(d.vchat)) {
			_controller->showPeerHistory(
				chat,
				SectionShow::Way::Forward);
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
	MTP::send(
		MTPmessages_ImportChatInvite(MTP_string(_inviteHash)),
		rpcDone(&MainWidget::inviteImportDone),
		rpcFail(&MainWidget::inviteImportFail));
}

void MainWidget::inviteImportDone(const MTPUpdates &updates) {
	App::main()->sentUpdatesReceived(updates);

	Ui::hideLayer();
	const QVector<MTPChat> *v = 0;
	switch (updates.type()) {
	case mtpc_updates: v = &updates.c_updates().vchats.v; break;
	case mtpc_updatesCombined: v = &updates.c_updatesCombined().vchats.v; break;
	default: LOG(("API Error: unexpected update cons %1 (MainWidget::inviteImportDone)").arg(updates.type())); break;
	}
	if (v && !v->isEmpty()) {
		auto &mtpChat = v->front();
		auto peerId = [&] {
			if (mtpChat.type() == mtpc_chat) {
				return peerFromChat(mtpChat.c_chat().vid.v);
			} else if (mtpChat.type() == mtpc_channel) {
				return peerFromChannel(mtpChat.c_channel().vid.v);
			}
			return PeerId(0);
		}();
		if (auto peer = App::peerLoaded(peerId)) {
			_controller->showPeerHistory(
				peer,
				SectionShow::Way::Forward);
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

void MainWidget::startWithSelf(const MTPUserFull &result) {
	Expects(result.type() == mtpc_userFull);
	auto &d = result.c_userFull();
	start(&d.vuser);
	if (auto user = App::self()) {
		Auth().api().processFullPeer(user, result);
	}
}

void MainWidget::applyNotifySetting(
		const MTPNotifyPeer &notifyPeer,
		const MTPPeerNotifySettings &settings,
		History *history) {
	if (notifyPeer.type() != mtpc_notifyPeer) {
		// Ignore those for now, they were not ever used.
		return;
	}

	const auto &data = notifyPeer.c_notifyPeer();
	const auto peer = App::peerLoaded(peerFromMTP(data.vpeer));
	if (!peer || !peer->notifyChange(settings)) {
		return;
	}

	updateNotifySettingsLocal(peer, history);
}

void MainWidget::updateNotifySettings(
		not_null<PeerData*> peer,
		Data::NotifySettings::MuteChange mute,
		Data::NotifySettings::SilentPostsChange silent,
		int muteForSeconds) {
	if (peer->notifyChange(mute, silent, muteForSeconds)) {
		updateNotifySettingsLocal(peer);
		updateNotifySettingPeers.insert(peer);
		updateNotifySettingTimer.start(NotifySettingSaveTimeout);
	}
}

void MainWidget::updateNotifySettingsLocal(
		not_null<PeerData*> peer,
		History *history) {
	if (!history) {
		history = App::historyLoaded(peer->id);
	}

	const auto muteFinishesIn = peer->notifyMuteFinishesIn();
	const auto muted = (muteFinishesIn > 0);
	if (history && history->changeMute(muted)) {
		// Notification already sent.
	} else {
		Notify::peerUpdatedDelayed(
			peer,
			Notify::PeerUpdate::Flag::NotificationsEnabled);
	}
	if (muted) {
		App::regMuted(peer, muteFinishesIn);
		if (history) {
			Auth().notifications().clearFromHistory(history);
		}
	} else {
		App::unregMuted(peer);
	}
}

void MainWidget::incrementSticker(DocumentData *sticker) {
	if (!sticker || !sticker->sticker()) return;
	if (sticker->sticker()->set.type() == mtpc_inputStickerSetEmpty) return;

	bool writeRecentStickers = false;
	auto &sets = Auth().data().stickerSetsRef();
	auto it = sets.find(Stickers::CloudRecentSetId);
	if (it == sets.cend()) {
		if (it == sets.cend()) {
			it = sets.insert(Stickers::CloudRecentSetId, Stickers::Set(Stickers::CloudRecentSetId, 0, lang(lng_recent_stickers), QString(), 0, 0, MTPDstickerSet_ClientFlag::f_special | 0));
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
	_history->updateRecentStickers();
}

void MainWidget::activate() {
	if (_a_show.animating()) return;
	if (!_mainSection) {
		if (_hider) {
			if (_hider->wasOffered()) {
				_hider->setFocus();
			} else {
				_dialogs->activate();
			}
        } else if (App::wnd() && !Ui::isLayerShown()) {
			if (!cSendPaths().isEmpty()) {
				showSendPathsLayer();
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

bool MainWidget::isActive() const {
	return !_isIdle && isVisible() && !_a_show.animating();
}

bool MainWidget::doWeReadServerHistory() const {
	return isActive() && !_mainSection && _history->doWeReadServerHistory();
}

bool MainWidget::doWeReadMentions() const {
	return isActive() && !_mainSection && _history->doWeReadMentions();
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
}

void MainWidget::updateOnline(bool gotOtherOffline) {
	if (this != App::main()) return;
	InvokeQueued(this, [] { Auth().checkAutoLock(); });

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
	if (isOnline != _lastWasOnline
		|| (isOnline && _lastSetOnline + Global::OnlineUpdatePeriod() <= ms)
		|| (isOnline && gotOtherOffline)) {
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
			Auth().api().saveDraftToCloudDelayed(history);
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

	Auth().checkAutoLock();

	if (mtpTypeId(*from) == mtpc_new_session_created) {
		try {
			MTPNewSession newSession;
			newSession.read(from, end);
		} catch (mtpErrorUnexpected &) {
		}
		updSeq = 0;
		MTP_LOG(0, ("getDifference { after new_session_created }%1").arg(cTestMode() ? " TESTMODE" : ""));
		return getDifference();
	} else {
		try {
			MTPUpdates updates;
			updates.read(from, end);

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
	for_const (auto &entity, entities.v) {
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
			for_const (const MTPint &userId, d.vaction.c_messageActionChatAddUser().vusers.v) {
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
		if (ptsUpdateAndApply(d.vpts.v, d.vpts_count.v, updates)) {
			// We could've added an item.
			// Better would be for history to be subscribed to new messages.
			_history->peerMessagesUpdated();

			// Update date as well.
			updSetState(0, d.vdate.v, updQts, updSeq);
		}
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
			if (noFrom) {
				Auth().api().requestFullPeer(App::chatLoaded(d.vchat_id.v));
			}
			return getDifference();
		}
		if (ptsUpdateAndApply(d.vpts.v, d.vpts_count.v, updates)) {
			// We could've added an item.
			// Better would be for history to be subscribed to new messages.
			_history->peerMessagesUpdated();

			// Update date as well.
			updSetState(0, d.vdate.v, updQts, updSeq);
		}
	} break;

	case mtpc_updateShortSentMessage: {
		auto &d = updates.c_updateShortSentMessage();
		if (!IsServerMsgId(d.vid.v)) {
			LOG(("API Error: Bad msgId got from server: %1").arg(d.vid.v));
		} else if (randomId) {
			PeerId peerId = 0;
			QString text;
			App::histSentDataByItem(randomId, peerId, text);

			auto wasAlready = peerId && (App::histItemById(peerToChannel(peerId), d.vid.v) != nullptr);
			feedUpdate(MTP_updateMessageID(d.vid, MTP_long(randomId))); // ignore real date
			if (peerId) {
				if (auto item = App::histItemById(peerToChannel(peerId), d.vid.v)) {
					if (d.has_entities() && !mentionUsersLoaded(d.ventities)) {
						Auth().api().requestMessageData(
							item->history()->peer->asChannel(),
							item->id,
							ApiWrap::RequestMessageDataCallback());
					}
					auto entities = d.has_entities() ? TextUtilities::EntitiesFromMTP(d.ventities.v) : EntitiesInText();
					item->setText({ text, entities });
					item->updateMedia(d.has_media() ? (&d.vmedia) : nullptr);
					item->addToUnreadMentions(AddToUnreadMentionsMethod::New);
					if (!wasAlready) {
						if (auto sharedMediaTypes = item->sharedMediaTypes()) {
							Auth().storage().add(Storage::SharedMediaAddNew(
								peerId,
								sharedMediaTypes,
								item->id));
						}
					}
				}
			}
		}

		if (ptsUpdateAndApply(d.vpts.v, d.vpts_count.v, updates)) {
			// Update date as well.
			updSetState(0, d.vdate.v, updQts, updSeq);
		}
	} break;

	case mtpc_updatesTooLong: {
		MTP_LOG(0, ("getDifference { good - updatesTooLong received }%1").arg(cTestMode() ? " TESTMODE" : ""));
		return getDifference();
	} break;
	}
}

void MainWidget::feedUpdate(const MTPUpdate &update) {
	switch (update.type()) {

	// New messages.
	case mtpc_updateNewMessage: {
		auto &d = update.c_updateNewMessage();

		DataIsLoadedResult isDataLoaded = allDataLoadedForMessage(d.vmessage);
		if (!requestingDifference() && isDataLoaded != DataIsLoadedResult::Ok) {
			MTP_LOG(0, ("getDifference { good - after not all data loaded in updateNewMessage }%1").arg(cTestMode() ? " TESTMODE" : ""));

			// This can be if this update was created by grouping
			// some short message update into an updates vector.
			return getDifference();
		}

		if (ptsUpdateAndApply(d.vpts.v, d.vpts_count.v, update)) {
			// We could've added an item.
			// Better would be for history to be subscribed to new messages.
			_history->peerMessagesUpdated();
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
			if (isDataLoaded == DataIsLoadedResult::FromNotLoaded && channel && channel->isMegagroup()) {
				if (channel->mgInfo->lastParticipants.size() < Global::ChatSizeMax() && (channel->mgInfo->lastParticipants.empty() || channel->mgInfo->lastParticipants.size() < channel->membersCount())) {
					Auth().api().requestLastParticipants(channel);
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
			} else if (channel->ptsUpdateAndApply(d.vpts.v, d.vpts_count.v, update)) {
				// We could've added an item.
				// Better would be for history to be subscribed to new messages.
				_history->peerMessagesUpdated();
			}
		} else {
			Auth().api().applyUpdateNoPtsCheck(update);

			// We could've added an item.
			// Better would be for history to be subscribed to new messages.
			_history->peerMessagesUpdated();
		}
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
					Auth().messageIdChanging.notify({ msgRow, d.vid.v }, true);
					msgRow->setId(d.vid.v);
					App::historyRegItem(msgRow);
					Auth().data().requestItemRepaint(msgRow);
				}
			}
			App::historyUnregRandom(d.vrandom_id.v);
		}
		App::historyUnregSentData(d.vrandom_id.v);
	} break;

	// Message contents being read.
	case mtpc_updateReadMessagesContents: {
		auto &d = update.c_updateReadMessagesContents();
		ptsUpdateAndApply(d.vpts.v, d.vpts_count.v, update);
	} break;

	case mtpc_updateChannelReadMessagesContents: {
		auto &d = update.c_updateChannelReadMessagesContents();
		auto channel = App::channelLoaded(d.vchannel_id.v);
		if (!channel) {
			if (!_byMinChannelTimer.isActive()) { // getDifference after timeout
				_byMinChannelTimer.start(WaitForSkippedTimeout);
			}
			return;
		}
		auto possiblyReadMentions = base::flat_set<MsgId>();
		for_const (auto &msgId, d.vmessages.v) {
			if (auto item = App::histItemById(channel, msgId.v)) {
				if (item->isMediaUnread()) {
					item->markMediaRead();
					Auth().data().requestItemRepaint(item);
				}
			} else {
				// Perhaps it was an unread mention!
				possiblyReadMentions.insert(msgId.v);
			}
		}
		Auth().api().checkForUnreadMentions(possiblyReadMentions, channel);
	} break;

	// Edited messages.
	case mtpc_updateEditMessage: {
		auto &d = update.c_updateEditMessage();
		ptsUpdateAndApply(d.vpts.v, d.vpts_count.v, update);
	} break;

	case mtpc_updateEditChannelMessage: {
		auto &d = update.c_updateEditChannelMessage();
		auto channel = App::channelLoaded(peerToChannel(peerFromMessage(d.vmessage)));

		if (channel && !_handlingChannelDifference) {
			if (channel->ptsRequesting()) { // skip global updates while getting channel difference
				return;
			} else {
				channel->ptsUpdateAndApply(d.vpts.v, d.vpts_count.v, update);
			}
		} else {
			Auth().api().applyUpdateNoPtsCheck(update);
		}
	} break;

	// Messages being read.
	case mtpc_updateReadHistoryInbox: {
		auto &d = update.c_updateReadHistoryInbox();
		ptsUpdateAndApply(d.vpts.v, d.vpts_count.v, update);
	} break;

	case mtpc_updateReadHistoryOutbox: {
		auto &d = update.c_updateReadHistoryOutbox();
		if (ptsUpdateAndApply(d.vpts.v, d.vpts_count.v, update)) {
			// We could've updated the double checks.
			// Better would be for history to be subscribed to outbox read events.
			_history->update();
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

	// Deleted messages.
	case mtpc_updateDeleteMessages: {
		auto &d = update.c_updateDeleteMessages();

		if (ptsUpdateAndApply(d.vpts.v, d.vpts_count.v, update)) {
			// We could've removed some items.
			// Better would be for history to be subscribed to removed messages.
			_history->peerMessagesUpdated();
		}
	} break;

	case mtpc_updateDeleteChannelMessages: {
		auto &d = update.c_updateDeleteChannelMessages();
		auto channel = App::channelLoaded(d.vchannel_id.v);

		if (channel && !_handlingChannelDifference) {
			if (channel->ptsRequesting()) { // skip global updates while getting channel difference
				return;
			} else if (channel->ptsUpdateAndApply(d.vpts.v, d.vpts_count.v, update)) {
				// We could've removed some items.
				// Better would be for history to be subscribed to removed messages.
				_history->peerMessagesUpdated();
			}
		} else {
			// We could've removed some items.
			// Better would be for history to be subscribed to removed messages.
			_history->peerMessagesUpdated();

			Auth().api().applyUpdateNoPtsCheck(update);
		}
	} break;

	case mtpc_updateWebPage: {
		auto &d = update.c_updateWebPage();

		// Update web page anyway.
		App::feedWebPage(d.vwebpage);
		_history->updatePreview();
		webPagesOrGamesUpdate();

		ptsUpdateAndApply(d.vpts.v, d.vpts_count.v, update);
	} break;

	case mtpc_updateChannelWebPage: {
		auto &d = update.c_updateChannelWebPage();

		// Update web page anyway.
		App::feedWebPage(d.vwebpage);
		_history->updatePreview();
		webPagesOrGamesUpdate();

		auto channel = App::channelLoaded(d.vchannel_id.v);
		if (channel && !_handlingChannelDifference) {
			if (channel->ptsRequesting()) { // skip global updates while getting channel difference
				return;
			} else {
				channel->ptsUpdateAndApply(d.vpts.v, d.vpts_count.v, update);
			}
		} else {
			Auth().api().applyUpdateNoPtsCheck(update);
		}
	} break;

	case mtpc_updateUserTyping: {
		auto &d = update.c_updateUserTyping();
		const auto userId = peerFromUser(d.vuser_id);
		const auto history = App::historyLoaded(userId);
		const auto user = App::userLoaded(d.vuser_id.v);
		if (history && user) {
			const auto when = requestingDifference() ? 0 : unixtime();
			App::histories().registerSendAction(history, user, d.vaction, when);
		}
	} break;

	case mtpc_updateChatUserTyping: {
		auto &d = update.c_updateChatUserTyping();
		const auto history = [&]() -> History* {
			if (auto chat = App::chatLoaded(d.vchat_id.v)) {
				return App::historyLoaded(chat->id);
			} else if (auto channel = App::channelLoaded(d.vchat_id.v)) {
				return App::historyLoaded(channel->id);
			}
			return nullptr;
		}();
		const auto user = (d.vuser_id.v == Auth().userId())
			? nullptr
			: App::userLoaded(d.vuser_id.v);
		if (history && user) {
			const auto when = requestingDifference() ? 0 : unixtime();
			App::histories().registerSendAction(history, user, d.vaction, when);
		}
	} break;

	case mtpc_updateChatParticipants: {
		App::feedParticipants(update.c_updateChatParticipants().vparticipants, true);
	} break;

	case mtpc_updateChatParticipantAdd: {
		App::feedParticipantAdd(update.c_updateChatParticipantAdd());
	} break;

	case mtpc_updateChatParticipantDelete: {
		App::feedParticipantDelete(update.c_updateChatParticipantDelete());
	} break;

	case mtpc_updateChatAdmins: {
		App::feedChatAdmins(update.c_updateChatAdmins());
	} break;

	case mtpc_updateChatParticipantAdmin: {
		App::feedParticipantAdmin(update.c_updateChatParticipantAdmin());
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
			Notify::peerUpdatedDelayed(user, Notify::PeerUpdate::Flag::UserOnlineChanged);
		}
		if (d.vuser_id.v == Auth().userId()) {
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
				user->setName(TextUtilities::SingleLine(qs(d.vfirst_name)), TextUtilities::SingleLine(qs(d.vlast_name)), user->nameOrPhone, TextUtilities::SingleLine(qs(d.vusername)));
			} else {
				user->setName(TextUtilities::SingleLine(user->firstName), TextUtilities::SingleLine(user->lastName), user->nameOrPhone, TextUtilities::SingleLine(qs(d.vusername)));
			}
		}
	} break;

	case mtpc_updateUserPhoto: {
		auto &d = update.c_updateUserPhoto();
		if (auto user = App::userLoaded(d.vuser_id.v)) {
			user->setPhoto(d.vphoto);
			user->loadUserpic();
			if (mtpIsTrue(d.vprevious) || !user->userpicPhotoId()) {
				Auth().storage().remove(Storage::UserPhotosRemoveAfter(
					user->bareId(),
					user->userpicPhotoId()));
			} else {
				Auth().storage().add(Storage::UserPhotosAddNew(
					user->bareId(),
					user->userpicPhotoId()));
			}
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

	case mtpc_updateConfig: {
		Messenger::Instance().mtp()->requestConfig();
	} break;

	case mtpc_updateUserPhone: {
		auto &d = update.c_updateUserPhone();
		if (auto user = App::userLoaded(d.vuser_id.v)) {
			auto newPhone = qs(d.vphone);
			if (newPhone != user->phone()) {
				user->setPhone(newPhone);
				user->setName(user->firstName, user->lastName, (user->contact || isServiceUser(user->id) || user->isSelf() || user->phone().isEmpty()) ? QString() : App::formatPhone(user->phone()), user->username);

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
		Calls::Current().handleUpdate(update.c_updatePhoneCall());
	} break;

	case mtpc_updateUserBlocked: {
		auto &d = update.c_updateUserBlocked();
		if (auto user = App::userLoaded(d.vuser_id.v)) {
			user->setBlockStatus(mtpIsTrue(d.vblocked) ? UserData::BlockStatus::Blocked : UserData::BlockStatus::NotBlocked);
		}
	} break;

	case mtpc_updateServiceNotification: {
		auto &d = update.c_updateServiceNotification();
		if (d.is_popup()) {
			Ui::show(Box<InformBox>(qs(d.vmessage)));
		} else {
			App::wnd()->serviceNotification({ qs(d.vmessage), TextUtilities::EntitiesFromMTP(d.ventities.v) }, d.vmedia);
			emit App::wnd()->checkNewAuthorization();
		}
	} break;

	case mtpc_updatePrivacy: {
		auto &d = update.c_updatePrivacy();
		Auth().api().handlePrivacyChange(d.vkey.type(), d.vrules);
	} break;

	case mtpc_updatePinnedDialogs: {
		auto &d = update.c_updatePinnedDialogs();
		if (d.has_order()) {
			auto allLoaded = true;
			auto &order = d.vorder.v;
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
					Assert(history != nullptr);
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

	case mtpc_updateChannel: {
		auto &d = update.c_updateChannel();
		if (auto channel = App::channelLoaded(d.vchannel_id.v)) {
			channel->inviter = 0;
			if (!channel->amIn()) {
				deleteConversation(channel, false);
			} else if (!channel->amCreator() && App::history(channel->id)) { // create history
				_updatedChannels.insert(channel, true);
				Auth().api().requestSelfParticipant(channel);
			}
		}
	} break;

	case mtpc_updateChannelPinnedMessage: {
		auto &d = update.c_updateChannelPinnedMessage();
		if (auto channel = App::channelLoaded(d.vchannel_id.v)) {
			channel->setPinnedMessageId(d.vid.v);
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

	case mtpc_updateChannelAvailableMessages: {
		auto &d = update.c_updateChannelAvailableMessages();
		if (auto channel = App::channelLoaded(d.vchannel_id.v)) {
			channel->setAvailableMinId(d.vavailable_min_id.v);
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
					auto &sets = Auth().data().stickerSetsRef();
					auto it = sets.find(s.vid.v);
					if (it == sets.cend()) {
						it = sets.insert(s.vid.v, Stickers::Set(s.vid.v, s.vaccess_hash.v, Stickers::GetSetTitle(s), qs(s.vshort_name), s.vcount.v, s.vhash.v, s.vflags.v | MTPDstickerSet::Flag::f_installed));
					} else {
						it->flags |= MTPDstickerSet::Flag::f_installed;
						if (it->flags & MTPDstickerSet::Flag::f_archived) {
							it->flags &= ~MTPDstickerSet::Flag::f_archived;
							writeArchived = true;
						}
					}
					auto inputSet = MTP_inputStickerSetID(MTP_long(it->id), MTP_long(it->access));
					auto &v = set.vdocuments.v;
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
					auto &packs = set.vpacks.v;
					for (auto i = 0, l = packs.size(); i != l; ++i) {
						if (packs[i].type() != mtpc_stickerPack) continue;
						auto &pack = packs.at(i).c_stickerPack();
						if (auto emoji = Ui::Emoji::Find(qs(pack.vemoticon))) {
							emoji = emoji->original();
							auto &stickers = pack.vdocuments.v;

							Stickers::Pack p;
							p.reserve(stickers.size());
							for (auto j = 0, c = stickers.size(); j != c; ++j) {
								auto doc = App::document(stickers[j].v);
								if (!doc || !doc->sticker()) continue;

								p.push_back(doc);
							}
							it->emoji.insert(emoji, p);
						}
					}

					auto &order = Auth().data().stickerSetsOrderRef();
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
					Auth().data().markStickersUpdated();
				}
			}
		}
	} break;

	case mtpc_updateStickerSetsOrder: {
		auto &d = update.c_updateStickerSetsOrder();
		if (!d.is_masks()) {
			auto &order = d.vorder.v;
			auto &sets = Auth().data().stickerSets();
			Stickers::Order result;
			for (int i = 0, l = order.size(); i < l; ++i) {
				if (sets.constFind(order.at(i).v) == sets.cend()) {
					break;
				}
				result.push_back(order.at(i).v);
			}
			if (result.size() != Auth().data().stickerSetsOrder().size() || result.size() != order.size()) {
				Auth().data().setLastStickersUpdate(0);
				Auth().api().updateStickers();
			} else {
				Auth().data().stickerSetsOrderRef() = std::move(result);
				Local::writeInstalledStickers();
				Auth().data().markStickersUpdated();
			}
		}
	} break;

	case mtpc_updateStickerSets: {
		Auth().data().setLastStickersUpdate(0);
		Auth().api().updateStickers();
	} break;

	case mtpc_updateRecentStickers: {
		Auth().data().setLastRecentStickersUpdate(0);
		Auth().api().updateStickers();
	} break;

	case mtpc_updateFavedStickers: {
		Auth().data().setLastFavedStickersUpdate(0);
		Auth().api().updateStickers();
	} break;

	case mtpc_updateReadFeaturedStickers: {
		// We read some of the featured stickers, perhaps not all of them.
		// Here we don't know what featured sticker sets were read, so we
		// request all of them once again.
		Auth().data().setLastFeaturedStickersUpdate(0);
		Auth().api().updateStickers();
	} break;

	////// Cloud saved GIFs
	case mtpc_updateSavedGifs: {
		Auth().data().setLastSavedGifsUpdate(0);
		Auth().api().updateStickers();
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

	////// Cloud langpacks
	case mtpc_updateLangPack: {
		auto &langpack = update.c_updateLangPack();
		Lang::CurrentCloudManager().applyLangPackDifference(langpack.vdifference);
	} break;

	case mtpc_updateLangPackTooLong: {
		Lang::CurrentCloudManager().requestLangPackDifference();
	} break;

	}
}
