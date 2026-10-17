/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/controls/history_view_bottom_controls.h"

#include "apiwrap.h"
#include "boxes/star_gift_box.h"
#include "chat_helpers/message_field.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_chat_participant_status.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_peer.h"
#include "data/data_peer_values.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/notify/data_notify_settings.h"
#include "history/history.h"
#include "history/history_item_helpers.h"
#include "history/view/controls/compose_controls_common.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/widgets/buttons.h"
#include "window/section_widget.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"

namespace HistoryView {

BottomControls::BottomControls(
	QWidget *parent,
	BottomControlsDescriptor descriptor)
: RpWidget(parent)
, _controller(descriptor.controller)
, _history(descriptor.history)
, _peer(descriptor.history ? descriptor.history->peer.get() : nullptr)
, _repliesRootId(descriptor.repliesRootId)
, _topic(descriptor.topic)
, _sublist(descriptor.sublist)
, _mode(descriptor.mode) {
	setupButtons();
	setupOpenChatButton();
	setupAboutHiddenAuthor();
	setupPeerUpdates();
}

BottomControls::~BottomControls() = default;

void BottomControls::setCanSendMessages(bool value) {
	if (_canSendMessages == value) {
		return;
	}
	_canSendMessages = value;
	updateControlsVisibility();
}

void BottomControls::setTopic(Data::ForumTopic *topic) {
	if (_topic == topic) {
		return;
	}
	_topic = topic;
	refreshJoinGroupText();
	updateControlsVisibility();
}

void BottomControls::setInReportMode(bool value) {
	if (_inReportMode == value) {
		return;
	}
	_inReportMode = value;
	updateReportMessagesText(0);
	updateControlsVisibility();
}

void BottomControls::updateReportMessagesText(int selectedCount) {
	if (!_reportMessages) {
		return;
	}
	const auto transparent = Qt::WA_TransparentForMouseEvents;
	if (selectedCount == 0) {
		_reportMessages->clearState();
		_reportMessages->setAttribute(transparent);
		_reportMessages->setColorOverride(st::windowSubTextFg->c);
	} else if (_reportMessages->testAttribute(transparent)) {
		_reportMessages->setAttribute(transparent, false);
		_reportMessages->setColorOverride(std::nullopt);
	}
	_reportMessages->setText(selectedCount
		? tr::lng_report_messages_count(
			tr::now,
			lt_count,
			selectedCount,
			tr::upper)
		: tr::lng_report_messages_none(tr::now, tr::upper));
}

void BottomControls::applyPeerUpdate(Data::PeerUpdate::Flags flags) {
	using Flag = Data::PeerUpdate::Flag;
	if (flags & Flag::IsBlocked) {
		refreshUnblockText();
	}
	if (flags & Flag::ChannelAmIn) {
		refreshJoinChannelText();
	}
	if (flags & (Flag::ChannelAmIn | Flag::Rights)) {
		refreshJoinGroupText();
	}
	if (flags & (Flag::FullInfo
		| Flag::Rights
		| Flag::ChannelAmIn
		| Flag::StarsPerMessage)) {
		refreshGiftToChannelShown();
		refreshDirectMessageShown();
	}
	if (flags & Flag::Notifications) {
		refreshMuteUnmuteText();
	}
}

void BottomControls::updateControlsVisibility() {
	if (!_unblock
		&& !_joinGroup
		&& !_openChatButton
		&& !_aboutHiddenAuthor) {
		recomputeContentHeight();
		_isButtonActive = false;
		return;
	}
	const auto toggle = [&](Ui::FlatButton *shown) {
		const auto toggleOne = [&](Ui::FlatButton *button) {
			if (!button) {
				return;
			}
			if (button != shown) {
				button->hide();
			} else if (button->isHidden()) {
				button->clearState();
				button->show();
			}
		};
		toggleOne(_reportMessages.get());
		toggleOne(_joinChannel.get());
		toggleOne(_joinGroup.get());
		toggleOne(_muteUnmute.get());
		toggleOne(_botStart.get());
		toggleOne(_unblock.get());
	};
	const auto active = isButtonActive();
	if (active) {
		if (isReportMessages()) {
			toggle(_reportMessages.get());
		} else if (isBlocked()) {
			toggle(_unblock.get());
		} else if (isJoinChannel()) {
			toggle(_joinChannel.get());
		} else if (isJoinGroup()) {
			toggle(_joinGroup.get());
		} else if (isMuteUnmute()) {
			toggle(_muteUnmute.get());
		} else if (isBotStart()) {
			toggle(_botStart.get());
		} else {
			toggle(nullptr);
		}
	} else {
		toggle(nullptr);
	}
	_isButtonActive = active;
	recomputeContentHeight();
}

bool BottomControls::isButtonActive() const {
	if (_mode == BottomControlsMode::History) {
		return isBlocked()
			|| isJoinChannel()
			|| isMuteUnmute()
			|| isBotStart()
			|| isReportMessages();
	} else if (_mode == BottomControlsMode::Replies) {
		return isJoinGroup();
	}
	return false;
}

bool BottomControls::botStartShown() const {
	return _botStart && !_botStart->isHidden();
}

bool BottomControls::canSendTexts() const {
	return _canSendTexts;
}

bool BottomControls::hasOpenChatButton() const {
	return (_openChatButton != nullptr);
}

bool BottomControls::hasAboutHiddenAuthor() const {
	return (_aboutHiddenAuthor != nullptr);
}

int BottomControls::contentHeight() const {
	return _contentHeight.current();
}

rpl::producer<bool> BottomControls::isButtonActiveValue() const {
	return _isButtonActive.value();
}

rpl::producer<int> BottomControls::contentHeightValue() const {
	return _contentHeight.value();
}

rpl::producer<BottomControlsAction> BottomControls::actionRequests() const {
	return _actionRequests.events();
}

void BottomControls::setupButtons() {
	if (_mode == BottomControlsMode::History) {
		_unblock = std::make_unique<Ui::FlatButton>(
			this,
			tr::lng_unblock_button(tr::now).toUpper(),
			st::historyUnblock);
		_botStart = std::make_unique<Ui::FlatButton>(
			this,
			tr::lng_bot_start(tr::now).toUpper(),
			st::historyComposeButton);
		_joinChannel = std::make_unique<Ui::FlatButton>(
			this,
			tr::lng_profile_join_channel(tr::now).toUpper(),
			st::historyComposeButton);
		_muteUnmute = std::make_unique<Ui::FlatButton>(
			this,
			tr::lng_channel_mute(tr::now).toUpper(),
			st::historyComposeButton);
		_reportMessages = std::make_unique<Ui::FlatButton>(
			this,
			QString(),
			st::historyComposeButton);
		_unblock->hide();
		_botStart->hide();
		_joinChannel->hide();
		_muteUnmute->hide();
		_reportMessages->hide();
		_unblock->setClickedCallback([=] {
			_actionRequests.fire(BottomControlsAction::Unblock);
		});
		_botStart->setClickedCallback([=] {
			_actionRequests.fire(BottomControlsAction::BotStart);
		});
		_joinChannel->setClickedCallback([=] {
			_actionRequests.fire(BottomControlsAction::JoinChannel);
		});
		_muteUnmute->setClickedCallback([=] {
			_actionRequests.fire(BottomControlsAction::MuteUnmute);
		});
		_reportMessages->setClickedCallback([=] {
			_actionRequests.fire(BottomControlsAction::Report);
		});
		setupGiftToChannelButton();
		setupDirectMessageButton();
		refreshJoinChannelText();
		refreshGiftToChannelShown();
		refreshDirectMessageShown();
		refreshMuteUnmuteText();
		refreshUnblockText();
	} else if (_mode == BottomControlsMode::Replies
		&& _peer
		&& _peer->isChannel()) {
		_joinGroup = std::make_unique<Ui::FlatButton>(
			this,
			QString(),
			st::historyComposeButton);
		_joinGroup->hide();
		_joinGroup->setClickedCallback([=] {
			_actionRequests.fire(BottomControlsAction::JoinGroup);
		});
		refreshJoinGroupText();
	}
}

void BottomControls::setupGiftToChannelButton() {
	_giftToChannel = Ui::CreateChild<Ui::IconButton>(
		_muteUnmute.get(),
		st::historyGiftToChannel);
	_giftToChannel->setAccessibleName(tr::lng_gift_channel_title(tr::now));
	_giftToChannel->setClickedCallback([=] {
		Ui::ShowStarGiftBox(_controller, _peer);
	});
	setupOverlayIconButton(_giftToChannel, true, [=] {
		refreshGiftToChannelShown();
	});
}

void BottomControls::setupDirectMessageButton() {
	_directMessage = Ui::CreateChild<Ui::IconButton>(
		_muteUnmute.get(),
		st::historyDirectMessage);
	_directMessage->setAccessibleName(
		tr::lng_profile_direct_messages(tr::now));
	_directMessage->setClickedCallback([=] {
		if (const auto channel = _peer ? _peer->asChannel() : nullptr) {
			if (channel->invitePeekExpires()) {
				_controller->showToast(
					tr::lng_channel_invite_private(tr::now));
			} else if (const auto monoforum = channel->monoforumLink()) {
				_controller->showPeerHistory(
					monoforum,
					Window::SectionShow::Way::Forward);
			}
		}
	});
	setupOverlayIconButton(_directMessage, false, [=] {
		refreshDirectMessageShown();
	});
}

void BottomControls::setupOverlayIconButton(
		not_null<Ui::IconButton*> button,
		bool alignRight,
		Fn<void()> refresh) {
	widthValue() | rpl::on_next([=](int width) {
		if (alignRight) {
			button->moveToRight(0, 0, width);
		} else {
			button->moveToLeft(0, 0, width);
		}
	}, button->lifetime());
	rpl::combine(
		_muteUnmute->shownValue(),
		_joinChannel->shownValue()
	) | rpl::on_next([=](bool muteUnmute, bool joinChannel) {
		const auto newParent = (muteUnmute && !joinChannel)
			? _muteUnmute.get()
			: (joinChannel && !muteUnmute)
			? _joinChannel.get()
			: nullptr;
		if (newParent) {
			button->setParent(newParent);
			if (alignRight) {
				button->moveToRight(0, 0);
			} else {
				button->moveToLeft(0, 0);
			}
			refresh();
		}
	}, button->lifetime());
}

void BottomControls::setupOpenChatButton() {
	if (_mode != BottomControlsMode::Sublist
		|| !_sublist
		|| _sublist->sublistPeer()->isSavedHiddenAuthor()) {
		return;
	}
	if (_sublist->parentChat()) {
		_canSendTexts = true;
		return;
	}
	_openChatButton = std::make_unique<Ui::FlatButton>(
		this,
		(_sublist->sublistPeer()->isBroadcast()
			? tr::lng_saved_open_channel(tr::now)
			: _sublist->sublistPeer()->isUser()
			? tr::lng_saved_open_chat(tr::now)
			: tr::lng_saved_open_group(tr::now)),
		st::historyComposeButton);

	_openChatButton->setClickedCallback([=] {
		_controller->showPeerHistory(
			_sublist->sublistPeer(),
			Window::SectionShow::Way::Forward);
	});
}

void BottomControls::setupAboutHiddenAuthor() {
	if (_mode != BottomControlsMode::Sublist
		|| !_sublist
		|| !_sublist->sublistPeer()->isSavedHiddenAuthor()) {
		return;
	}
	if (_sublist->parentChat()) {
		_canSendTexts = true;
		return;
	}
	_aboutHiddenAuthor = std::make_unique<Ui::RpWidget>(this);
	_aboutHiddenAuthor->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(_aboutHiddenAuthor.get());
		auto rect = _aboutHiddenAuthor->rect();

		p.fillRect(rect, st::historyReplyBg);

		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			rect.marginsRemoved(
				QMargins(st::historySendPadding, 0, st::historySendPadding, 0)),
			tr::lng_saved_about_hidden(tr::now),
			style::al_center);
	}, _aboutHiddenAuthor->lifetime());
}

