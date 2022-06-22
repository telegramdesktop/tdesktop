/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_peer_menu.h"

#include "api/api_chat_participants.h"
#include "lang/lang_keys.h"
#include "ui/boxes/confirm_box.h"
#include "base/options.h"
#include "base/unixtime.h"
#include "boxes/delete_messages_box.h"
#include "boxes/max_invite_box.h"
#include "boxes/mute_settings_box.h"
#include "boxes/add_contact_box.h"
#include "boxes/choose_filter_box.h"
#include "boxes/create_poll_box.h"
#include "boxes/pin_messages_box.h"
#include "boxes/premium_limits_box.h"
#include "boxes/report_messages_box.h"
#include "boxes/peers/add_bot_to_chat_box.h"
#include "boxes/peers/add_participants_box.h"
#include "boxes/peers/edit_contact_box.h"
#include "ui/boxes/report_box.h"
#include "ui/toast/toast.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/checkbox.h"
#include "ui/layers/generic_box.h"
#include "ui/toasts/common_toasts.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "menu/menu_mute.h"
#include "menu/menu_ttl_validator.h"
#include "apiwrap.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "api/api_blocked_peers.h"
#include "api/api_chat_filters.h"
#include "api/api_polls.h"
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
#include "data/notify/data_notify_settings.h"
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
#include "styles/style_menu_icons.h"

#include <QAction>

