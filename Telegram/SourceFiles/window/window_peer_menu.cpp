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
#include "boxes/create_poll_box.h"
#include "boxes/peers/add_participants_box.h"
#include "boxes/peers/edit_contact_box.h"
#include "ui/boxes/report_box.h"
#include "ui/toast/toast.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/checkbox.h"
#include "ui/layers/generic_box.h"
#include "ui/toasts/common_toasts.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "apiwrap.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "api/api_chat_filters.h"
#include "api/api_updates.h"
#include "mtproto/mtproto_config.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_message.h" // GetErrorTextForSending.
#include "history/view/history_view_context_menu.h"
#include "window/window_adaptive.h" // Adaptive::isThreeColumn
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "support/support_helper.h"
#include "info/info_memento.h"
#include "info/info_controller.h"
#include "info/profile/info_profile_values.h"
#include "data/data_changes.h"
#include "data/data_session.h"
#include "data/data_folder.h"
#include "data/data_poll.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_drafts.h"
#include "data/data_user.h"
#include "data/data_scheduled_messages.h"
#include "data/data_histories.h"
#include "data/data_chat_filters.h"
#include "dialogs/dialogs_key.h"
#include "core/application.h"
#include "export/export_manager.h"
#include "boxes/peers/edit_peer_info_box.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_window.h" // st::windowMinWidth

#include <QtWidgets/QAction>

namespace Window {
namespace {

constexpr auto kArchivedToastDuration = crl::time(5000);
constexpr auto kMaxUnreadWithoutConfirmation = 10000;

void SetActionText(not_null<QAction*> action, rpl::producer<QString> &&text) {
	const auto lifetime = Ui::CreateChild<rpl::lifetime>(action.get());
	std::move(
		text
	) | rpl::start_with_next([=](const QString &actionText) {
		action->setText(actionText);
	}, *lifetime);
}

[[nodiscard]] bool IsUnreadHistory(not_null<History*> history) {
	return (history->chatListUnreadCount() > 0)
		|| (history->chatListUnreadMark());
}

void MarkAsReadHistory(not_null<History*> history) {
	const auto read = [&](not_null<History*> history) {
		if (IsUnreadHistory(history)) {
			history->peer->owner().histories().readInbox(history);
		}
	};
	read(history);
	if (const auto migrated = history->migrateSibling()) {
		read(migrated);
	}
}

void MarkAsReadChatList(not_null<Dialogs::MainList*> list) {
	auto mark = std::vector<not_null<History*>>();
	for (const auto &row : list->indexed()->all()) {
		if (const auto history = row->history()) {
			mark.push_back(history);
		}
	}
	ranges::for_each(mark, MarkAsReadHistory);
}

class Filler {
public:
	Filler(
		not_null<SessionController*> controller,
		Dialogs::EntryState request,
		const PeerMenuCallback &addAction);
	void fill();

private:
	using Section = Dialogs::EntryState::Section;

	[[nodiscard]] bool showInfo();
	[[nodiscard]] bool showHidePromotion();
	[[nodiscard]] bool showToggleArchived();
	[[nodiscard]] bool showTogglePin();
	void addHidePromotion();
	void addTogglePin();
	void addInfo();
	//void addSearch();
	void addToggleUnreadMark();
	void addToggleArchive();
	void addUserActions(not_null<UserData*> user);
	void addBlockUser(not_null<UserData*> user);
	void addChatActions(not_null<ChatData*> chat);
	void addChannelActions(not_null<ChannelData*> channel);
	void addTogglesForArchive();

	void addPollAction(not_null<PeerData*> peer);

