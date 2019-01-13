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
#include "observer_peer.h"
#include "auth_session.h"
#include "apiwrap.h"

namespace Dialogs {

void ShowSearchFromBox(
		not_null<Window::Navigation*> navigation,
		not_null<PeerData*> peer,
		Fn<void(not_null<UserData*>)> callback,
		Fn<void()> closedCallback) {
	auto createController = [
		navigation,
		peer,
		callback = std::move(callback)
	]() -> std::unique_ptr<PeerListController> {
		if (peer && (peer->isChat() || peer->isMegagroup())) {
			return std::make_unique<Dialogs::SearchFromController>(
				navigation,
				peer,
				std::move(callback));
		}
		return nullptr;
	};
	if (auto controller = createController()) {
		auto subscription = std::make_shared<rpl::lifetime>();
		auto box = Ui::show(Box<PeerListBox>(std::move(controller), [subscription](not_null<PeerListBox*> box) {
			box->addButton(langFactory(lng_cancel), [box, subscription] {
				box->closeBox();
			});
		}), LayerOption::KeepOther);
		box->boxClosing() | rpl::start_with_next(
			std::move(closedCallback),
			*subscription);
	}
}

SearchFromController::SearchFromController(
	not_null<Window::Navigation*> navigation,
	not_null<PeerData*> peer,
	Fn<void(not_null<UserData*>)> callback)
: ParticipantsBoxController(
	navigation,
	peer,
	ParticipantsBoxController::Role::Members)
, _callback(std::move(callback)) {
}

void SearchFromController::prepare() {
	ParticipantsBoxController::prepare();
	delegate()->peerListSetTitle(langFactory(lng_search_messages_from));
}

void SearchFromController::rowClicked(not_null<PeerListRow*> row) {
	Expects(row->peer()->isUser());

	if (const auto onstack = base::duplicate(_callback)) {
		onstack(row->peer()->asUser());
	}
}

std::unique_ptr<PeerListRow> SearchFromController::createRow(
		not_null<UserData*> user) const {
	return std::make_unique<PeerListRow>(user);
}

} // namespace Dialogs
