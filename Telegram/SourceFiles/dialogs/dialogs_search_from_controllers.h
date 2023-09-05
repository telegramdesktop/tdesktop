/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "boxes/peer_list_box.h"
#include "boxes/peers/add_participants_box.h"

namespace Dialogs {

object_ptr<Ui::BoxContent> SearchFromBox(
	not_null<PeerData*> peer,
	Fn<void(not_null<PeerData*>)> callback,
	Fn<void()> closedCallback);

class SearchFromController : public AddSpecialBoxController {
public:
	SearchFromController(
		not_null<PeerData*> peer,
		Fn<void(not_null<PeerData*>)> callback);

	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;

private:
	Fn<void(not_null<PeerData*>)> _callback;

};

} // namespace Dialogs
