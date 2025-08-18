/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_common.h"
#include "base/object_ptr.h"
#include "menu/menu_send.h"
#include "data/data_poll.h"
#include "ui/widgets/menu/menu_add_action_callback.h"

class History;

namespace Api {
struct SendOptions;
} // namespace Api

namespace Ui {
class RpWidget;
class BoxContent;
class GenericBox;
class Show;
} // namespace Ui

namespace Data {
class Forum;
class Folder;
class Session;
struct ForwardDraft;
class ForumTopic;
class SavedMessages;
class SavedSublist;
class Thread;
} // namespace Data

namespace Dialogs {
class MainList;
struct EntryState;
struct UnreadState;
class Key;
class Entry;
} // namespace Dialogs

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace InlineBots {
enum class PeerType : uint8;
using PeerTypes = base::flags<PeerType>;
} // namespace InlineBots

namespace Main {
class SessionShow;
} // namespace Main

namespace Window {

class Controller;
class SessionController;
class SessionNavigation;

extern const char kOptionViewProfileInChatsListContextMenu[];

using PeerMenuCallback = Ui::Menu::MenuCallback;

void FillDialogsEntryMenu(
	not_null<SessionController*> controller,
	Dialogs::EntryState request,
	const PeerMenuCallback &addAction);
bool FillVideoChatMenu(
	not_null<SessionController*> controller,
	Dialogs::EntryState request,
	const PeerMenuCallback &addAction);

void FillSenderUserpicMenu(
	not_null<SessionController*> controller,
	not_null<PeerData*> peer,
	Ui::InputField *fieldForMention,
	Dialogs::Key searchInEntry,
	const PeerMenuCallback &addAction);

void MenuAddMarkAsReadAllChatsAction(
	not_null<Main::Session*> session,
	std::shared_ptr<Ui::Show> show,
	const PeerMenuCallback &addAction);

void MenuAddMarkAsReadChatListAction(
	not_null<Window::SessionController*> controller,
	Fn<not_null<Dialogs::MainList*>()> &&list,
	const PeerMenuCallback &addAction,
	Fn<Dialogs::UnreadState()> customUnreadState = nullptr);

void PeerMenuExportChat(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);
void PeerMenuDeleteContact(
	not_null<Window::SessionController*> controller,
	not_null<UserData*> user);
void PeerMenuShareContactBox(
	not_null<Window::SessionNavigation*> navigation,
	not_null<UserData*> user);
void PeerMenuAddChannelMembers(
	not_null<Window::SessionNavigation*> navigation,
	not_null<ChannelData*> channel);
void PeerMenuCreatePoll(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	FullReplyTo replyTo = FullReplyTo(),
	SuggestPostOptions suggest = SuggestPostOptions(),
	PollData::Flags chosen = PollData::Flags(),
	PollData::Flags disabled = PollData::Flags(),
	Api::SendType sendType = Api::SendType::Normal,
	SendMenu::Details sendMenuDetails = SendMenu::Details());
enum class TodoWantsPremium {
	Create,
	Add,
	Mark,
};
void PeerMenuTodoWantsPremium(TodoWantsPremium type);
void PeerMenuCreateTodoList(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	FullReplyTo replyTo = FullReplyTo(),
	SuggestPostOptions suggest = SuggestPostOptions(),
	Api::SendType sendType = Api::SendType::Normal,
	SendMenu::Details sendMenuDetails = SendMenu::Details());
void PeerMenuEditTodoList(
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item);
[[nodiscard]] bool PeerMenuShowAddTodoListTasks(not_null<HistoryItem*> item);
void PeerMenuAddTodoListTasks(
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item);
void PeerMenuDeleteTopicWithConfirmation(
	not_null<Window::SessionNavigation*> navigation,
	not_null<Data::ForumTopic*> topic);
void PeerMenuDeleteTopic(
	not_null<Window::SessionNavigation*> navigation,
	not_null<Data::ForumTopic*> topic);

struct ClearChat {
};
struct ClearReply {
	FullMsgId replyId;
};
void PeerMenuBlockUserBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::Controller*> window,
	not_null<PeerData*> peer,
	std::variant<v::null_t, bool> suggestReport,
	std::variant<v::null_t, ClearChat, ClearReply> suggestClear);
void PeerMenuUnblockUserWithBotRestart(
	std::shared_ptr<Ui::Show> show,
	not_null<UserData*> user);

void BlockSenderFromRepliesBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	FullMsgId id);

void ToggleHistoryArchived(
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<History*> history,
	bool archived);