namespace Window {

const char kOptionViewProfileInChatsListContextMenu[] =
	"view-profile-in-chats-list-context-menu";

namespace {

constexpr auto kArchivedToastDuration = crl::time(5000);
constexpr auto kMaxUnreadWithoutConfirmation = 1000;

base::options::toggle ViewProfileInChatsListContextMenu({
	.id = kOptionViewProfileInChatsListContextMenu,
	.name = "Add \"View Profile\"",
	.description = "Add \"View Profile\" to context menu in chats list",
});

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

void PeerMenuAddMuteSubmenuAction(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		const PeerMenuCallback &addAction) {
	peer->owner().notifySettings().request(peer);
	const auto isMuted = peer->owner().notifySettings().isMuted(peer);
	if (isMuted) {
		const auto text = tr::lng_context_unmute(tr::now)
			+ '\t'
			+ Ui::FormatMuteForTiny(peer->notifyMuteUntil().value_or(0)
				- base::unixtime::now());
		addAction(text, [=] {
			peer->owner().notifySettings().update(peer, 0);
		}, &st::menuIconUnmute);
	} else {
		const auto show = std::make_shared<Window::Show>(controller);
		addAction(PeerMenuCallback::Args{
			.text = tr::lng_context_mute(tr::now),
			.handler = nullptr,
			.icon = peer->owner().notifySettings().sound(peer).none
				? &st::menuIconSilent
				: &st::menuIconMute,
			.fillSubmenu = [=](not_null<Ui::PopupMenu*> menu) {
				MuteMenu::FillMuteMenu(menu, { peer, show });
			},
		});
	}
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

	void fillChatsListActions();
	void fillHistoryActions();
	void fillProfileActions();
	void fillRepliesActions();
	void fillScheduledActions();
	void fillArchiveActions();

	void addHidePromotion();
	void addTogglePin();
	void addToggleMuteSubmenu(bool addSeparator);
	void addSupportInfo();
	void addInfo();
	void addToggleFolder();
	void addToggleUnreadMark();
	void addToggleArchive();
	void addClearHistory();
	void addDeleteChat();
	void addLeaveChat();
	void addManageChat();
	void addCreatePoll();
	void addThemeEdit();
	void addBlockUser();
	void addViewDiscussion();
	void addExportChat();
	void addReport();
	void addNewContact();
	void addShareContact();
	void addEditContact();
	void addBotToGroup();
	void addNewMembers();
	void addDeleteContact();
	void addTTLSubmenu(bool addSeparator);

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

bool PinnedLimitReached(
		not_null<Window::SessionController*> controller,
		not_null<History*> history,
		FilterId filterId) {
	Expects(filterId != 0 || history->folderKnown());

	const auto owner = &history->owner();
	const auto folder = history->folder();
	if (owner->pinnedCanPin(folder, filterId, history)) {
		return false;
	}
	// Some old chat, that was converted, maybe is still pinned.
	const auto wasted = filterId ? nullptr : FindWastedPin(owner, folder);
	if (wasted) {
		owner->setChatPinned(wasted, FilterId(), false);
		owner->setChatPinned(history, FilterId(), true);
		history->session().api().savePinnedOrder(folder);
	} else if (filterId) {
		controller->show(
			Box(FilterPinsLimitBox, &history->session(), filterId));
	} else if (folder) {
		controller->show(Box(FolderPinsLimitBox, &history->session()));
	} else {
		controller->show(Box(PinsLimitBox, &history->session()));
	}
	return true;
}

void TogglePinnedDialog(
		not_null<Window::SessionController*> controller,
		not_null<History*> history) {
	if (!history->folderKnown()) {
		return;
	}
	const auto owner = &history->owner();
	const auto isPinned = !history->isPinnedDialog(0);
	if (isPinned && PinnedLimitReached(controller, history, 0)) {
		return;
	}

	owner->setChatPinned(history, FilterId(), isPinned);
	const auto flags = isPinned
		? MTPmessages_ToggleDialogPin::Flag::f_pinned
		: MTPmessages_ToggleDialogPin::Flag(0);
	history->session().api().request(MTPmessages_ToggleDialogPin(
		MTP_flags(flags),
		MTP_inputDialogPeer(history->peer->input)
	)).done([=] {
		owner->notifyPinnedDialogsOrderUpdated();
	}).send();
	if (isPinned) {
		controller->content()->dialogsToUp();
	}
}

void TogglePinnedDialog(
		not_null<Window::SessionController*> controller,
		not_null<History*> history,
		FilterId filterId) {
	if (!filterId) {
		return TogglePinnedDialog(controller, history);
	}
	const auto owner = &history->owner();

	// This can happen when you remove this filter from another client.
	if (!ranges::contains(
			(&owner->session())->data().chatsFilters().list(),
			filterId,
			&Data::ChatFilter::id)) {
		Ui::Toast::Show(
			Window::Show(controller).toastParent(),
			tr::lng_cant_do_this(tr::now));
		return;
	}

	const auto isPinned = !history->isPinnedDialog(filterId);
	if (isPinned && PinnedLimitReached(controller, history, filterId)) {
		return;
	}

	owner->setChatPinned(history, filterId, isPinned);
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

void Filler::addHidePromotion() {
	const auto history = _peer->owner().historyLoaded(_peer);
	if (!history
		|| !history->useTopPromotion()
		|| history->topPromotionType().isEmpty()) {
		return;
	}
	_addAction(tr::lng_context_hide_psa(tr::now), [=] {
		history->cacheTopPromotion(false, QString(), QString());
		history->session().api().request(MTPhelp_HidePromoData(
			history->peer->input
		)).send();
	}, &st::menuIconRemove);
}

void Filler::addTogglePin() {
	const auto controller = _controller;
	const auto filterId = _request.filterId;
	const auto peer = _peer;
	const auto history = peer->owner().historyLoaded(peer);
	if (!history || history->fixedOnTopIndex()) {
		return;
	}
	const auto pinText = [=] {
		return history->isPinnedDialog(filterId)
			? tr::lng_context_unpin_from_top(tr::now)
			: tr::lng_context_pin_to_top(tr::now);
	};
	const auto pinToggle = [=] {
		TogglePinnedDialog(controller, history, filterId);
	};
	const auto pinAction = _addAction(
		pinText(),
		pinToggle,
		(history->isPinnedDialog(filterId)
			? &st::menuIconUnpin
			: &st::menuIconPin));

	auto actionText = history->session().changes().historyUpdates(
		history,
		Data::HistoryUpdate::Flag::IsPinned
	) | rpl::map(pinText);
	SetActionText(pinAction, std::move(actionText));
}

void Filler::addToggleMuteSubmenu(bool addSeparator) {
	if (_peer->isSelf()) {
		return;
	}
	PeerMenuAddMuteSubmenuAction(_controller, _peer, _addAction);
	if (addSeparator) {
		_addAction(PeerMenuCallback::Args{ .isSeparator = true });
	}
}

void Filler::addSupportInfo() {
	if (!_peer->session().supportMode()) {
		return;
	}
	const auto user = _peer->asUser();
	if (!user) {
		return;
	}
	const auto controller = _controller;
	_addAction("Edit support info", [=] {
		user->session().supportHelper().editInfo(controller, user);
	}, &st::menuIconEdit);
}

void Filler::addInfo() {
	if (_peer->isSelf() || _peer->isRepliesChat()) {
		return;
	} else if (_controller->adaptive().isThreeColumn()) {
		if (Core::App().settings().thirdSectionInfoEnabled()
			|| Core::App().settings().tabbedReplacedWithInfo()) {
			return;
		}
	}
	const auto controller = _controller;
	const auto peer = _peer;
	const auto text = (peer->isChat() || peer->isMegagroup())
		? tr::lng_context_view_group(tr::now)
		: (peer->isUser()
			? tr::lng_context_view_profile(tr::now)
			: tr::lng_context_view_channel(tr::now));
	_addAction(text, [=] {
		controller->showPeerInfo(peer);
	}, peer->isUser() ? &st::menuIconProfile : &st::menuIconInfo);
}

void Filler::addToggleFolder() {
	const auto controller = _controller;
	const auto history = _request.key.history();
	if (!history || !history->owner().chatsFilters().has()) {
		return;
	}
	_addAction(PeerMenuCallback::Args{
		.text = tr::lng_filters_menu_add(tr::now),
		.handler = nullptr,
		.icon = &st::menuIconAddToFolder,
		.fillSubmenu = [=](not_null<Ui::PopupMenu*> menu) {
			FillChooseFilterMenu(controller, menu, history);
		},
	});
}

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
	}, (IsUnreadHistory(history)
		? &st::menuIconMarkRead
		: &st::menuIconMarkUnread));

