/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_search_from_controllers.h"

#include "lang/lang_keys.h"
#include "data/data_peer_values.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "apiwrap.h"

namespace Dialogs {

void ShowSearchFromBox(
		not_null<PeerData*> peer,
		Fn<void(not_null<PeerData*>)> callback,
		Fn<void()> closedCallback) {
	auto createController = [
		peer,
		callback = std::move(callback)
	]() -> std::unique_ptr<PeerListController> {
		if (peer && (peer->isChat() || peer->isMegagroup())) {
			return std::make_unique<Dialogs::SearchFromController>(
				peer,
				std::move(callback));
		}
		return nullptr;
	};
	if (auto controller = createController()) {
		auto subscription = std::make_shared<rpl::lifetime>();
		auto box = Ui::show(Box<PeerListBox>(std::move(controller), [subscription](not_null<PeerListBox*> box) {
			box->addButton(tr::lng_cancel(), [box, subscription] {
				box->closeBox();
			});
		}), Ui::LayerOption::KeepOther);
		box->boxClosing() | rpl::start_with_next(
			std::move(closedCallback),
			*subscription);
	}
}

SearchFromController::SearchFromController(
	not_null<PeerData*> peer,
	Fn<void(not_null<PeerData*>)> callback)
: AddSpecialBoxController(
	peer,
	ParticipantsBoxController::Role::Members,
	AdminDoneCallback(),
	BannedDoneCallback())
, _callback(std::move(callback)) {
	_excludeSelf = false;
}

void SearchFromController::prepare() {
	AddSpecialBoxController::prepare();
	delegate()->peerListSetTitle(tr::lng_search_messages_from());
	if (const auto megagroup = peer()->asMegagroup()) {
		if (!delegate()->peerListFindRow(megagroup->id.value)) {
			delegate()->peerListAppendRow(
				std::make_unique<PeerListRow>(megagroup));
			setDescriptionText({});
			delegate()->peerListRefreshRows();
		}
	}
}

void SearchFromController::rowClicked(not_null<PeerListRow*> row) {
	if (const auto onstack = base::duplicate(_callback)) {
		onstack(row->peer());
	}
}

} // namespace Dialogs
