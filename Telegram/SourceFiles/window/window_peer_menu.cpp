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
#include "window/window_peer_menu.h"

#include "lang/lang_keys.h"
#include "boxes/confirm_box.h"
#include "boxes/mute_settings_box.h"
#include "auth_session.h"
#include "apiwrap.h"
#include "mainwidget.h"
#include "observer_peer.h"
#include "styles/style_boxes.h"

namespace Window {
namespace{

class Filler {
public:
	Filler(
		not_null<PeerData*> peer,
		const PeerMenuCallback &callback,
		const PeerMenuOptions &options);
	void fill();

private:
	void addPinToggle();
	void addInfo();
	void addNotifications();
	void addSearch();
	void addUserActions(not_null<UserData*> user);
	void addBlockUser(not_null<UserData*> user);
	void addChatActions(not_null<ChatData*> chat);
	void addChannelActions(not_null<ChannelData*> channel);

	not_null<PeerData*> _peer;
	const PeerMenuCallback &_callback;
	const PeerMenuOptions &_options;

};

History *FindWastedPin() {
	auto order = App::histories().getPinnedOrder();
	for_const (auto pinned, order) {
		if (pinned->peer->isChat()
			&& pinned->peer->asChat()->isDeactivated()
			&& !pinned->inChatList(Dialogs::Mode::All)) {
			return pinned;
		}
	}
	return nullptr;
}

auto ClearHistoryHandler(not_null<PeerData*> peer) {
	return [peer] {
		auto text = peer->isUser() ? lng_sure_delete_history(lt_contact, peer->name) : lng_sure_delete_group_history(lt_group, peer->name);
		Ui::show(Box<ConfirmBox>(text, lang(lng_box_delete), st::attentionBoxButton, [peer] {
			if (!App::main()) return;

			Ui::hideLayer();
			App::main()->clearHistory(peer);
		}));
	};
}

auto DeleteAndLeaveHandler(not_null<PeerData*> peer) {
	return [peer] {
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
			} else if (auto chat = peer->asChat()) {
				App::main()->deleteAndExit(chat);
			} else if (auto channel = peer->asChannel()) {
				if (auto migrateFrom = channel->migrateFrom()) {
					App::main()->deleteConversation(migrateFrom);
				}
				Auth().api().leaveChannel(channel);
			}
		}));
	};
}

Filler::Filler(
	not_null<PeerData*> peer,
	const PeerMenuCallback &callback,
	const PeerMenuOptions &options)
: _peer(peer)
, _callback(callback)
, _options(options) {
}