	not_null<SessionController*> _controller;
	Dialogs::EntryState _request;
	PeerData *_peer = nullptr;
	Data::Folder *_folder = nullptr;
	const PeerMenuCallback &_addAction;

};

History *FindWastedPin(not_null<Data::Session*> data, Data::Folder *folder) {
	const auto &order = data->pinnedChatsOrder(folder, FilterId());
	for (const auto &pinned : order) {
		if (const auto history = pinned.history()) {
			if (history->peer->isChat()
				&& history->peer->asChat()->isDeactivated()
				&& !history->inChatList()) {
				return history;
			}
		}
	}
	return nullptr;
}

void AddChatMembers(
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChatData*> chat) {
	AddParticipantsBoxController::Start(navigation, chat);
}

bool PinnedLimitReached(Dialogs::Key key, FilterId filterId) {
	Expects(key.entry()->folderKnown());

	const auto entry = key.entry();
	const auto owner = &entry->owner();
	const auto folder = entry->folder();
	const auto pinnedCount = owner->pinnedChatsCount(folder, filterId);
	const auto pinnedMax = owner->pinnedChatsLimit(folder, filterId);
	if (pinnedCount < pinnedMax) {
		return false;
	}
	// Some old chat, that was converted, maybe is still pinned.
	const auto wasted = filterId ? nullptr : FindWastedPin(owner, folder);
	if (wasted) {
		owner->setChatPinned(wasted, FilterId(), false);
		owner->setChatPinned(key, FilterId(), true);
		entry->session().api().savePinnedOrder(folder);
	} else {
		const auto errorText = filterId
			? tr::lng_filters_error_pinned_max(tr::now)
			: tr::lng_error_pinned_max(
				tr::now,
				lt_count,
				pinnedMax);
		Ui::show(Box<InformBox>(errorText));
	}
	return true;
}

void TogglePinnedDialog(
		not_null<Window::SessionController*> controller,
		Dialogs::Key key) {
	if (!key.entry()->folderKnown()) {
		return;
	}
	const auto owner = &key.entry()->owner();
	const auto isPinned = !key.entry()->isPinnedDialog(0);
	if (isPinned && PinnedLimitReached(key, 0)) {
		return;
	}

	owner->setChatPinned(key, FilterId(), isPinned);
	const auto flags = isPinned
		? MTPmessages_ToggleDialogPin::Flag::f_pinned
		: MTPmessages_ToggleDialogPin::Flag(0);
	if (const auto history = key.history()) {
		history->session().api().request(MTPmessages_ToggleDialogPin(
			MTP_flags(flags),
			MTP_inputDialogPeer(key.history()->peer->input)
		)).done([=](const MTPBool &result) {
			owner->notifyPinnedDialogsOrderUpdated();
		}).send();
	} else if (const auto folder = key.folder()) {
		folder->session().api().request(MTPmessages_ToggleDialogPin(
			MTP_flags(flags),
			MTP_inputDialogPeerFolder(MTP_int(folder->id()))
		)).send();
	}
	if (isPinned) {
		controller->content()->dialogsToUp();
	}
}

void TogglePinnedDialog(
		not_null<Window::SessionController*> controller,
		Dialogs::Key key,
		FilterId filterId) {
	if (!filterId) {
		return TogglePinnedDialog(controller, key);
	}
	const auto owner = &key.entry()->owner();

	// This can happen when you remove this filter from another client.
	if (!ranges::contains(
		(&owner->session())->data().chatsFilters().list(),
		filterId,
		&Data::ChatFilter::id)) {
		Ui::Toast::Show(tr::lng_cant_do_this(tr::now));
		return;
	}

	const auto isPinned = !key.entry()->isPinnedDialog(filterId);
	if (isPinned && PinnedLimitReached(key, filterId)) {
		return;
	}

	owner->setChatPinned(key, filterId, isPinned);
	Api::SaveNewFilterPinned(&owner->session(), filterId);
	if (isPinned) {
		controller->content()->dialogsToUp();
	}
}

Filler::Filler(
	not_null<SessionController*> controller,
	Dialogs::EntryState request,
	const PeerMenuCallback &addAction)
: _controller(controller)
, _request(request)
, _peer(request.key.peer())
, _folder(request.key.folder())
, _addAction(addAction) {
}

bool Filler::showInfo() {
	if (_request.section == Section::Profile
		|| _peer->isSelf()
		|| _peer->isRepliesChat()) {
		return false;
	} else if (_controller->activeChatCurrent().peer() != _peer) {
		return true;
	} else if (!_controller->adaptive().isThreeColumn()) {
		return true;
	} else if (!Core::App().settings().thirdSectionInfoEnabled()
		&& !Core::App().settings().tabbedReplacedWithInfo()) {
		return true;
	}
	return false;
}

bool Filler::showHidePromotion() {
	if (_request.section != Section::ChatsList) {
		return false;
	}
	const auto history = _peer->owner().historyLoaded(_peer);
	return history
		&& history->useTopPromotion()
		&& !history->topPromotionType().isEmpty();
}

bool Filler::showToggleArchived() {
	if (_request.section != Section::ChatsList) {
		return false;
	}
	const auto history = _peer->owner().historyLoaded(_peer);
	if (history && history->useTopPromotion()) {
		return false;
	} else if (!_peer->isNotificationsUser() && !_peer->isSelf()) {
		return true;
	}
	return history && (history->folder() != nullptr);
}

bool Filler::showTogglePin() {
	if (_request.section != Section::ChatsList) {
		return false;
	}
	const auto history = _peer->owner().historyLoaded(_peer);
	return history && !history->fixedOnTopIndex();
}

void Filler::addHidePromotion() {
	const auto history = _peer->owner().history(_peer);
	_addAction(tr::lng_context_hide_psa(tr::now), [=] {
		history->cacheTopPromotion(false, QString(), QString());
		history->session().api().request(MTPhelp_HidePromoData(
			history->peer->input
		)).send();
	});
}

void Filler::addTogglePin() {
	const auto controller = _controller;
	const auto filterId = _request.filterId;
	const auto peer = _peer;
	const auto history = peer->owner().history(peer);
	const auto pinText = [=] {
		return history->isPinnedDialog(filterId)
			? tr::lng_context_unpin_from_top(tr::now)
			: tr::lng_context_pin_to_top(tr::now);
	};
	const auto pinToggle = [=] {
		TogglePinnedDialog(controller, history, filterId);
	};
	const auto pinAction = _addAction(pinText(), pinToggle);

	auto actionText = history->session().changes().historyUpdates(
		history,
		Data::HistoryUpdate::Flag::IsPinned
	) | rpl::map(pinText);
	SetActionText(pinAction, std::move(actionText));
}

void Filler::addInfo() {
	const auto controller = _controller;
	const auto peer = _peer;
	const auto text = (peer->isChat() || peer->isMegagroup())
		? tr::lng_context_view_group(tr::now)
		: (peer->isUser()
			? tr::lng_context_view_profile(tr::now)
			: tr::lng_context_view_channel(tr::now));
	_addAction(text, [=] {
		controller->showPeerInfo(peer);
	});
}

//void Filler::addSearch() {
//	const auto controller = _controller;
//	const auto peer = _peer;
//	_addAction(tr::lng_profile_search_messages(tr::now), [=] {
//		controller->content()->searchInChat(peer->owner().history(peer));
//	});
//}

void Filler::addToggleUnreadMark() {
	const auto peer = _peer;
	const auto history = peer->owner().history(peer);
	const auto label = [=] {
		return IsUnreadHistory(history)
			? tr::lng_context_mark_read(tr::now)
			: tr::lng_context_mark_unread(tr::now);
	};
	auto action = _addAction(label(), [=] {
		const auto markAsRead = IsUnreadHistory(history);
		if (markAsRead) {
			MarkAsReadHistory(history);
		} else {
			peer->owner().histories().changeDialogUnreadMark(
				history,
				!markAsRead);
		}
	});

	auto actionText = history->session().changes().historyUpdates(
		history,
		Data::HistoryUpdate::Flag::UnreadView
	) | rpl::map(label);
	SetActionText(action, std::move(actionText));
}

void Filler::addToggleArchive() {
	const auto peer = _peer;
	const auto history = peer->owner().history(peer);
	const auto isArchived = [=] {
		return (history->folder() != nullptr);
	};
	const auto label = [=] {
		return isArchived()
			? tr::lng_archived_remove(tr::now)
			: tr::lng_archived_add(tr::now);
	};
	const auto toggle = [=] {
		ToggleHistoryArchived(history, !isArchived());
	};
	const auto archiveAction = _addAction(label(), toggle);

	auto actionText = history->session().changes().historyUpdates(
		history,
		Data::HistoryUpdate::Flag::Folder
	) | rpl::map(label);
	SetActionText(archiveAction, std::move(actionText));
}

void Filler::addBlockUser(not_null<UserData*> user) {
	const auto window = &_controller->window();
	const auto blockText = [](not_null<UserData*> user) {
		return user->isBlocked()
			? ((user->isBot() && !user->isSupport())
				? tr::lng_profile_restart_bot(tr::now)
				: tr::lng_profile_unblock_user(tr::now))
			: ((user->isBot() && !user->isSupport())
				? tr::lng_profile_block_bot(tr::now)
				: tr::lng_profile_block_user(tr::now));
	};
	const auto blockAction = _addAction(blockText(user), [=] {
		if (user->isBlocked()) {
			PeerMenuUnblockUserWithBotRestart(user);
		} else if (user->isBot()) {
			user->session().api().blockPeer(user);
		} else {
			window->show(Box(
				PeerMenuBlockUserBox,
				window,
				user,
				v::null,
				v::null));
		}
	});

	auto actionText = _peer->session().changes().peerUpdates(
		_peer,
		Data::PeerUpdate::Flag::IsBlocked
	) | rpl::map([=] { return blockText(user); });
	SetActionText(blockAction, std::move(actionText));

	if (user->blockStatus() == UserData::BlockStatus::Unknown) {
		user->session().api().requestFullPeer(user);
	}
}

void Filler::addUserActions(not_null<UserData*> user) {
	const auto controller = _controller;
	const auto window = &_controller->window();
	if (_request.section != Section::ChatsList) {
		if (user->session().supportMode()) {
			_addAction("Edit support info", [=] {
				user->session().supportHelper().editInfo(controller, user);
			});
		}
		if (!user->isContact() && !user->isSelf() && !user->isBot()) {
			_addAction(
				tr::lng_info_add_as_contact(tr::now),
				[=] { window->show(Box(EditContactBox, controller, user)); });
		}
		if (user->canShareThisContact()) {
			_addAction(
				tr::lng_info_share_contact(tr::now),
				[=] { PeerMenuShareContactBox(controller, user); });
		}
		if (user->isContact() && !user->isSelf()) {
			_addAction(
				tr::lng_info_edit_contact(tr::now),
				[=] { window->show(Box(EditContactBox, controller, user)); });
			_addAction(
				tr::lng_info_delete_contact(tr::now),
				[=] { PeerMenuDeleteContact(user); });
		}
		if (user->isBot()
			&& !user->isRepliesChat()
			&& !user->botInfo->cantJoinGroups) {
			using AddBotToGroup = AddBotToGroupBoxController;
			_addAction(
				tr::lng_profile_invite_to_group(tr::now),
				[=] { AddBotToGroup::Start(user); });
		}
		addPollAction(user);
		if (user->canExportChatHistory()) {
			_addAction(
				tr::lng_profile_export_chat(tr::now),
				[=] { PeerMenuExportChat(user); });
		}
	}
	_addAction(
		tr::lng_profile_delete_conversation(tr::now),
		DeleteAndLeaveHandler(user));
	_addAction(
		tr::lng_profile_clear_history(tr::now),
		ClearHistoryHandler(user));
	if (!user->isInaccessible()
		&& user != user->session().user()
		&& !user->isRepliesChat()
		&& _request.section != Section::ChatsList) {
		addBlockUser(user);
	}
}

void Filler::addChatActions(not_null<ChatData*> chat) {
	const auto navigation = _controller;
	if (_request.section != Section::ChatsList) {
		if (EditPeerInfoBox::Available(chat)) {
			const auto text = tr::lng_manage_group_title(tr::now);
			_addAction(text, [=] {
				navigation->showEditPeerBox(chat);
			});
		}
		if (chat->canAddMembers()) {
			_addAction(
				tr::lng_channel_add_members(tr::now),
				[=] { AddChatMembers(navigation, chat); });
		}
		addPollAction(chat);
		if (chat->canExportChatHistory()) {
			_addAction(
				tr::lng_profile_export_chat(tr::now),
				[=] { PeerMenuExportChat(chat); });
		}
	}
	_addAction(
		tr::lng_profile_clear_and_exit(tr::now),
		DeleteAndLeaveHandler(_peer));
	_addAction(
		tr::lng_profile_clear_history(tr::now),
		ClearHistoryHandler(_peer));
	if (_request.section != Section::ChatsList) {
		if (!chat->amCreator()) {
			_addAction(tr::lng_profile_report(tr::now), [=] {
				HistoryView::ShowReportPeerBox(navigation, chat);
			});
		}
	}
}

void Filler::addChannelActions(not_null<ChannelData*> channel) {
	const auto isGroup = channel->isMegagroup();
	const auto navigation = _controller;
	if (_request.section != Section::ChatsList) {
		if (channel->isBroadcast()) {
			if (const auto chat = channel->linkedChat()) {
				_addAction(tr::lng_profile_view_discussion(tr::now), [=] {
					if (channel->invitePeekExpires()) {
						Ui::Toast::Show(
							tr::lng_channel_invite_private(tr::now));
						return;
					}
					navigation->showPeerHistory(
						chat,
						Window::SectionShow::Way::Forward);
				});
			}
		}
		if (EditPeerInfoBox::Available(channel)) {
			const auto text = isGroup
				? tr::lng_manage_group_title(tr::now)
				: tr::lng_manage_channel_title(tr::now);
			_addAction(text, [=] {
				navigation->showEditPeerBox(channel);
			});
		}
		if (channel->canAddMembers()) {
			_addAction(
				(channel->isMegagroup()
					? tr::lng_channel_add_members(tr::now)
					: tr::lng_channel_add_users(tr::now)),
				[=] { PeerMenuAddChannelMembers(navigation, channel); });
		}
		addPollAction(channel);
		if (channel->canExportChatHistory()) {
			_addAction(
				(isGroup
					? tr::lng_profile_export_chat(tr::now)
					: tr::lng_profile_export_channel(tr::now)),
				[=] { PeerMenuExportChat(channel); });
		}
	}
	if (channel->amIn()) {
		auto text = isGroup
			? tr::lng_profile_leave_group(tr::now)
			: tr::lng_profile_leave_channel(tr::now);
		_addAction(text, DeleteAndLeaveHandler(channel));
		if ((isGroup && !channel->isPublic())
			|| channel->canDeleteMessages()) {
			_addAction(
				tr::lng_profile_clear_history(tr::now),
				ClearHistoryHandler(channel));
		}
	} else {
		auto text = isGroup
			? tr::lng_profile_join_group(tr::now)
			: tr::lng_profile_join_channel(tr::now);
		_addAction(
			text,
			[=] { channel->session().api().joinChannel(channel); });
	}
	if (_request.section != Section::ChatsList) {
		if (!channel->amCreator()) {
			_addAction(tr::lng_profile_report(tr::now), [=] {
				HistoryView::ShowReportPeerBox(navigation, channel);
			});
		}
	}
}

void Filler::addPollAction(not_null<PeerData*> peer) {
	if (!peer->canSendPolls()) {
		return;
	}
	const auto controller = _controller;
	const auto source = (_request.section == Section::Scheduled)
		? Api::SendType::Scheduled
		: Api::SendType::Normal;
	const auto sendMenuType = (_request.section == Section::Scheduled)
		? SendMenu::Type::Disabled
		: (_request.section == Section::Replies)
		? SendMenu::Type::SilentOnly
		: SendMenu::Type::Scheduled;
	const auto flag = PollData::Flags();
	const auto replyToId = _request.currentReplyToId
		? _request.currentReplyToId
		: _request.rootId;
	auto callback = [=] {
		PeerMenuCreatePoll(
			controller,
			peer,
			replyToId,
			flag,
			flag,
			source,
			sendMenuType);
	};
	_addAction(tr::lng_polls_create(tr::now), std::move(callback));
}

void Filler::fill() {
	if (_folder) {
		addTogglesForArchive();
		return;
	} else if (_request.section == Section::Scheduled
		|| _request.section == Section::Replies) {
		addPollAction(_peer);
		return;
	}
	if (showHidePromotion()) {
		addHidePromotion();
	}
	if (showToggleArchived()) {
		addToggleArchive();
	}
	if (showTogglePin()) {
		addTogglePin();
	}
	if (showInfo()) {
		addInfo();
	}
	if (_request.section != Section::Profile && !_peer->isSelf()) {
		PeerMenuAddMuteAction(_peer, _addAction);
	}
	if (_request.section == Section::ChatsList) {
		//addSearch();
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

void Filler::addTogglesForArchive() {
	Expects(_folder != nullptr);

	if (_folder->id() != Data::Folder::kId) {
		return;
	}
	const auto controller = _controller;
	const auto hidden = controller->session().settings().archiveCollapsed();
	const auto text = hidden
		? tr::lng_context_archive_expand(tr::now)
		: tr::lng_context_archive_collapse(tr::now);
	_addAction(text, [=] {
		controller->session().settings().setArchiveCollapsed(!hidden);
		controller->session().saveSettingsDelayed();
	});

	_addAction(tr::lng_context_archive_to_menu(tr::now), [=] {
		Ui::Toast::Show(Ui::Toast::Config{
			.text = { tr::lng_context_archive_to_menu_info(tr::now) },
			.st = &st::windowArchiveToast,
			.durationMs = kArchivedToastDuration,
			.multiline = true,
		});

		controller->session().settings().setArchiveInMainMenu(
			!controller->session().settings().archiveInMainMenu());
		controller->session().saveSettingsDelayed();
	});

	MenuAddMarkAsReadChatListAction(
		[folder = _folder] { return folder->chatsList(); },
		_addAction);
}

} // namespace

void PeerMenuExportChat(not_null<PeerData*> peer) {
	Core::App().exportManager().start(peer);
}

void PeerMenuDeleteContact(not_null<UserData*> user) {
	const auto text = tr::lng_sure_delete_contact(
		tr::now,
		lt_contact,
		user->name);
	const auto deleteSure = [=] {
		Ui::hideLayer();
		user->session().api().request(MTPcontacts_DeleteContacts(
			MTP_vector<MTPInputUser>(1, user->inputUser)
		)).done([=](const MTPUpdates &result) {
			user->session().api().applyUpdates(result);
		}).send();
	};
	Ui::show(Box<ConfirmBox>(
		text,
		tr::lng_box_delete(tr::now),
		deleteSure));
}

void PeerMenuShareContactBox(
		not_null<Window::SessionNavigation*> navigation,
		not_null<UserData*> user) {
	const auto weak = std::make_shared<QPointer<PeerListBox>>();
	auto callback = [=](not_null<PeerData*> peer) {
		if (!peer->canWrite()) {
			Ui::show(Box<InformBox>(
				tr::lng_forward_share_cant(tr::now)),
				Ui::LayerOption::KeepOther);
			return;
		} else if (peer->isSelf()) {
			auto action = Api::SendAction(peer->owner().history(peer));
			action.clearDraft = false;
			user->session().api().shareContact(user, action);
			Ui::Toast::Show(tr::lng_share_done(tr::now));
			if (auto strong = *weak) {
				strong->closeBox();
			}
			return;
		}
		auto recipient = peer->isUser()
			? peer->name
			: '\xAB' + peer->name + '\xBB';
		Ui::show(Box<ConfirmBox>(
			tr::lng_forward_share_contact(tr::now, lt_recipient, recipient),
			tr::lng_forward_send(tr::now),
			[peer, user, navigation] {
				const auto history = peer->owner().history(peer);
				navigation->showPeerHistory(
					history,
					Window::SectionShow::Way::ClearStack,
					ShowAtTheEndMsgId);
				auto action = Api::SendAction(history);
				action.clearDraft = false;
				user->session().api().shareContact(user, action);
			}), Ui::LayerOption::KeepOther);
	};
	*weak = Ui::show(Box<PeerListBox>(
		std::make_unique<ChooseRecipientBoxController>(
			&navigation->session(),
			std::move(callback)),
		[](not_null<PeerListBox*> box) {
			box->addButton(tr::lng_cancel(), [=] {
				box->closeBox();
			});
		}));
}

void PeerMenuCreatePoll(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		MsgId replyToId,
		PollData::Flags chosen,
		PollData::Flags disabled,
		Api::SendType sendType,
		SendMenu::Type sendMenuType) {
	if (peer->isChannel() && !peer->isMegagroup()) {
		chosen &= ~PollData::Flag::PublicVotes;
		disabled |= PollData::Flag::PublicVotes;
	}
	const auto box = Ui::show(Box<CreatePollBox>(
		controller,
		chosen,
		disabled,
		sendType,
		sendMenuType));
	const auto lock = box->lifetime().make_state<bool>(false);
	box->submitRequests(
	) | rpl::start_with_next([=](const CreatePollBox::Result &result) {
		if (std::exchange(*lock, true)) {
			return;
		}
		auto action = Api::SendAction(peer->owner().history(peer));
		action.clearDraft = false;
		action.options = result.options;
		action.replyTo = replyToId;
		if (const auto localDraft = action.history->localDraft()) {
			action.clearDraft = localDraft->textWithTags.text.isEmpty();
		}
		const auto api = &peer->session().api();
		api->createPoll(result.poll, action, crl::guard(box, [=] {
			box->closeBox();
		}), crl::guard(box, [=](const MTP::Error &error) {
			*lock = false;
			box->submitFailed(tr::lng_attach_failed(tr::now));
		}));
	}, box->lifetime());
}

void PeerMenuBlockUserBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::Controller*> window,
		not_null<PeerData*> peer,
		std::variant<v::null_t, bool> suggestReport,
		std::variant<v::null_t, ClearChat, ClearReply> suggestClear) {
	const auto settings = peer->settings().value_or(PeerSettings(0));
	const auto reportNeeded = v::is_null(suggestReport)
		? ((settings & PeerSetting::ReportSpam) != 0)
		: v::get<bool>(suggestReport);

	const auto user = peer->asUser();
	const auto name = user ? user->shortName() : peer->name;
	if (user) {
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_blocked_list_confirm_text(
				lt_name,
				rpl::single(Ui::Text::Bold(name)),
				Ui::Text::WithEntities),
			st::blockUserConfirmation));