	auto actionText = history->session().changes().historyUpdates(
		history,
		Data::HistoryUpdate::Flag::UnreadView
	) | rpl::map(label);
	SetActionText(action, std::move(actionText));
}

void Filler::addToggleArchive() {
	const auto peer = _peer;
	const auto history = peer->owner().historyLoaded(peer);
	if (history && history->useTopPromotion()) {
		return;
	} else if (peer->isNotificationsUser() || peer->isSelf()) {
		if (!history || !history->folder()) {
			return;
		}
	}
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
	const auto archiveAction = _addAction(
		label(),
		toggle,
		isArchived() ? &st::menuIconUnarchive : &st::menuIconArchive);

	auto actionText = history->session().changes().historyUpdates(
		history,
		Data::HistoryUpdate::Flag::Folder
	) | rpl::map(label);
	SetActionText(archiveAction, std::move(actionText));
}

void Filler::addClearHistory() {
	const auto channel = _peer->asChannel();
	const auto isGroup = _peer->isChat() || _peer->isMegagroup();
	if (channel) {
		if (!channel->amIn()) {
			return;
		} else if (!channel->canDeleteMessages()
			&& (!isGroup || channel->isPublic())) {
			return;
		}
	}
	_addAction(
		tr::lng_profile_clear_history(tr::now),
		ClearHistoryHandler(_controller, _peer),
		&st::menuIconClear);
}

void Filler::addDeleteChat() {
	if (_peer->isChannel()) {
		return;
	}
	_addAction({
		.text = (_peer->isUser()
			? tr::lng_profile_delete_conversation(tr::now)
			: tr::lng_profile_clear_and_exit(tr::now)),
		.handler = DeleteAndLeaveHandler(_controller, _peer),
		.icon = &st::menuIconDeleteAttention,
		.isAttention = true,
	});
}

void Filler::addLeaveChat() {
	const auto channel = _peer->asChannel();
	if (!channel || !channel->amIn()) {
		return;
	}
	_addAction({
		.text = (_peer->isMegagroup()
			? tr::lng_profile_leave_group(tr::now)
			: tr::lng_profile_leave_channel(tr::now)),
		.handler = DeleteAndLeaveHandler(_controller, _peer),
		.icon = &st::menuIconLeaveAttention,
		.isAttention = true,
	});
}

void Filler::addBlockUser() {
	const auto user = _peer->asUser();
	if (!user
		|| user->isInaccessible()
		|| user->isSelf()
		|| user->isRepliesChat()) {
		return;
	}
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
			user->session().api().blockedPeers().block(user);
		} else {
			window->show(Box(
				PeerMenuBlockUserBox,
				window,
				user,
				v::null,
				v::null));
		}
	}, (!user->isBlocked()
		? &st::menuIconBlock
		: user->isBot()
		? &st::menuIconRestartBot
		: &st::menuIconUnblock));

	auto actionText = _peer->session().changes().peerUpdates(
		_peer,
		Data::PeerUpdate::Flag::IsBlocked
	) | rpl::map([=] { return blockText(user); });
	SetActionText(blockAction, std::move(actionText));

	if (user->blockStatus() == UserData::BlockStatus::Unknown) {
		user->session().api().requestFullPeer(user);
	}
}

