/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_peer_menu.h"

#include "lang/lang_keys.h"
#include "boxes/confirm_box.h"
#include "boxes/mute_settings_box.h"
#include "boxes/add_contact_box.h"
#include "boxes/report_box.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/peers/manage_peer_box.h"
#include "boxes/peers/edit_peer_info_box.h"
#include "ui/toast/toast.h"
#include "core/tl_help.h"
#include "auth_session.h"
#include "apiwrap.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "observer_peer.h"
#include "styles/style_boxes.h"
#include "history/history.h"
#include "window/window_controller.h"
#include "info/info_memento.h"
#include "info/info_controller.h"
#include "info/feed/info_feed_channels_controllers.h"
#include "info/profile/info_profile_values.h"
#include "data/data_session.h"
#include "data/data_feed.h"
#include "dialogs/dialogs_key.h"

namespace Window {
namespace {

class Filler {
public:
	Filler(
		not_null<Controller*> controller,
		not_null<PeerData*> peer,
		const PeerMenuCallback &addAction,
		PeerMenuSource source);
	void fill();

private:
	bool showInfo();
	void addPinToggle();
	void addInfo();
	void addSearch();
	void addToggleUnreadMark();
	void addUserActions(not_null<UserData*> user);
	void addBlockUser(not_null<UserData*> user);
	void addChatActions(not_null<ChatData*> chat);
	void addChannelActions(not_null<ChannelData*> channel);

	not_null<Controller*> _controller;
	not_null<PeerData*> _peer;
	const PeerMenuCallback &_addAction;
	PeerMenuSource _source;

};

class FeedFiller {
public:
	FeedFiller(
		not_null<Controller*> controller,
		not_null<Data::Feed*> feed,
		const PeerMenuCallback &addAction,
		PeerMenuSource source);
	void fill();

private:
	bool showInfo();
	void addPinToggle();
	void addInfo();
	void addSearch();
	void addNotifications();
	void addUngroup();