		box->addSkip(st::boxMediumSkip);
	}
	const auto report = reportNeeded
		? box->addRow(object_ptr<Ui::Checkbox>(
			box,
			tr::lng_report_spam(tr::now),
			true,
			st::defaultBoxCheckbox))
		: nullptr;

	if (report) {
		box->addSkip(st::boxMediumSkip);
	}

	const auto clear = v::is<ClearChat>(suggestClear)
		? box->addRow(object_ptr<Ui::Checkbox>(
			box,
			tr::lng_blocked_list_confirm_clear(tr::now),
			true,
			st::defaultBoxCheckbox))
		: v::is<ClearReply>(suggestClear)
		? box->addRow(object_ptr<Ui::Checkbox>(
			box,
			tr::lng_context_delete_msg(tr::now),
			true,
			st::defaultBoxCheckbox))
		: nullptr;
	if (clear) {
		box->addSkip(st::boxMediumSkip);
	}
	const auto allFromUser = v::is<ClearReply>(suggestClear)
		? box->addRow(object_ptr<Ui::Checkbox>(
			box,
			tr::lng_delete_all_from(tr::now),
			true,
			st::defaultBoxCheckbox))
		: nullptr;

	if (allFromUser) {
		box->addSkip(st::boxLittleSkip);
	}