void Filler::addPinToggle() {
	auto peer = _peer;
	auto isPinned = false;
	if (auto history = App::historyLoaded(peer)) {
		isPinned = history->isPinnedDialog();
	}
	auto pinText = [](bool isPinned) {
		return lang(isPinned
			? lng_context_unpin_from_top
			: lng_context_pin_to_top);
	};
	auto pinToggle = [peer] {
		auto history = App::history(peer);
		auto isPinned = !history->isPinnedDialog();
		auto pinnedCount = App::histories().pinnedCount();
		auto pinnedMax = Global::PinnedDialogsCountMax();
		if (isPinned && pinnedCount >= pinnedMax) {
			// Some old chat, that was converted to supergroup, maybe is still pinned.
			if (auto wasted = FindWastedPin()) {
				wasted->setPinnedDialog(false);
				history->setPinnedDialog(isPinned);
				App::histories().savePinnedToServer();
			} else {
				auto errorText = lng_error_pinned_max(
					lt_count,
					pinnedMax);
				Ui::show(Box<InformBox>(errorText));
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
	};
	auto pinAction = _callback(pinText(isPinned), pinToggle);

	auto lifetime = Notify::PeerUpdateViewer(
		peer,
		Notify::PeerUpdate::Flag::PinnedChanged)
		| rpl::start_with_next([peer, pinAction, pinText] {
			auto isPinned = App::history(peer)->isPinnedDialog();
			pinAction->setText(pinText(isPinned));
		});

	Ui::AttachAsChild(pinAction, std::move(lifetime));
}

void Filler::addInfo() {
	auto infoKey = (_peer->isChat() || _peer->isMegagroup())
		? lng_context_view_group
		: (_peer->isUser()
			? lng_context_view_profile
			: lng_context_view_channel);
	_callback(lang(infoKey), [peer = _peer] {
		Ui::showPeerProfile(peer);
	});
}

void Filler::addNotifications() {
	auto peer = _peer;
	auto muteText = [](bool isMuted) {
		return lang(isMuted
			? lng_enable_notifications_from_tray
			: lng_disable_notifications_from_tray);
	};
	auto muteAction = _callback(muteText(peer->isMuted()), [peer] {
		if (!peer->isMuted()) {
			Ui::show(Box<MuteSettingsBox>(peer));
		} else {
			App::main()->updateNotifySetting(
				peer,
				NotifySettingSetNotify);
		}
	});

	auto lifetime = Notify::PeerUpdateViewer(
		_peer,
		Notify::PeerUpdate::Flag::NotificationsEnabled)
		| rpl::start_with_next([=] {
			muteAction->setText(muteText(peer->isMuted()));
		});

	Ui::AttachAsChild(muteAction, std::move(lifetime));
}

void Filler::addSearch() {
	_callback(lang(lng_profile_search_messages), [peer = _peer] {
		App::main()->searchInPeer(peer);
	});
}

void Filler::addBlockUser(not_null<UserData*> user) {
	auto blockText = [](not_null<UserData*> user) {
		return lang(user->isBlocked()
			? (user->botInfo
				? lng_profile_unblock_bot
				: lng_profile_unblock_user)
			: (user->botInfo
				? lng_profile_block_bot
				: lng_profile_block_user));
	};
	auto blockAction = _callback(blockText(user), [user] {
		auto willBeBlocked = !user->isBlocked();
		auto handler = ::rpcDone([user, willBeBlocked](const MTPBool &result) {
			user->setBlockStatus(willBeBlocked
				? UserData::BlockStatus::Blocked
				: UserData::BlockStatus::NotBlocked);
		});
		if (willBeBlocked) {
			MTP::send(
				MTPcontacts_Block(user->inputUser),
				std::move(handler));
		} else {
			MTP::send(
				MTPcontacts_Unblock(user->inputUser),
				std::move(handler));
		}
	});

	auto lifetime = Notify::PeerUpdateViewer(
		_peer,
		Notify::PeerUpdate::Flag::UserIsBlocked)
		| rpl::start_with_next([=] {
			blockAction->setText(blockText(user));
		});

	Ui::AttachAsChild(blockAction, std::move(lifetime));

	if (user->blockStatus() == UserData::BlockStatus::Unknown) {
		Auth().api().requestFullPeer(user);
	}
}

void Filler::addUserActions(not_null<UserData*> user) {
	if (user->isContact()) {
		// edit contact
		// share contact
	} else if (user->canShareThisContact()) {
		// add contact
		// share contact
	}
	_callback(
		lang(lng_profile_delete_conversation),
		DeleteAndLeaveHandler(user));
	_callback(
		lang(lng_profile_clear_history),
		ClearHistoryHandler(user));
	if (!user->isInaccessible() && user != App::self()) {
		addBlockUser(user);
	}
}

void Filler::addChatActions(not_null<ChatData*> chat) {
	_callback(
		lang(lng_profile_clear_and_exit),
		DeleteAndLeaveHandler(_peer));
	_callback(
		lang(lng_profile_clear_history),
		ClearHistoryHandler(_peer));
}

void Filler::addChannelActions(not_null<ChannelData*> channel) {
	if (channel->amIn() && !channel->amCreator()) {
		auto leaveText = lang(channel->isMegagroup()
			? lng_profile_leave_group
			: lng_profile_leave_channel);
		_callback(leaveText, DeleteAndLeaveHandler(channel));
	}
}

void Filler::fill() {
	if (_options.pinToggle) {
		addPinToggle();
	}
	if (_options.showInfo) {
		addInfo();
	}
	addNotifications();
	if (_options.search) {
		addSearch();
	}

	if (auto user = _peer->asUser()) {
		addUserActions(user);
	} else if (auto chat = _peer->asChat()) {
		addChatActions(chat);
	} else if (auto channel = _peer->asChannel()) {
		addChannelActions(channel);
	}
}

} // namespace

void FillPeerMenu(
		not_null<PeerData*> peer,
		const PeerMenuCallback &callback,
		const PeerMenuOptions &options) {
	Filler filler(peer, callback, options);
	filler.fill();
}

} // namespace Window
