/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"

class ChannelData;

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class Show;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Api {

void CheckChatInvite(
	not_null<Window::SessionController*> controller,
	const QString &hash,
	ChannelData *invitePeekChannel = nullptr,
	Fn<void()> loaded = nullptr);

void ProcessChatInviteJoinResult(
	not_null<Main::Session*> session,
	std::shared_ptr<Ui::Show> show,
	const MTPmessages_ChatInviteJoinResult &result,
	Fn<void(const MTPUpdates &updates)> done,
	base::weak_ptr<Window::SessionController> controller = {});

} // namespace Api
