/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_peer_menu.h"

#include "base/call_delayed.h"
#include "menu/menu_check_item.h"
#include "boxes/about_box.h"
#include "boxes/share_box.h"
#include "boxes/star_gift_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/share_message_phrase_factory.h"
#include "ui/controls/userpic_button.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/fields/input_field.h"
#include "api/api_chat_participants.h"
#include "api/api_global_privacy.h"
#include "lang/lang_keys.h"
#include "ui/boxes/confirm_box.h"
#include "base/random.h"
#include "base/options.h"
#include "base/unixtime.h"
#include "base/unique_qptr.h"
#include "base/qt/qt_key_modifiers.h"
#include "boxes/delete_messages_box.h"
#include "boxes/max_invite_box.h"
#include "boxes/moderate_messages_box.h"
#include "boxes/choose_filter_box.h"
#include "boxes/create_poll_box.h"
#include "boxes/edit_todo_list_box.h"
#include "boxes/pin_messages_box.h"
#include "boxes/premium_limits_box.h"
#include "boxes/report_messages_box.h"
#include "boxes/peers/add_bot_to_chat_box.h"
#include "boxes/peers/add_participants_box.h"
#include "boxes/peers/edit_forum_topic_box.h"
#include "boxes/peers/edit_contact_box.h"
#include "boxes/peers/prepare_short_info_box.h"
#include "calls/calls_instance.h"
#include "inline_bots/bot_attach_web_view.h" // InlineBots::PeerType.
#include "ui/toast/toast.h"
#include "ui/text/custom_emoji_helper.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/chat_filters_tabs_strip.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/layers/generic_box.h"
#include "ui/delayed_activation.h"
#include "ui/vertical_list.h"
#include "ui/ui_utility.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "menu/menu_mute.h"
#include "menu/menu_ttl_validator.h"
#include "apiwrap.h"
#include "mainwidget.h"
#include "api/api_blocked_peers.h"
#include "api/api_chat_filters.h"
#include "api/api_polls.h"
#include "api/api_todo_lists.h"
#include "api/api_updates.h"
#include "mtproto/mtproto_config.h"
#include "history/history.h"
#include "history/history_item_helpers.h" // GetErrorForSending.
#include "history/history_item_components.h"
#include "history/view/history_view_context_menu.h"
#include "window/window_separate_id.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "settings/settings_advanced.h"
#include "support/support_helper.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "info/channel_statistics/boosts/info_boosts_widget.h"
#include "info/channel_statistics/earn/info_channel_earn_widget.h"
#include "info/channel_statistics/earn/earn_icons.h"
#include "info/profile/info_profile_cover.h"
#include "info/profile/info_profile_values.h"
#include "info/statistics/info_statistics_widget.h"
#include "info/stories/info_stories_widget.h"
#include "data/components/scheduled_messages.h"
#include "data/notify/data_notify_settings.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_changes.h"
#include "data/data_session.h"
#include "data/data_folder.h"
#include "data/data_poll.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_user.h"
#include "data/data_saved_messages.h"
#include "data/data_saved_sublist.h"
#include "data/data_histories.h"
#include "data/data_chat_filters.h"
#include "dialogs/dialogs_key.h"
#include "core/application.h"
#include "core/ui_integration.h"
#include "export/export_manager.h"
#include "boxes/peers/edit_peer_info_box.h"
#include "boxes/premium_preview_box.h"
#include "styles/style_chat.h"
#include "styles/style_credits.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_window.h" // st::windowMinWidth
#include "styles/style_menu_icons.h"

#include <QAction>
#include <QtWidgets/QApplication>

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
			MTPInputPeer(), // send_as
			MTPInputQuickReplyShortcut(),
			MTPlong(),
			MTPlong(),
			MTPSuggestedPost()
		), [=](const MTPUpdates &, const MTP::Response &) {
	}, [=](const MTP::Error &error, const MTP::Response &) {
		history->session().api().sendMessageFail(error, history->peer);
	});
}

} // namespace

const char kOptionViewProfileInChatsListContextMenu[]
	= "view-profile-in-chats-list-context-menu";