	not_null<Controller*> _controller;
	not_null<Data::Feed*> _feed;
	const PeerMenuCallback &_addAction;
	PeerMenuSource _source;

};

History *FindWastedPin() {
	const auto &order = Auth().data().pinnedDialogsOrder();
	for (const auto pinned : order) {
		if (const auto history = pinned.history()) {
			if (history->peer->isChat()
				&& history->peer->asChat()->isDeactivated()
				&& !history->inChatList(Dialogs::Mode::All)) {
				return history;
			}
		}
	}
	return nullptr;
}

void AddChatMembers(not_null<ChatData*> chat) {
	if (chat->count >= Global::ChatSizeMax() && chat->amCreator()) {
		Ui::show(Box<ConvertToSupergroupBox>(chat));
	} else {
		AddParticipantsBoxController::Start(chat);
	}
}

bool PinnedLimitReached(Dialogs::Key key) {
	const auto pinnedCount = Auth().data().pinnedDialogsCount();
	const auto pinnedMax = Global::PinnedDialogsCountMax();
	if (pinnedCount < pinnedMax) {
		return false;
	}
	// Some old chat, that was converted, maybe is still pinned.
	if (auto wasted = FindWastedPin()) {
		Auth().data().setPinnedDialog(wasted, false);
		Auth().data().setPinnedDialog(key, true);
		Auth().api().savePinnedOrder();
	} else {
		auto errorText = lng_error_pinned_max(
			lt_count,
			pinnedMax);
		Ui::show(Box<InformBox>(errorText));
	}
	return true;
}

void TogglePinnedDialog(Dialogs::Key key) {
	const auto isPinned = !key.entry()->isPinnedDialog();
	if (isPinned && PinnedLimitReached(key)) {
		return;
	}

	Auth().data().setPinnedDialog(key, isPinned);
	auto flags = MTPmessages_ToggleDialogPin::Flags(0);
	if (isPinned) {
		flags |= MTPmessages_ToggleDialogPin::Flag::f_pinned;
	}
	//MTP::send(MTPmessages_ToggleDialogPin( // #feed
	//	MTP_flags(flags),
	//	key.history()
	//		? MTP_inputDialogPeer(key.history()->peer->input)
	//		: MTP_inputDialogPeerFeed(MTP_int(key.feed()->id()))));
	if (key.history()) {
		MTP::send(MTPmessages_ToggleDialogPin(
			MTP_flags(flags),
			MTP_inputDialogPeer(key.history()->peer->input)));
	}
	if (isPinned) {
		if (const auto main = App::main()) {
			main->dialogsToUp();
		}
	}

}

Filler::Filler(
	not_null<Controller*> controller,
	not_null<PeerData*> peer,
	const PeerMenuCallback &addAction,
	PeerMenuSource source)
: _controller(controller)
, _peer(peer)
, _addAction(addAction)
, _source(source) {
}

bool Filler::showInfo() {
	if (_source == PeerMenuSource::Profile || _peer->isSelf()) {
		return false;
	} else if (_controller->activeChatCurrent().peer() != _peer) {
		return true;
	} else if (!Adaptive::ThreeColumn()) {
		return true;
	} else if (
		!Auth().settings().thirdSectionInfoEnabled() &&
		!Auth().settings().tabbedReplacedWithInfo()) {
		return true;
	}
	return false;
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
	auto pinToggle = [=] {
		TogglePinnedDialog(App::history(peer));
	};
	auto pinAction = _addAction(pinText(isPinned), pinToggle);

	auto lifetime = Notify::PeerUpdateViewer(
		peer,
		Notify::PeerUpdate::Flag::PinnedChanged
	) | rpl::start_with_next([peer, pinAction, pinText] {
		auto isPinned = App::history(peer)->isPinnedDialog();
		pinAction->setText(pinText(isPinned));
	});

	Ui::AttachAsChild(pinAction, std::move(lifetime));
}

void Filler::addInfo() {
	auto controller = _controller;
	auto peer = _peer;
	auto infoKey = (peer->isChat() || peer->isMegagroup())
		? lng_context_view_group
		: (peer->isUser()
			? lng_context_view_profile
			: lng_context_view_channel);
	_addAction(lang(infoKey), [=] {
		controller->showPeerInfo(peer);
	});
}

void Filler::addSearch() {
	_addAction(lang(lng_profile_search_messages), [peer = _peer] {
		App::main()->searchInChat(App::history(peer));
	});
}

void Filler::addToggleUnreadMark() {
	const auto peer = _peer;
	const auto isUnread = [](not_null<PeerData*> peer) {
		if (const auto history = App::historyLoaded(peer)) {
			return (history->chatListUnreadCount() > 0)
				|| (history->chatListUnreadMark());
		}
		return false;
	};
	const auto label = [=](not_null<PeerData*> peer) {
		return lang(isUnread(peer)
			? lng_context_mark_read
			: lng_context_mark_unread);
	};
	auto action = _addAction(label(peer), [=] {
		const auto markAsRead = isUnread(peer);
		const auto handle = [&](not_null<History*> history) {
			if (markAsRead) {
				Auth().api().readServerHistory(history);
			} else {
				Auth().api().changeDialogUnreadMark(history, !markAsRead);
			}
		};
		const auto history = App::history(peer);
		handle(history);
		if (markAsRead) {
			if (const auto migrated = history->migrateSibling()) {
				handle(migrated);
			}
		}
	});

	auto lifetime = Notify::PeerUpdateViewer(
		_peer,
		Notify::PeerUpdate::Flag::UnreadViewChanged
	) | rpl::start_with_next([=] {
		action->setText(label(peer));
	});

	Ui::AttachAsChild(action, std::move(lifetime));
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
	auto blockAction = _addAction(blockText(user), [user] {
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
		Notify::PeerUpdate::Flag::UserIsBlocked
	) | rpl::start_with_next([=] {
		blockAction->setText(blockText(user));
	});

	Ui::AttachAsChild(blockAction, std::move(lifetime));

	if (user->blockStatus() == UserData::BlockStatus::Unknown) {
		Auth().api().requestFullPeer(user);
	}
}

void Filler::addUserActions(not_null<UserData*> user) {
	if (_source != PeerMenuSource::ChatsList) {
		if (user->isContact()) {
			if (!user->isSelf()) {
				_addAction(
					lang(lng_info_share_contact),
					[user] { PeerMenuShareContactBox(user); });
				_addAction(
					lang(lng_info_edit_contact),
					[user] { Ui::show(Box<AddContactBox>(user)); });
				_addAction(
					lang(lng_info_delete_contact),
					[user] { PeerMenuDeleteContact(user); });
			}
		} else if (user->canShareThisContact()) {
			if (!user->isSelf()) {
				_addAction(
					lang(lng_info_add_as_contact),
					[user] { PeerMenuAddContact(user); });
			}
			_addAction(
				lang(lng_info_share_contact),
				[user] { PeerMenuShareContactBox(user); });
		} else if (user->botInfo && !user->botInfo->cantJoinGroups) {
			_addAction(
				lang(lng_profile_invite_to_group),
				[user] { AddBotToGroupBoxController::Start(user); });
		}
		_addAction(
			lang(lng_profile_export_chat),
			[=] { PeerMenuExportChat(user); });
	}
	_addAction(
		lang(lng_profile_delete_conversation),
		DeleteAndLeaveHandler(user));
	_addAction(
		lang(lng_profile_clear_history),
		ClearHistoryHandler(user));
	if (!user->isInaccessible() && user != Auth().user()) {
		addBlockUser(user);
	}
}

void Filler::addChatActions(not_null<ChatData*> chat) {
	if (_source != PeerMenuSource::ChatsList) {
		if (chat->canEdit()) {
			_addAction(
				lang(lng_manage_group_title),
				[chat] { Ui::show(Box<EditPeerInfoBox>(chat)); });
			_addAction(
				lang(lng_profile_add_participant),
				[chat] { AddChatMembers(chat); });
		}
		_addAction(
			lang(lng_profile_export_chat),
			[=] { PeerMenuExportChat(chat); });
	}
	_addAction(
		lang(lng_profile_clear_and_exit),
		DeleteAndLeaveHandler(_peer));
	_addAction(
		lang(lng_profile_clear_history),
		ClearHistoryHandler(_peer));
}

void Filler::addChannelActions(not_null<ChannelData*> channel) {
	auto isGroup = channel->isMegagroup();
	if (!isGroup) {
		const auto feed = channel->feed();
		const auto grouped = (feed != nullptr);
		if (!grouped || feed->channels().size() > 1) {
			//_addAction( // #feed
			//	lang(grouped ? lng_feed_ungroup : lng_feed_group),
			//	[=] { ToggleChannelGrouping(channel, !grouped); });
		}
	}
	if (_source != PeerMenuSource::ChatsList) {
		if (ManagePeerBox::Available(channel)) {
			auto text = lang(isGroup
				? lng_manage_group_title
				: lng_manage_channel_title);
			_addAction(text, [channel] {
				Ui::show(Box<ManagePeerBox>(channel));
			});
		}
		if (channel->canAddMembers()) {
			_addAction(
				lang(lng_channel_add_members),
				[channel] { PeerMenuAddChannelMembers(channel); });
		}
		_addAction(
			lang(isGroup
				? lng_profile_export_chat
				: lng_profile_export_channel),
			[=] { PeerMenuExportChat(channel); });
	}
	if (channel->amIn()) {
		if (isGroup && !channel->isPublic()) {
			_addAction(
				lang(lng_profile_clear_history),
				ClearHistoryHandler(channel));
		}
		auto text = lang(isGroup
			? lng_profile_leave_group
			: lng_profile_leave_channel);
		_addAction(text, DeleteAndLeaveHandler(channel));
	} else {
		auto text = lang(isGroup
			? lng_profile_join_group
			: lng_profile_join_channel);
		_addAction(
			text,
			[channel] { Auth().api().joinChannel(channel); });
	}
	if (_source != PeerMenuSource::ChatsList) {
		auto needReport = !channel->amCreator()
			&& (!isGroup || channel->isPublic());
		if (needReport) {
			_addAction(lang(lng_profile_report), [channel] {
				Ui::show(Box<ReportBox>(channel));
			});
		}
	}
}

void Filler::fill() {
	if (_source == PeerMenuSource::ChatsList) {
		if (const auto history = App::historyLoaded(_peer)) {
			if (!history->useProxyPromotion()) {
				addPinToggle();
			}
		}
	}
	if (showInfo()) {
		addInfo();
	}
	if (_source != PeerMenuSource::Profile && !_peer->isSelf()) {
		PeerMenuAddMuteAction(_peer, _addAction);
	}
	if (_source == PeerMenuSource::ChatsList) {
		addSearch();
		addToggleUnreadMark();
	}

	if (const auto user = _peer->asUser()) {
		addUserActions(user);
	} else if (const auto chat = _peer->asChat()) {
		addChatActions(chat);
	} else if (const auto channel = _peer->asChannel()) {
		addChannelActions(channel);
	}
}

FeedFiller::FeedFiller(
	not_null<Controller*> controller,
	not_null<Data::Feed*> feed,
	const PeerMenuCallback &addAction,
	PeerMenuSource source)
	: _controller(controller)
	, _feed(feed)
	, _addAction(addAction)
	, _source(source) {
}

void FeedFiller::fill() {
	if (_source == PeerMenuSource::ChatsList) {
		addPinToggle();
	}
	if (showInfo()) {
		addInfo();
	}
	addNotifications();
	if (_source == PeerMenuSource::ChatsList) {
		addSearch();
	}
	addUngroup();
}

bool FeedFiller::showInfo() {
	if (_source == PeerMenuSource::Profile) {
		return false;
	} else if (_controller->activeChatCurrent().feed() != _feed) {
		return true;
	} else if (!Adaptive::ThreeColumn()) {
		return true;
	} else if (
		!Auth().settings().thirdSectionInfoEnabled() &&
		!Auth().settings().tabbedReplacedWithInfo()) {
		return true;
	}
	return false;
}

void FeedFiller::addPinToggle() {
	const auto feed = _feed;
	const auto isPinned = feed->isPinnedDialog();
	const auto pinText = [](bool isPinned) {
		return lang(isPinned
			? lng_context_unpin_from_top
			: lng_context_pin_to_top);
	};
	_addAction(pinText(isPinned), [=] {
		TogglePinnedDialog(feed);
	});
}

void FeedFiller::addInfo() {
	auto controller = _controller;
	auto feed = _feed;
	_addAction(lang(lng_context_view_feed_info), [=] {
		controller->showSection(Info::Memento(
			feed,
			Info::Section(Info::Section::Type::Profile)));
	});
}

void FeedFiller::addNotifications() {
	const auto feed = _feed;
	_addAction(lang(lng_feed_notifications), [=] {
		Info::FeedProfile::NotificationsController::Start(feed);
	});
}

void FeedFiller::addSearch() {
	const auto feed = _feed;
	_addAction(lang(lng_profile_search_messages), [=] {
		App::main()->searchInChat(feed);
	});
}

void FeedFiller::addUngroup() {
	const auto feed = _feed;
	//_addAction(lang(lng_feed_ungroup_all), [=] { // #feed
	//	PeerMenuUngroupFeed(feed);
	//});
}

} // namespace

void PeerMenuExportChat(not_null<PeerData*> peer) {
	Auth().data().startExport(peer);
}

void PeerMenuDeleteContact(not_null<UserData*> user) {
	auto text = lng_sure_delete_contact(
		lt_contact,
		App::peerName(user));
	auto deleteSure = [=] {
		Ui::hideLayer();
		MTP::send(
			MTPcontacts_DeleteContact(user->inputUser),
			App::main()->rpcDone(
				&MainWidget::deletedContact,
				user.get()));
	};
	auto box = Box<ConfirmBox>(
		text,
		lang(lng_box_delete),
		std::move(deleteSure));
	Ui::show(std::move(box));
}

void PeerMenuAddContact(not_null<UserData*> user) {
	Ui::show(Box<AddContactBox>(
		user->firstName,
		user->lastName,
		Auth().data().findContactPhone(user)));
}

void PeerMenuShareContactBox(not_null<UserData*> user) {
	const auto weak = std::make_shared<QPointer<PeerListBox>>();
	auto callback = [=](not_null<PeerData*> peer) {
		if (!peer->canWrite()) {
			Ui::show(Box<InformBox>(
				lang(lng_forward_share_cant)),
				LayerOption::KeepOther);
			return;
		} else if (peer->isSelf()) {
			auto options = ApiWrap::SendOptions(App::history(peer));
			Auth().api().shareContact(user, options);
			Ui::Toast::Show(lang(lng_share_done));
			if (auto strong = *weak) {
				strong->closeBox();
			}
			return;
		}
		auto recipient = peer->isUser()
			? peer->name
			: '\xAB' + peer->name + '\xBB';
		Ui::show(Box<ConfirmBox>(
			lng_forward_share_contact(lt_recipient, recipient),
			lang(lng_forward_send),
			[peer, user] {
				const auto history = App::history(peer);
				Ui::showPeerHistory(history, ShowAtTheEndMsgId);
				auto options = ApiWrap::SendOptions(history);
				Auth().api().shareContact(user, options);
			}), LayerOption::KeepOther);
	};
	*weak = Ui::show(Box<PeerListBox>(
		std::make_unique<ChooseRecipientBoxController>(std::move(callback)),
		[](not_null<PeerListBox*> box) {
			box->addButton(langFactory(lng_cancel), [box] {
				box->closeBox();
			});
		}));
}

QPointer<Ui::RpWidget> ShowForwardMessagesBox(
		MessageIdsList &&items,
		FnMut<void()> &&successCallback) {
	const auto weak = std::make_shared<QPointer<PeerListBox>>();
	auto callback = [
		ids = std::move(items),
		callback = std::move(successCallback),
		weak
	](not_null<PeerData*> peer) mutable {
		if (peer->isSelf()) {
			auto items = Auth().data().idsToItems(ids);
			if (!items.empty()) {
				auto options = ApiWrap::SendOptions(App::history(peer));
				options.generateLocal = false;
				Auth().api().forwardMessages(std::move(items), options, [] {
					Ui::Toast::Show(lang(lng_share_done));
				});
			}
		} else {
			App::main()->setForwardDraft(peer->id, std::move(ids));
		}
		if (const auto strong = *weak) {
			strong->closeBox();
		}
		if (callback) {
			callback();
		}
	};
	auto initBox = [](not_null<PeerListBox*> box) {
		box->addButton(langFactory(lng_cancel), [box] {
			box->closeBox();
		});
	};
	*weak = Ui::show(Box<PeerListBox>(
		std::make_unique<ChooseRecipientBoxController>(std::move(callback)),
		std::move(initBox)), LayerOption::KeepOther);
	return weak->data();
}

void PeerMenuAddChannelMembers(not_null<ChannelData*> channel) {
	if (!channel->isMegagroup()
		&& channel->membersCount() >= Global::ChatSizeMax()) {
		Ui::show(
			Box<MaxInviteBox>(channel),
			LayerOption::KeepOther);
		return;
	}
	auto callback = [channel](const MTPchannels_ChannelParticipants &result) {
		Auth().api().parseChannelParticipants(channel, result, [&](
				int availableCount,
				const QVector<MTPChannelParticipant> &list) {
			auto already = (
				list
			) | ranges::view::transform([&](auto &&p) {
				return TLHelp::ReadChannelParticipantUserId(p);
			}) | ranges::view::transform([](UserId userId) {
				return App::userLoaded(userId);
			}) | ranges::view::filter([](UserData *user) {
				return (user != nullptr);
			}) | ranges::to_vector;

			AddParticipantsBoxController::Start(
				channel,
				{ already.begin(), already.end() });
		});
	};
	Auth().api().requestChannelMembersForAdd(channel, callback);
}

void PeerMenuAddMuteAction(
		not_null<PeerData*> peer,
		const PeerMenuCallback &addAction) {
	Auth().data().requestNotifySettings(peer);
	const auto muteText = [](bool isMuted) {
		return lang(isMuted
			? lng_enable_notifications_from_tray
			: lng_disable_notifications_from_tray);
	};
	const auto muteAction = addAction(QString("-"), [=] {
		if (!Auth().data().notifyIsMuted(peer)) {
			Ui::show(Box<MuteSettingsBox>(peer));
		} else {
			Auth().data().updateNotifySettings(peer, 0);
		}
	});

	auto lifetime = Info::Profile::NotificationsEnabledValue(
		peer
	) | rpl::start_with_next([=](bool enabled) {
		muteAction->setText(muteText(!enabled));
	});

	Ui::AttachAsChild(muteAction, std::move(lifetime));
}
// #feed
//void PeerMenuUngroupFeed(not_null<Data::Feed*> feed) {
//	Ui::show(Box<ConfirmBox>(
//		lang(lng_feed_sure_ungroup_all),
//		lang(lng_feed_ungroup_sure),
//		[=] { Ui::hideLayer(); Auth().api().ungroupAllFromFeed(feed); }));
//}
//
//void ToggleChannelGrouping(not_null<ChannelData*> channel, bool group) {
//	const auto callback = [=] {
//		Ui::Toast::Show(lang(group
//			? lng_feed_channel_added
//			: lng_feed_channel_removed));
//	};
//	if (group) {
//		const auto feed = Auth().data().feed(Data::Feed::kId);
//		if (feed->channels().size() < 2) {
//			Info::FeedProfile::EditController::Start(feed, channel);
//			return;
//		}
//	}
//	Auth().api().toggleChannelGrouping(
//		channel,
//		group,
//		callback);
//}

Fn<void()> ClearHistoryHandler(not_null<PeerData*> peer) {
	return [peer] {
		const auto weak = std::make_shared<QPointer<ConfirmBox>>();
		const auto text = peer->isSelf()
			? lang(lng_sure_delete_saved_messages)
			: peer->isUser()
			? lng_sure_delete_history(lt_contact, peer->name)
			: lng_sure_delete_group_history(lt_group, peer->name);
		auto callback = [=] {
			if (auto strong = *weak) {
				strong->closeBox();
			}
			Auth().api().clearHistory(peer);
		};
		*weak = Ui::show(
			Box<ConfirmBox>(
				text,
				lang(lng_box_delete),
				st::attentionBoxButton,
				std::move(callback)),
			LayerOption::KeepOther);
	};
}

Fn<void()> DeleteAndLeaveHandler(not_null<PeerData*> peer) {
	return [peer] {
		const auto warningText = peer->isSelf()
			? lang(lng_sure_delete_saved_messages)
			: peer->isUser()
			? lng_sure_delete_history(lt_contact, peer->name)
			: peer->isChat()
			? lng_sure_delete_and_exit(lt_group, peer->name)
			: lang(peer->isMegagroup()
				? lng_sure_leave_group
				: lng_sure_leave_channel);
		const auto confirmText = lang(peer->isUser()
			? lng_box_delete
			: lng_box_leave);
		const auto &confirmStyle = peer->isChannel()
			? st::defaultBoxButton
			: st::attentionBoxButton;
		auto callback = [peer] {
			Ui::hideLayer();
			const auto controller = App::wnd()->controller();
			if (controller->activeChatCurrent().peer() == peer) {
				Ui::showChatsList();
			}
			if (peer->isUser()) {
				App::main()->deleteConversation(peer);
			} else if (const auto chat = peer->asChat()) {
				App::main()->deleteAndExit(chat);
			} else if (const auto channel = peer->asChannel()) {
				// Don't delete old history by default,
				// because Android app doesn't.
				//
				//if (auto migrateFrom = channel->migrateFrom()) {
				//	App::main()->deleteConversation(migrateFrom);
				//}
				Auth().api().leaveChannel(channel);
			}
		};
		Ui::show(
			Box<ConfirmBox>(
				warningText,
				confirmText,
				confirmStyle,
				std::move(callback)),
			LayerOption::KeepOther);
	};
}

void FillPeerMenu(
		not_null<Controller*> controller,
		not_null<PeerData*> peer,
		const PeerMenuCallback &callback,
		PeerMenuSource source) {
	Filler filler(controller, peer, callback, source);
	filler.fill();
}

void FillFeedMenu(
		not_null<Controller*> controller,
		not_null<Data::Feed*> feed,
		const PeerMenuCallback &callback,
		PeerMenuSource source) {
	FeedFiller filler(controller, feed, callback, source);
	filler.fill();
}

} // namespace Window