void BottomControls::setupPeerUpdates() {
	if (!_peer) {
		return;
	}
	using DefaultNotify = Data::DefaultNotify;
	rpl::merge(
		_peer->session().data().notifySettings().defaultUpdates(
			DefaultNotify::User),
		_peer->session().data().notifySettings().defaultUpdates(
			DefaultNotify::Group),
		_peer->session().data().notifySettings().defaultUpdates(
			DefaultNotify::Broadcast)
	) | rpl::on_next([=] {
		refreshMuteUnmuteText();
		updateControlsVisibility();
	}, lifetime());
}

void BottomControls::refreshJoinChannelText() {
	if (!_joinChannel) {
		return;
	}
	if (const auto channel = _peer->asChannel()) {
		_joinChannel->setText((channel->isBroadcast()
			? tr::lng_profile_join_channel(tr::now)
			: (channel->requestToJoin() && !channel->amCreator())
			? tr::lng_profile_apply_to_join_group(tr::now)
			: tr::lng_profile_join_group(tr::now)).toUpper());
	}
}

void BottomControls::refreshJoinGroupText() {
	if (!_joinGroup) {
		return;
	}
	if (const auto channel = _peer->asChannel()) {
		_joinGroup->setText((channel->isBroadcast()
			? tr::lng_profile_join_channel(tr::now)
			: (channel->requestToJoin() && !channel->amCreator())
			? tr::lng_profile_apply_to_join_group(tr::now)
			: tr::lng_profile_join_group(tr::now)).toUpper());
	}
	_canSendTexts = !isJoinGroup();
}

