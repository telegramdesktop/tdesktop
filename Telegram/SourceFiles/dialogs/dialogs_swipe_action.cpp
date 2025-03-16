/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_swipe_action.h"

#include "dialogs/ui/dialogs_swipe_context.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "main/main_session.h"
#include "menu/menu_mute.h"

namespace Dialogs {

void PerformSwipeDialogAction(
		not_null<PeerData*> peer,
		Ui::SwipeDialogAction action) {
	if (action == Dialogs::Ui::SwipeDialogAction::Mute) {
		const auto history = peer->owner().history(peer);
		MuteMenu::ThreadDescriptor(history).updateMutePeriod(
			std::numeric_limits<TimeId>::max());
	}
}

} // namespace Dialogs