void Filler::addViewDiscussion() {
	const auto channel = _peer->asBroadcast();
	if (!channel) {
		return;
	}
	const auto chat = channel->linkedChat();
	if (!chat) {
		return;
	}
	const auto navigation = _controller;
	_addAction(tr::lng_profile_view_discussion(tr::now), [=] {
		if (channel->invitePeekExpires()) {
			Ui::Toast::Show(
				Window::Show(navigation).toastParent(),
				tr::lng_channel_invite_private(tr::now));
			return;
		}
		navigation->showPeerHistory(
			chat,
			Window::SectionShow::Way::Forward);
	}, &st::menuIconDiscussion);
}

void Filler::addExportChat() {
	if (!_peer->canExportChatHistory()) {
		return;
	}
	const auto peer = _peer;
	_addAction(
		tr::lng_profile_export_chat(tr::now),
		[=] { PeerMenuExportChat(peer); },
		&st::menuIconExport);
}

void Filler::addReport() {
	const auto chat = _peer->asChat();
	const auto channel = _peer->asChannel();
	if ((!chat || chat->amCreator())
		&& (!channel || channel->amCreator())) {
		return;
	}
	const auto peer = _peer;
	const auto navigation = _controller;
	_addAction(tr::lng_profile_report(tr::now), [=] {
		ShowReportPeerBox(navigation, peer);
	}, &st::menuIconReport);
}

void Filler::addNewContact() {
	const auto user = _peer->asUser();
	if (!user
		|| user->isContact()
		|| user->isSelf()
		|| user->isInaccessible()
		|| user->isBot()) {
		return;
	}
	const auto controller = _controller;
	_addAction(
		tr::lng_info_add_as_contact(tr::now),
		[=] { controller->show(Box(EditContactBox, controller, user)); },
		&st::menuIconInvite);
}

void Filler::addShareContact() {
	const auto user = _peer->asUser();
	if (!user || !user->canShareThisContact()) {
		return;
	}
	const auto controller = _controller;
	_addAction(
		tr::lng_info_share_contact(tr::now),
		[=] { PeerMenuShareContactBox(controller, user); },
		&st::menuIconShare);
}

void Filler::addEditContact() {
	const auto user = _peer->asUser();
	if (!user || !user->isContact() || user->isSelf()) {
		return;
	}
	const auto controller = _controller;
	_addAction(
		tr::lng_info_edit_contact(tr::now),
		[=] { controller->show(Box(EditContactBox, controller, user)); },
		&st::menuIconEdit);
}

void Filler::addBotToGroup() {
	const auto user = _peer->asUser();
	if (!user) {
		return;
	}
	[[maybe_unused]] const auto lifetime = Info::Profile::InviteToChatButton(
		user
	) | rpl::take(1) | rpl::start_with_next([=](QString label) {
		if (!label.isEmpty()) {
			const auto controller = _controller;
			_addAction(
				label,
				[=] { AddBotToGroupBoxController::Start(controller, user); },
				&st::menuIconInvite);
		}
	});
}

void Filler::addNewMembers() {
	const auto chat = _peer->asChat();
	const auto channel = _peer->asChannel();
	if ((!chat || !chat->canAddMembers())
		&& (!channel || !channel->canAddMembers())) {
		return;
	}
	const auto navigation = _controller;
	const auto callback = chat
		? Fn<void()>([=] { AddChatMembers(navigation, chat); })
		: [=] { PeerMenuAddChannelMembers(navigation, channel); };
	_addAction(
		((chat || channel->isMegagroup())
			? tr::lng_channel_add_members(tr::now)
			: tr::lng_channel_add_users(tr::now)),
		callback,
		&st::menuIconInvite);
}

void Filler::addDeleteContact() {
	const auto user = _peer->asUser();
	if (!user || !user->isContact() || user->isSelf()) {
		return;
	}
	const auto controller = _controller;
	_addAction({
		.text = tr::lng_info_delete_contact(tr::now),
		.handler = [=] { PeerMenuDeleteContact(controller, user); },
		.icon = &st::menuIconDeleteAttention,
		.isAttention = true,
	});
}

