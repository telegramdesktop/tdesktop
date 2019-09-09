/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/peer_list_box.h"
#include "boxes/peers/add_participants_box.h"

namespace Dialogs {

void ShowSearchFromBox(
	not_null<PeerData*> peer,
	Fn<void(not_null<UserData*>)> callback,
	Fn<void()> closedCallback);

class SearchFromController : public AddSpecialBoxController {
public:
	SearchFromController(
		not_null<PeerData*> peer,
		Fn<void(not_null<UserData*>)> callback);

	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;

protected:
	std::unique_ptr<PeerListRow> createRow(not_null<UserData*> user) const;

private:
	Fn<void(not_null<UserData*>)> _callback;

};

} // namespace Dialogs
