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
#include "profile/profile_block_actions.h"

#include "styles/style_profile.h"
#include "styles/style_boxes.h"
#include "ui/widgets/buttons.h"
#include "boxes/confirm_box.h"
#include "boxes/report_box.h"
#include "mainwidget.h"
#include "observer_peer.h"
#include "apiwrap.h"
#include "auth_session.h"
#include "lang/lang_keys.h"
#include "profile/profile_channel_controllers.h"

namespace Profile {

constexpr auto kEnableSearchMembersAfterCount = 50;
constexpr auto kMaxChannelMembersDeleteAllowed = 1000;

using UpdateFlag = Notify::PeerUpdate::Flag;

ActionsWidget::ActionsWidget(QWidget *parent, PeerData *peer) : BlockWidget(parent, peer, lang(lng_profile_actions_section)) {
	auto observeEvents = UpdateFlag::ChannelAmIn
		| UpdateFlag::UserIsBlocked
		| UpdateFlag::BotCommandsChanged
		| UpdateFlag::MembersChanged;
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(observeEvents, [this](const Notify::PeerUpdate &update) {
		notifyPeerUpdated(update);
	}));

	validateBlockStatus();
	refreshButtons();
}

void ActionsWidget::notifyPeerUpdated(const Notify::PeerUpdate &update) {
	if (update.peer != peer()) {
		return;
	}

	auto needFullRefresh = [&update, this]() {
		if (update.flags & UpdateFlag::BotCommandsChanged) {
			if (_hasBotHelp != hasBotCommand(qsl("help")) || _hasBotSettings != hasBotCommand(qsl("settings"))) {
				return true;
			}
		}
		if (update.flags & UpdateFlag::MembersChanged) {
			if (peer()->isMegagroup()) {
				// Search members button could change.
				return true;
			}
		}
		return false;
	};
	if (needFullRefresh()) {
		refreshButtons();
	} else {
		if (update.flags & UpdateFlag::MembersChanged) {
			refreshDeleteChannel();
		}
		if (update.flags & UpdateFlag::ChannelAmIn) {
			refreshLeaveChannel();
		}
		if (update.flags & UpdateFlag::UserIsBlocked) {
			refreshBlockUser();
		}
		refreshVisibility();
	}

	contentSizeUpdated();
}

void ActionsWidget::validateBlockStatus() const {
	auto needFullPeer = [this]() {
		if (auto user = peer()->asUser()) {
			if (user->blockStatus() == UserData::BlockStatus::Unknown) {
				return true;
			} else if (user->botInfo && !user->botInfo->inited) {
				return true;
			}
		}
		return false;
	};
	if (needFullPeer()) {
		if (App::api()) App::api()->requestFullPeer(peer());
	}
}

Ui::LeftOutlineButton *ActionsWidget::addButton(const QString &text, const char *slot, const style::OutlineButton &st, int skipHeight) {
	auto result = new Ui::LeftOutlineButton(this, text, st);
	connect(result, SIGNAL(clicked()), this, slot);
	result->show();

	int top = buttonsBottom() + skipHeight;
	resizeButton(result, width(), top);

	_buttons.push_back(result);
	return result;
};

void ActionsWidget::resizeButton(Ui::LeftOutlineButton *button, int newWidth, int top) {
	int left = defaultOutlineButtonLeft();
	int availableWidth = newWidth - left - st::profileBlockMarginRight;
	accumulate_min(availableWidth, st::profileBlockOneLineWidthMax);
	button->resizeToWidth(availableWidth);
	button->moveToLeft(left, top);
}

