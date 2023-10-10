/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_common.h"
#include "menu/menu_send.h"
#include "data/data_poll.h"
#include "ui/widgets/menu/menu_add_action_callback.h"

class History;

namespace Ui {
class RpWidget;
class BoxContent;
class GenericBox;
} // namespace Ui

namespace Data {
class Forum;
class Folder;
class Session;
struct ForwardDraft;
class ForumTopic;
class Thread;
} // namespace Data

namespace Dialogs {
class MainList;
struct EntryState;
struct UnreadState;
} // namespace Dialogs

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace InlineBots {
enum class PeerType : uint8;
using PeerTypes = base::flags<PeerType>;
} // namespace InlineBots

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

void MenuAddMarkAsReadAllChatsAction(
	not_null<Window::SessionController*> controller,
	const PeerMenuCallback &addAction);

void MenuAddMarkAsReadChatListAction(
	not_null<Window::SessionController*> controller,
	Fn<not_null<Dialogs::MainList*>()> &&list,
	const PeerMenuCallback &addAction,
	Fn<Dialogs::UnreadState()> customUnreadState = nullptr);

void PeerMenuExportChat(not_null<PeerData*> peer);
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
	PollData::Flags chosen = PollData::Flags(),
	PollData::Flags disabled = PollData::Flags(),
	Api::SendType sendType = Api::SendType::Normal,
	SendMenu::Type sendMenuType = SendMenu::Type::Scheduled);
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
void PeerMenuUnblockUserWithBotRestart(not_null<UserData*> user);

void BlockSenderFromRepliesBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	FullMsgId id);

void ToggleHistoryArchived(not_null<History*> history, bool archived);
Fn<void()> ClearHistoryHandler(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);
Fn<void()> DeleteAndLeaveHandler(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);

QPointer<Ui::BoxContent> ShowChooseRecipientBox(
	not_null<Window::SessionNavigation*> navigation,
	FnMut<bool(not_null<Data::Thread*>)> &&chosen,
	rpl::producer<QString> titleOverride = nullptr,
	FnMut<void()> &&successCallback = nullptr,
	InlineBots::PeerTypes typesRestriction = 0);
QPointer<Ui::BoxContent> ShowForwardMessagesBox(
	std::shared_ptr<ChatHelpers::Show> show,
	Data::ForwardDraft &&draft,
	Fn<void()> &&successCallback = nullptr);
QPointer<Ui::BoxContent> ShowForwardMessagesBox(
	not_null<Window::SessionNavigation*> navigation,
	Data::ForwardDraft &&draft,
	Fn<void()> &&successCallback = nullptr);
QPointer<Ui::BoxContent> ShowForwardMessagesBox(
	not_null<Window::SessionNavigation*> navigation,
	MessageIdsList &&items,
	Fn<void()> &&successCallback = nullptr);
QPointer<Ui::BoxContent> ShowShareUrlBox(
	not_null<Window::SessionNavigation*> navigation,
	const QString &url,
	const QString &text,
	FnMut<void()> &&successCallback = nullptr);
QPointer<Ui::BoxContent> ShowShareGameBox(
	not_null<Window::SessionNavigation*> navigation,
	not_null<UserData*> bot,
	QString shortName);
QPointer<Ui::BoxContent> ShowDropMediaBox(
	not_null<Window::SessionNavigation*> navigation,
	std::shared_ptr<QMimeData> data,
	not_null<Data::Forum*> forum,
	FnMut<void()> &&successCallback = nullptr);

QPointer<Ui::BoxContent> ShowSendNowMessagesBox(
	not_null<Window::SessionNavigation*> navigation,
	not_null<History*> history,
	MessageIdsList &&items,
	Fn<void()> &&successCallback = nullptr);

void ToggleMessagePinned(
	not_null<Window::SessionNavigation*> navigation,
	FullMsgId itemId,
	bool pin);
void HidePinnedBar(
	not_null<Window::SessionNavigation*> navigation,
	not_null<PeerData*> peer,
	MsgId topicRootId,
	Fn<void()> onHidden);
void UnpinAllMessages(
	not_null<Window::SessionNavigation*> navigation,
	not_null<Data::Thread*> thread);

[[nodiscard]] bool IsUnreadThread(not_null<Data::Thread*> thread);
void MarkAsReadThread(not_null<Data::Thread*> thread);

void AddSeparatorAndShiftUp(const PeerMenuCallback &addAction);

} // namespace Window