void Filler::addManageChat() {
	if (!EditPeerInfoBox::Available(_peer)) {
		return;
	}
	const auto peer = _peer;
	const auto navigation = _controller;
	const auto text = (peer->isChat() || peer->isMegagroup())
		? tr::lng_manage_group_title(tr::now)
		: tr::lng_manage_channel_title(tr::now);
	_addAction(text, [=] {
		navigation->showEditPeerBox(peer);
	}, &st::menuIconManage);
}

void Filler::addCreatePoll() {
	if (!_peer->canSendPolls()) {
		return;
	}
	const auto peer = _peer;
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
	_addAction(
		tr::lng_polls_create(tr::now),
		std::move(callback),
		&st::menuIconCreatePoll);
}

void Filler::addThemeEdit() {
	const auto user = _peer->asUser();
	if (!user || user->isBot()) {
		return;
	}
	const auto controller = _controller;
	_addAction(
		tr::lng_chat_theme_change(tr::now),
		[=] { controller->toggleChooseChatTheme(user); },
		&st::menuIconChangeColors);
}

void Filler::addTTLSubmenu(bool addSeparator) {
	const auto validator = TTLMenu::TTLValidator(
		std::make_shared<Window::Show>(_controller),
		_peer);
	if (!validator.can()) {
		return;
	}
	const auto text = tr::lng_manage_messages_ttl_menu(tr::now)
		+ (_peer->messagesTTL()
			? ('\t' + Ui::FormatTTLTiny(_peer->messagesTTL()))
			: QString());
	_addAction(text, [=] { validator.showBox(); }, validator.icon());
	if (addSeparator) {
		_addAction(PeerMenuCallback::Args{ .isSeparator = true });
	}
}

void Filler::fill() {
	if (_folder) {
		fillArchiveActions();
		return;
	}
	switch (_request.section) {
	case Section::ChatsList: fillChatsListActions(); break;
	case Section::History: fillHistoryActions(); break;
	case Section::Profile: fillProfileActions(); break;
	case Section::Replies: fillRepliesActions(); break;
	case Section::Scheduled: fillScheduledActions(); break;
	default: Unexpected("_request.section in Filler::fill.");
	}
}

void Filler::fillChatsListActions() {
	addHidePromotion();
	addToggleArchive();
	addTogglePin();
	if (ViewProfileInChatsListContextMenu.value()) {
		addInfo();
	}
	addToggleMuteSubmenu(false);
	addToggleUnreadMark();
	addToggleFolder();
	if (const auto user = _peer->asUser()) {
		if (!user->isContact()) {
			addBlockUser();
		}
	}
	addClearHistory();
	addDeleteChat();
	addLeaveChat();
}

void Filler::fillHistoryActions() {
	addToggleMuteSubmenu(true);
	addInfo();
	addSupportInfo();
	addManageChat();
	addCreatePoll();
	addThemeEdit();
	addViewDiscussion();
	addExportChat();
	addReport();
	addClearHistory();
	addDeleteChat();
	addLeaveChat();
}

void Filler::fillProfileActions() {
	addTTLSubmenu(true);
	addSupportInfo();
	addNewContact();
	addShareContact();
	addEditContact();
	addBotToGroup();
	addNewMembers();
	addManageChat();
	addViewDiscussion();
	addExportChat();
	addBlockUser();
	addReport();
	addLeaveChat();
	addDeleteContact();
}

void Filler::fillRepliesActions() {
	addCreatePoll();
}

void Filler::fillScheduledActions() {
	addCreatePoll();
}

void Filler::fillArchiveActions() {
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
	}, hidden ? &st::menuIconExpand : &st::menuIconCollapse);

	_addAction(tr::lng_context_archive_to_menu(tr::now), [=] {
		Ui::Toast::Show(
			Window::Show(controller).toastParent(),
			Ui::Toast::Config{
				.text = { tr::lng_context_archive_to_menu_info(tr::now) },
				.st = &st::windowArchiveToast,
				.durationMs = kArchivedToastDuration,
				.multiline = true,
			});

		controller->session().settings().setArchiveInMainMenu(
			!controller->session().settings().archiveInMainMenu());
		controller->session().saveSettingsDelayed();
	}, &st::menuIconToMainMenu);

	MenuAddMarkAsReadChatListAction(
		controller,
		[folder = _folder] { return folder->chatsList(); },
		_addAction);
}

} // namespace