Fn<void()> ClearHistoryHandler(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);
Fn<void()> DeleteAndLeaveHandler(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);
Fn<void()> DeleteSublistHandler(
	not_null<Window::SessionController*> controller,
	not_null<Data::SavedSublist*> sublist);

object_ptr<Ui::BoxContent> PrepareChooseRecipientBox(
	not_null<Main::Session*> session,
	FnMut<bool(not_null<Data::Thread*>)> &&chosen,
	rpl::producer<QString> titleOverride = nullptr,
	FnMut<void()> &&successCallback = nullptr,
	InlineBots::PeerTypes typesRestriction = 0,
	Fn<void(
		std::vector<not_null<Data::Thread*>>,
		Api::SendOptions)> sendMany = nullptr);
base::weak_qptr<Ui::BoxContent> ShowChooseRecipientBox(
	not_null<Window::SessionNavigation*> navigation,
	FnMut<bool(not_null<Data::Thread*>)> &&chosen,
	rpl::producer<QString> titleOverride = nullptr,
	FnMut<void()> &&successCallback = nullptr,
	InlineBots::PeerTypes typesRestriction = 0);
base::weak_qptr<Ui::BoxContent> ShowForwardMessagesBox(
	std::shared_ptr<ChatHelpers::Show> show,
	Data::ForwardDraft &&draft,
	Fn<void()> &&successCallback = nullptr);
base::weak_qptr<Ui::BoxContent> ShowForwardMessagesBox(
	not_null<Window::SessionNavigation*> navigation,
	Data::ForwardDraft &&draft,
	Fn<void()> &&successCallback = nullptr);
base::weak_qptr<Ui::BoxContent> ShowForwardMessagesBox(
	not_null<Window::SessionNavigation*> navigation,
	MessageIdsList &&items,
	Fn<void()> &&successCallback = nullptr);
base::weak_qptr<Ui::BoxContent> ShowShareUrlBox(
	not_null<Window::SessionNavigation*> navigation,
	const QString &url,
	const QString &text,
	FnMut<void()> &&successCallback = nullptr);
base::weak_qptr<Ui::BoxContent> ShowShareGameBox(
	not_null<Window::SessionNavigation*> navigation,
	not_null<UserData*> bot,
	QString shortName);
base::weak_qptr<Ui::BoxContent> ShowDropMediaBox(
	not_null<Window::SessionNavigation*> navigation,
	std::shared_ptr<QMimeData> data,
	not_null<Data::Forum*> forum,
	FnMut<void()> &&successCallback = nullptr);
base::weak_qptr<Ui::BoxContent> ShowDropMediaBox(
	not_null<Window::SessionNavigation*> navigation,
	std::shared_ptr<QMimeData> data,
	not_null<Data::SavedMessages*> monoforum,
	FnMut<void()> &&successCallback = nullptr);

base::weak_qptr<Ui::BoxContent> ShowSendNowMessagesBox(
	not_null<Window::SessionNavigation*> navigation,
	not_null<History*> history,
	MessageIdsList &&items,
	Fn<void()> &&successCallback = nullptr);

void ToggleMessagePinned(
	not_null<Window::SessionNavigation*> navigation,
	FullMsgId itemId,
	bool pin);
void TogglePinnedThread(
	not_null<Window::SessionController*> controller,
	not_null<Dialogs::Entry*> entry,
	FilterId filterId,
	Fn<void()> onToggled);
void HidePinnedBar(
	not_null<Window::SessionNavigation*> navigation,
	not_null<PeerData*> peer,
	MsgId topicRootId,
	PeerId monoforumPeerId,
	Fn<void()> onHidden);
void UnpinAllMessages(
	not_null<Window::SessionNavigation*> navigation,
	not_null<Data::Thread*> thread);

[[nodiscard]] bool IsUnreadThread(not_null<Data::Thread*> thread);
void MarkAsReadThread(not_null<Data::Thread*> thread);

void AddSeparatorAndShiftUp(const PeerMenuCallback &addAction);

[[nodiscard]] bool IsArchived(not_null<History*> history);
[[nodiscard]] bool CanArchive(History *history, PeerData *peer);

void PeerMenuConfirmToggleFee(
	not_null<Window::SessionNavigation*> navigation,
	std::shared_ptr<rpl::variable<int>> paidAmount,
	not_null<PeerData*> peer,
	not_null<UserData*> user,
	bool removeFee);

void ForwardToSelf(
	std::shared_ptr<Main::SessionShow> show,
	const Data::ForwardDraft &draft);

} // namespace Window