void ActionsWidget::refreshButtons() {
	auto buttons = base::take(_buttons);
	for_const (auto &button, buttons) {
		delete button;
	}
	_blockUser = _leaveChannel = nullptr;

	if (auto user = peer()->asUser()) {
		if ((_hasBotHelp = hasBotCommand(qsl("help")))) {
			addButton(lang(lng_profile_bot_help), SLOT(onBotHelp()));
		}
		if ((_hasBotSettings = hasBotCommand(qsl("settings")))) {
			addButton(lang(lng_profile_bot_settings), SLOT(onBotSettings()));
		}
		addButton(lang(lng_profile_clear_history), SLOT(onClearHistory()));
		addButton(lang(lng_profile_delete_conversation), SLOT(onDeleteConversation()));
		if (user->botInfo) {
			addButton(lang(lng_profile_report), SLOT(onReport()), st::defaultLeftOutlineButton, st::profileBlockOneLineSkip);
		}
		refreshBlockUser();
	} else if (auto chat = peer()->asChat()) {
		if (chat->amCreator()) {
			addButton(lang(lng_profile_migrate_button), SLOT(onUpgradeToSupergroup()));
		}
		addButton(lang(lng_profile_clear_history), SLOT(onClearHistory()));
		addButton(lang(lng_profile_clear_and_exit), SLOT(onDeleteConversation()));
	} else if (auto channel = peer()->asChannel()) {
		if (channel->isMegagroup() && channel->membersCount() > kEnableSearchMembersAfterCount) {
			addButton(lang(lng_profile_search_members), SLOT(onSearchMembers()));
		}
		if (!channel->amCreator() && (!channel->isMegagroup() || channel->isPublic())) {
			addButton(lang(lng_profile_report), SLOT(onReport()));
		}
		refreshDeleteChannel();
		refreshLeaveChannel();
	}

	refreshVisibility();
}

void ActionsWidget::refreshVisibility() {
	setVisible(!_buttons.isEmpty());
}

QString ActionsWidget::getBlockButtonText() const {
	auto user = peer()->asUser();
	if (!user || (user->id == AuthSession::CurrentUserPeerId())) return QString();
	if (user->blockStatus() == UserData::BlockStatus::Unknown) return QString();

	if (user->isBlocked()) {
		if (user->botInfo) {
			return lang(lng_profile_unblock_bot);
		}
		return lang(lng_profile_unblock_user);
	} else if (user->botInfo) {
		return lang(lng_profile_block_bot);
	}
	return lang(lng_profile_block_user);
}

bool ActionsWidget::hasBotCommand(const QString &command) const {
	auto user = peer()->asUser();
	if (!user || !user->botInfo || user->botInfo->commands.isEmpty()) {
		return false;
	}

	for_const (auto &cmd, user->botInfo->commands) {
		if (!cmd.command.compare(command, Qt::CaseInsensitive)) {
			return true;
		}
	}
	return false;
}

void ActionsWidget::sendBotCommand(const QString &command) {
	auto user = peer()->asUser();
	if (user && user->botInfo && !user->botInfo->commands.isEmpty()) {
		for_const (auto &cmd, user->botInfo->commands) {
			if (!cmd.command.compare(command, Qt::CaseInsensitive)) {
				Ui::showPeerHistory(user, ShowAtTheEndMsgId);
				App::sendBotCommand(user, user, '/' + cmd.command);
				return;
			}
		}
	}

	// Command was not found.
	refreshButtons();
	contentSizeUpdated();
}

void ActionsWidget::refreshBlockUser() {
	if (auto user = peer()->asUser()) {
		auto blockText = getBlockButtonText();
		if (_blockUser) {
			if (blockText.isEmpty()) {
				_buttons.removeOne(_blockUser);
				delete _blockUser;
				_blockUser = nullptr;
			} else {
				_blockUser->setText(blockText);
			}
		} else if (!blockText.isEmpty()) {
			_blockUser = addButton(blockText, SLOT(onBlockUser()), st::attentionLeftOutlineButton, st::profileBlockOneLineSkip);
		}
	}
}

void ActionsWidget::refreshDeleteChannel() {
	if (auto channel = peer()->asChannel()) {
		if (channel->canDelete() && !_deleteChannel) {
			_deleteChannel = addButton(lang(channel->isMegagroup() ? lng_profile_delete_group : lng_profile_delete_channel), SLOT(onDeleteChannel()), st::attentionLeftOutlineButton);
		} else if (!channel->canDelete() && _deleteChannel) {
			_buttons.removeOne(_deleteChannel);
			delete _deleteChannel;
			_deleteChannel = nullptr;
		}
	}
}

void ActionsWidget::refreshLeaveChannel() {
	if (auto channel = peer()->asChannel()) {
		if (!channel->amCreator()) {
			if (channel->amIn() && !_leaveChannel) {
				_leaveChannel = addButton(lang(channel->isMegagroup() ? lng_profile_leave_group : lng_profile_leave_channel), SLOT(onLeaveChannel()));
			} else if (!channel->amIn() && _leaveChannel) {
				_buttons.removeOne(_leaveChannel);
				delete _leaveChannel;
				_leaveChannel = nullptr;
			}
		}
	}
}