namespace {

constexpr auto kArchivedToastDuration = crl::time(5000);
constexpr auto kMaxUnreadWithoutConfirmation = 1000;

base::options::toggle ViewProfileInChatsListContextMenu({
	.id = kOptionViewProfileInChatsListContextMenu,
	.name = "Add \"View Profile\"",
	.description = "Add \"View Profile\" to context menu in chat list",
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
	void fillSavedSublistActions();
	void fillContextMenuActions();
	void fillMonoforumPeerActions();

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
	void addCreateTodoList();
	void addThemeEdit();
	void addBlockUser();
	void addViewDiscussion();
	void addDirectMessages();
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
	void addSendGift();
	void addCreateTopic();
	void addViewAsMessages();
	void addViewAsTopics();
	void addSearchTopics();
	void addDeleteTopic();
	void addVideoChat();
	void addViewStatistics();
	void addBoostChat();
	void addToggleFee();

	[[nodiscard]] bool skipCreateActions() const;

	not_null<SessionController*> _controller;
	Dialogs::EntryState _request;
	Data::Thread *_thread = nullptr;
	Data::ForumTopic *_topic = nullptr;
	PeerData *_peer = nullptr;
	Data::Folder *_folder = nullptr;
	Data::SavedSublist *_sublist = nullptr;
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
		not_null<Dialogs::Entry*> entry) {
	const auto owner = &entry->owner();
	if (owner->pinnedCanPin(entry)) {
		return false;
	}
	// Some old chat, that was converted, maybe is still pinned.
	if (const auto sublist = entry->asSublist()) {
		controller->show(Box(SublistsPinsLimitBox, &sublist->session()));
		return true;
	} else if (const auto topic = entry->asTopic()) {
		controller->show(Box(ForumPinsLimitBox, topic->forum()));
		return true;
	}
	const auto history = entry->asHistory();
	Assert(history != nullptr);
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
		not_null<Dialogs::Entry*> entry,
		Fn<void()> onToggled) {
	if (!entry->folderKnown()) {
		return;
	}
	const auto owner = &entry->owner();
	const auto isPinned = !entry->isPinnedDialog(FilterId());
	if (isPinned && PinnedLimitReached(controller, entry)) {
		return;
	}

	owner->setChatPinned(entry, FilterId(), isPinned);
	if (const auto history = entry->asHistory()) {
		const auto flags = isPinned
			? MTPmessages_ToggleDialogPin::Flag::f_pinned
			: MTPmessages_ToggleDialogPin::Flag(0);
		owner->session().api().request(MTPmessages_ToggleDialogPin(
			MTP_flags(flags),
			MTP_inputDialogPeer(history->peer->input)
		)).done([=] {
			owner->notifyPinnedDialogsOrderUpdated();
			if (onToggled) {
				onToggled();
			}
		}).send();
		if (isPinned) {
			controller->content()->dialogsToUp();
		}
	} else if (const auto topic = entry->asTopic()) {
		const auto peer = topic->peer();
		owner->session().api().request(MTPmessages_UpdatePinnedForumTopic(
			peer->input,
			MTP_int(topic->rootId()),
			MTP_bool(isPinned)
		)).done([=](const MTPUpdates &result) {
			owner->session().api().applyUpdates(result);
			if (onToggled) {
				onToggled();
			}
		}).send();
	} else if (const auto sublist = entry->asSublist()) {
		const auto flags = isPinned
			? MTPmessages_ToggleSavedDialogPin::Flag::f_pinned
			: MTPmessages_ToggleSavedDialogPin::Flag(0);
		owner->session().api().request(MTPmessages_ToggleSavedDialogPin(
			MTP_flags(flags),
			MTP_inputDialogPeer(sublist->sublistPeer()->input)
		)).done([=] {
			owner->notifyPinnedDialogsOrderUpdated();
			if (onToggled) {
				onToggled();
			}
		}).send();
		//if (isPinned) {
		//	controller->content()->dialogsToUp();
		//}
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
, _sublist(request.key.sublist())
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
	const auto text = closed
		? tr::lng_forum_topic_reopen(tr::now)
		: tr::lng_forum_topic_close(tr::now);
	_addAction(text, [=] {
		if (const auto topic = weak.get()) {
			topic->setClosedAndSave(!closed);
		}
	}, closed ? &st::menuIconRestartBot : &st::menuIconBlock);
}

void Filler::addTogglePin() {
	if ((!_sublist && !_peer) || (_topic && !_topic->canTogglePinned())) {
		return;
	} else if (_request.section == Section::SubsectionTabsMenu
		&& !_sublist
		&& !_topic) {
		return;
	} else if (_sublist && !_peer->isSelf()) {
		return;
	}
	const auto controller = _controller;
	const auto filterId = _request.filterId;
	const auto entry = _thread ? (Dialogs::Entry*)_thread : _sublist;
	if (!entry || entry->fixedOnTopIndex()) {
		return;
	}
	const auto pinText = [=] {
		return entry->isPinnedDialog(filterId)
			? tr::lng_context_unpin_from_top(tr::now)
			: tr::lng_context_pin_to_top(tr::now);
	};
	const auto weak = base::make_weak(entry);
	const auto pinToggle = [=] {
		if (const auto strong = weak.get()) {
			TogglePinnedThread(controller, strong, filterId, nullptr);
		}
	};
	_addAction(
		pinText(),
		pinToggle,
		(entry->isPinnedDialog(filterId)
			? &st::menuIconUnpin
			: &st::menuIconPin));
}

void Filler::addToggleMuteSubmenu(bool addSeparator) {
	if (!_thread
		|| _thread->peer()->isSelf()
		|| _thread->asSublist()
		|| (_thread->asHistory() && _thread->asHistory()->isForum())) {
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
	const auto sublist = _thread ? _thread->asSublist() : nullptr;
	const auto infoPeer = sublist ? sublist->sublistPeer().get() : _peer;
	if (infoPeer
		&& (infoPeer->isSelf()
			|| infoPeer->isRepliesChat()
			|| infoPeer->isVerifyCodes())) {
		return;
	} else if (!_thread) {
		return;
	} else if (_controller->adaptive().isThreeColumn()) {
		const auto thread = _controller->activeChatCurrent().thread();
		if (thread && !thread->asSublist() && thread == _thread) {
			if (Core::App().settings().thirdSectionInfoEnabled()
				|| Core::App().settings().tabbedReplacedWithInfo()) {
				return;
			}
		}
	}
	const auto controller = _controller;
	const auto weak = base::make_weak(_thread);
	const auto text = _thread->asTopic()
		? (_thread->peer()->isBot()
			? tr::lng_context_view_thread(tr::now)
			: tr::lng_context_view_topic(tr::now))
		: (infoPeer->isChat() || infoPeer->isMegagroup())
		? tr::lng_context_view_group(tr::now)
		: infoPeer->isUser()
		? tr::lng_context_view_profile(tr::now)
		: tr::lng_context_view_channel(tr::now);
	_addAction(text, [=] {
		if (const auto strong = weak.get()) {
			if (base::IsCtrlPressed()) {
				controller->uiShow()->showBox(
					PrepareShortInfoBox(infoPeer, controller));
			} else {
				controller->showPeerInfo(strong);
			}
		}
	}, infoPeer->isUser() ? &st::menuIconProfile : &st::menuIconInfo);
}

void Filler::addStoryArchive() {
	const auto channel = _peer ? _peer->asChannel() : nullptr;
	if (!channel || !channel->canEditStories()) {
		return;
	}
	const auto controller = _controller;
	const auto weak = base::make_weak(_thread);
	_addAction(tr::lng_stories_archive_button(tr::now), [=] {
		if ([[maybe_unused]] const auto strong = weak.get()) {
			controller->showSection(Info::Stories::Make(
				channel,
				Info::Stories::ArchiveId()));
		}
	}, &st::menuIconStoriesArchiveSection);
}

void Filler::addToggleFolder() {
	const auto controller = _controller;
	const auto history = _request.key.history();
	if (_topic
		|| !history
		|| !history->owner().chatsFilters().has()
		|| !history->inChatList()) {
		return;
	} else if (_request.section == Section::SubsectionTabsMenu
		&& !_sublist
		&& !_topic) {
		return;
	}
	_addAction(PeerMenuCallback::Args{
		.text = tr::lng_filters_menu_add(tr::now),
		.handler = nullptr,
		.icon = &st::menuIconAddToFolder,
		.fillSubmenu = [&](not_null<Ui::PopupMenu*> menu) {
			FillChooseFilterMenu(controller, menu, history);
		},
		.submenuSt = &st::foldersMenu,
	});
}

void Filler::addToggleUnreadMark() {
	const auto peer = _peer;
	const auto unread = IsUnreadThread(_thread);
	const auto history = _request.key.history();
	if (!_thread || !_thread->canToggleUnread(unread)) {
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
		} else if (const auto sublist = thread->asSublist()) {
			peer->owner().histories().changeSublistUnreadMark(sublist, true);
		} else if (history) {
			peer->owner().histories().changeDialogUnreadMark(history, true);
		}
	}, (unread ? &st::menuIconMarkRead : &st::menuIconMarkUnread));
}

void Filler::addNewWindow() {
	const auto controller = _controller;
	if (_folder) {
		_addAction(tr::lng_context_new_window(tr::now), [=] {
			Ui::PreventDelayedActivation();
			controller->showInNewWindow(SeparateId(
				SeparateType::Archive,
				&controller->session()));
		}, &st::menuIconNewWindow);
		AddSeparatorAndShiftUp(_addAction);
		return;
	} else if (const auto weak = base::make_weak(_sublist)) {
		_addAction(tr::lng_context_new_window(tr::now), [=] {
			Ui::PreventDelayedActivation();
			if (const auto sublist = weak.get()) {
				controller->showInNewWindow(SeparateId(
					SeparateType::SavedSublist,
					sublist));
			}
		}, &st::menuIconNewWindow);
		AddSeparatorAndShiftUp(_addAction);
		return;
	}
	const auto history = _request.key.history();
	if (!_peer
		|| (history
			&& history->useTopPromotion()
			&& !history->topPromotionType().isEmpty())) {
		return;
	}
	const auto peer = _peer;
	const auto thread = _topic
		? not_null<Data::Thread*>(_topic)
		: _peer->owner().history(_peer);
	const auto weak = base::make_weak(thread);
	_addAction(tr::lng_context_new_window(tr::now), [=] {
		Ui::PreventDelayedActivation();
		if (const auto strong = weak.get()) {
			const auto forum = !strong->asTopic()
				&& peer->isForum()
				&& !peer->useSubsectionTabs();
			controller->showInNewWindow(SeparateId(
				forum ? SeparateType::Forum : SeparateType::Chat,
				strong));
		}
	}, &st::menuIconNewWindow);
	AddSeparatorAndShiftUp(_addAction);
}

void Filler::addToggleArchive() {
	if (!_peer
		|| _topic
		|| _request.section == Section::SubsectionTabsMenu) {
		return;
	}
	const auto peer = _peer;
	const auto history = _request.key.history();
	if (!CanArchive(history, peer)) {
		return;
	}
	const auto isArchived = [=] {
		return IsArchived(history);
	};
	const auto label = [=] {
		return isArchived()
			? tr::lng_archived_remove(tr::now)
			: tr::lng_archived_add(tr::now);
	};
	const auto toggle = [=, show = _controller->uiShow()] {
		ToggleHistoryArchived(show, history, !isArchived());
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
	if (_topic || _peer->isMonoforum()) {
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
	if (_topic || (!_sublist && _peer->isChannel())) {
		return;
	}
	_addAction({
		.text = ((_peer->isUser() || _sublist)
			? tr::lng_profile_delete_conversation(tr::now)
			: tr::lng_profile_clear_and_exit(tr::now)),
		.handler = (_sublist
			? DeleteSublistHandler(_controller, _sublist)
			: DeleteAndLeaveHandler(_controller, _peer)),
		.icon = &st::menuIconDeleteAttention,
		.isAttention = true,
	});
}

void Filler::addLeaveChat() {
	const auto channel = _peer->asChannel();
	if (_topic || _sublist || !channel || !channel->amIn()) {
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
		|| user->isRepliesChat()
		|| user->isVerifyCodes()) {
		return;
	}
	const auto window = _controller;
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
		const auto show = window->uiShow();
		if (show->showFrozenError()) {
			return;
		} else if (user->isBlocked()) {
			PeerMenuUnblockUserWithBotRestart(show, user);
		} else if (user->isBot()) {
			user->session().api().blockedPeers().block(user);
		} else {
			window->show(Box(
				PeerMenuBlockUserBox,
				&window->window(),
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
	const auto chat = channel->discussionLink();
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

void Filler::addDirectMessages() {
	const auto channel = _peer->asBroadcast();
	if (!channel) {
		return;
	}
	const auto monoforum = channel->broadcastMonoforum();
	if (!monoforum || !monoforum->amMonoforumAdmin()) {
		return;
	}
	const auto navigation = _controller;
	_addAction(tr::lng_profile_direct_messages(tr::now), [=] {
		navigation->showPeerHistory(
			monoforum,
			Window::SectionShow::Way::Forward);
	}, &st::menuIconChatDiscuss);
}

void Filler::addExportChat() {
	if (_thread->asTopic() || !_peer->canExportChatHistory()) {
		return;
	}
	const auto peer = _peer;
	const auto navigation = _controller;
	_addAction(
		tr::lng_profile_export_chat(tr::now),
		[=] { PeerMenuExportChat(navigation, peer); },
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
		ShowReportMessageBox(navigation->uiShow(), peer, {}, {});
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
	const auto edit = [=] {
		if (controller->showFrozenError()) {
			return;
		}
		controller->show(Box(EditContactBox, controller, user));
	};
	_addAction(
		tr::lng_info_add_as_contact(tr::now),
		edit,
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
	const auto edit = [=] {
		if (controller->showFrozenError()) {
			return;
		}
		controller->show(Box(EditContactBox, controller, user));
	};
	_addAction(
		tr::lng_info_edit_contact(tr::now),
		edit,
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
	if (!channel) {
		return;
	}
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
	const auto text = _topic->forum()->peer()->isBot()
		? tr::lng_bot_thread_edit(tr::now)
		: tr::lng_forum_topic_edit(tr::now);
	_addAction(text, [=] {
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

void Filler::addBoostChat() {
	if (const auto channel = _peer->asChannel()) {
		if (channel->isMonoforum()) {
			return;
		}
		const auto text = channel->isMegagroup()
			? tr::lng_boost_group_button(tr::now)
			: tr::lng_boost_channel_button(tr::now);
		const auto weak = base::make_weak(_controller);
		_addAction(text, [=] {
			if (const auto strong = weak.get()) {
				strong->resolveBoostState(channel);
			}
		}, &st::menuIconBoosts);
	}
}

void Filler::addViewStatistics() {
	if (const auto channel = _peer->asChannel()) {
		if (channel->isMonoforum()) {
			return;
		}
		const auto controller = _controller;
		const auto weak = base::make_weak(_thread);
		const auto peer = _peer;
		using Flag = ChannelDataFlag;
		const auto canGetStats = (channel->flags() & Flag::CanGetStatistics);
		const auto canViewEarn = (channel->flags() & Flag::CanViewRevenue);
		const auto canViewCreditsEarn
			= (channel->flags() & Flag::CanViewCreditsRevenue);
		if (canGetStats) {
			_addAction(tr::lng_stats_title(tr::now), [=] {
				if ([[maybe_unused]] const auto strong = weak.get()) {
					using namespace Info;
					controller->showSection(Statistics::Make(peer, {}, {}));
				}
			}, &st::menuIconStats);
		}
		if (canGetStats
			|| channel->amCreator()
			|| channel->canPostStories()) {
			_addAction(tr::lng_boosts_title(tr::now), [=] {
				if ([[maybe_unused]] const auto strong = weak.get()) {
					controller->showSection(Info::Boosts::Make(peer));
				}
			}, &st::menuIconBoosts);
		}
		if (canViewEarn || canViewCreditsEarn) {
			_addAction(tr::lng_channel_earn_title(tr::now), [=] {
				if ([[maybe_unused]] const auto strong = weak.get()) {
					controller->showSection(Info::ChannelEarn::Make(peer));
				}
			}, &st::menuIconEarn);
		}
	}
}

bool Filler::skipCreateActions() const {
	if (_peer && _peer->isRepliesChat()) {
		return true;
	}
	const auto isJoinChannel = [&] {
		if (_request.section != Section::Replies) {
			if (const auto c = _peer->asChannel(); c && !c->amIn()) {
				return true;
			}
		}
		return false;
	}();
	const auto isBotStart = [&] {
		const auto user = _peer ? _peer->asUser() : nullptr;
		if (!user || !user->isBot()) {
			return false;
		} else if (!user->botInfo->startToken.isEmpty()) {
			return true;
		}
		const auto history = _peer->owner().history(_peer);
		if (history && history->isEmpty() && !history->lastMessage()) {
			return true;
		}
		return false;
	}();
	const auto isBlocked = [&] {
		return _peer && _peer->isUser() && _peer->asUser()->isBlocked();
	}();
	return isBlocked || isJoinChannel || isBotStart;
}

void Filler::addCreatePoll() {
	if (skipCreateActions()) {
		return;
	}
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
		: (_request.section == Section::Replies
			|| _peer->starsPerMessageChecked())
		? SendMenu::Type::SilentOnly
		: SendMenu::Type::Scheduled;
	const auto flag = PollData::Flags();
	const auto replyTo = _request.currentReplyTo;
	const auto suggest = _request.currentSuggest;
	auto callback = [=] {
		PeerMenuCreatePoll(
			controller,
			peer,
			replyTo,
			suggest,
			flag,
			flag,
			source,
			{ sendMenuType });
	};
	_addAction(
		tr::lng_polls_create(tr::now),
		std::move(callback),
		&st::menuIconCreatePoll);
}

void Filler::addCreateTodoList() {
	if (skipCreateActions()) {
		return;
	}
	const auto can = _topic
		? (_peer->session().premium()
			&& Data::CanSend(_topic, ChatRestriction::SendPolls))
		: _peer->canCreateTodoLists();
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
		: (_request.section == Section::Replies
			|| _peer->starsPerMessageChecked())
		? SendMenu::Type::SilentOnly
		: SendMenu::Type::Scheduled;
	const auto replyTo = _request.currentReplyTo;
	const auto suggest = _request.currentSuggest;
	auto callback = [=] {
		PeerMenuCreateTodoList(
			controller,
			peer,
			replyTo,
			suggest,
			source,
			{ sendMenuType });
	};
	_addAction(
		tr::lng_todo_create(tr::now),
		std::move(callback),
		&st::menuIconCreateTodoList);
}

void Filler::addThemeEdit() {
	if (_peer->isVerifyCodes() || _peer->isRepliesChat()) {
		return;
	}
	const auto user = _peer->asUser();
	if (!user || user->isInaccessible()) {
		return;
	}
	if (user->requiresPremiumToWrite() && !user->session().premium()) {
		return;
	}
	const auto controller = _controller;
	_addAction(
		tr::lng_chat_theme_wallpaper(tr::now),
		[=] { controller->toggleChooseChatTheme(user); },
		&st::menuIconChangeColors);
}

void Filler::addTTLSubmenu(bool addSeparator) {
	if (_thread->asTopic() || !_peer || _peer->isMonoforum()) {
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

void Filler::addSendGift() {
	const auto user = _peer->asUser();
	const auto channel = _peer->asBroadcast();
	if (!user && !channel) {
		return;
	} else if (user
		&& (user->isInaccessible()
			|| user->isSelf()
			|| user->isBot()
			|| user->isServiceUser()
			|| user->isNotificationsUser()
			|| user->isRepliesChat()
			|| user->isVerifyCodes()
			|| !user->session().premiumCanBuy())) {
		return;
	} else if (channel
		&& (channel->isForbidden() || !channel->stargiftsAvailable())) {
		return;
	}

	const auto peer = _peer;
	const auto navigation = _controller;
	_addAction(tr::lng_profile_gift_premium(tr::now), [=] {
		Ui::ShowStarGiftBox(navigation, peer);
	}, &st::menuIconGiftPremium);
}

void Filler::fill() {
	if (_folder) {
		fillArchiveActions();
	} else if (_sublist && _peer->isSelf()) {
		fillSavedSublistActions();
	} else switch (_request.section) {
	case Section::ChatsList: fillChatsListActions(); break;
	case Section::History: fillHistoryActions(); break;
	case Section::Profile: fillProfileActions(); break;
	case Section::Replies: fillRepliesActions(); break;
	case Section::Scheduled: fillScheduledActions(); break;
	case Section::ContextMenu:
	case Section::SubsectionTabsMenu: fillContextMenuActions(); break;
	case Section::SavedSublist: fillMonoforumPeerActions(); break;
	default: Unexpected("_request.section in Filler::fill.");
	}
}

void Filler::addCreateTopic() {
	if (!_peer || !_peer->canCreateTopics() || _peer->isBot()) {
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
	const auto parentHideRequests = std::make_shared<rpl::event_stream<>>();
	const auto filterOutChatPreview = [=] {
		if (base::IsAltPressed()) {
			const auto callback = [=](bool shown) {
				if (!shown) {
					parentHideRequests->fire({});
				}
			};
			controller->showChatPreview({
				peer->owner().history(peer),
				FullMsgId(),
			}, callback, QApplication::activePopupWidget());
			return true;
		} else if (base::IsCtrlPressed()) {
			Ui::PreventDelayedActivation();
			controller->showInNewWindow(SeparateId(
				SeparateType::Chat,
				peer->owner().history(peer)));
			return true;
		}
		return false;
	};
	const auto open = [=] {
		if (const auto forum = peer->forum()) {
			peer->owner().saveViewAsMessages(forum, true);
		}
		controller->showPeerHistory(peer->id);
	};
	auto to_instant = rpl::map_to(anim::type::instant);
	_addAction({
		.text = tr::lng_forum_view_as_messages(tr::now),
		.handler = open,
		.icon = &st::menuIconAsMessages,
		.triggerFilter = filterOutChatPreview,
		.hideRequests = parentHideRequests->events() | to_instant
	});
}

void Filler::addViewAsTopics() {
	if (!_peer
		|| !_peer->isForum()
		|| !_peer->isChannel()
		|| (_peer->asChannel()->flags() & ChannelDataFlag::ForumTabs)
		|| !_controller->adaptive().isOneColumn()) {
		return;
	}
	const auto peer = _peer;
	const auto controller = _controller;
	_addAction(tr::lng_forum_view_as_topics(tr::now), [=] {
		if (const auto forum = peer->forum()) {
			peer->owner().saveViewAsMessages(forum, false);
			controller->showForum(forum);
		}
	}, &st::menuIconAsTopics);
}

void Filler::addSearchTopics() {
	const auto forum = _peer ? _peer->forum() : nullptr;
	if (!forum) {
		return;
	}
	const auto history = forum->history();
	const auto controller = _controller;
	_addAction(tr::lng_dlg_filter(tr::now), [=] {
		controller->searchInChat(history);
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
	addBoostChat();
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
	addCreateTopic();
	addInfo();
	addViewAsTopics();
	addManageChat();
	addStoryArchive();
	addSupportInfo();
	addBoostChat();
	addCreatePoll();
	addCreateTodoList();
	addThemeEdit();
	addViewDiscussion();
	addDirectMessages();
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
	addBotToGroup();
	addNewMembers();
	addSendGift();
	addViewStatistics();
	addStoryArchive();
	addManageChat();
	addTopicLink();
	addManageTopic();
	addToggleTopicClosed();
	addViewDiscussion();
	addDirectMessages();
	addExportChat();
	addToggleFolder();
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
	addBoostChat();
	addCreatePoll();
	addCreateTodoList();
	addToggleTopicClosed();
	addDeleteTopic();
}

void Filler::fillScheduledActions() {
	addCreatePoll();
	addCreateTodoList();
}

void Filler::fillArchiveActions() {
	Expects(_folder != nullptr);

	if (_folder->id() != Data::Folder::kId) {
		return;
	}
	addNewWindow();

	const auto controller = _controller;
	const auto hidden = controller->session().settings().archiveCollapsed();
	const auto inmenu = controller->session().settings().archiveInMainMenu();
	if (!inmenu) {
		const auto text = hidden
			? tr::lng_context_archive_expand(tr::now)
			: tr::lng_context_archive_collapse(tr::now);
		_addAction(text, [=] {
			controller->session().settings().setArchiveCollapsed(!hidden);
			controller->session().saveSettingsDelayed();
		}, hidden ? &st::menuIconExpand : &st::menuIconCollapse);
	}
	{
		const auto text = inmenu
			? tr::lng_context_archive_to_list(tr::now)
			: tr::lng_context_archive_to_menu(tr::now);
		_addAction(text, [=] {
			if (!inmenu) {
				controller->showToast({
					.text = {
						tr::lng_context_archive_to_menu_info(tr::now)
					},
					.st = &st::windowArchiveToast,
					.duration = kArchivedToastDuration,
				});
			}
			controller->session().settings().setArchiveInMainMenu(!inmenu);
			controller->session().saveSettingsDelayed();
			controller->window().hideSettingsAndLayer();
		}, inmenu ? &st::menuIconFromMainMenu : &st::menuIconToMainMenu);
	}

	MenuAddMarkAsReadChatListAction(
		controller,
		[folder = _folder] { return folder->chatsList(); },
		_addAction);

	_addAction({ .isSeparator = true });

	Settings::PreloadArchiveSettings(&controller->session());
	const auto openSettings = [=] {
		controller->show(Box(Settings::ArchiveSettingsBox, controller));
	};
	_addAction(
		tr::lng_context_archive_settings(tr::now),
		openSettings,
		&st::menuIconManage);
	_addAction(tr::lng_context_archive_how_does_it_work(tr::now), [=] {
		const auto unarchiveOnNewMessage = controller->session().api(
			).globalPrivacy().unarchiveOnNewMessageCurrent();
		controller->show(
			Box(
				ArchiveHintBox,
				unarchiveOnNewMessage != Api::UnarchiveOnNewMessage::None,
				openSettings),
			Ui::LayerOption::CloseOther);
	}, &st::menuIconFaq);
}

void Filler::fillSavedSublistActions() {
	addNewWindow();
	addTogglePin();
}

void Filler::fillMonoforumPeerActions() {
	Expects(_sublist != nullptr);

	addToggleFee();
}

void Filler::addToggleFee() {
	const auto feeRemoved = _sublist->isFeeRemoved();
	const auto text = feeRemoved
		? tr::lng_context_charge_fee(tr::now)
		: tr::lng_context_remove_fee(tr::now);
	const auto navigation = _controller;
	const auto parent = _sublist->parentChat();
	const auto user = _sublist->sublistPeer()->asUser();
	if (!parent || !user) {
		return;
	}
	const auto paidAmount = std::make_shared<rpl::variable<int>>();
	_addAction(text, [=] {
		const auto removeFee = !feeRemoved;
		PeerMenuConfirmToggleFee(
			navigation,
			paidAmount,
			parent,
			user,
			removeFee);
	}, feeRemoved ? &st::menuIconEarn : &st::menuIconCancelFee);
	_addAction({ .isSeparator = true });
	_addAction({ .make = [=](not_null<Ui::RpWidget*> actionParent) {
		auto helper = Ui::Text::CustomEmojiHelper();
		const auto text = feeRemoved
			? tr::lng_context_fee_free(
				tr::now,
				lt_name,
				TextWithEntities{ user->shortName() },
				Ui::Text::WithEntities)
			: tr::lng_context_fee_now(
				tr::now,
				lt_name,
				TextWithEntities{ user->shortName() },
				lt_amount,
				helper.paletteDependent(
					Ui::Earn::IconCurrencyEmojiSmall()
				).append(Lang::FormatCountDecimal(
					user->owner().commonStarsPerMessage(parent)
				)),
				Ui::Text::WithEntities);
		const auto action = new QAction(actionParent);
		action->setDisabled(true);
		auto result = base::make_unique_q<Ui::Menu::Action>(
			actionParent,
			st::windowFeeItem,
			action,
			nullptr,
			nullptr);
		result->setMarkedText(
			text,
			QString(),
			Core::TextContext({ .session = &user->session() }));
		return result;
	} });
}

} // namespace

void PeerMenuExportChat(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	base::call_delayed(st::defaultPopupMenu.showDuration, [=] {
		Core::App().exportManager().start(peer);
	});
}

void PeerMenuDeleteContact(
		not_null<Window::SessionController*> controller,
		not_null<UserData*> user) {
	if (controller->showFrozenError()) {
		return;
	}
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
	auto box = Box([=](not_null<Ui::GenericBox*> box) {
		Ui::AddSkip(box->verticalLayout());
		Ui::IconWithTitle(
			box->verticalLayout(),
			Ui::CreateChild<Ui::UserpicButton>(
				box,
				user,
				st::mainMenuUserpic),
			Ui::CreateChild<Ui::FlatLabel>(
				box,
				tr::lng_info_delete_contact() | Ui::Text::ToBold(),
				box->getDelegate()->style().title));
		Ui::ConfirmBox(box, {
			.text = text,
			.confirmed = deleteSure,
			.confirmText = tr::lng_box_delete(),
			.confirmStyle = &st::attentionBoxButton,
		});
	});
	controller->show(std::move(box), Ui::LayerOption::CloseOther);
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
	const auto controller = navigation->parentController();
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		Ui::AddSkip(box->verticalLayout());
		Ui::IconWithTitle(
			box->verticalLayout(),
			Ui::CreateChild<Info::Profile::TopicIconButton>(
				box,
				controller,
				topic),
			Ui::CreateChild<Ui::FlatLabel>(
				box,
				topic->title(),
				box->getDelegate()->style().title));
		Ui::AddSkip(box->verticalLayout());
		Ui::AddSkip(box->verticalLayout());
		Ui::ConfirmBox(box, {
			.text = tr::lng_forum_topic_delete_sure(tr::now),
			.confirmed = callback,
			.confirmText = tr::lng_box_delete(),
			.confirmStyle = &st::attentionBoxButton,
			.labelPadding = st::boxRowPadding,
		});
	}));
}

void PeerMenuDeleteTopic(
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer,
		MsgId rootId) {
	const auto api = &peer->session().api();
	api->request(MTPmessages_DeleteTopicHistory(
		peer->input,
		MTP_int(rootId)
	)).done([=](const MTPmessages_AffectedHistory &result) {
		const auto offset = api->applyAffectedHistory(peer, result);
		if (offset > 0) {
			PeerMenuDeleteTopic(navigation, peer, rootId);
		} else if (const auto forum = peer->forum()) {
			forum->applyTopicDeleted(rootId);
		}
	}).send();
}

void PeerMenuDeleteTopic(
		not_null<Window::SessionNavigation*> navigation,
		not_null<Data::ForumTopic*> topic) {
	PeerMenuDeleteTopic(navigation, topic->peer(), topic->rootId());
}

void PeerMenuShareContactBox(
		not_null<Window::SessionNavigation*> navigation,
		not_null<UserData*> user) {
	if (navigation->showFrozenError()) {
		return;
	}
	// There is no async to make weak from controller.
	const auto weak = std::make_shared<base::weak_qptr<Ui::BoxContent>>();
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
		struct State {
			base::weak_ptr<Data::Thread> weak;
			Fn<void(Api::SendOptions)> share;
			SendPaymentHelper sendPayment;
		};
		const auto state = std::make_shared<State>();
		state->weak = thread;
		state->share = [=](Api::SendOptions options) {
			const auto strong = state->weak.get();
			if (!strong) {
				state->share = nullptr;
				return;
			}

			auto action = Api::SendAction(strong, options);
			action.clearDraft = false;

			const auto withPaymentApproved = [=](int stars) {
				if (const auto onstack = state->share) {
					auto copy = options;
					copy.starsApproved = stars;
					onstack(copy);
				}
			};
			const auto checked = state->sendPayment.check(
				navigation,
				peer,
				action.options,
				1,
				withPaymentApproved);
			if (!checked) {
				return;
			}
			navigation->showThread(
				strong,
				ShowAtTheEndMsgId,
				Window::SectionShow::Way::ClearStack);
			strong->session().api().shareContact(user, action);
			state->share = nullptr;
		};

		navigation->parentController()->show(
			Ui::MakeConfirmBox({
				.text = tr::lng_forward_share_contact(
					tr::now,
					lt_recipient,
					recipient),
				.confirmed = [state](Fn<void()> &&close) {
					state->share({});
					close();
				},
				.confirmText = tr::lng_forward_send(),
			}));
	};
	*weak = navigation->parentController()->show(
		Box<PeerListBox>(
			std::make_unique<ChooseRecipientBoxController>(
				ChooseRecipientArgs{
					.session = &navigation->session(),
					.callback = std::move(callback),
					.moneyRestrictionError = WriteMoneyRestrictionError,
				}),
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
		SuggestPostOptions suggest,
		PollData::Flags chosen,
		PollData::Flags disabled,
		Api::SendType sendType,
		SendMenu::Details sendMenuDetails) {
	if (peer->isChannel() && !peer->isMegagroup()) {
		chosen &= ~PollData::Flag::PublicVotes;
		disabled |= PollData::Flag::PublicVotes;
	}
	auto starsRequired = peer->session().changes().peerFlagsValue(
		peer,
		Data::PeerUpdate::Flag::FullInfo
		| Data::PeerUpdate::Flag::StarsPerMessage
	) | rpl::map([=] {
		return peer->starsPerMessageChecked();
	});
	auto box = Box<CreatePollBox>(
		controller,
		chosen,
		disabled,
		std::move(starsRequired),
		sendType,
		sendMenuDetails);
	struct State {
		Fn<void(const CreatePollBox::Result &)> create;
		SendPaymentHelper sendPayment;
		bool lock = false;
	};
	const auto weak = base::make_weak(box);
	const auto state = box->lifetime().make_state<State>();
	state->create = [=](const CreatePollBox::Result &result) {
		auto action = Api::SendAction(
			peer->owner().history(peer),
			result.options);
		action.replyTo = replyTo;
		action.options.suggest = suggest;

		const auto withPaymentApproved = crl::guard(weak, [=](int stars) {
			if (const auto onstack = state->create) {
				auto copy = result;
				copy.options.starsApproved = stars;
				onstack(copy);
			}
		});
		const auto checked = state->sendPayment.check(
			controller,
			peer,
			action.options,
			1,
			withPaymentApproved);
		if (!checked || std::exchange(state->lock, true)) {
			return;
		}

		const auto local = action.history->localDraft(
			replyTo.topicRootId,
			replyTo.monoforumPeerId);
		if (local) {
			action.clearDraft = local->textWithTags.text.isEmpty();
		} else {
			action.clearDraft = false;
		}
		const auto api = &peer->session().api();
		api->polls().create(result.poll, action, crl::guard(weak, [=] {
			state->create = nullptr;
			weak->closeBox();
		}), crl::guard(weak, [=] {
			state->lock = false;
			weak->submitFailed(tr::lng_attach_failed(tr::now));
		}));
	};
	box->submitRequests(
	) | rpl::start_with_next(state->create, box->lifetime());
	controller->show(std::move(box), Ui::LayerOption::CloseOther);
}

void PeerMenuTodoWantsPremium(TodoWantsPremium type) {
	const auto window = Core::App().activeWindow();
	if (!window) {
		return;
	}
	const auto filter = [=](const auto &...) {
		if (const auto controller = window->sessionController()) {
			ShowPremiumPreviewBox(controller, PremiumFeature::TodoLists);
			window->activate();
		}
		return false;
	};
	const auto link = Ui::Text::Link(
		Ui::Text::Semibold(tr::lng_todo_premium_link(tr::now)));
	const auto text = [&] {
		switch (type) {
		case TodoWantsPremium::Create: return tr::lng_todo_create_premium;
		case TodoWantsPremium::Add: return tr::lng_todo_add_premium;
		case TodoWantsPremium::Mark: return tr::lng_todo_mark_premium;
		}
		Unexpected("Type in PeerMenuTodoWantsPremium.");
	}();
	constexpr auto kToastDuration = crl::time(4000);
	window->uiShow()->showToast(Ui::Toast::Config{
		.text = text(
			tr::now,
			lt_link,
			link,
			Ui::Text::WithEntities),
		.filter = filter,
		.duration = kToastDuration,
	});
}

void PeerMenuCreateTodoList(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		FullReplyTo replyTo,
		SuggestPostOptions suggest,
		Api::SendType sendType,
		SendMenu::Details sendMenuDetails) {
	if (!peer->session().premium()) {
		PeerMenuTodoWantsPremium(TodoWantsPremium::Create);
		return;
	}
	auto starsRequired = peer->session().changes().peerFlagsValue(
		peer,
		Data::PeerUpdate::Flag::FullInfo
		| Data::PeerUpdate::Flag::StarsPerMessage
	) | rpl::map([=] {
		return peer->starsPerMessageChecked();
	});
	auto box = Box<EditTodoListBox>(
		controller,
		std::move(starsRequired),
		sendType,
		sendMenuDetails);
	struct State {
		Fn<void(const EditTodoListBox::Result &)> create;
		SendPaymentHelper sendPayment;
		bool lock = false;
	};
	const auto weak = base::make_weak(box);
	const auto state = box->lifetime().make_state<State>();
	state->create = [=](const EditTodoListBox::Result &result) {
		const auto withPaymentApproved = crl::guard(weak, [=](int stars) {
			if (const auto onstack = state->create) {
				auto copy = result;
				copy.options.starsApproved = stars;
				onstack(copy);
			}
		});
		auto action = Api::SendAction(
			peer->owner().history(peer),
			result.options);
		action.replyTo = replyTo;
		action.options.suggest = suggest;

		const auto checked = state->sendPayment.check(
			controller,
			peer,
			action.options,
			1,
			withPaymentApproved);
		if (!checked || std::exchange(state->lock, true)) {
			return;
		}

		const auto local = action.history->localDraft(
			replyTo.topicRootId,
			replyTo.monoforumPeerId);
		if (local) {
			action.clearDraft = local->textWithTags.text.isEmpty();
		} else {
			action.clearDraft = false;
		}
		const auto api = &peer->session().api();
		api->todoLists().create(result.todolist, action, crl::guard(weak, [=] {
			state->create = nullptr;
			weak->closeBox();
		}), crl::guard(weak, [=](const QString &error) {
			state->lock = false;
			weak->submitFailed(error);
		}));
	};
	box->submitRequests(
	) | rpl::start_with_next(state->create, box->lifetime());
	controller->show(std::move(box), Ui::LayerOption::CloseOther);
}

void PeerMenuEditTodoList(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item) {
	const auto media = item->media();
	const auto todolist = media ? media->todolist() : nullptr;
	if (!todolist) {
		return;
	} else if (!item->history()->session().premium()) {
		PeerMenuTodoWantsPremium(TodoWantsPremium::Add);
		return;
	}
	auto box = Box<EditTodoListBox>(controller, item);
	const auto weak = base::make_weak(box);
	box->submitRequests(
	) | rpl::start_with_next([=](const EditTodoListBox::Result &result) {
		const auto api = &item->history()->session().api();
		api->todoLists().edit(
			item,
			result.todolist,
			result.options,
			crl::guard(weak, [=] { weak->closeBox(); }),
			crl::guard(weak, [=](const QString &error) {
				weak->submitFailed(error);
			}));
	}, box->lifetime());
	controller->show(std::move(box), Ui::LayerOption::CloseOther);
}

bool PeerMenuShowAddTodoListTasks(not_null<HistoryItem*> item) {
	const auto media = item ? item->media() : nullptr;
	const auto todolist = media ? media->todolist() : nullptr;
	const auto appConfig = &item->history()->session().appConfig();
	return item->isRegular()
		&& !item->Has<HistoryMessageForwarded>()
		&& todolist
		&& (todolist->items.size() < appConfig->todoListItemsLimit())
		&& (item->out() || todolist->othersCanAppend());
}

void PeerMenuAddTodoListTasks(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item) {
	const auto session = &item->history()->session();
	if (!session->premium()) {
		PeerMenuTodoWantsPremium(TodoWantsPremium::Add);
		return;
	}
	const auto media = item->media();
	const auto todolist = media ? media->todolist() : nullptr;
	if (!todolist) {
		return;
	}
	auto box = Box<AddTodoListTasksBox>(controller, item);
	const auto raw = box.data();
	box->submitRequests(
	) | rpl::start_with_next([=](const AddTodoListTasksBox::Result &result) {
		const auto show = raw->uiShow();
		raw->closeBox();
		session->api().todoLists().add(
			item,
			result.items,
			[] {},
			[=](const QString &error) { show->showToast(error); });
	}, box->lifetime());
	controller->show(std::move(box), Ui::LayerOption::CloseOther);
}

void PeerMenuBlockUserBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::Controller*> window,
		not_null<PeerData*> peer,
		std::variant<v::null_t, bool> suggestReport,
		std::variant<v::null_t, ClearChat, ClearReply> suggestClear) {
	const auto settings = peer->barSettings().value_or(PeerBarSettings(0));
	const auto reportNeeded = v::is_null(suggestReport)
		? ((settings & PeerBarSetting::ReportSpam) != 0)
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

void PeerMenuUnblockUserWithBotRestart(
		std::shared_ptr<Ui::Show> show,
		not_null<UserData*> user) {
	user->session().api().blockedPeers().unblock(user, [=](bool success) {
		if (success && user->isBot() && !user->isSupport()) {
			user->session().api().sendBotStart(show, user);
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

object_ptr<Ui::BoxContent> PrepareChooseRecipientBox(
		not_null<Main::Session*> session,
		FnMut<bool(not_null<Data::Thread*>)> &&chosen,
		rpl::producer<QString> titleOverride,
		FnMut<void()> &&successCallback,
		InlineBots::PeerTypes typesRestriction,
		Fn<void(
			std::vector<not_null<Data::Thread*>>,
			Api::SendOptions)> sendMany) {
	const auto weak = std::make_shared<base::weak_qptr<PeerListBox>>();
	const auto selectable = (sendMany != nullptr);
	class Controller final : public ChooseRecipientBoxController {
	public:
		using Chosen = not_null<Data::Thread*>;

		Controller(
			not_null<Main::Session*> session,
			FnMut<void(Chosen)> callback,
			Fn<bool(Chosen)> filter,
			bool selectable)
		: ChooseRecipientBoxController({
			.session = session,
			.callback = std::move(callback),
			.filter = filter,
			.moneyRestrictionError = WriteMoneyRestrictionError,
		})
		, _selectable(selectable) {
		}

		using PeerListController::setSearchNoResultsText;

		void rowClicked(not_null<PeerListRow*> row) override final {
			if (!_selectable) {
				return ChooseRecipientBoxController::rowClicked(row);
			}
			const auto count = delegate()->peerListSelectedRowsCount();
			if (showLockedError(row) || (count && row->peer()->isForum())) {
				return;
			} else if (row->peer()->isForum()) {
				ChooseRecipientBoxController::rowClicked(row);
			} else {
				delegate()->peerListSetRowChecked(row, !row->checked());
				_selectionChanges.fire({});
			}
		}

		base::unique_qptr<Ui::PopupMenu> rowContextMenu(
				QWidget *parent,
				not_null<PeerListRow*> row) override final {
			if (!_selectable) {
				return ChooseRecipientBoxController::rowContextMenu(
					parent,
					row);
			}
			if (!row->checked() && !row->peer()->isForum()) {
				auto menu = base::make_unique_q<Ui::PopupMenu>(
					parent,
					st::popupMenuWithIcons);
				menu->addAction(tr::lng_bot_choose_chat(tr::now), [=] {
					delegate()->peerListSetRowChecked(row, true);
					_selectionChanges.fire({});
				}, &st::menuIconSelect);
				return menu;
			}
			return nullptr;
		}

		[[nodiscard]] rpl::producer<> selectionChanges() const {
			return _selectionChanges.events_starting_with({});
		}
		[[nodiscard]] bool hasSelected() const {
			return delegate()->peerListSelectedRowsCount() > 0;
		}

		[[nodiscard]] rpl::producer<Chosen> singleChosen() const {
			return _singleChosen.events();
		}

	private:
		rpl::event_stream<Chosen> _singleChosen;
		rpl::event_stream<> _selectionChanges;
		bool _selectable = false;

	};

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
	auto controller = std::make_unique<Controller>(
		session,
		std::move(callback),
		std::move(filter),
		selectable);
	const auto raw = controller.get();

	struct State {
		Fn<void(Api::SendOptions)> submit;
		rpl::variable<int> starsToSend;
		Fn<void()> refreshStarsToSend;
		rpl::lifetime submitLifetime;
	};
	const auto state = std::make_shared<State>();
	auto initBox = [=](not_null<PeerListBox*> box) {
		state->refreshStarsToSend = [=] {
			auto perMessage = 0;
			for (const auto &peer : box->collectSelectedRows()) {
				perMessage += peer->starsPerMessageChecked();
			}
			state->starsToSend = perMessage;
		};
		raw->selectionChanges(
		) | rpl::start_with_next([=] {
			box->clearButtons();
			state->refreshStarsToSend();
			const auto shown = raw->hasSelected();
			if (shown) {
				const auto weak = base::make_weak(box);
				state->submit = [=](Api::SendOptions options) {
					state->submitLifetime.destroy();
					const auto show = box->peerListUiShow();
					const auto peers = box->collectSelectedRows();
					const auto withPaymentApproved = crl::guard(weak, [=](
							int approved) {
						auto copy = options;
						copy.starsApproved = approved;
						if (const auto onstack = state->submit) {
							onstack(copy);
						}
					});

					const auto alreadyApproved = options.starsApproved;
					auto paid = std::vector<not_null<PeerData*>>();
					auto waiting = base::flat_set<not_null<PeerData*>>();
					auto totalStars = 0;
					for (const auto &peer : peers) {
						const auto details = ComputePaymentDetails(peer, 1);
						if (!details) {
							waiting.emplace(peer);
						} else if (details->stars > 0) {
							totalStars += details->stars;
							paid.push_back(peer);
						}
					}
					if (!waiting.empty()) {
						session->changes().peerUpdates(
							Data::PeerUpdate::Flag::FullInfo
						) | rpl::start_with_next([=](const Data::PeerUpdate &update) {
							if (waiting.contains(update.peer)) {
								withPaymentApproved(alreadyApproved);
							}
						}, state->submitLifetime);

						if (!session->credits().loaded()) {
							session->credits().loadedValue(
							) | rpl::filter(
								rpl::mappers::_1
							) | rpl::take(1) | rpl::start_with_next([=] {
								withPaymentApproved(alreadyApproved);
							}, state->submitLifetime);
						}
						return;
					} else if (totalStars > alreadyApproved) {
						ShowSendPaidConfirm(show, paid, SendPaymentDetails{
							.messages = 1,
							.stars = totalStars,
						}, [=] { withPaymentApproved(totalStars); });
						return;
					}
					state->submit = nullptr;

					sendMany(ranges::views::all(
						peers
					) | ranges::views::transform([&](
						not_null<PeerData*> peer) -> Controller::Chosen {
						return peer->owner().history(peer);
					}) | ranges::to_vector, options);
				};
				const auto send = box->addButton(
					tr::lng_send_button(),
					[=] {
						if (const auto onstack = state->submit) {
							onstack({});
						}
					});
				send->setText(PaidSendButtonText(
					state->starsToSend.value(),
					tr::lng_send_button()));
			}
			box->addButton(tr::lng_cancel(), [=] {
				box->closeBox();
			});
		}, box->lifetime());
		if (titleOverride) {
			box->setTitle(std::move(titleOverride));
		}
	};
	auto result = Box<PeerListBox>(
		std::move(controller),
		std::move(initBox));
	*weak = result.data();

	return result;
}

base::weak_qptr<Ui::BoxContent> ShowChooseRecipientBox(
		not_null<Window::SessionNavigation*> navigation,
		FnMut<bool(not_null<Data::Thread*>)> &&chosen,
		rpl::producer<QString> titleOverride,
		FnMut<void()> &&successCallback,
		InlineBots::PeerTypes typesRestriction) {
	return navigation->parentController()->show(PrepareChooseRecipientBox(
		&navigation->session(),
		std::move(chosen),
		std::move(titleOverride),
		std::move(successCallback),
		typesRestriction));
}

base::weak_qptr<Ui::BoxContent> ShowForwardMessagesBox(
		std::shared_ptr<ChatHelpers::Show> show,
		Data::ForwardDraft &&draft,
		Fn<void()> &&successCallback) {
	const auto session = &show->session();
	const auto owner = &session->data();
	const auto itemsList = owner->idsToItems(draft.ids);
	const auto msgIds = owner->itemsToIds(itemsList);
	const auto sendersCount = ItemsForwardSendersCount(itemsList);
	const auto captionsCount = ItemsForwardCaptionsCount(itemsList);
	if (msgIds.empty()) {
		return nullptr;
	}

	class ListBox final : public PeerListBox {
	public:
		ListBox(
			QWidget *parent,
			std::unique_ptr<PeerListController> controller,
			Fn<void(not_null<ListBox*>)> init)
		: PeerListBox(
			parent,
			std::move(controller),
			[=](not_null<PeerListBox*> box) {
				init(static_cast<ListBox*>(box.get()));
			}) {
		}

		void setBottomSkip(int bottomSkip) {
			PeerListBox::setInnerBottomSkip(bottomSkip);
		}

		[[nodiscard]] rpl::producer<> focusRequests() const {
			return _focusRequests.events();
		}

		[[nodiscard]] Data::ForwardOptions forwardOptionsData() const {
			return (_forwardOptions.captionsCount
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

		not_null<PeerListContent*> peerListContent() const {
			return PeerListBox::content();
		}

		void setFilterId(FilterId filterId) {
			_filterId = filterId;
		}
		[[nodiscard]] FilterId filterId() const {
			return _filterId;
		}

	private:
		rpl::event_stream<> _focusRequests;
		Ui::ForwardOptions _forwardOptions;
		FilterId _filterId = 0;

	};

	class Controller final : public ChooseRecipientBoxController {
	public:
		using Chosen = not_null<Data::Thread*>;

		Controller(not_null<Main::Session*> session)
		: ChooseRecipientBoxController({
			.session = session,
			.callback = [=](Chosen thread) {
				_singleChosen.fire_copy(thread);
			},
			.moneyRestrictionError = WriteMoneyRestrictionError,
		}) {
		}

		std::unique_ptr<PeerListRow> createRestoredRow(
				not_null<PeerData*> peer) override final {
			return ChooseRecipientBoxController::createRow(
				peer->owner().history(peer));
		}

		using PeerListController::setSearchNoResultsText;

		void rowClicked(not_null<PeerListRow*> row) override final {
			const auto count = delegate()->peerListSelectedRowsCount();
			if (showLockedError(row) || (count && row->peer()->isForum())) {
				return;
			} else if (!count || row->peer()->isForum()) {
				ChooseRecipientBoxController::rowClicked(row);
			} else if (count) {
				delegate()->peerListSetRowChecked(row, !row->checked());
				_selectionChanges.fire({});
			}
		}

		base::unique_qptr<Ui::PopupMenu> rowContextMenu(
				QWidget *parent,
				not_null<PeerListRow*> row) override final {
			if (!row->checked() && !row->peer()->isForum()) {
				auto menu = base::make_unique_q<Ui::PopupMenu>(
					parent,
					st::popupMenuWithIcons);
				menu->addAction(tr::lng_bot_choose_chat(tr::now), [=] {
					delegate()->peerListSetRowChecked(row, true);
					_selectionChanges.fire({});
				}, &st::menuIconSelect);
				return menu;
			}
			return nullptr;
		}

		[[nodiscard]] rpl::producer<> selectionChanges() const {
			return _selectionChanges.events_starting_with({});
		}
		[[nodiscard]] bool hasSelected() const {
			return delegate()->peerListSelectedRowsCount() > 0;
		}

		[[nodiscard]] rpl::producer<Chosen> singleChosen() const{
			return _singleChosen.events();
		}

	private:
		rpl::event_stream<Chosen> _singleChosen;
		rpl::event_stream<> _selectionChanges;

	};

	struct State {
		not_null<ListBox*> box;
		not_null<Controller*> controller;
		base::unique_qptr<Ui::PopupMenu> menu;
		Fn<void(Api::SendOptions options)> submit;
		rpl::variable<int> starsToSend;
		Fn<void()> refreshStarsToSend;
		rpl::lifetime submitLifetime;
	};

	const auto applyFilter = [=](not_null<ListBox*> box, FilterId id) {
		box->scrollToY(0);
		auto &filters = session->data().chatsFilters();
		const auto &list = filters.list();
		if (list.size() <= 1) {
			return;
		}
		if (box->filterId() == id) {
			return;
		}
		box->setFilterId(id);

		using SavedState = PeerListController::SavedStateBase;
		auto state = std::make_unique<PeerListState>();
		state->controllerState = std::make_unique<SavedState>();

		const auto addList = [&](auto chats) {
			for (const auto &row : chats->all()) {
				if (const auto history = row->history()) {
					state->list.push_back(history->peer);
				}
			}
		};

		if (!id) {
			state->list.push_back(session->user());
			addList(session->data().chatsList()->indexed());
			const auto folderId = Data::Folder::kId;
			if (const auto folder = session->data().folderLoaded(folderId)) {
				addList(folder->chatsList()->indexed());
			}
			addList(session->data().contactsNoChatsList());
		} else {
			addList(session->data().chatsFilters().chatsList(id)->indexed());
		}
		box->peerListContent()->restoreState(std::move(state));
	};

	const auto state = [&] {
		auto controller = std::make_unique<Controller>(session);
		const auto controllerRaw = controller.get();
		auto init = [=](not_null<ListBox*> box) {
			controllerRaw->setSearchNoResultsText(
				tr::lng_bot_chats_not_found(tr::now));
			const auto lastFilterId = box->lifetime().make_state<FilterId>(0);
			const auto chatsFilters = Ui::AddChatFiltersTabsStrip(
				box,
				session,
				[=](FilterId id) {
					*lastFilterId = id;
					applyFilter(box, id);
				},
				Window::GifPauseReason::Layer);
			chatsFilters->lower();
			rpl::combine(
				chatsFilters->heightValue(),
				rpl::producer<bool>([=](auto consumer) {
					auto lifetime = rpl::lifetime();
					consumer.put_next(false);
					box->appendQueryChangedCallback([=](const QString &q) {
						const auto hasQuery = !q.isEmpty();
						applyFilter(box, hasQuery ? 0 : (*lastFilterId));
						consumer.put_next_copy(hasQuery);
					});
					return lifetime;
				})
			) | rpl::start_with_next([box](int h, bool hasQuery) {
				box->setAddedTopScrollSkip(hasQuery ? 0 : h);
			}, box->lifetime());
			box->multiSelectHeightValue() | rpl::start_with_next([=](int h) {
				chatsFilters->moveToLeft(0, h);
			}, chatsFilters->lifetime());
		};
		auto box = Box<ListBox>(std::move(controller), std::move(init));
		const auto boxRaw = box.data();
		boxRaw->setForwardOptions({
			.sendersCount = sendersCount,
			.captionsCount = captionsCount,
		});
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
			const auto id = SeparateId(
				((peer->isForum() && !peer->useSubsectionTabs())
					? SeparateType::Forum
					: SeparateType::Chat),
				thread);
			auto controller = Core::App().windowFor(id);
			if (!controller) {
				return false;
			}
			if (controller->maybeSession() != &peer->session()) {
				controller = Core::App().ensureSeparateWindowFor(id);
				if (controller->maybeSession() != &peer->session()) {
					return false;
				}
			}
			const auto content = controller->sessionController()->content();
			return content->setForwardDraft(thread, std::move(draft));
		};
		auto callback = [=, chosen = std::move(chosen)](
				Controller::Chosen thread) mutable {
			const auto weak = base::make_weak(state->box);
			if (!chosen(thread)) {
				return;
			} else if (const auto strong = weak.get()) {
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

	const auto history = session->data().message(msgIds.front())->history();
	const auto send = ShareBox::DefaultForwardCallback(
		show,
		history,
		msgIds);
	const auto countMessages = ShareBox::DefaultForwardCountMessages(
		history,
		msgIds);

	const auto weak = base::make_weak(state->box);
	const auto field = comment->entity();
	state->submit = [=](Api::SendOptions options) {
		const auto peers = state->box->collectSelectedRows();
		auto comment = field->getTextWithAppliedMarkdown();
		const auto checkPaid = [=] {
			const auto withPaymentApproved = crl::guard(weak, [=](
					int approved) {
				auto copy = options;
				copy.starsApproved = approved;
				if (const auto onstack = state->submit) {
					onstack(copy);
				}
			});

			const auto alreadyApproved = options.starsApproved;
			const auto messagesCount = countMessages(comment);
			auto paid = std::vector<not_null<PeerData*>>();
			auto waiting = base::flat_set<not_null<PeerData*>>();
			auto totalStars = 0;
			for (const auto &peer : peers) {
				const auto details = ComputePaymentDetails(
					peer,
					messagesCount);
				if (!details) {
					waiting.emplace(peer);
				} else if (details->stars > 0) {
					totalStars += details->stars;
					paid.push_back(peer);
				}
			}
			if (!waiting.empty()) {
				session->changes().peerUpdates(
					Data::PeerUpdate::Flag::FullInfo
				) | rpl::start_with_next([=](const Data::PeerUpdate &update) {
					if (waiting.contains(update.peer)) {
						withPaymentApproved(alreadyApproved);
					}
				}, state->submitLifetime);

				if (!session->credits().loaded()) {
					session->credits().loadedValue(
					) | rpl::filter(
						rpl::mappers::_1
					) | rpl::take(1) | rpl::start_with_next([=] {
						withPaymentApproved(alreadyApproved);
					}, state->submitLifetime);
				}
				return false;
			} else if (totalStars > alreadyApproved) {
				ShowSendPaidConfirm(show, paid, SendPaymentDetails{
					.messages = messagesCount,
					.stars = totalStars,
				}, [=] { withPaymentApproved(totalStars); });
				return false;
			}
			state->submit = nullptr;
			return true;
		};
		send(
			ranges::views::all(
				peers
			) | ranges::views::transform([&](
					not_null<PeerData*> peer) -> Controller::Chosen {
				return peer->owner().history(peer);
			}) | ranges::to_vector,
			checkPaid,
			std::move(comment),
			options,
			state->box->forwardOptionsData());
		if (!state->submit && successCallback) {
			successCallback();
		}
	};

	const auto sendMenuType = [=] {
		const auto selected = state->box->collectSelectedRows();
		const auto hasPaid = [&] {
			for (const auto peer : selected) {
				if (peer->starsPerMessageChecked()) {
					return true;
				}
			}
			return false;
		}();
		return hasPaid
			? SendMenu::Type::SilentOnly
			: ranges::all_of(selected, HistoryView::CanScheduleUntilOnline)
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
				state->box->forwardOptions(),
				[=](Ui::ForwardOptions o) {
					state->box->setForwardOptions(o);
				},
				state->menu->lifetime());

			state->menu->addSeparator();
		}
		state->menu->setForcedVerticalOrigin(
			Ui::PopupMenu::VerticalOrigin::Bottom);
		SendMenu::FillSendMenu(
			state->menu.get(),
			show,
			SendMenu::Details{ sendMenuType() },
			SendMenu::DefaultCallback(show, crl::guard(parent, [=](
					Api::SendOptions options) {
				if (const auto onstack = state->submit) {
					onstack(options);
				}
			})));
		if (showForwardOptions || !state->menu->empty()) {
			state->menu->popup(QCursor::pos());
		}
	};

	state->refreshStarsToSend = [=] {
		auto perMessage = 0;
		for (const auto &peer : state->box->collectSelectedRows()) {
			perMessage += peer->starsPerMessageChecked();
		}
		state->starsToSend = perMessage
			* countMessages(field->getTextWithTags());
	};

	comment->hide(anim::type::instant);
	comment->toggleOn(state->controller->selectionChanges(
	) | rpl::map([=] {
		return state->controller->hasSelected();
	}));

	rpl::combine(
		state->box->sizeValue(),
		comment->heightValue()
	) | rpl::start_with_next([=](const QSize &size, int commentHeight) {
		comment->moveToLeft(0, size.height() - commentHeight);
		comment->resizeToWidth(size.width());

		state->box->setBottomSkip(comment->isHidden() ? 0 : commentHeight);
	}, comment->lifetime());

	field->submits(
	) | rpl::start_with_next([=] {
		if (const auto onstack = state->submit) {
			onstack({});
		}
	}, field->lifetime());
	InitMessageFieldHandlers({
		.session = session,
		.show = show,
		.field = field,
		.customEmojiPaused = [=] {
			return show->paused(GifPauseReason::Layer);
		},
	});
	field->setSubmitSettings(Core::App().settings().sendSubmitWay());
	field->changes() | rpl::start_with_next([=] {
		state->refreshStarsToSend();
	}, field->lifetime());

	Ui::SendPendingMoveResizeEvents(comment);

	state->box->focusRequests(
	) | rpl::start_with_next([=] {
		if (!comment->isHidden()) {
			comment->entity()->setFocusFast();
		}
	}, comment->lifetime());

	state->controller->selectionChanges(
	) | rpl::start_with_next([=] {
		const auto shown = state->controller->hasSelected();

		state->box->clearButtons();
		state->refreshStarsToSend();
		if (shown) {
			const auto send = state->box->addButton(
				tr::lng_send_button(),
				[=] {
					if (const auto onstack = state->submit) {
						onstack({});
					}
				});
			send->setAcceptBoth();
			send->clicks(
			) | rpl::start_with_next([=](Qt::MouseButton button) {
				if (button == Qt::RightButton) {
					showMenu(send);
				}
			}, send->lifetime());
			send->setText(PaidSendButtonText(
				state->starsToSend.value(),
				tr::lng_send_button()));
		}
		state->box->addButton(tr::lng_cancel(), [=] {
			state->box->closeBox();
		});
	}, state->box->lifetime());

	return base::make_weak(state->box);
}

base::weak_qptr<Ui::BoxContent> ShowForwardMessagesBox(
		not_null<Window::SessionNavigation*> navigation,
		Data::ForwardDraft &&draft,
		Fn<void()> &&successCallback) {
	return ShowForwardMessagesBox(
		navigation->uiShow(),
		std::move(draft),
		std::move(successCallback));
}

base::weak_qptr<Ui::BoxContent> ShowForwardMessagesBox(
		not_null<Window::SessionNavigation*> navigation,
		MessageIdsList &&items,
		Fn<void()> &&successCallback) {
	return ShowForwardMessagesBox(
		navigation,
		Data::ForwardDraft{ .ids = std::move(items) },
		std::move(successCallback));
}

base::weak_qptr<Ui::BoxContent> ShowShareGameBox(
		not_null<Window::SessionNavigation*> navigation,
		not_null<UserData*> bot,
		QString shortName) {
	const auto weak = std::make_shared<base::weak_qptr<Ui::BoxContent>>();
	auto chosen = [=](not_null<Data::Thread*> thread) mutable {
		const auto confirm = std::make_shared<base::weak_qptr<Ui::BoxContent>>();
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
		std::make_unique<ChooseRecipientBoxController>(ChooseRecipientArgs{
			.session = &navigation->session(),
			.callback = std::move(chosen),
			.filter = std::move(filter),
			.moneyRestrictionError = WriteMoneyRestrictionError,
		}),
		std::move(initBox)));
	return weak->get();
}

base::weak_qptr<Ui::BoxContent> ShowDropMediaBox(
		not_null<Window::SessionNavigation*> navigation,
		std::shared_ptr<QMimeData> data,
		not_null<Data::Forum*> forum,
		FnMut<void()> &&successCallback) {
	const auto weak = std::make_shared<base::weak_qptr<Ui::BoxContent>>();
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
	return weak->get();
}

base::weak_qptr<Ui::BoxContent> ShowDropMediaBox(
		not_null<Window::SessionNavigation*> navigation,
		std::shared_ptr<QMimeData> data,
		not_null<Data::SavedMessages*> monoforum,
		FnMut<void()> &&successCallback) {
	const auto weak = std::make_shared<base::weak_qptr<Ui::BoxContent>>();
	auto chosen = [
		data = std::move(data),
		callback = std::move(successCallback),
		weak,
		navigation
	](not_null<Data::SavedSublist*> sublist) mutable {
		const auto content = navigation->parentController()->content();
		if (!content->filesOrForwardDrop(sublist, data.get())) {
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

		monoforum->destroyed(
		) | rpl::start_with_next([=] {
			box->closeBox();
		}, box->lifetime());
	};
	*weak = navigation->parentController()->show(Box<PeerListBox>(
		std::make_unique<ChooseSublistBoxController>(
			monoforum,
			std::move(chosen)),
		std::move(initBox)));
	return weak->get();
}

base::weak_qptr<Ui::BoxContent> ShowSendNowMessagesBox(
		not_null<Window::SessionNavigation*> navigation,
		not_null<History*> history,
		MessageIdsList &&items,
		Fn<void()> &&successCallback) {
	const auto session = &navigation->session();
	const auto text = (items.size() > 1)
		? tr::lng_scheduled_send_now_many(tr::now, lt_count, items.size())
		: tr::lng_scheduled_send_now(tr::now);

	const auto list = session->data().idsToItems(items);
	const auto error = GetErrorForSending(
		history->peer,
		{ .forward = &list });
	if (error) {
		Data::ShowSendErrorToast(navigation, history->peer, error);
		return { nullptr };
	}
	auto done = [
		=,
		list = std::move(items),
		callback = std::move(successCallback)
	](Fn<void()> &&close) {
		close();
		auto ids = QVector<MTPint>();
		auto sorted = session->data().idsToItems(list);
		ranges::sort(sorted, ranges::less(), &HistoryItem::date);
		for (const auto &item : sorted) {
			if (item->allowsSendNow()) {
				ids.push_back(
					MTP_int(session->scheduledMessages().lookupId(item)));
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
	}));
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
		PeerId monoforumPeerId,
		Fn<void()> onHidden) {
	const auto callback = crl::guard(navigation, [=](Fn<void()> &&close) {
		close();
		auto &session = peer->session();
		const auto migrated = (topicRootId || monoforumPeerId)
			? nullptr
			: peer->migrateFrom();
		const auto top = Data::ResolveTopPinnedId(
			peer,
			topicRootId,
			monoforumPeerId,
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
				monoforumPeerId,
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
			const auto sublist = strong->asSublist();
			const auto monoforumPeerId = strong->monoforumPeerId();
			using Flag = MTPmessages_UnpinAllMessages::Flag;
			api->request(MTPmessages_UnpinAllMessages(
				MTP_flags((topicRootId ? Flag::f_top_msg_id : Flag())
					| (sublist ? Flag::f_saved_peer_id : Flag())),
				history->peer->input,
				MTP_int(topicRootId.bare),
				sublist ? sublist->sublistPeer()->input : MTPInputPeer()
			)).done([=](const MTPmessages_AffectedHistory &result) {
				const auto peer = history->peer;
				const auto offset = api->applyAffectedHistory(peer, result);
				if (offset > 0) {
					self(self);
				} else {
					history->unpinMessagesFor(topicRootId, monoforumPeerId);
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
		not_null<Main::Session*> session,
		std::shared_ptr<Ui::Show> show,
		const PeerMenuCallback &addAction) {
	auto callback = [=, owner = &session->data()] {
		auto boxCallback = [=](Fn<void()> &&close) {
			close();

			MarkAsReadChatList(owner->chatsList());
			if (const auto folder = owner->folderLoaded(Data::Folder::kId)) {
				MarkAsReadChatList(folder->chatsList());
			}
		};
		show->show(
			Box([=](not_null<Ui::GenericBox*> box) {
				Ui::AddSkip(box->verticalLayout());
				Ui::AddSkip(box->verticalLayout());
				const auto userpic = Ui::CreateChild<Ui::UserpicButton>(
					box->verticalLayout(),
					session->user(),
					st::mainMenuUserpic);
				Ui::IconWithTitle(
					box->verticalLayout(),
					userpic,
					Ui::CreateChild<Ui::FlatLabel>(
						box->verticalLayout(),
						Info::Profile::NameValue(session->user()),
						box->getDelegate()->style().title));
				auto text = rpl::combine(
					tr::lng_context_mark_read_all_sure(),
					tr::lng_context_mark_read_all_sure_2(
						Ui::Text::RichLangValue)
				) | rpl::map([](QString t1, TextWithEntities t2) {
					return TextWithEntities()
						.append(std::move(t1))
						.append('\n')
						.append('\n')
						.append(std::move(t2));
				});
				Ui::ConfirmBox(box, {
					.text = std::move(text),
					.confirmed = std::move(boxCallback),
					.confirmStyle = &st::attentionBoxButton,
				});
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

void ToggleHistoryArchived(
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<History*> history,
		bool archived) {
	const auto callback = [=] {
		show->showToast(Ui::Toast::Config{
			.text = { (archived
				? tr::lng_archived_added(tr::now)
				: tr::lng_archived_removed(tr::now)) },
			.st = &st::windowArchiveToast,
			.duration = (archived
				? kArchivedToastDuration
				: Ui::Toast::kDefaultDuration),
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
		if (!controller->showFrozenError()) {
			controller->show(Box<DeleteMessagesBox>(peer, true));
		}
	};
}

Fn<void()> DeleteAndLeaveHandler(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	return [=] {
		if (!controller->showFrozenError()) {
			controller->show(Box(DeleteChatBox, peer));
		}
	};
}

Fn<void()> DeleteSublistHandler(
		not_null<Window::SessionController*> controller,
		not_null<Data::SavedSublist*> sublist) {
	const auto weak = base::make_weak(sublist.get());
	return [=] {
		if (const auto strong = weak.get()) {
			if (!controller->showFrozenError()) {
				controller->show(Box(DeleteSublistBox, strong));
			}
		}
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
			&st::menuIconStartStream);
	}
	if (!has && manager) {
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

void FillSenderUserpicMenu(
		not_null<SessionController*> controller,
		not_null<PeerData*> peer,
		Ui::InputField *fieldForMention,
		Dialogs::Key searchInEntry,
		const PeerMenuCallback &addAction) {
	const auto group = (peer->isChat() || peer->isMegagroup());
	const auto channel = peer->isChannel();
	const auto viewProfileText = group
		? tr::lng_context_view_group(tr::now)
		: channel
		? tr::lng_context_view_channel(tr::now)
		: tr::lng_context_view_profile(tr::now);
	addAction(viewProfileText, [=] {
		controller->showPeerInfo(peer, Window::SectionShow::Way::Forward);
	}, channel ? &st::menuIconInfo : &st::menuIconProfile);

	const auto showHistoryText = group
		? tr::lng_context_open_group(tr::now)
		: channel
		? tr::lng_context_open_channel(tr::now)
		: tr::lng_profile_send_message(tr::now);
	addAction(showHistoryText, [=] {
		controller->showPeerHistory(peer, Window::SectionShow::Way::Forward);
	}, channel ? &st::menuIconChannel : &st::menuIconChatBubble);

	const auto username = peer->username();
	const auto mention = !username.isEmpty() || peer->isUser();
	if (const auto guard = mention ? fieldForMention : nullptr) {
		addAction(tr::lng_context_mention(tr::now), crl::guard(guard, [=] {
			if (!username.isEmpty()) {
				fieldForMention->insertTag('@' + username);
			} else {
				fieldForMention->insertTag(
					peer->shortName(),
					PrepareMentionTag(peer->asUser()));
			}
		}), &st::menuIconUsername);
	}

	if (searchInEntry) {
		addAction(tr::lng_context_search_from(tr::now), [=] {
			controller->searchInChat(searchInEntry, peer);
		}, &st::menuIconSearch);
	}
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
		forum->enumerateTopics([](not_null<Data::ForumTopic*> topic) {
			MarkAsReadThread(topic);
		});
	} else if (const auto history = thread->asHistory()) {
		readHistory(history);
		if (const auto migrated = history->migrateSibling()) {
			readHistory(migrated);
		}
	} else if (const auto topic = thread->asTopic()) {
		topic->readTillEnd();
	} else if (const auto sublist = thread->asSublist()) {
		sublist->readTillEnd();
	}
}

void AddSeparatorAndShiftUp(const PeerMenuCallback &addAction) {
	addAction({
		.separatorSt = &st::popupMenuExpandedSeparator.menu.separator,
	});

	const auto &st = st::popupMenuExpandedSeparator.menu;
	const auto shift = st::popupMenuExpandedSeparator.scrollPadding.top()
		+ st.itemPadding.top()
		+ st.itemStyle.font->height
		+ st.itemPadding.bottom()
		+ st.separator.padding.top()
		+ st.separator.width / 2;
	addAction({ .addTopShift = -shift });
}

void TogglePinnedThread(
		not_null<Window::SessionController*> controller,
		not_null<Dialogs::Entry*> entry,
		FilterId filterId,
		Fn<void()> onToggled) {
	if (!filterId) {
		return TogglePinnedThread(controller, entry, onToggled);
	}
	const auto history = entry->asHistory();
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
		if (onToggled) {
			onToggled();
		}
	}
}

bool IsArchived(not_null<History*> history) {
	return (history->folder() != nullptr);
}

bool CanArchive(History *history, PeerData *peer) {
	if (history && history->useTopPromotion()) {
		return false;
	} else if (peer && (peer->isNotificationsUser() || peer->isSelf())) {
		if (!history || !history->folder()) {
			return false;
		}
	}
	return true;
}

void PeerMenuConfirmToggleFee(
		not_null<Window::SessionNavigation*> navigation,
		std::shared_ptr<rpl::variable<int>> paidAmount,
		not_null<PeerData*> peer,
		not_null<UserData*> user,
		bool removeFee) {
	const auto parent = peer->isChannel() ? peer->asChannel() : nullptr;
	const auto exception = [=](bool refund) {
		using Flag = MTPaccount_ToggleNoPaidMessagesException::Flag;
		const auto api = &user->session().api();
		api->request(MTPaccount_ToggleNoPaidMessagesException(
			MTP_flags((refund ? Flag::f_refund_charged : Flag())
				| (removeFee ? Flag() : Flag::f_require_payment)
				| (parent ? Flag::f_parent_peer : Flag())),
			(parent ? parent->input : MTPInputPeer()),
			user->inputUser
		)).done([=] {
			if (!parent) {
				user->clearPaysPerMessage();
			} else if (const auto monoforum = peer->monoforum()) {
				if (const auto sublist = monoforum->sublistLoaded(user)) {
					sublist->toggleFeeRemoved(removeFee);
				}
			}
		}).send();
	};
	if (!removeFee) {
		exception(false);
		return;
	}
	navigation->uiShow()->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto refund = std::make_shared<base::weak_qptr<Ui::Checkbox>>();
		Ui::ConfirmBox(box, {
			.text = tr::lng_payment_refund_text(
				tr::now,
				lt_name,
				Ui::Text::Bold(user->shortName()),
				Ui::Text::WithEntities),
			.confirmed = [=](Fn<void()> close) {
				exception(*refund && (*refund)->checked());
				close();
			},
			.confirmText = tr::lng_payment_refund_confirm(tr::now),
			.title = tr::lng_payment_refund_title(tr::now),
		});
		const auto paid = box->lifetime().make_state<
			rpl::variable<int>
		>();
		*paid = paidAmount->value();
		paid->value() | rpl::start_with_next([=](int already) {
			if (!already) {
				delete base::take(*refund).get();
			} else if (!*refund) {
				const auto skip = st::defaultCheckbox.margin.top();
				*refund = box->addRow(
					object_ptr<Ui::Checkbox>(
						box,
						tr::lng_payment_refund_also(
							lt_count,
							paid->value() | tr::to_count()),
						false,
						st::defaultCheckbox),
					st::boxRowPadding + QMargins(0, skip, 0, skip));
			}
		}, box->lifetime());

		using Flag = MTPaccount_GetPaidMessagesRevenue::Flag;
		user->session().api().request(MTPaccount_GetPaidMessagesRevenue(
			MTP_flags(parent ? Flag::f_parent_peer : Flag()),
			parent ? parent->input : MTPInputPeer(),
			user->inputUser
		)).done([=](const MTPaccount_PaidMessagesRevenue &result) {
			*paidAmount = result.data().vstars_amount().v;
		}).send();
	}));
}

void ForwardToSelf(
		std::shared_ptr<Main::SessionShow> show,
		const Data::ForwardDraft &draft) {
	const auto session = &show->session();
	const auto history = session->data().history(session->user());
	auto resolved = history->resolveForwardDraft(draft);
	if (!resolved.items.empty()) {
		const auto count = resolved.items.size();
		auto action = Api::SendAction(history);
		action.clearDraft = false;
		action.generateLocal = false;
		session->api().forwardMessages(
			std::move(resolved),
			action,
			[=] {
				auto phrase = rpl::variable<TextWithEntities>(
					ChatHelpers::ForwardedMessagePhrase({
					.toCount = 1,
					.singleMessage = (count == 1),
					.to1 = session->user(),
				})).current();
				if (!phrase.empty()) {
					show->showToast(std::move(phrase));
				}
			});
	}
}

} // namespace Window
