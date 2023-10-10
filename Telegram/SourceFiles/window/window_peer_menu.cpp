/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_peer_menu.h"

#include "menu/menu_check_item.h"
#include "boxes/share_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "chat_helpers/message_field.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/fields/input_field.h"
#include "api/api_chat_participants.h"
#include "lang/lang_keys.h"
#include "ui/boxes/confirm_box.h"
#include "base/random.h"
#include "base/options.h"
#include "base/unixtime.h"
#include "boxes/delete_messages_box.h"
#include "boxes/max_invite_box.h"
#include "boxes/choose_filter_box.h"
#include "boxes/create_poll_box.h"
#include "boxes/pin_messages_box.h"
#include "boxes/premium_limits_box.h"
#include "boxes/report_messages_box.h"
#include "boxes/peers/add_bot_to_chat_box.h"
#include "boxes/peers/add_participants_box.h"
#include "boxes/peers/edit_forum_topic_box.h"
#include "boxes/peers/edit_contact_box.h"
#include "calls/calls_instance.h"
#include "inline_bots/bot_attach_web_view.h" // InlineBots::PeerType.
#include "ui/boxes/report_box.h"
#include "ui/toast/toast.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/layers/generic_box.h"
#include "ui/delayed_activation.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "menu/menu_mute.h"
#include "menu/menu_ttl_validator.h"
#include "apiwrap.h"
#include "mainwidget.h"
#include "api/api_blocked_peers.h"
#include "api/api_chat_filters.h"
#include "api/api_polls.h"
#include "api/api_updates.h"
#include "mtproto/mtproto_config.h"
#include "history/history.h"
#include "history/history_item_helpers.h" // GetErrorTextForSending.
#include "history/view/history_view_context_menu.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "settings/settings_advanced.h"
#include "support/support_helper.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "info/boosts/info_boosts_widget.h"
#include "info/profile/info_profile_values.h"
#include "info/statistics/info_statistics_widget.h"
#include "info/stories/info_stories_widget.h"
#include "data/notify/data_notify_settings.h"
#include "data/data_changes.h"
#include "data/data_session.h"
#include "data/data_folder.h"
#include "data/data_poll.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_user.h"
#include "data/data_scheduled_messages.h"
#include "data/data_histories.h"
#include "data/data_chat_filters.h"
#include "dialogs/dialogs_key.h"
#include "core/application.h"
#include "export/export_manager.h"
#include "boxes/peers/edit_peer_info_box.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_window.h" // st::windowMinWidth
#include "styles/style_menu_icons.h"

#include <QAction>
#include <QtGui/QGuiApplication>

