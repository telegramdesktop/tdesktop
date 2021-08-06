/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Shortcuts {

enum class Command {
	Close,
	Lock,
	Minimize,
	Quit,

	MediaPlay,
	MediaPause,
	MediaPlayPause,
	MediaStop,
	MediaPrevious,
	MediaNext,

	Search,

	ChatPrevious,
	ChatNext,
	ChatFirst,
	ChatLast,
	ChatSelf,
	ChatPinned1,
	ChatPinned2,
	ChatPinned3,
	ChatPinned4,
	ChatPinned5,

	ShowAllChats,
	ShowFolder1,
	ShowFolder2,
	ShowFolder3,
	ShowFolder4,
	ShowFolder5,
	ShowFolder6,
	ShowFolderLast,

	FolderNext,
	FolderPrevious,

	ShowArchive,
	ShowContacts,

	JustSendMessage,
	SendSilentMessage,
	ScheduleMessage,

	ReadChat,

	SupportReloadTemplates,
	SupportToggleMuted,
	SupportScrollToCurrent,
	SupportHistoryBack,
	SupportHistoryForward,
};

[[maybe_unused]] constexpr auto kShowFolder = {
	Command::ShowAllChats,
	Command::ShowFolder1,
	Command::ShowFolder2,
	Command::ShowFolder3,
	Command::ShowFolder4,
	Command::ShowFolder5,
	Command::ShowFolder6,
	Command::ShowFolderLast,
};

[[nodiscard]] FnMut<bool()> RequestHandler(Command command);

class Request {
public:
	bool check(Command command, int priority = 0);
	bool handle(FnMut<bool()> handler);

private:
	explicit Request(std::vector<Command> commands);

	std::vector<Command> _commands;
	int _handlerPriority = -1;
	FnMut<bool()> _handler;

	friend FnMut<bool()> RequestHandler(std::vector<Command> commands);

};

rpl::producer<not_null<Request*>> Requests();

void Start();
void Finish();

bool Launch(Command command);
bool HandleEvent(not_null<QShortcutEvent*> event);

const QStringList &Errors();

// Media shortcuts are not enabled by default, because other
// applications also use them. They are enabled only when
// the in-app player is active and disabled back after.
void ToggleMediaShortcuts(bool toggled);

// Support shortcuts are not enabled by default, because they
// have some conflicts with default input shortcuts, like Ctrl+Delete.
void ToggleSupportShortcuts(bool toggled);

} // namespace Shortcuts
