/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

namespace Ui {
class BoxContent;
} // namespace Ui

namespace Window {
class SessionNavigation;
} // namespace Window

[[nodiscard]] object_ptr<Ui::BoxContent> EditLinkedChatBox(
	not_null<Window::SessionNavigation*> navigation,
	not_null<ChannelData*> channel,
	not_null<ChannelData*> chat,
	bool canEdit,
	Fn<void(ChannelData*)> callback);

[[nodiscard]] object_ptr<Ui::BoxContent> EditLinkedChatBox(
	not_null<Window::SessionNavigation*> navigation,
	not_null<ChannelData*> channel,
	std::vector<not_null<PeerData*>> &&chats,
	Fn<void(ChannelData*)> callback);

void ShowForumForDiscussionError(
	not_null<Window::SessionNavigation*> navigation);
