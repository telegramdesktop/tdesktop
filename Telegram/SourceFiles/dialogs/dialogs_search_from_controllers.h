/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/peer_list_box.h"
#include "profile/profile_channel_controllers.h"

namespace Dialogs {

void ShowSearchFromBox(
	not_null<Window::Navigation*> navigation,
	not_null<PeerData*> peer,
	base::lambda<void(not_null<UserData*>)> callback,
	base::lambda<void()> closedCallback);

class ChatSearchFromController : public PeerListController, protected base::Subscriber {
public:
	ChatSearchFromController(
		not_null<Window::Navigation*> navigation,
		not_null<ChatData*> chat,
		base::lambda<void(not_null<UserData*>)> callback);

	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;

private:
	void rebuildRows();
	void checkForEmptyRows();
	void appendRow(not_null<UserData*> user);

	not_null<ChatData*> _chat;
	base::lambda<void(not_null<UserData*>)> _callback;

};

class ChannelSearchFromController : public Profile::ParticipantsBoxController {
public:
	ChannelSearchFromController(
		not_null<Window::Navigation*> navigation,
		not_null<ChannelData*> channel,
		base::lambda<void(not_null<UserData*>)> callback);

	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;

protected:
	std::unique_ptr<PeerListRow> createRow(not_null<UserData*> user) const override;

private:
	base::lambda<void(not_null<UserData*>)> _callback;

};

} // namespace Dialogs