void PeerMenuExportChat(not_null<PeerData*> peer) {
	Core::App().exportManager().start(peer);
}

void PeerMenuDeleteContact(
		not_null<Window::SessionController*> controller,
		not_null<UserData*> user) {
	const auto text = tr::lng_sure_delete_contact(
		tr::now,
		lt_contact,
		user->name);
	const auto deleteSure = [=](Fn<void()> &&close) {
		close();
		user->session().api().request(MTPcontacts_DeleteContacts(
			MTP_vector<MTPInputUser>(1, user->inputUser)
		)).done([=](const MTPUpdates &result) {
			user->session().api().applyUpdates(result);
		}).send();
	};
	controller->show(
		Ui::MakeConfirmBox({
			.text = text,
			.confirmed = deleteSure,
			.confirmText = tr::lng_box_delete(),
		}),
		Ui::LayerOption::CloseOther);
}

void PeerMenuShareContactBox(
		not_null<Window::SessionNavigation*> navigation,
		not_null<UserData*> user) {
	// There is no async to make weak from controller.
	const auto weak = std::make_shared<QPointer<Ui::BoxContent>>();
	auto callback = [=](not_null<PeerData*> peer) {
		if (!peer->canWrite()) {
			navigation->parentController()->show(
				Ui::MakeInformBox(tr::lng_forward_share_cant()),
				Ui::LayerOption::KeepOther);
			return;
		} else if (peer->isSelf()) {
			auto action = Api::SendAction(peer->owner().history(peer));
			action.clearDraft = false;
			user->session().api().shareContact(user, action);
			Ui::Toast::Show(
				Window::Show(navigation).toastParent(),
				tr::lng_share_done(tr::now));
			if (auto strong = *weak) {
				strong->closeBox();
			}
			return;
		}
		auto recipient = peer->isUser()
			? peer->name
			: '\xAB' + peer->name + '\xBB';
		navigation->parentController()->show(
			Ui::MakeConfirmBox({
				.text = tr::lng_forward_share_contact(
					tr::now,
					lt_recipient,
					recipient),
				.confirmed = [peer, user, navigation](Fn<void()> &&close) {
					const auto history = peer->owner().history(peer);
					navigation->showPeerHistory(
						history,
						Window::SectionShow::Way::ClearStack,
						ShowAtTheEndMsgId);
					auto action = Api::SendAction(history);
					action.clearDraft = false;
					user->session().api().shareContact(user, action);
					close();
				},
				.confirmText = tr::lng_forward_send(),
			}),
			Ui::LayerOption::KeepOther);
	};
	*weak = navigation->parentController()->show(
		Box<PeerListBox>(
			std::make_unique<ChooseRecipientBoxController>(
				&navigation->session(),
				std::move(callback)),
			[](not_null<PeerListBox*> box) {
				box->addButton(tr::lng_cancel(), [=] {
					box->closeBox();
				});
			}),
		Ui::LayerOption::CloseOther);
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
	auto box = Box<CreatePollBox>(
		controller,
		chosen,
		disabled,
		sendType,
		sendMenuType);
	const auto weak = Ui::MakeWeak(box.data());
	const auto lock = box->lifetime().make_state<bool>(false);
	box->submitRequests(
	) | rpl::start_with_next([=](const CreatePollBox::Result &result) {
		if (std::exchange(*lock, true)) {
			return;
		}
		auto action = Api::SendAction(
			peer->owner().history(peer),
			result.options);
		action.clearDraft = false;
		action.replyTo = replyToId;
		if (const auto localDraft = action.history->localDraft()) {
			action.clearDraft = localDraft->textWithTags.text.isEmpty();
		}
		const auto api = &peer->session().api();
		api->polls().create(result.poll, action, crl::guard(weak, [=] {
			weak->closeBox();
		}), crl::guard(weak, [=](const MTP::Error &error) {
			*lock = false;
			weak->submitFailed(tr::lng_attach_failed(tr::now));
		}));
	}, box->lifetime());
	controller->show(std::move(box), Ui::LayerOption::CloseOther);
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
			tr::lng_delete_all_from_user(
				tr::now,
				lt_user,
				Ui::Text::Bold(peer->name),
				Ui::Text::WithEntities),
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
			peer->session().api().blockedPeers().block(peer);
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
			Window::Show(window).toastParent(),
			tr::lng_new_contact_block_done(tr::now, lt_user, name));
	}, st::attentionBoxButton);

	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