namespace Window {
namespace {

constexpr auto kTopicsSearchMinCount = 1;

void ShareBotGame(
		not_null<UserData*> bot,
		not_null<Data::Thread*> thread,
		const QString &shortName) {
	auto &histories = thread->owner().histories();
	const auto history = thread->owningHistory();
	const auto randomId = base::RandomValue<uint64>();
	const auto replyTo = thread->topicRootId();
	const auto topicRootId = replyTo;
	auto flags = MTPmessages_SendMedia::Flags(0);
	if (replyTo) {
		flags |= MTPmessages_SendMedia::Flag::f_reply_to;
	}
	histories.sendPreparedMessage(
		history,
		FullReplyTo{
			.messageId = { replyTo ? history->peer->id : 0, replyTo },
			.topicRootId = topicRootId,
		},
		randomId,
		Data::Histories::PrepareMessage<MTPmessages_SendMedia>(
			MTP_flags(flags),
			history->peer->input,
			Data::Histories::ReplyToPlaceholder(),
			MTP_inputMediaGame(
				MTP_inputGameShortName(
					bot->inputUser,
					MTP_string(shortName))),
			MTP_string(),
			MTP_long(randomId),
			MTPReplyMarkup(),
			MTPVector<MTPMessageEntity>(),
			MTP_int(0), // schedule_date
			MTPInputPeer() // send_as
		), [=](const MTPUpdates &, const MTP::Response &) {
	}, [=](const MTP::Error &error, const MTP::Response &) {
		history->session().api().sendMessageFail(error, history->peer);
	});
}

} // namespace

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

void MarkAsReadChatList(not_null<Dialogs::MainList*> list) {
	auto mark = std::vector<not_null<History*>>();
	for (const auto &row : list->indexed()->all()) {
		if (const auto history = row->history()) {
			mark.push_back(history);
		}
	}
	ranges::for_each(mark, MarkAsReadThread);
}

void PeerMenuAddMuteSubmenuAction(
		not_null<Window::SessionController*> controller,
		not_null<Data::Thread*> thread,
		const PeerMenuCallback &addAction) {
	const auto notifySettings = &thread->owner().notifySettings();
	notifySettings->request(thread);
	const auto weak = base::make_weak(thread);
	const auto with = [=](Fn<void(not_null<Data::Thread*>)> callback) {
		return [=] {
			if (const auto strong = weak.get()) {
				callback(strong);
			}
		};
	};
	const auto isMuted = notifySettings->isMuted(thread);
	if (isMuted) {
		const auto text = tr::lng_context_unmute(tr::now)
			+ '\t'
			+ Ui::FormatMuteForTiny(thread->notify().muteUntil().value_or(0)
				- base::unixtime::now());
		addAction(text, with([=](not_null<Data::Thread*> thread) {
			notifySettings->update(thread, { .unmute = true });
		}), &st::menuIconUnmute);
	} else {
		const auto show = controller->uiShow();
		addAction(PeerMenuCallback::Args{
			.text = tr::lng_context_mute(tr::now),
			.handler = nullptr,
			.icon = (notifySettings->sound(thread).none
				? &st::menuIconSilent
				: &st::menuIconMute),
			.fillSubmenu = [&](not_null<Ui::PopupMenu*> menu) {
				MuteMenu::FillMuteMenu(menu, thread, show);
			},
		});
	}
}

void ForwardToSelf(
		std::shared_ptr<Main::SessionShow> show,
		const Data::ForwardDraft &draft) {
	const auto session = &show->session();
	const auto history = session->data().history(session->user());
	auto resolved = history->resolveForwardDraft(draft);
	if (!resolved.items.empty()) {
		auto action = Api::SendAction(history);
		action.clearDraft = false;
		action.generateLocal = false;
		session->api().forwardMessages(
			std::move(resolved),
			action,
			[=] { show->showToast(tr::lng_share_done(tr::now)); });
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
	void fillContextMenuActions();

	void addHidePromotion();
	void addTogglePin();
	void addToggleMuteSubmenu(bool addSeparator);
	void addSupportInfo();
	void addInfo();
	void addStoryArchive();
	void addNewWindow();
	void addToggleFolder();
	void addToggleUnreadMark();
	void addToggleArchive();
	void addClearHistory();
	void addDeleteChat();
	void addLeaveChat();
	void addJoinChat();
	void addTopicLink();
	void addManageTopic();
	void addManageChat();
	void addCreatePoll();
	void addThemeEdit();
	void addBlockUser();
	void addViewDiscussion();
	void addToggleTopicClosed();
	void addExportChat();
	void addTranslate();
	void addReport();
	void addNewContact();
	void addShareContact();
	void addEditContact();
	void addBotToGroup();
	void addNewMembers();
	void addDeleteContact();
	void addTTLSubmenu(bool addSeparator);
	void addGiftPremium();
	void addCreateTopic();
	void addViewAsMessages();
	void addSearchTopics();
	void addDeleteTopic();
	void addVideoChat();
	void addViewStatistics();

	not_null<SessionController*> _controller;
	Dialogs::EntryState _request;
	Data::Thread *_thread = nullptr;
	Data::ForumTopic *_topic = nullptr;
	PeerData *_peer = nullptr;
	Data::Folder *_folder = nullptr;
	const PeerMenuCallback &_addAction;

};

History *FindWastedPin(not_null<Data::Session*> data, Data::Folder *folder) {
	const auto &order = data->pinnedChatsOrder(folder);
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
		not_null<Data::Thread*> thread) {
	const auto owner = &thread->owner();
	if (owner->pinnedCanPin(thread)) {
		return false;
	}
	// Some old chat, that was converted, maybe is still pinned.
	const auto history = thread->asHistory();
	if (!history) {
		controller->show(Box(ForumPinsLimitBox, thread->asTopic()->forum()));
		return true;
	}
	const auto folder = history->folder();
	const auto wasted = FindWastedPin(owner, folder);
	if (wasted) {
		owner->setChatPinned(wasted, FilterId(), false);
		owner->setChatPinned(history, FilterId(), true);
		history->session().api().savePinnedOrder(folder);
	} else if (folder) {
		controller->show(Box(FolderPinsLimitBox, &history->session()));
	} else {
		controller->show(Box(PinsLimitBox, &history->session()));
	}
	return true;
}

bool PinnedLimitReached(
		not_null<Window::SessionController*> controller,
		not_null<History*> history,
		FilterId filterId) {
	const auto owner = &history->owner();
	if (owner->pinnedCanPin(filterId, history)) {
		return false;
	}
	controller->show(
		Box(FilterPinsLimitBox, &history->session(), filterId));
	return true;
}

void TogglePinnedThread(
		not_null<Window::SessionController*> controller,
		not_null<Data::Thread*> thread) {
	if (!thread->folderKnown()) {
		return;
	}
	const auto owner = &thread->owner();
	const auto isPinned = !thread->isPinnedDialog(FilterId());
	if (isPinned && PinnedLimitReached(controller, thread)) {
		return;
	}

	owner->setChatPinned(thread, FilterId(), isPinned);
	if (const auto history = thread->asHistory()) {
		const auto flags = isPinned
			? MTPmessages_ToggleDialogPin::Flag::f_pinned
			: MTPmessages_ToggleDialogPin::Flag(0);
		owner->session().api().request(MTPmessages_ToggleDialogPin(
			MTP_flags(flags),
			MTP_inputDialogPeer(history->peer->input)
		)).done([=] {
			owner->notifyPinnedDialogsOrderUpdated();
		}).send();
		if (isPinned) {
			controller->content()->dialogsToUp();
		}
	} else if (const auto topic = thread->asTopic()) {
		owner->session().api().request(MTPchannels_UpdatePinnedForumTopic(
			topic->channel()->inputChannel,
			MTP_int(topic->rootId()),
			MTP_bool(isPinned)
		)).done([=](const MTPUpdates &result) {
			owner->session().api().applyUpdates(result);
		}).send();
	}
}

void TogglePinnedThread(
		not_null<Window::SessionController*> controller,
		not_null<Data::Thread*> thread,
		FilterId filterId) {
	if (!filterId) {
		return TogglePinnedThread(controller, thread);
	}
	const auto history = thread->asHistory();
	if (!history) {
		return;
	}
	const auto owner = &history->owner();

	// This can happen when you remove this filter from another client.
	if (!ranges::contains(
			(&owner->session())->data().chatsFilters().list(),
			filterId,
			&Data::ChatFilter::id)) {
		controller->showToast(tr::lng_cant_do_this(tr::now));
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
, _thread(request.key.thread())
, _topic(request.key.topic())
, _peer(request.key.peer())
, _folder(request.key.folder())
, _addAction(addAction) {
}

void Filler::addHidePromotion() {
	const auto history = _request.key.history();
	if (_topic
		|| !history
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

void Filler::addToggleTopicClosed() {
	if (!_topic || !_topic->canToggleClosed()) {
		return;
	}
	const auto closed = _topic->closed();
	const auto weak = base::make_weak(_topic);
	_addAction(closed ? tr::lng_forum_topic_reopen(tr::now) : tr::lng_forum_topic_close(tr::now), [=] {
		if (const auto topic = weak.get()) {
			topic->setClosedAndSave(!closed);
		}
	}, closed ? &st::menuIconRestartBot : &st::menuIconBlock);
}

void Filler::addTogglePin() {
	if (!_peer || (_topic && !_topic->canTogglePinned())) {
		return;
	}
	const auto controller = _controller;
	const auto filterId = _request.filterId;
	const auto thread = _request.key.thread();
	if (!thread || thread->fixedOnTopIndex()) {
		return;
	}
	const auto pinText = [=] {
		return thread->isPinnedDialog(filterId)
			? tr::lng_context_unpin_from_top(tr::now)
			: tr::lng_context_pin_to_top(tr::now);
	};
	const auto weak = base::make_weak(thread);
	const auto pinToggle = [=] {
		if (const auto strong = weak.get()) {
			TogglePinnedThread(controller, strong, filterId);
		}
	};
	_addAction(
		pinText(),
		pinToggle,
		(thread->isPinnedDialog(filterId)
			? &st::menuIconUnpin
			: &st::menuIconPin));
}

void Filler::addToggleMuteSubmenu(bool addSeparator) {
	if (_thread->peer()->isSelf()) {
		return;
	}
	PeerMenuAddMuteSubmenuAction(_controller, _thread, _addAction);
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
	if (_peer && (_peer->isSelf() || _peer->isRepliesChat())) {
		return;
	} else if (_controller->adaptive().isThreeColumn()) {
		const auto thread = _controller->activeChatCurrent().thread();
		if (thread && thread == _thread) {
			if (Core::App().settings().thirdSectionInfoEnabled()
				|| Core::App().settings().tabbedReplacedWithInfo()) {
				return;
			}
		}
	} else if (!_thread) {
		return;
	}
	const auto controller = _controller;
	const auto weak = base::make_weak(_thread);
	const auto text = _thread->asTopic()
		? tr::lng_context_view_topic(tr::now)
		: (_peer->isChat() || _peer->isMegagroup())
		? tr::lng_context_view_group(tr::now)
		: _peer->isUser()
		? tr::lng_context_view_profile(tr::now)
		: tr::lng_context_view_channel(tr::now);
	_addAction(text, [=] {
		if (const auto strong = weak.get()) {
			controller->showPeerInfo(strong);
		}
	}, _peer->isUser() ? &st::menuIconProfile : &st::menuIconInfo);
}

void Filler::addStoryArchive() {
	const auto channel = _peer ? _peer->asChannel() : nullptr;
	if (!channel || !channel->canEditStories()) {
		return;
	}
	const auto controller = _controller;
	const auto weak = base::make_weak(_thread);
	_addAction(tr::lng_stories_archive_button(tr::now), [=] {
		if (const auto strong = weak.get()) {
			controller->showSection(Info::Stories::Make(
				channel,
				Info::Stories::Tab::Archive));
		}
	}, &st::menuIconStoriesArchiveSection);
}

void Filler::addToggleFolder() {
	const auto controller = _controller;
	const auto history = _request.key.history();
	if (_topic || !history || !history->owner().chatsFilters().has()) {
		return;
	}
	_addAction(PeerMenuCallback::Args{
		.text = tr::lng_filters_menu_add(tr::now),
		.handler = nullptr,
		.icon = &st::menuIconAddToFolder,
		.fillSubmenu = [&](not_null<Ui::PopupMenu*> menu) {
			FillChooseFilterMenu(controller, menu, history);
		},
	});
}

void Filler::addToggleUnreadMark() {
	const auto peer = _peer;
	const auto history = _request.key.history();
	if (!_thread) {
		return;
	}
	const auto unread = IsUnreadThread(_thread);
	if ((_thread->asTopic() || peer->isForum()) && !unread) {
		return;
	}
	const auto weak = base::make_weak(_thread);
	const auto label = unread
		? tr::lng_context_mark_read(tr::now)
		: tr::lng_context_mark_unread(tr::now);
	_addAction(label, [=] {
		const auto thread = weak.get();
		if (!thread) {
			return;
		}
		if (unread) {
			MarkAsReadThread(thread);
		} else if (history) {
			peer->owner().histories().changeDialogUnreadMark(history, true);
		}
	}, (unread ? &st::menuIconMarkRead : &st::menuIconMarkUnread));
}

void Filler::addNewWindow() {
	const auto history = _request.key.history();
	if (!_peer
		|| _topic
		|| _peer->isForum()
		|| (history
			&& history->useTopPromotion()
			&& !history->topPromotionType().isEmpty())) {
		return;
	}
	const auto peer = _peer;
	const auto controller = _controller;
	_addAction(tr::lng_context_new_window(tr::now), [=] {
		Ui::PreventDelayedActivation();
		controller->showInNewWindow(peer);
	}, &st::menuIconNewWindow);
	AddSeparatorAndShiftUp(_addAction);
}

void Filler::addToggleArchive() {
	if (!_peer || _topic) {
		return;
	}
	const auto peer = _peer;
	const auto history = _request.key.history();
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
	if (_topic) {
		return;
	}
	const auto channel = _peer->asChannel();
	const auto isGroup = _peer->isChat() || _peer->isMegagroup();
	if (channel) {
		if (!channel->amIn()) {
			return;
		} else if (!channel->canDeleteMessages()
			&& (!isGroup || channel->isPublic() || channel->isForum())) {
			return;
		}
	}
	_addAction(
		tr::lng_profile_clear_history(tr::now),
		ClearHistoryHandler(_controller, _peer),
		&st::menuIconClear);
}

void Filler::addDeleteChat() {
	if (_topic || _peer->isChannel()) {
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
	if (_topic || !channel || !channel->amIn()) {
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

void Filler::addJoinChat() {
	const auto channel = _peer->asChannel();
	if (_topic || !channel || channel->amIn()) {
		return;
	}
	const auto label = _peer->isMegagroup()
		? tr::lng_profile_join_group(tr::now)
		: tr::lng_profile_join_channel(tr::now);
	_addAction(label, [=] {
		channel->session().api().joinChannel(channel);
	}, &st::menuIconAddToFolder);
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
			navigation->showToast(tr::lng_channel_invite_private(tr::now));
			return;
		}
		navigation->showPeerHistory(
			chat,
			Window::SectionShow::Way::Forward);
	}, &st::menuIconDiscussion);
}

void Filler::addExportChat() {
	if (_thread->asTopic() || !_peer->canExportChatHistory()) {
		return;
	}
	const auto peer = _peer;
	_addAction(
		tr::lng_profile_export_chat(tr::now),
		[=] { PeerMenuExportChat(peer); },
		&st::menuIconExport);
}

void Filler::addTranslate() {
	if (_peer->translationFlag() != PeerData::TranslationFlag::Disabled
		|| !_peer->session().premium()
		|| !Core::App().settings().translateChatEnabled()) {
		return;
	}
	const auto history = _peer->owner().historyLoaded(_peer);
	if (!history
		|| !history->translateOfferedFrom()
		|| history->translatedTo()) {
		return;
	}
	_addAction(tr::lng_context_translate(tr::now), [=] {
		history->peer->saveTranslationDisabled(false);
	}, &st::menuIconTranslate);
}

void Filler::addReport() {
	const auto chat = _peer->asChat();
	const auto channel = _peer->asChannel();
	if (_topic
		|| ((!chat || chat->amCreator())
			&& (!channel || channel->amCreator()))) {
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

void Filler::addDeleteTopic() {
	if (!_topic || !_topic->canDelete()) {
		return;
	}
	const auto controller = _controller;
	const auto weak = base::make_weak(_topic);
	const auto callback = [=] {
		if (const auto strong = weak.get()) {
			PeerMenuDeleteTopicWithConfirmation(controller, strong);
		}
	};
	_addAction({
		.text = tr::lng_forum_topic_delete(tr::now),
		.handler = callback,
		.icon = &st::menuIconDeleteAttention,
		.isAttention = true,
	});
}

void Filler::addTopicLink() {
	if (!_topic || _topic->creating()) {
		return;
	}
	const auto channel = _topic->channel();
	const auto id = _topic->rootId();
	const auto controller = _controller;
	_addAction(tr::lng_context_copy_topic_link(tr::now), [=] {
		const auto base = channel->hasUsername()
			? channel->username()
			: "c/" + QString::number(peerToChannel(channel->id).bare);
		const auto query = base + '/' + QString::number(id.bare);
		const auto link = channel->session().createInternalLinkFull(query);
		QGuiApplication::clipboard()->setText(link);
		controller->showToast(channel->hasUsername()
			? tr::lng_channel_public_link_copied(tr::now)
			: tr::lng_context_about_private_link(tr::now));
	}, &st::menuIconCopy);
}

void Filler::addManageTopic() {
	if (!_topic || !_topic->canEdit()) {
		return;
	}
	const auto history = _topic->history();
	const auto rootId = _topic->rootId();
	const auto navigation = _controller;
	_addAction(tr::lng_forum_topic_edit(tr::now), [=] {
		navigation->show(
			Box(EditForumTopicBox, navigation, history, rootId));
	}, &st::menuIconEdit);
}

void Filler::addManageChat() {
	if (!EditPeerInfoBox::Available(_peer)) {
		return;
	}
	const auto peer = _peer;
	const auto navigation = _controller;
	const auto text = peer->isUser()
		? tr::lng_manage_bot_title(tr::now)
		: (peer->isChat() || peer->isMegagroup())
		? tr::lng_manage_group_title(tr::now)
		: tr::lng_manage_channel_title(tr::now);
	_addAction(text, [=] {
		navigation->showEditPeerBox(peer);
	}, &st::menuIconManage);
}

void Filler::addViewStatistics() {
	if (const auto channel = _peer->asChannel()) {
		if (channel->flags() & ChannelDataFlag::CanGetStatistics) {
			const auto controller = _controller;
			const auto weak = base::make_weak(_thread);
			const auto peer = _peer;
			_addAction(tr::lng_stats_title(tr::now), [=] {
				if (const auto strong = weak.get()) {
					controller->showSection(Info::Statistics::Make(peer, {}));
				}
			}, &st::menuIconStats);
			if (!channel->isMegagroup()) {
				_addAction(tr::lng_boosts_title(tr::now), [=] {
					if (const auto strong = weak.get()) {
						controller->showSection(Info::Boosts::Make(peer));
					}
				}, &st::menuIconBoosts);
			}
		}
	}
}

void Filler::addCreatePoll() {
	const auto can = _topic
		? Data::CanSend(_topic, ChatRestriction::SendPolls)
		: _peer->canCreatePolls();
	if (!can) {
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
	const auto replyTo = _request.currentReplyTo;
	auto callback = [=] {
		PeerMenuCreatePoll(
			controller,
			peer,
			replyTo,
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
		tr::lng_chat_theme_wallpaper(tr::now),
		[=] { controller->toggleChooseChatTheme(user); },
		&st::menuIconChangeColors);
}

void Filler::addTTLSubmenu(bool addSeparator) {
	if (_thread->asTopic()) {
		return; // #TODO later forum
	}
	const auto validator = TTLMenu::TTLValidator(
		_controller->uiShow(),
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

void Filler::addGiftPremium() {
	const auto user = _peer->asUser();
	if (!user
		|| user->isInaccessible()
		|| user->isSelf()
		|| user->isBot()
		|| user->isNotificationsUser()
		|| !user->canReceiveGifts()
		|| user->isRepliesChat()) {
		return;
	}

	const auto navigation = _controller;
	_addAction(tr::lng_profile_gift_premium(tr::now), [=] {
		navigation->showGiftPremiumBox(user);
	}, &st::menuIconGiftPremium);
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
	case Section::ContextMenu: fillContextMenuActions(); break;
	default: Unexpected("_request.section in Filler::fill.");
	}
}

void Filler::addCreateTopic() {
	if (!_peer || !_peer->canCreateTopics()) {
		return;
	}
	const auto peer = _peer;
	const auto controller = _controller;
	_addAction(tr::lng_forum_create_topic(tr::now), [=] {
		if (const auto forum = peer->forum()) {
			controller->show(Box(
				NewForumTopicBox,
				controller,
				forum->history()));
		}
	}, &st::menuIconDiscussion);
	_addAction(PeerMenuCallback::Args{ .isSeparator = true });
}

void Filler::addViewAsMessages() {
	if (!_peer || !_peer->isForum()) {
		return;
	}
	const auto peer = _peer;
	const auto controller = _controller;
	_addAction(tr::lng_forum_view_as_messages(tr::now), [=] {
		controller->showPeerHistory(peer->id);
	}, &st::menuIconViewReplies);
}

void Filler::addSearchTopics() {
	const auto forum = _peer ? _peer->forum() : nullptr;
	if (!forum) {
		return;
	}
	const auto history = forum->history();
	const auto controller = _controller;
	_addAction(tr::lng_dlg_filter(tr::now), [=] {
		controller->content()->searchInChat(history);
	}, &st::menuIconSearch);
}

void Filler::fillChatsListActions() {
	if (!_peer || !_peer->isForum()) {
		return;
	}
	addCreateTopic();
	addInfo();
	addViewAsMessages();
	const auto &all = _peer->forum()->topicsList()->indexed()->all();
	if (all.size() > kTopicsSearchMinCount) {
		addSearchTopics();
	}
	addManageChat();
	addNewMembers();
	addVideoChat();
	_addAction(PeerMenuCallback::Args{ .isSeparator = true });
	addReport();
	if (_peer->asChannel()->amIn()) {
		addLeaveChat();
	} else {
		addJoinChat();
	}
}

void Filler::addVideoChat() {
	auto test = Ui::PopupMenu(nullptr);
	FillVideoChatMenu(
		_controller,
		_request,
		Ui::Menu::CreateAddActionCallback(&test));
	if (test.actions().size() < 2) {
		FillVideoChatMenu(_controller, _request, _addAction);
		return;
	}
	_addAction(PeerMenuCallback::Args{
		.text = tr::lng_menu_start_group_call_options(tr::now),
		.handler = nullptr,
		.icon = &st::menuIconVideoChat,
		.fillSubmenu = [&](not_null<Ui::PopupMenu*> menu) {
			FillVideoChatMenu(
				_controller,
				_request,
				Ui::Menu::CreateAddActionCallback(menu));
		},
	});
}

void Filler::fillContextMenuActions() {
	addNewWindow();
	addHidePromotion();
	addToggleArchive();
	addTogglePin();
	if (ViewProfileInChatsListContextMenu.value()) {
		addInfo();
	}
	addToggleMuteSubmenu(false);
	addToggleUnreadMark();
	addToggleTopicClosed();
	addToggleFolder();
	if (const auto user = _peer->asUser()) {
		if (!user->isContact()) {
			addBlockUser();
		}
	}
	addClearHistory();
	addDeleteChat();
	addLeaveChat();
	addDeleteTopic();
}

void Filler::fillHistoryActions() {
	addToggleMuteSubmenu(true);
	addInfo();
	addStoryArchive();
	addSupportInfo();
	addManageChat();
	addCreatePoll();
	addThemeEdit();
	addViewDiscussion();
	addExportChat();
	addTranslate();
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
	addGiftPremium();
	addBotToGroup();
	addNewMembers();
	addViewStatistics();
	addStoryArchive();
	addManageChat();
	addTopicLink();
	addManageTopic();
	addToggleTopicClosed();
	addViewDiscussion();
	addExportChat();
	addBlockUser();
	addReport();
	addLeaveChat();
	addDeleteContact();
	addDeleteTopic();
}

void Filler::fillRepliesActions() {
	if (_topic) {
		addInfo();
		addManageTopic();
	}
	addCreatePoll();
	addToggleTopicClosed();
	addDeleteTopic();
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
		controller->showToast({
			.text = { tr::lng_context_archive_to_menu_info(tr::now) },
			.st = &st::windowArchiveToast,
			.duration = kArchivedToastDuration,
		});

		controller->session().settings().setArchiveInMainMenu(
			!controller->session().settings().archiveInMainMenu());
		controller->session().saveSettingsDelayed();
	}, &st::menuIconToMainMenu);

	MenuAddMarkAsReadChatListAction(
		controller,
		[folder = _folder] { return folder->chatsList(); },
		_addAction);

	_addAction({ .isSeparator = true });
	Settings::PreloadArchiveSettings(&controller->session());
	_addAction(tr::lng_context_archive_settings(tr::now), [=] {
		controller->show(Box(Settings::ArchiveSettingsBox, controller));
	}, &st::menuIconManage);
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
		user->name());
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


void PeerMenuDeleteTopicWithConfirmation(
		not_null<Window::SessionNavigation*> navigation,
		not_null<Data::ForumTopic*> topic) {
	const auto weak = base::make_weak(topic);
	const auto callback = [=](Fn<void()> &&close) {
		close();
		if (const auto strong = weak.get()) {
			PeerMenuDeleteTopic(navigation, strong);
		}
	};
	navigation->parentController()->show(Ui::MakeConfirmBox({
		.text = tr::lng_forum_topic_delete_sure(tr::now),
		.confirmed = callback,
		.confirmText = tr::lng_box_delete(),
		.confirmStyle = &st::attentionBoxButton,
	}));
}

void PeerMenuDeleteTopic(
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChannelData*> channel,
		MsgId rootId) {
	const auto api = &channel->session().api();
	api->request(MTPchannels_DeleteTopicHistory(
		channel->inputChannel,
		MTP_int(rootId)
	)).done([=](const MTPmessages_AffectedHistory &result) {
		const auto offset = api->applyAffectedHistory(channel, result);
		if (offset > 0) {
			PeerMenuDeleteTopic(navigation, channel, rootId);
		} else if (const auto forum = channel->forum()) {
			forum->applyTopicDeleted(rootId);
		}
	}).send();
}

void PeerMenuDeleteTopic(
		not_null<Window::SessionNavigation*> navigation,
		not_null<Data::ForumTopic*> topic) {
	PeerMenuDeleteTopic(navigation, topic->channel(), topic->rootId());
}

void PeerMenuShareContactBox(
		not_null<Window::SessionNavigation*> navigation,
		not_null<UserData*> user) {
	// There is no async to make weak from controller.
	const auto weak = std::make_shared<QPointer<Ui::BoxContent>>();
	auto callback = [=](not_null<Data::Thread*> thread) {
		const auto peer = thread->peer();
		if (!Data::CanSend(thread, ChatRestriction::SendOther)) {
			navigation->parentController()->show(
				Ui::MakeInformBox(tr::lng_forward_share_cant()));
			return;
		} else if (peer->isSelf()) {
			auto action = Api::SendAction(thread);
			action.clearDraft = false;
			user->session().api().shareContact(user, action);
			navigation->showToast(tr::lng_share_done(tr::now));
			if (auto strong = *weak) {
				strong->closeBox();
			}
			return;
		}
		const auto title = thread->asTopic()
			? thread->asTopic()->title()
			: peer->name();
		auto recipient = peer->isUser()
			? title
			: ('\xAB' + title + '\xBB');
		const auto weak = base::make_weak(thread);
		navigation->parentController()->show(
			Ui::MakeConfirmBox({
				.text = tr::lng_forward_share_contact(
					tr::now,
					lt_recipient,
					recipient),
				.confirmed = [weak, user, navigation](Fn<void()> &&close) {
					if (const auto strong = weak.get()) {
						navigation->showThread(
							strong,
							ShowAtTheEndMsgId,
							Window::SectionShow::Way::ClearStack);
						auto action = Api::SendAction(strong);
						action.clearDraft = false;
						strong->session().api().shareContact(user, action);
					}
					close();
				},
				.confirmText = tr::lng_forward_send(),
			}));
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
			}));
}

void PeerMenuCreatePoll(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		FullReplyTo replyTo,
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
		action.replyTo = replyTo;
		const auto topicRootId = replyTo.topicRootId;
		if (const auto local = action.history->localDraft(topicRootId)) {
			action.clearDraft = local->textWithTags.text.isEmpty();
		} else {
			action.clearDraft = false;
		}
		const auto api = &peer->session().api();
		api->polls().create(result.poll, action, crl::guard(weak, [=] {
			weak->closeBox();
		}), crl::guard(weak, [=] {
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
	const auto name = user ? user->shortName() : peer->name();
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
				Ui::Text::Bold(peer->name()),
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

		window->showToast(
			tr::lng_new_contact_block_done(tr::now, lt_user, name));
	}, st::attentionBoxButton);

	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

void PeerMenuUnblockUserWithBotRestart(not_null<UserData*> user) {
	user->session().api().blockedPeers().unblock(user, [=](bool success) {
		if (success && user->isBot() && !user->isSupport()) {
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
		item->originalSender(),
		true,
		Window::ClearReply{ id });
}

QPointer<Ui::BoxContent> ShowChooseRecipientBox(
		not_null<Window::SessionNavigation*> navigation,
		FnMut<bool(not_null<Data::Thread*>)> &&chosen,
		rpl::producer<QString> titleOverride,
		FnMut<void()> &&successCallback,
		InlineBots::PeerTypes typesRestriction) {
	const auto weak = std::make_shared<QPointer<Ui::BoxContent>>();
	auto callback = [
		chosen = std::move(chosen),
		success = std::move(successCallback),
		weak
	](not_null<Data::Thread*> thread) mutable {
		if (!chosen(thread)) {
			return;
		} else if (const auto strong = *weak) {
			strong->closeBox();
		}
		if (success) {
			success();
		}
	};
	auto filter = typesRestriction
		? [=](not_null<Data::Thread*> thread) -> bool {
			using namespace InlineBots;
			const auto peer = thread->peer();
			if (const auto user = peer->asUser()) {
				if (user->isBot()) {
					return (typesRestriction & PeerType::Bot);
				} else {
					return (typesRestriction & PeerType::User);
				}
			} else if (peer->isBroadcast()) {
				return (typesRestriction & PeerType::Broadcast);
			} else {
				return (typesRestriction & PeerType::Group);
			}
		}
		: Fn<bool(not_null<Data::Thread*>)>();
	auto initBox = [=](not_null<PeerListBox*> box) {
		box->addButton(tr::lng_cancel(), [box] {
			box->closeBox();
		});
		if (titleOverride) {
			box->setTitle(std::move(titleOverride));
		}
	};
	*weak = navigation->parentController()->show(Box<PeerListBox>(
		std::make_unique<ChooseRecipientBoxController>(
			&navigation->session(),
			std::move(callback),
			std::move(filter)),
		std::move(initBox)));
	return weak->data();
}

QPointer<Ui::BoxContent> ShowForwardMessagesBox(
		std::shared_ptr<ChatHelpers::Show> show,
		Data::ForwardDraft &&draft,
		Fn<void()> &&successCallback) {
	const auto session = &show->session();
	const auto owner = &session->data();
	const auto msgIds = owner->itemsToIds(owner->idsToItems(draft.ids));
	if (msgIds.empty()) {
		return nullptr;
	}

	class ListBox final : public PeerListBox {
	public:
		using PeerListBox::PeerListBox;

		void setBottomSkip(int bottomSkip) {
			PeerListBox::setInnerBottomSkip(bottomSkip);
		}

		[[nodiscard]] rpl::producer<> focusRequests() const {
			return _focusRequests.events();
		}

		[[nodiscard]] Data::ForwardOptions forwardOptionsData() const {
			return (_forwardOptions.hasCaptions
					&& _forwardOptions.dropCaptions)
				? Data::ForwardOptions::NoNamesAndCaptions
				: _forwardOptions.dropNames
				? Data::ForwardOptions::NoSenderNames
				: Data::ForwardOptions::PreserveInfo;
		}
		[[nodiscard]] Ui::ForwardOptions forwardOptions() const {
			return _forwardOptions;
		}
		void setForwardOptions(Ui::ForwardOptions forwardOptions) {
			_forwardOptions = forwardOptions;
		}

	private:
		rpl::event_stream<> _focusRequests;
		Ui::ForwardOptions _forwardOptions;

	};

	class Controller final : public ChooseRecipientBoxController {
	public:
		using Chosen = not_null<Data::Thread*>;

		Controller(not_null<Main::Session*> session)
		: ChooseRecipientBoxController(
			session,
			[=](Chosen thread) mutable { _singleChosen.fire_copy(thread); },
			nullptr) {
		}

		void rowClicked(not_null<PeerListRow*> row) override final {
			const auto count = delegate()->peerListSelectedRowsCount();
			if (count && row->peer()->isForum()) {
				return;
			} else if (!count || row->peer()->isForum()) {
				ChooseRecipientBoxController::rowClicked(row);
			} else if (count) {
				delegate()->peerListSetRowChecked(row, !row->checked());
				_hasSelectedChanges.fire(
					delegate()->peerListSelectedRowsCount() > 0);
			}
		}

		base::unique_qptr<Ui::PopupMenu> rowContextMenu(
				QWidget *parent,
				not_null<PeerListRow*> row) override final {
			const auto count = delegate()->peerListSelectedRowsCount();
			if (!count && !row->peer()->isForum()) {
				auto menu = base::make_unique_q<Ui::PopupMenu>(
					parent,
					st::popupMenuWithIcons);
				menu->addAction(tr::lng_bot_choose_chat(tr::now), [=] {
					delegate()->peerListSetRowChecked(row, !row->checked());
					_hasSelectedChanges.fire(
						delegate()->peerListSelectedRowsCount() > 0);
				}, &st::menuIconSelect);
				return menu;
			}
			return nullptr;
		}

		[[nodiscard]] rpl::producer<bool> hasSelectedChanges() const{
			return _hasSelectedChanges.events_starting_with(false);
		}

		[[nodiscard]] rpl::producer<Chosen> singleChosen() const{
			return _singleChosen.events();
		}

	private:
		rpl::event_stream<Chosen> _singleChosen;
		rpl::event_stream<bool> _hasSelectedChanges;

	};

	struct State {
		not_null<ListBox*> box;
		not_null<Controller*> controller;
		base::unique_qptr<Ui::PopupMenu> menu;
	};
	const auto state = [&] {
		auto controller = std::make_unique<Controller>(session);
		const auto controllerRaw = controller.get();
		auto box = Box<ListBox>(std::move(controller), nullptr);
		const auto boxRaw = box.data();
		show->showBox(std::move(box));
		auto state = State{ boxRaw, controllerRaw };
		return boxRaw->lifetime().make_state<State>(std::move(state));
	}();

	{ // Chosen a single.
		auto chosen = [show, draft = std::move(draft)](
				not_null<Data::Thread*> thread) mutable {
			const auto peer = thread->peer();
			if (peer->isSelf()
				&& !draft.ids.empty()
				&& draft.ids.front().peer != peer->id) {
				ForwardToSelf(show, draft);
				return true;
			}
			auto controller = Core::App().windowFor(peer);
			if (!controller) {
				return false;
			}
			if (controller->maybeSession() != &peer->session()) {
				controller = peer->isForum()
					? Core::App().ensureSeparateWindowForAccount(
						&peer->account())
					: Core::App().ensureSeparateWindowForPeer(
						peer,
						ShowAtUnreadMsgId);
				if (controller->maybeSession() != &peer->session()) {
					return false;
				}
			}
			const auto content = controller->sessionController()->content();
			return content->setForwardDraft(thread, std::move(draft));
		};
		auto callback = [=, chosen = std::move(chosen)](
				Controller::Chosen thread) mutable {
			const auto weak = Ui::MakeWeak(state->box);
			if (!chosen(thread)) {
				return;
			} else if (const auto strong = weak.data()) {
				strong->closeBox();
			}
			if (successCallback) {
				successCallback();
			}
		};
		state->controller->singleChosen(
		) | rpl::start_with_next(std::move(callback), state->box->lifetime());
	}

	const auto comment = Ui::CreateChild<Ui::SlideWrap<Ui::InputField>>(
		state->box.get(),
		object_ptr<Ui::InputField>(
			state->box,
			st::shareComment,
			Ui::InputField::Mode::MultiLine,
			tr::lng_photos_comment()),
		st::shareCommentPadding);

	const auto send = ShareBox::DefaultForwardCallback(
		show,
		session->data().message(msgIds.front())->history(),
		msgIds);

	const auto submit = [=](Api::SendOptions options) {
		const auto peers = state->box->collectSelectedRows();
		send(
			ranges::views::all(
				peers
			) | ranges::views::transform([&](
					not_null<PeerData*> peer) -> Controller::Chosen {
				return peer->owner().history(peer);
			}) | ranges::to_vector,
			comment->entity()->getTextWithAppliedMarkdown(),
			options,
			state->box->forwardOptionsData());
		if (successCallback) {
			successCallback();
		}
	};

	const auto sendMenuType = [=] {
		const auto selected = state->box->collectSelectedRows();
		return ranges::all_of(selected, HistoryView::CanScheduleUntilOnline)
			? SendMenu::Type::ScheduledToUser
			: ((selected.size() == 1) && selected.front()->isSelf())
			? SendMenu::Type::Reminder
			: SendMenu::Type::Scheduled;
	};

	const auto showForwardOptions = true;
	const auto showMenu = [=](not_null<Ui::RpWidget*> parent) {
		if (state->menu) {
			state->menu = nullptr;
			return;
		}
		state->menu.emplace(parent, st::popupMenuWithIcons);

		if (showForwardOptions) {
			auto createView = [&](
					rpl::producer<QString> &&text,
					bool checked) {
				auto item = base::make_unique_q<Menu::ItemWithCheck>(
					state->menu->menu(),
					st::popupMenuWithIcons.menu,
					Ui::CreateChild<QAction>(state->menu->menu().get()),
					nullptr,
					nullptr);
				std::move(
					text
				) | rpl::start_with_next([action = item->action()](
						QString text) {
					action->setText(text);
				}, item->lifetime());
				item->init(checked);
				const auto view = item->checkView();
				state->menu->addAction(std::move(item));
				return view;
			};
			Ui::FillForwardOptions(
				std::move(createView),
				msgIds.size(),
				state->box->forwardOptions(),
				[=](Ui::ForwardOptions o) {
					state->box->setForwardOptions(o);
				},
				state->menu->lifetime());

			state->menu->addSeparator();
		}
		const auto type = sendMenuType();
		const auto result = SendMenu::FillSendMenu(
			state->menu.get(),
			type,
			SendMenu::DefaultSilentCallback(submit),
			SendMenu::DefaultScheduleCallback(state->box, type, submit),
			SendMenu::DefaultWhenOnlineCallback(submit));
		const auto success = (result == SendMenu::FillMenuResult::Success);
		if (showForwardOptions || success) {
			state->menu->setForcedVerticalOrigin(
				Ui::PopupMenu::VerticalOrigin::Bottom);
			state->menu->popup(QCursor::pos());
		}
	};

	comment->hide(anim::type::instant);
	comment->toggleOn(state->controller->hasSelectedChanges());

	rpl::combine(
		state->box->sizeValue(),
		comment->heightValue()
	) | rpl::start_with_next([=](const QSize &size, int commentHeight) {
		comment->moveToLeft(0, size.height() - commentHeight);
		comment->resizeToWidth(size.width());

		state->box->setBottomSkip(comment->isHidden() ? 0 : commentHeight);
	}, comment->lifetime());

	const auto field = comment->entity();

	field->submits(
	) | rpl::start_with_next([=] { submit({}); }, field->lifetime());
	InitMessageFieldHandlers(
		session,
		show,
		field,
		[=] { return show->paused(GifPauseReason::Layer); });
	field->setSubmitSettings(Core::App().settings().sendSubmitWay());

	Ui::SendPendingMoveResizeEvents(comment);

	state->box->focusRequests(
	) | rpl::start_with_next([=] {
		if (!comment->isHidden()) {
			comment->entity()->setFocusFast();
		}
	}, comment->lifetime());

	state->controller->hasSelectedChanges(
	) | rpl::start_with_next([=](bool shown) {
		state->box->clearButtons();
		if (shown) {
			const auto send = state->box->addButton(
				tr::lng_send_button(),
				[=] { submit({}); });
			send->setAcceptBoth();
			send->clicks(
			) | rpl::start_with_next([=](Qt::MouseButton button) {
				if (button == Qt::RightButton) {
					showMenu(send);
				}
			}, send->lifetime());
		}
		state->box->addButton(tr::lng_cancel(), [=] {
			state->box->closeBox();
		});
	}, state->box->lifetime());

	return QPointer<Ui::BoxContent>(state->box);
}

QPointer<Ui::BoxContent> ShowForwardMessagesBox(
		not_null<Window::SessionNavigation*> navigation,
		Data::ForwardDraft &&draft,
		Fn<void()> &&successCallback) {
	return ShowForwardMessagesBox(
		navigation->uiShow(),
		std::move(draft),
		std::move(successCallback));
}

QPointer<Ui::BoxContent> ShowForwardMessagesBox(
		not_null<Window::SessionNavigation*> navigation,
		MessageIdsList &&items,
		Fn<void()> &&successCallback) {
	return ShowForwardMessagesBox(
		navigation,
		Data::ForwardDraft{ .ids = std::move(items) },
		std::move(successCallback));
}

QPointer<Ui::BoxContent> ShowShareGameBox(
		not_null<Window::SessionNavigation*> navigation,
		not_null<UserData*> bot,
		QString shortName) {
	const auto weak = std::make_shared<QPointer<Ui::BoxContent>>();
	auto chosen = [=](not_null<Data::Thread*> thread) mutable {
		const auto confirm = std::make_shared<QPointer<Ui::BoxContent>>();
		auto send = crl::guard(thread, [=] {
			ShareBotGame(bot, thread, shortName);
			if (const auto strong = *weak) {
				strong->closeBox();
			}
			if (const auto strong = *confirm) {
				strong->closeBox();
			}
			navigation->showThread(
				thread,
				ShowAtUnreadMsgId,
				SectionShow::Way::ClearStack);
		});
		const auto confirmText = thread->peer()->isUser()
			? tr::lng_bot_sure_share_game(
				tr::now,
				lt_user,
				thread->chatListName())
			: tr::lng_bot_sure_share_game_group(
				tr::now,
				lt_group,
				thread->chatListName());
		*confirm = navigation->parentController()->show(
			Ui::MakeConfirmBox({
				.text = confirmText,
				.confirmed = std::move(send),
			}));
	};
	auto filter = [](not_null<Data::Thread*> thread) {
		return !thread->peer()->isSelf()
			&& (Data::CanSend(thread, ChatRestriction::SendGames)
				|| thread->asForum());
	};
	auto initBox = [](not_null<PeerListBox*> box) {
		box->addButton(tr::lng_cancel(), [box] {
			box->closeBox();
		});
	};
	*weak = navigation->parentController()->show(Box<PeerListBox>(
		std::make_unique<ChooseRecipientBoxController>(
			&navigation->session(),
			std::move(chosen),
			std::move(filter)),
		std::move(initBox)));
	return weak->data();
}

QPointer<Ui::BoxContent> ShowDropMediaBox(
		not_null<Window::SessionNavigation*> navigation,
		std::shared_ptr<QMimeData> data,
		not_null<Data::Forum*> forum,
		FnMut<void()> &&successCallback) {
	const auto weak = std::make_shared<QPointer<Ui::BoxContent>>();
	auto chosen = [
		data = std::move(data),
		callback = std::move(successCallback),
		weak,
		navigation
	](not_null<Data::ForumTopic*> topic) mutable {
		const auto content = navigation->parentController()->content();
		if (!content->filesOrForwardDrop(topic, data.get())) {
			return;
		} else if (const auto strong = *weak) {
			strong->closeBox();
		}
		if (callback) {
			callback();
		}
	};
	auto initBox = [=](not_null<PeerListBox*> box) {
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});

		forum->destroyed(
		) | rpl::start_with_next([=] {
			box->closeBox();
		}, box->lifetime());
	};
	*weak = navigation->parentController()->show(Box<PeerListBox>(
		std::make_unique<ChooseTopicBoxController>(
			forum,
			std::move(chosen)),
		std::move(initBox)));
	return weak->data();
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

	const auto list = session->data().idsToItems(items);
	const auto error = GetErrorTextForSending(
		history->peer,
		{ .forward = &list });
	if (!error.isEmpty()) {
		navigation->showToast(error);
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
	return navigation->parentController()->show(Ui::MakeConfirmBox({
		.text = text,
		.confirmed = std::move(done),
		.confirmText = tr::lng_send_button(),
	})).data();
}

void PeerMenuAddChannelMembers(
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChannelData*> channel) {
	if (!channel->isMegagroup()
		&& (channel->membersCount()
			>= channel->session().serverConfig().chatSizeMax)) {
		navigation->parentController()->show(Box<MaxInviteBox>(channel));
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
			Box(PinMessageBox, item),
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
		MsgId topicRootId,
		Fn<void()> onHidden) {
	const auto callback = crl::guard(navigation, [=](Fn<void()> &&close) {
		close();
		auto &session = peer->session();
		const auto migrated = topicRootId ? nullptr : peer->migrateFrom();
		const auto top = Data::ResolveTopPinnedId(
			peer,
			topicRootId,
			migrated);
		const auto universal = !top
			? MsgId(0)
			: (migrated && !peerIsChannel(top.peer))
			? (top.msg - ServerMaxMsgId)
			: top.msg;
		if (universal) {
			session.settings().setHiddenPinnedMessageId(
				peer->id,
				topicRootId,
				universal);
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
		not_null<Data::Thread*> thread) {
	const auto weak = base::make_weak(thread);
	const auto callback = crl::guard(navigation, [=](Fn<void()> &&close) {
		close();
		const auto strong = weak.get();
		if (!strong) {
			return;
		}
		const auto api = &strong->session().api();
		const auto sendRequest = [=](auto self) -> void {
			const auto history = strong->owningHistory();
			const auto topicRootId = strong->topicRootId();
			using Flag = MTPmessages_UnpinAllMessages::Flag;
			api->request(MTPmessages_UnpinAllMessages(
				MTP_flags(topicRootId ? Flag::f_top_msg_id : Flag()),
				history->peer->input,
				MTP_int(topicRootId.bare)
			)).done([=](const MTPmessages_AffectedHistory &result) {
				const auto peer = history->peer;
				const auto offset = api->applyAffectedHistory(peer, result);
				if (offset > 0) {
					self(self);
				} else {
					history->unpinMessagesFor(topicRootId);
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
		const PeerMenuCallback &addAction,
		Fn<Dialogs::UnreadState()> customUnreadState) {
	// There is no async to make weak from controller.
	const auto unreadState = customUnreadState
		? customUnreadState()
		: list()->unreadState();
	if (!unreadState.messages && !unreadState.marks && !unreadState.chats) {
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
			.duration = (archived
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
		controller->show(Box<DeleteMessagesBox>(peer, true));
	};
}

Fn<void()> DeleteAndLeaveHandler(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	return [=] {
		controller->show(Box<DeleteMessagesBox>(peer, false));
	};
}

void FillDialogsEntryMenu(
		not_null<SessionController*> controller,
		Dialogs::EntryState request,
		const PeerMenuCallback &callback) {
	Filler(controller, request, callback).fill();
}

bool FillVideoChatMenu(
		not_null<SessionController*> controller,
		Dialogs::EntryState request,
		const PeerMenuCallback &addAction) {
	const auto peer = request.key.peer();
	if (!peer || peer->isUser()) {
		return false;
	}

	const auto callback = [=](Calls::StartGroupCallArgs &&args) {
		controller->startOrJoinGroupCall(peer, std::move(args));
	};
	const auto rtmpCallback = [=] {
		Core::App().calls().showStartWithRtmp(controller->uiShow(), peer);
	};
	const auto livestream = !peer->isMegagroup() && peer->isChannel();
	const auto has = (peer->groupCall() != nullptr);
	const auto manager = peer->canManageGroupCall();
	const auto creator = peer->isChat()
		? peer->asChat()->amCreator()
		: peer->asChannel()->amCreator();
	if (has) {
		addAction(
			tr::lng_menu_start_group_call_join(tr::now),
			[=] { callback({}); },
			&st::menuIconVideoChat);
	} else if (manager) {
		addAction(
			(livestream
				? tr::lng_menu_start_group_call_channel
				: tr::lng_menu_start_group_call)(tr::now),
			[=] { callback({}); },
			creator ? &st::menuIconStartStream : &st::menuIconVideoChat);
	}
	if (!has && creator) {
		addAction(
			(livestream
				? tr::lng_menu_start_group_call_scheduled_channel
				: tr::lng_menu_start_group_call_scheduled)(tr::now),
			[=] { callback({ .scheduleNeeded = true }); },
			&st::menuIconReschedule);
		addAction(
			(livestream
				? tr::lng_menu_start_group_call_with_channel
				: tr::lng_menu_start_group_call_with)(tr::now),
			rtmpCallback,
			&st::menuIconStartStreamWith);
	}
	return has || manager;
}

bool IsUnreadThread(not_null<Data::Thread*> thread) {
	return thread->chatListBadgesState().unread;
}

void MarkAsReadThread(not_null<Data::Thread*> thread) {
	const auto readHistory = [&](not_null<History*> history) {
		history->owner().histories().readInbox(history);
	};
	if (!IsUnreadThread(thread)) {
		return;
	} else if (const auto forum = thread->asForum()) {
		forum->enumerateTopics([](
			not_null<Data::ForumTopic*> topic) {
			MarkAsReadThread(topic);
		});
	} else if (const auto history = thread->asHistory()) {
		readHistory(history);
		if (const auto migrated = history->migrateSibling()) {
			readHistory(migrated);
		}
	} else if (const auto topic = thread->asTopic()) {
		topic->readTillEnd();
	}
}

void AddSeparatorAndShiftUp(const PeerMenuCallback &addAction) {
	addAction({ .isSeparator = true });

	const auto &st = st::popupMenuExpandedSeparator.menu;
	const auto shift = st::popupMenuExpandedSeparator.scrollPadding.top()
		+ st.itemPadding.top()
		+ st.itemStyle.font->height
		+ st.itemPadding.bottom()
		+ st.separator.padding.top()
		+ st.separator.width / 2;
	addAction({ .addTopShift = -shift });
}

} // namespace Window
