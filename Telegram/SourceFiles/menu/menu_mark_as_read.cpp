/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "menu/menu_mark_as_read.h"

#include "base/options.h"
#include "data/data_folder.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_histories.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "info/profile/info_profile_values.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/userpic_button.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_window.h"

namespace MarkAsReadMenu {

const char kOptionMarkAsReadMutedChats[] = "mark-as-read-muted-chats";

namespace {

constexpr auto kMaxUnreadWithoutConfirmation = 1000;

base::options::toggle MarkAsReadMutedChats({
	.id = kOptionMarkAsReadMutedChats,
	.name = "Mark muted chats as read",
	.description = "Let \"Mark all chats as read\" read muted chats as well.",
});

[[nodiscard]] MarkAsReadMuted MarkAsReadMutedMode() {
	return MarkAsReadMutedChats.value()
		? MarkAsReadMuted::Include
		: MarkAsReadMuted::Skip;
}

[[nodiscard]] Dialogs::UnreadState MarkAsReadUnreadState(
		not_null<Dialogs::MainList*> list,
		MarkAsReadMuted muted) {
	auto result = Dialogs::UnreadState();
	for (const auto &row : list->indexed()->all()) {
		const auto history = row->history();
		if (!history) {
			continue;
		} else if ((muted == MarkAsReadMuted::Skip) && history->muted()) {
			continue;
		}
		result += history->chatListUnreadState();
	}
	result.known = true;
	return result;
}

[[nodiscard]] Dialogs::UnreadState MarkAsReadAllChatsState(
		not_null<Data::Session*> owner,
		MarkAsReadMuted muted) {
	auto result = MarkAsReadUnreadState(owner->chatsList(), muted);
	if (const auto folder = owner->folderLoaded(Data::Folder::kId)) {
		result += MarkAsReadUnreadState(folder->chatsList(), muted);
	}
	return result;
}

} // namespace

bool IsUnreadThread(not_null<Data::Thread*> thread) {
	return thread->chatListBadgesState().unread;
}

void MarkAsReadThread(
		not_null<Data::Thread*> thread,
		MarkAsReadMuted muted) {
	const auto readHistory = [&](not_null<History*> history) {
		history->owner().histories().readInbox(history);
	};
	if (!IsUnreadThread(thread)
		|| ((muted == MarkAsReadMuted::Skip) && thread->muted())) {
		return;
	} else if (const auto forum = thread->asForum()) {
		forum->enumerateTopics([=](not_null<Data::ForumTopic*> topic) {
			MarkAsReadThread(topic, muted);
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

void MarkAsReadChatList(
		not_null<Dialogs::MainList*> list,
		MarkAsReadMuted muted) {
	auto mark = std::vector<not_null<History*>>();
	for (const auto &row : list->indexed()->all()) {
		if (const auto history = row->history()) {
			mark.push_back(history);
		}
	}
	for (const auto &history : mark) {
		MarkAsReadThread(history, muted);
	}
}

void AddAllChatsAction(
		not_null<Main::Session*> session,
		std::shared_ptr<Ui::Show> show,
		const Ui::Menu::MenuCallback &addAction) {
	const auto owner = &session->data();
	const auto muted = MarkAsReadMutedMode();
	const auto unreadState = MarkAsReadAllChatsState(owner, muted);
	if (!unreadState.messages && !unreadState.marks && !unreadState.chats) {
		return;
	}

	auto callback = [=] {
		const auto markAll = [=] {
			MarkAsReadChatList(owner->chatsList(), muted);
			if (const auto folder = owner->folderLoaded(Data::Folder::kId)) {
				MarkAsReadChatList(folder->chatsList(), muted);
			}
		};
		if (unreadState.messages <= kMaxUnreadWithoutConfirmation) {
			markAll();
			return;
		}
		auto boxCallback = [=](Fn<void()> &&close) {
			close();
			markAll();
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
						tr::rich)
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

void AddChatListAction(
		not_null<Window::SessionController*> controller,
		Fn<not_null<Dialogs::MainList*>()> &&list,
		const Ui::Menu::MenuCallback &addAction,
		Fn<Dialogs::UnreadState()> customUnreadState) {
	// There is no async to make weak from controller.
	const auto muted = MarkAsReadMutedMode();
	const auto unreadState = (muted == MarkAsReadMuted::Skip)
		? MarkAsReadUnreadState(list(), muted)
		: customUnreadState
		? customUnreadState()
		: list()->unreadState();
	if (!unreadState.messages && !unreadState.marks && !unreadState.chats) {
		return;
	}

	auto callback = [=] {
		if (unreadState.messages > kMaxUnreadWithoutConfirmation) {
			auto boxCallback = [=](Fn<void()> &&close) {
				MarkAsReadChatList(list(), muted);
				close();
			};
			controller->show(
				Ui::MakeConfirmBox({
					tr::lng_context_mark_read_sure(),
					std::move(boxCallback)
				}),
				Ui::LayerOption::CloseOther);
		} else {
			MarkAsReadChatList(list(), muted);
		}
	};
	addAction(
		tr::lng_context_mark_read(tr::now),
		std::move(callback),
		&st::menuIconMarkRead);
}

} // namespace MarkAsReadMenu
