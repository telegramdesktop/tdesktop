/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/filters/edit_filter_chats_list.h"

#include "history/history.h"
#include "window/window_session_controller.h"
#include "lang/lang_keys.h"

namespace {

constexpr auto kMaxExceptions = 100;

} // namespace

EditFilterChatsListController::EditFilterChatsListController(
	not_null<Window::SessionNavigation*> navigation,
	rpl::producer<QString> title,
	Flags options,
	Flags selected,
	const base::flat_set<not_null<History*>> &peers)
: ChatsListBoxController(navigation)
, _navigation(navigation)
, _title(std::move(title))
, _peers(peers) {
}

Main::Session &EditFilterChatsListController::session() const {
	return _navigation->session();
}

void EditFilterChatsListController::rowClicked(not_null<PeerListRow*> row) {
	const auto count = delegate()->peerListSelectedRowsCount();
	if (count < kMaxExceptions || row->checked()) {
		delegate()->peerListSetRowChecked(row, !row->checked());
		updateTitle();
	}
}

void EditFilterChatsListController::itemDeselectedHook(
		not_null<PeerData*> peer) {
	updateTitle();
}

void EditFilterChatsListController::prepareViewHook() {
	delegate()->peerListSetTitle(std::move(_title));
	delegate()->peerListAddSelectedRows(
		_peers | ranges::view::transform(&History::peer));
}

auto EditFilterChatsListController::createRow(not_null<History*> history)
-> std::unique_ptr<Row> {
	return std::make_unique<Row>(history);
}

void EditFilterChatsListController::updateTitle() {
	const auto count = delegate()->peerListSelectedRowsCount();
	const auto additional = qsl("%1 / %2").arg(count).arg(kMaxExceptions);
	delegate()->peerListSetTitle(tr::lng_profile_add_participant());
	delegate()->peerListSetAdditionalTitle(rpl::single(additional));

}
