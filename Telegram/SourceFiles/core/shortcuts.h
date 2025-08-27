/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class QKeyEvent;
class QShortcutEvent;

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
	ChatPinned6,
	ChatPinned7,
	ChatPinned8,

	ShowAccount1,
	ShowAccount2,
	ShowAccount3,
	ShowAccount4,
	ShowAccount5,
	ShowAccount6,

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

	ShowScheduled,

	ShowArchive,
	ShowContacts,

	JustSendMessage,
	SendSilentMessage,
	ScheduleMessage,

	RecordVoice,
	RecordRound,

	ReadChat,
	ArchiveChat,

	MediaViewerFullscreen,

	ShowChatMenu,
	ShowChatPreview,

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

[[maybe_unused]] constexpr auto kShowAccount = {
	Command::ShowAccount1,
	Command::ShowAccount2,
	Command::ShowAccount3,
	Command::ShowAccount4,
	Command::ShowAccount5,
	Command::ShowAccount6,
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

[[nodiscard]] rpl::producer<not_null<Request*>> Requests();

void Start();
void Finish();

void Listen(not_null<QWidget*> widget);

bool Launch(Command command);
bool HandleEvent(not_null<QObject*> object, not_null<QShortcutEvent*> event);

bool HandlePossibleChatSwitch(not_null<QKeyEvent*> event);

struct ChatSwitchRequest {
	Qt::Key action = Qt::Key_Tab; // Key_Tab, Key_Backtab or Key_Escape.
	bool started = false;
};
[[nodiscard]] rpl::producer<ChatSwitchRequest> ChatSwitchRequests();

[[nodiscard]] const QStringList &Errors();

// Media shortcuts are not enabled by default, because other
// applications also use them. They are enabled only when
// the in-app player is active and disabled back after.
void ToggleMediaShortcuts(bool toggled);

// Support shortcuts are not enabled by default, because they
// have some conflicts with default input shortcuts, like Ctrl+Delete.
void ToggleSupportShortcuts(bool toggled);

void Pause();
void Unpause();

[[nodiscard]] auto KeysDefaults()
-> base::flat_map<QKeySequence, base::flat_set<Command>>;
[[nodiscard]] auto KeysCurrents()
-> base::flat_map<QKeySequence, base::flat_set<Command>>;

void Change(
	QKeySequence was,
	QKeySequence now,
	Command command,
	std::optional<Command> restore = {});
void ResetToDefaults();

[[nodiscard]] bool AllowWithoutModifiers(int key);

} // namespace Shortcuts