void BottomControls::refreshUnblockText() {
	if (!_unblock) {
		return;
	}
	_unblock->setText(((_peer->isUser()
		&& _peer->asUser()->isBot()
		&& !_peer->asUser()->isSupport())
			? tr::lng_restart_button(tr::now)
			: tr::lng_unblock_button(tr::now)).toUpper());
}

void BottomControls::refreshMuteUnmuteText() {
	if (!_muteUnmute) {
		return;
	}
	_muteUnmute->setText((_history->muted()
		? tr::lng_channel_unmute(tr::now)
		: tr::lng_channel_mute(tr::now)).toUpper());
}

void BottomControls::refreshGiftToChannelShown() {
	if (!_giftToChannel) {
		return;
	}
	const auto channel = _peer->asChannel();
	_giftToChannel->setVisible(channel
		&& channel->isBroadcast()
		&& channel->stargiftsAvailable());
}

void BottomControls::refreshDirectMessageShown() {
	if (!_directMessage) {
		return;
	}
	const auto channel = _peer->asChannel();
	const auto monoforum = channel ? channel->broadcastMonoforum() : nullptr;
	const auto visible = monoforum && !monoforum->monoforumDisabled();
	_directMessage->setVisible(visible);
	if (visible) {
		using Flags = Data::Flags<ChannelDataFlags>;
		_directMessageLifetime = monoforum->flagsValue(
		) | rpl::skip(
			1
		) | rpl::on_next([=](Flags::Change change) {
			if (change.diff & ChannelDataFlag::MonoforumDisabled) {
				refreshDirectMessageShown();
			}
		});
	}
}