int ActionsWidget::resizeGetHeight(int newWidth) {
	for_const (auto button, _buttons) {
		resizeButton(button, newWidth, button->y());
	}
	return buttonsBottom();
}

int ActionsWidget::buttonsBottom() const {
	if (_buttons.isEmpty()) {
		return contentTop();
	}
	auto lastButton = _buttons.back();
	return lastButton->y() + lastButton->height();
}

void ActionsWidget::onBotHelp() {
	sendBotCommand(qsl("help"));
}

void ActionsWidget::onBotSettings() {
	sendBotCommand(qsl("settings"));
}

void ActionsWidget::onClearHistory() {
	QString confirmation;
	if (auto user = peer()->asUser()) {
		confirmation = lng_sure_delete_history(lt_contact, App::peerName(peer()));
	} else if (auto chat = peer()->asChat()) {
		confirmation = lng_sure_delete_group_history(lt_group, App::peerName(peer()));
	}
	if (!confirmation.isEmpty()) {
		Ui::show(Box<ConfirmBox>(confirmation, lang(lng_box_delete), st::attentionBoxButton, base::lambda_guarded(this, [this] {
			Ui::hideLayer();
			App::main()->clearHistory(peer());
			Ui::showPeerHistory(peer(), ShowAtUnreadMsgId);
		})));
	}
}

void ActionsWidget::onDeleteConversation() {
	QString confirmation, confirmButton;
	if (auto user = peer()->asUser()) {
		confirmation = lng_sure_delete_history(lt_contact, App::peerName(peer()));
		confirmButton = lang(lng_box_delete);
	} else if (auto chat = peer()->asChat()) {
		confirmation = lng_sure_delete_and_exit(lt_group, App::peerName(peer()));
		confirmButton = lang(lng_box_leave);
	}
	if (!confirmation.isEmpty()) {
		Ui::show(Box<ConfirmBox>(confirmation, confirmButton, st::attentionBoxButton, base::lambda_guarded(this, [this] {
			Ui::hideLayer();
			Ui::showChatsList();
			if (auto user = peer()->asUser()) {
				App::main()->deleteConversation(peer());
			} else if (auto chat = peer()->asChat()) {
				App::main()->deleteAndExit(chat);
			}
		})));
	}
}

void ActionsWidget::onBlockUser() {
	if (auto user = peer()->asUser()) {
		if (user->isBlocked()) {
			App::api()->unblockUser(user);
		} else {
			App::api()->blockUser(user);
		}
	}
}

void ActionsWidget::onUpgradeToSupergroup() {
	if (auto chat = peer()->asChat()) {
		Ui::show(Box<ConvertToSupergroupBox>(chat));
	}
}

void ActionsWidget::onDeleteChannel() {
	if (auto channel = peer()->asChannel()) {
		if (channel->membersCount() > kMaxChannelMembersDeleteAllowed) {
			Ui::show(Box<InformBox>((channel->isMegagroup() ? lng_cant_delete_group : lng_cant_delete_channel)(lt_count, kMaxChannelMembersDeleteAllowed)));
			return;
		}
	}

	auto text = lang(peer()->isMegagroup() ? lng_sure_delete_group : lng_sure_delete_channel);
	Ui::show(Box<ConfirmBox>(text, lang(lng_box_delete), st::attentionBoxButton, base::lambda_guarded(this, [this] {
		Ui::hideLayer();
		Ui::showChatsList();
		if (auto chat = peer()->migrateFrom()) {
			App::main()->deleteAndExit(chat);
		}
		if (auto channel = peer()->asChannel()) {
			MTP::send(MTPchannels_DeleteChannel(channel->inputChannel), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::deleteChannelFailed));
		}
	})));
}

void ActionsWidget::onLeaveChannel() {
	auto channel = peer()->asChannel();
	if (!channel) return;

	auto text = lang(channel->isMegagroup() ? lng_sure_leave_group : lng_sure_leave_channel);
	Ui::show(Box<ConfirmBox>(text, lang(lng_box_leave), base::lambda_guarded(this, [this] {
		App::api()->leaveChannel(peer()->asChannel());
	})));
}

void ActionsWidget::onSearchMembers() {
	if (auto channel = peer()->asChannel()) {
		ParticipantsBoxController::Start(channel, ParticipantsBoxController::Role::Members);
	}
}

void ActionsWidget::onReport() {
	Ui::show(Box<ReportBox>(peer()));
}

} // namespace Profile
