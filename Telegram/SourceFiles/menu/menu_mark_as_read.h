/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/menu/menu_add_action_callback.h"

namespace Data {
class Thread;
} // namespace Data

namespace Dialogs {
class MainList;
struct UnreadState;
} // namespace Dialogs

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class Show;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace MarkAsReadMenu {

enum class MarkAsReadMuted : uchar {
	Include,
	Skip,
};

[[nodiscard]] bool IsUnreadThread(not_null<Data::Thread*> thread);
void MarkAsReadThread(
	not_null<Data::Thread*> thread,
	MarkAsReadMuted muted = MarkAsReadMuted::Include);
void MarkAsReadChatList(
	not_null<Dialogs::MainList*> list,
	MarkAsReadMuted muted = MarkAsReadMuted::Include);

void AddAllChatsAction(
	not_null<Main::Session*> session,
	std::shared_ptr<Ui::Show> show,
	const Ui::Menu::MenuCallback &addAction);

void AddChatListAction(
	not_null<Window::SessionController*> controller,
	Fn<not_null<Dialogs::MainList*>()> &&list,
	const Ui::Menu::MenuCallback &addAction,
	Fn<Dialogs::UnreadState()> customUnreadState = nullptr);

} // namespace MarkAsReadMenu
