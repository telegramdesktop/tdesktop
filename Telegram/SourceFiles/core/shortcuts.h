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

	SupportReloadTemplates,
	SupportToggleMuted,
	SupportScrollToCurrent,
};

[[nodiscard]] FnMut<bool()> RequestHandler(Command command);

class Request {
public:
	bool check(Command command, int priority = 0);
	bool handle(FnMut<bool()> handler);

private:
	explicit Request(Command command);

	Command _command;
	int _handlerPriority = -1;
	FnMut<bool()> _handler;

	friend FnMut<bool()> RequestHandler(Command command);

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
void EnableMediaShortcuts();
void DisableMediaShortcuts();

} // namespace Shortcuts