void BottomControls::recomputeContentHeight() {
	const auto h = _openChatButton
		? _openChatButton->height()
		: _aboutHiddenAuthor
		? st::historyUnblock.height
		: isButtonActive()
		? st::historyComposeButton.height
		: 0;
	resize(width(), h);
	setVisible(h > 0);
	_contentHeight = h;
}

bool BottomControls::isBotStart() const {
	if (_mode != BottomControlsMode::History) {
		return false;
	}
	const auto user = _peer->asUser();
	if (!user || !user->isBot() || !_canSendMessages) {
		return false;
	} else if (!user->botInfo->startToken.isEmpty()) {
		return true;
	} else if (_history->isEmpty() && !_history->lastMessage()) {
		return true;
	}
	return false;
}

bool BottomControls::isBlocked() const {
	if (_mode != BottomControlsMode::History) {
		return false;
	}
	return _peer->isUser() && _peer->asUser()->isBlocked();
}

bool BottomControls::isJoinChannel() const {
	if (_mode != BottomControlsMode::History) {
		return false;
	}
	if (const auto channel = _peer->asChannel()) {
		return !channel->amIn() && !channel->isMonoforum();
	}
	return false;
}

bool BottomControls::isJoinGroup() const {
	if (_mode != BottomControlsMode::Replies) {
		return false;
	}
	const auto channel = _peer->asChannel();
	if (!channel) {
		return false;
	}
	const auto canSend = !channel->isForum()
		? Data::CanSendAnything(channel)
		: (_topic && Data::CanSendAnything(_topic));
	return !channel->amIn() && !canSend;
}

bool BottomControls::isMuteUnmute() const {
	if (_mode != BottomControlsMode::History) {
		return false;
	}
	return (_peer->isBroadcast() && !_peer->asChannel()->canPostMessages())
		|| (_peer->isGigagroup() && !Data::CanSendAnything(_peer))
		|| _peer->isRepliesChat()
		|| _peer->isVerifyCodes();
}

bool BottomControls::isReportMessages() const {
	return (_mode == BottomControlsMode::History) && _inReportMode;
}

bool BottomControls::isChoosingTheme() const {
	return false;
}

void BottomControls::resizeEvent(QResizeEvent *e) {
	RpWidget::resizeEvent(e);
	const auto w = width();
	if (_openChatButton) {
		_openChatButton->setGeometry(0, 0, w, _openChatButton->height());
		return;
	}
	if (_aboutHiddenAuthor) {
		_aboutHiddenAuthor->setGeometry(0, 0, w, st::historyUnblock.height);
		return;
	}
	const auto fullRect = QRect(0, 0, w, st::historyComposeButton.height);
	if (_botStart) {
		_botStart->setGeometry(fullRect);
	}
	if (_unblock) {
		_unblock->setGeometry(fullRect);
	}
	if (_joinChannel) {
		_joinChannel->setGeometry(fullRect);
	}
	if (_joinGroup) {
		_joinGroup->setGeometry(fullRect);
	}
	if (_muteUnmute) {
		_muteUnmute->setGeometry(fullRect);
	}
	if (_reportMessages) {
		_reportMessages->setGeometry(fullRect);
	}
}

} // namespace HistoryView
