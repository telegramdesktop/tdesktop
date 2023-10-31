/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/boosts/giveaway/giveaway_list_controllers.h"

#include "data/data_peer.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"

namespace Giveaway {

AwardMembersListController::AwardMembersListController(
	not_null<Window::SessionNavigation*> navigation,
	not_null<PeerData*> peer)
: ParticipantsBoxController(navigation, peer, ParticipantsRole::Members) {
}

void AwardMembersListController::rowClicked(not_null<PeerListRow*> row) {
	delegate()->peerListSetRowChecked(row, !row->checked());
}

std::unique_ptr<PeerListRow> AwardMembersListController::createRow(
		not_null<PeerData*> participant) const {
	const auto user = participant->asUser();
	if (!user || user->isInaccessible() || user->isBot() || user->isSelf()) {
		return nullptr;
	}
	return std::make_unique<PeerListRow>(participant);
}

base::unique_qptr<Ui::PopupMenu> AwardMembersListController::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	return nullptr;
}

} // namespace Giveaway