	box->setTitle(tr::lng_blocked_list_confirm_title(
		lt_name,
		rpl::single(name)));

	box->addButton(tr::lng_blocked_list_confirm_ok(), [=] {
		const auto reportChecked = report && report->checked();
		const auto clearChecked = clear && clear->checked();
		const auto fromUserChecked = allFromUser && allFromUser->checked();

		box->closeBox();

		if (const auto clearReply = std::get_if<ClearReply>(&suggestClear)) {
			using Flag = MTPcontacts_BlockFromReplies::Flag;
			peer->session().api().request(MTPcontacts_BlockFromReplies(
				MTP_flags((clearChecked ? Flag::f_delete_message : Flag(0))
					| (fromUserChecked ? Flag::f_delete_history : Flag(0))
					| (reportChecked ? Flag::f_report_spam : Flag(0))),
				MTP_int(clearReply->replyId.msg)
			)).done([=](const MTPUpdates &result) {
				peer->session().updates().applyUpdates(result);
			}).send();
		} else {
			peer->session().api().blockPeer(peer);
			if (reportChecked) {
				peer->session().api().request(MTPmessages_ReportSpam(
					peer->input
				)).send();
			}
			if (clearChecked) {
				crl::on_main(&peer->session(), [=] {
					peer->session().api().deleteConversation(peer, false);
				});
				window->sessionController()->showBackFromStack();
			}
		}

		Ui::Toast::Show(
			tr::lng_new_contact_block_done(tr::now, lt_user, name));
	}, st::attentionBoxButton);

	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

void PeerMenuUnblockUserWithBotRestart(not_null<UserData*> user) {
	user->session().api().unblockPeer(user, [=] {
		if (user->isBot() && !user->isSupport()) {
			user->session().api().sendBotStart(user);
		}
	});
}

void BlockSenderFromRepliesBox(
		not_null<Ui::GenericBox*> box,
		not_null<SessionController*> controller,
		FullMsgId id) {
	const auto item = controller->session().data().message(id);
	Assert(item != nullptr);

	PeerMenuBlockUserBox(
		box,
		&controller->window(),
		item->senderOriginal(),
		true,
		Window::ClearReply{ id });
}

QPointer<Ui::RpWidget> ShowForwardMessagesBox(
		not_null<Window::SessionNavigation*> navigation,
		MessageIdsList &&items,
		FnMut<void()> &&successCallback) {
	const auto weak = std::make_shared<QPointer<PeerListBox>>();
	auto callback = [
		ids = std::move(items),
		callback = std::move(successCallback),
		weak,
		navigation
	](not_null<PeerData*> peer) mutable {
		if (peer->isSelf()) {
			auto items = peer->owner().idsToItems(ids);
			if (!items.empty()) {
				const auto api = &peer->session().api();
				auto action = Api::SendAction(peer->owner().history(peer));
				action.clearDraft = false;
				action.generateLocal = false;
				api->forwardMessages(std::move(items), action, [] {
					Ui::Toast::Show(tr::lng_share_done(tr::now));
				});
			}
		} else if (!navigation->parentController()->content()->setForwardDraft(peer->id, std::move(ids))) {
			return;
		}
		if (const auto strong = *weak) {
			strong->closeBox();
		}
		if (callback) {
			callback();
		}
	};
	auto initBox = [](not_null<PeerListBox*> box) {
		box->addButton(tr::lng_cancel(), [box] {
			box->closeBox();
		});
	};
	*weak = Ui::show(Box<PeerListBox>(
		std::make_unique<ChooseRecipientBoxController>(
			&navigation->session(),
			std::move(callback)),
		std::move(initBox)), Ui::LayerOption::KeepOther);
	return weak->data();
}

QPointer<Ui::RpWidget> ShowSendNowMessagesBox(
		not_null<Window::SessionNavigation*> navigation,
		not_null<History*> history,
		MessageIdsList &&items,
		FnMut<void()> &&successCallback) {
	const auto session = &navigation->session();
	const auto text = (items.size() > 1)
		? tr::lng_scheduled_send_now_many(tr::now, lt_count, items.size())
		: tr::lng_scheduled_send_now(tr::now);

	const auto error = GetErrorTextForSending(
		history->peer,
		session->data().idsToItems(items),
		TextWithTags());
	if (!error.isEmpty()) {
		Ui::ShowMultilineToast({
			.text = { error },
		});
		return { nullptr };
	}
	auto done = [
		=,
		list = std::move(items),
		callback = std::move(successCallback)
	](Fn<void()> &&close) mutable {
		close();
		auto ids = QVector<MTPint>();
		for (const auto item : session->data().idsToItems(list)) {
			if (item->allowsSendNow()) {
				ids.push_back(MTP_int(
					session->data().scheduledMessages().lookupId(item)));
			}
		}
		session->api().request(MTPmessages_SendScheduledMessages(
			history->peer->input,
			MTP_vector<MTPint>(ids)
		)).done([=](const MTPUpdates &result) {
			session->api().applyUpdates(result);
		}).fail([=](const MTP::Error &error) {
			session->api().sendMessageFail(error, history->peer);
		}).send();
		if (callback) {
			callback();
		}
	};
	return Ui::show(
		Box<ConfirmBox>(text, tr::lng_send_button(tr::now), std::move(done)),
		Ui::LayerOption::KeepOther).data();
}

void PeerMenuAddChannelMembers(
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChannelData*> channel) {
	if (!channel->isMegagroup()
		&& (channel->membersCount()
			>= channel->session().serverConfig().chatSizeMax)) {
		Ui::show(
			Box<MaxInviteBox>(channel),
			Ui::LayerOption::KeepOther);
		return;
	}
	const auto api = &channel->session().api();
	api->requestChannelMembersForAdd(channel, crl::guard(navigation, [=](
			const MTPchannels_ChannelParticipants &result) {
		api->parseChannelParticipants(channel, result, [&](
				int availableCount,
				const QVector<MTPChannelParticipant> &list) {
			auto already = (
				list
			) | ranges::views::transform([](const MTPChannelParticipant &p) {
				return p.match([](const MTPDchannelParticipantBanned &data) {
					return peerFromMTP(data.vpeer());
				}, [](const MTPDchannelParticipantLeft &data) {
					return peerFromMTP(data.vpeer());
				}, [](const auto &data) {
					return peerFromUser(data.vuser_id());
				});
			}) | ranges::views::transform([&](PeerId participantId) {
				return peerIsUser(participantId)
					? channel->owner().userLoaded(
						peerToUser(participantId))
					: nullptr;
			}) | ranges::views::filter([](UserData *user) {
				return (user != nullptr);
			}) | ranges::to_vector;

			AddParticipantsBoxController::Start(
				navigation,
				channel,
				{ already.begin(), already.end() });
		});
	}));
}

void ToggleMessagePinned(
		not_null<Window::SessionNavigation*> navigation,
		FullMsgId itemId,
		bool pin) {
	const auto item = navigation->session().data().message(itemId);
	if (!item || !item->canPin()) {
		return;
	}
	if (pin) {
		Ui::show(Box<PinMessageBox>(item->history()->peer, item->id));
	} else {
		const auto peer = item->history()->peer;
		const auto session = &peer->session();
		Ui::show(Box<ConfirmBox>(tr::lng_pinned_unpin_sure(tr::now), tr::lng_pinned_unpin(tr::now), crl::guard(session, [=] {
			Ui::hideLayer();
			session->api().request(MTPmessages_UpdatePinnedMessage(
				MTP_flags(MTPmessages_UpdatePinnedMessage::Flag::f_unpin),
				peer->input,
				MTP_int(itemId.msg)
			)).done([=](const MTPUpdates &result) {
				session->api().applyUpdates(result);
			}).send();
		})));
	}
}

void HidePinnedBar(
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer,
		Fn<void()> onHidden) {
	Ui::show(Box<ConfirmBox>(tr::lng_pinned_hide_all_sure(tr::now), tr::lng_pinned_hide_all_hide(tr::now), crl::guard(navigation, [=] {
		Ui::hideLayer();
		auto &session = peer->session();
		const auto migrated = peer->migrateFrom();
		const auto top = Data::ResolveTopPinnedId(peer, migrated);
		const auto universal = !top
			? int32(0)
			: (migrated && !top.channel)
			? (top.msg - ServerMaxMsgId)
			: top.msg;
		if (universal) {
			session.settings().setHiddenPinnedMessageId(peer->id, universal);
			session.saveSettingsDelayed();
			if (onHidden) {
				onHidden();
			}
		} else {
			session.api().requestFullPeer(peer);
		}
	})));
}

void UnpinAllMessages(
		not_null<Window::SessionNavigation*> navigation,
		not_null<History*> history) {
	Ui::show(Box<ConfirmBox>(tr::lng_pinned_unpin_all_sure(tr::now), tr::lng_pinned_unpin(tr::now), crl::guard(navigation, [=] {
		Ui::hideLayer();
		const auto api = &history->session().api();
		const auto sendRequest = [=](auto self) -> void {
			api->request(MTPmessages_UnpinAllMessages(
				history->peer->input
			)).done([=](const MTPmessages_AffectedHistory &result) {
				const auto peer = history->peer;
				const auto offset = api->applyAffectedHistory(peer, result);
				if (offset > 0) {
					self(self);
				} else {
					history->unpinAllMessages();
				}
			}).send();
		};
		sendRequest(sendRequest);
	})));
}

void PeerMenuAddMuteAction(
		not_null<PeerData*> peer,
		const PeerMenuCallback &addAction) {
	peer->owner().requestNotifySettings(peer);
	const auto muteText = [](bool isUnmuted) {
		return isUnmuted
			? tr::lng_disable_notifications_from_tray(tr::now)
			: tr::lng_enable_notifications_from_tray(tr::now);
	};
	const auto muteAction = addAction(QString("-"), [=] {
		if (!peer->owner().notifyIsMuted(peer)) {
			Ui::show(Box<MuteSettingsBox>(peer));
		} else {
			peer->owner().updateNotifySettings(peer, 0);
		}
	});

	auto actionText = Info::Profile::NotificationsEnabledValue(
		peer
	) | rpl::map(muteText);
	SetActionText(muteAction, std::move(actionText));
}

void MenuAddMarkAsReadAllChatsAction(
		not_null<Data::Session*> data,
		const PeerMenuCallback &addAction) {
	auto callback = [owner = data] {
		auto boxCallback = [=](Fn<void()> &&close) {
			close();

			MarkAsReadChatList(owner->chatsList());
			if (const auto folder = owner->folderLoaded(Data::Folder::kId)) {
				MarkAsReadChatList(folder->chatsList());
			}
		};
		Ui::show(Box<ConfirmBox>(
			tr::lng_context_mark_read_all_sure(tr::now),
			std::move(boxCallback)));
	};
	addAction(
		tr::lng_context_mark_read_all(tr::now),
		std::move(callback));
}

void MenuAddMarkAsReadChatListAction(
		Fn<not_null<Dialogs::MainList*>()> &&list,
		const PeerMenuCallback &addAction) {
	const auto unreadState = list()->unreadState();
	if (unreadState.empty()) {
		return;
	}

	auto callback = [=] {
		if (unreadState.messages > kMaxUnreadWithoutConfirmation) {
			auto boxCallback = [=](Fn<void()> &&close) {
				MarkAsReadChatList(list());
				close();
			};
			Ui::show(Box<ConfirmBox>(
				tr::lng_context_mark_read_sure(tr::now),
				std::move(boxCallback)));
		} else {
			MarkAsReadChatList(list());
		}
	};
	addAction(
		tr::lng_context_mark_read(tr::now),
		std::move(callback));
}

void ToggleHistoryArchived(not_null<History*> history, bool archived) {
	const auto callback = [=] {
		Ui::Toast::Show(Ui::Toast::Config{
			.text = { (archived
				? tr::lng_archived_added(tr::now)
				: tr::lng_archived_removed(tr::now)) },
			.st = &st::windowArchiveToast,
			.durationMs = (archived
				? kArchivedToastDuration
				: Ui::Toast::kDefaultDuration),
			.multiline = true,
		});
	};
	history->session().api().toggleHistoryArchived(
		history,
		archived,
		callback);
}

Fn<void()> ClearHistoryHandler(not_null<PeerData*> peer) {
	return [=] {
		Ui::show(
			Box<DeleteMessagesBox>(peer, true),
			Ui::LayerOption::KeepOther);
	};
}

Fn<void()> DeleteAndLeaveHandler(not_null<PeerData*> peer) {
	return [=] {
		Ui::show(
			Box<DeleteMessagesBox>(peer, false),
			Ui::LayerOption::KeepOther);
	};
}

void FillDialogsEntryMenu(
		not_null<SessionController*> controller,
		Dialogs::EntryState request,
		const PeerMenuCallback &callback) {
	Filler(controller, request, callback).fill();
}

} // namespace Window