void PeerMenuUnblockUserWithBotRestart(not_null<UserData*> user) {
	user->session().api().blockedPeers().unblock(user, [=] {
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

QPointer<Ui::BoxContent> ShowForwardMessagesBox(
		not_null<Window::SessionNavigation*> navigation,
		Data::ForwardDraft &&draft,
		FnMut<void()> &&successCallback) {
	const auto weak = std::make_shared<QPointer<Ui::BoxContent>>();
	auto callback = [
		draft = std::move(draft),
		callback = std::move(successCallback),
		weak,
		navigation
	](not_null<PeerData*> peer) mutable {
		const auto content = navigation->parentController()->content();
		if (peer->isSelf()
			&& !draft.ids.empty()
			&& draft.ids.front().peer != peer->id) {
			const auto history = peer->owner().history(peer);
			auto resolved = history->resolveForwardDraft(draft);
			if (!resolved.items.empty()) {
				const auto api = &peer->session().api();
				auto action = Api::SendAction(peer->owner().history(peer));
				action.clearDraft = false;
				action.generateLocal = false;
				const auto weakContent = Ui::MakeWeak(content);
				api->forwardMessages(
					std::move(resolved),
					action,
					crl::guard(weakContent, [w = weakContent] {
						Ui::Toast::Show(w, tr::lng_share_done(tr::now));
					}));
			}
		} else if (!content->setForwardDraft(peer->id, std::move(draft))) {
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
	*weak = navigation->parentController()->show(Box<PeerListBox>(
		std::make_unique<ChooseRecipientBoxController>(
			&navigation->session(),
			std::move(callback)),
		std::move(initBox)), Ui::LayerOption::KeepOther);
	return weak->data();
}

QPointer<Ui::BoxContent> ShowForwardMessagesBox(
		not_null<Window::SessionNavigation*> navigation,
		MessageIdsList &&items,
		FnMut<void()> &&successCallback) {
	return ShowForwardMessagesBox(
		navigation,
		Data::ForwardDraft{ .ids = std::move(items) },
		std::move(successCallback));
}

QPointer<Ui::BoxContent> ShowSendNowMessagesBox(
		not_null<Window::SessionNavigation*> navigation,
		not_null<History*> history,
		MessageIdsList &&items,
		Fn<void()> &&successCallback) {
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
			.parentOverride = Window::Show(navigation).toastParent(),
			.text = { error },
		});
		return { nullptr };
	}
	auto done = [
		=,
		list = std::move(items),
		callback = std::move(successCallback)
	](Fn<void()> &&close) {
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
	return navigation->parentController()->show(
		Ui::MakeConfirmBox({
			.text = text,
			.confirmed = std::move(done),
			.confirmText = tr::lng_send_button(),
		}),
		Ui::LayerOption::KeepOther).data();
}

void PeerMenuAddChannelMembers(
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChannelData*> channel) {
	if (!channel->isMegagroup()
		&& (channel->membersCount()
			>= channel->session().serverConfig().chatSizeMax)) {
		navigation->parentController()->show(
			Box<MaxInviteBox>(channel),
			Ui::LayerOption::KeepOther);
		return;
	}
	const auto api = &channel->session().api();
	api->chatParticipants().requestForAdd(channel, crl::guard(navigation, [=](
			const Api::ChatParticipants::TLMembers &data) {
		const auto &[availableCount, list] = Api::ChatParticipants::Parse(
			channel,
			data);
		const auto already = (
			list
		) | ranges::views::transform([&](const Api::ChatParticipant &p) {
			return p.isUser()
				? channel->owner().userLoaded(p.userId())
				: nullptr;
		}) | ranges::views::filter([](UserData *user) {
			return (user != nullptr);
		}) | ranges::to_vector;

		AddParticipantsBoxController::Start(
			navigation,
			channel,
			{ already.begin(), already.end() });
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
		navigation->parentController()->show(
			Box(PinMessageBox, item->history()->peer, item->id),
			Ui::LayerOption::CloseOther);
	} else {
		const auto peer = item->history()->peer;
		const auto session = &peer->session();
		const auto callback = crl::guard(session, [=](Fn<void()> &&close) {
			close();
			session->api().request(MTPmessages_UpdatePinnedMessage(
				MTP_flags(MTPmessages_UpdatePinnedMessage::Flag::f_unpin),
				peer->input,
				MTP_int(itemId.msg)
			)).done([=](const MTPUpdates &result) {
				session->api().applyUpdates(result);
			}).send();
		});
		navigation->parentController()->show(
			Ui::MakeConfirmBox({
				.text = tr::lng_pinned_unpin_sure(),
				.confirmed = callback,
				.confirmText = tr::lng_pinned_unpin(),
			}),
			Ui::LayerOption::CloseOther);
	}
}

void HidePinnedBar(
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer,
		Fn<void()> onHidden) {
	const auto callback = crl::guard(navigation, [=](Fn<void()> &&close) {
		close();
		auto &session = peer->session();
		const auto migrated = peer->migrateFrom();
		const auto top = Data::ResolveTopPinnedId(peer, migrated);
		const auto universal = !top
			? MsgId(0)
			: (migrated && !peerIsChannel(top.peer))
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
	});
	navigation->parentController()->show(
		Ui::MakeConfirmBox({
			.text = tr::lng_pinned_hide_all_sure(),
			.confirmed = callback,
			.confirmText = tr::lng_pinned_hide_all_hide(),
		}),
		Ui::LayerOption::CloseOther);
}

void UnpinAllMessages(
		not_null<Window::SessionNavigation*> navigation,
		not_null<History*> history) {
	const auto callback = crl::guard(navigation, [=](Fn<void()> &&close) {
		close();
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
	});
	navigation->parentController()->show(
		Ui::MakeConfirmBox({
			.text = tr::lng_pinned_unpin_all_sure(),
			.confirmed = callback,
			.confirmText = tr::lng_pinned_unpin(),
		}),
		Ui::LayerOption::CloseOther);
}

void PeerMenuAddMuteAction(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		const PeerMenuCallback &addAction) {
	// There is no async to make weak from controller.
	peer->owner().notifySettings().request(peer);
	const auto muteText = [](bool isUnmuted) {
		return isUnmuted
			? tr::lng_context_mute(tr::now)
			: tr::lng_context_unmute(tr::now);
	};
	const auto muteAction = addAction(QString("-"), [=] {
		if (!peer->owner().notifySettings().isMuted(peer)) {
			controller->show(
				Box<MuteSettingsBox>(peer),
				Ui::LayerOption::CloseOther);
		} else {
			peer->owner().notifySettings().update(peer, 0);
		}
	}, (peer->owner().notifySettings().isMuted(peer)
		? &st::menuIconUnmute
		: &st::menuIconMute));

	auto actionText = Info::Profile::NotificationsEnabledValue(
		peer
	) | rpl::map(muteText);
	SetActionText(muteAction, std::move(actionText));
}

void MenuAddMarkAsReadAllChatsAction(
		not_null<Window::SessionController*> controller,
		const PeerMenuCallback &addAction) {
	// There is no async to make weak from controller.
	auto callback = [=, owner = &controller->session().data()] {
		auto boxCallback = [=](Fn<void()> &&close) {
			close();

			MarkAsReadChatList(owner->chatsList());
			if (const auto folder = owner->folderLoaded(Data::Folder::kId)) {
				MarkAsReadChatList(folder->chatsList());
			}
		};
		controller->show(
			Ui::MakeConfirmBox({
				tr::lng_context_mark_read_all_sure(),
				std::move(boxCallback)
			}),
			Ui::LayerOption::CloseOther);
	};
	addAction(
		tr::lng_context_mark_read_all(tr::now),
		std::move(callback),
		&st::menuIconMarkRead);
}

void MenuAddMarkAsReadChatListAction(
		not_null<Window::SessionController*> controller,
		Fn<not_null<Dialogs::MainList*>()> &&list,
		const PeerMenuCallback &addAction) {
	// There is no async to make weak from controller.
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
			controller->show(
				Ui::MakeConfirmBox({
					tr::lng_context_mark_read_sure(),
					std::move(boxCallback)
				}),
				Ui::LayerOption::CloseOther);
		} else {
			MarkAsReadChatList(list());
		}
	};
	addAction(
		tr::lng_context_mark_read(tr::now),
		std::move(callback),
		&st::menuIconMarkRead);
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

Fn<void()> ClearHistoryHandler(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	return [=] {
		controller->show(
			Box<DeleteMessagesBox>(peer, true),
			Ui::LayerOption::KeepOther);
	};
}

Fn<void()> DeleteAndLeaveHandler(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	return [=] {
		controller->show(
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
