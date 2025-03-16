/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_swipe_action.h"

#include "dialogs/ui/dialogs_swipe_context.h"
#include "apiwrap.h"
#include "data/data_histories.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "dialogs/dialogs_entry.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "menu/menu_mute.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"

namespace Dialogs {

void PerformSwipeDialogAction(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		Ui::SwipeDialogAction action,
		FilterId filterId) {
	const auto history = peer->owner().history(peer);
	if (action == Dialogs::Ui::SwipeDialogAction::Mute) {
		const auto isMuted = rpl::variable<bool>(
			MuteMenu::ThreadDescriptor(history).isMutedValue()).current();
		MuteMenu::ThreadDescriptor(history).updateMutePeriod(isMuted
			? 0
			: std::numeric_limits<TimeId>::max());
	} else if (action == Dialogs::Ui::SwipeDialogAction::Pin) {
		const auto entry = (Dialogs::Entry*)(history);
		Window::TogglePinnedThread(controller, entry, filterId);
	} else if (action == Dialogs::Ui::SwipeDialogAction::Read) {
		if (Window::IsUnreadThread(history)) {
			Window::MarkAsReadThread(history);
		} else if (history) {
			peer->owner().histories().changeDialogUnreadMark(history, true);
		}
	} else if (action == Dialogs::Ui::SwipeDialogAction::Archive) {
		history->session().api().toggleHistoryArchived(
			history,
			!Window::IsArchived(history),
			[] {});
	} else if (action == Dialogs::Ui::SwipeDialogAction::Delete) {
		Window::DeleteAndLeaveHandler(controller, peer)();
	}
}

QString ResolveSwipeDialogLottieIconName(
		not_null<PeerData*> peer,
		Ui::SwipeDialogAction action,
		FilterId filterId) {
	if (action == Dialogs::Ui::SwipeDialogAction::Mute) {
		const auto history = peer->owner().history(peer);
		const auto isMuted = rpl::variable<bool>(
			MuteMenu::ThreadDescriptor(history).isMutedValue()).current();
		return isMuted ? u"swipe_unmute"_q : u"swipe_mute"_q;
	} else if (action == Dialogs::Ui::SwipeDialogAction::Pin) {
		const auto history = peer->owner().history(peer);
		const auto entry = (Dialogs::Entry*)(history);
		return entry->isPinnedDialog(filterId)
			? u"swipe_unpin"_q
			: u"swipe_pin"_q;
	} else if (action == Dialogs::Ui::SwipeDialogAction::Read) {
		const auto history = peer->owner().history(peer);
		return Window::IsUnreadThread(history)
			? u"swipe_read"_q
			: u"swipe_unread"_q;
	} else if (action == Dialogs::Ui::SwipeDialogAction::Archive) {
		const auto history = peer->owner().history(peer);
		return Window::IsArchived(history)
			? u"swipe_unarchive"_q
			: u"swipe_archive"_q;
	} else if (action == Dialogs::Ui::SwipeDialogAction::Delete) {
		return u"swipe_delete"_q;
	}
	return u"swipe_disabled"_q;
}

Ui::SwipeDialogActionLabel ResolveSwipeDialogLabel(
		not_null<History*> history,
		Ui::SwipeDialogAction action,
		FilterId filterId) {
	if (action == Dialogs::Ui::SwipeDialogAction::Mute) {
		const auto isMuted = rpl::variable<bool>(
			MuteMenu::ThreadDescriptor(history).isMutedValue()).current();
		return isMuted
			? Ui::SwipeDialogActionLabel::Unmute
			: Ui::SwipeDialogActionLabel::Mute;
	} else if (action == Dialogs::Ui::SwipeDialogAction::Pin) {
		const auto entry = (Dialogs::Entry*)(history);
		return entry->isPinnedDialog(filterId)
			? Ui::SwipeDialogActionLabel::Unpin
			: Ui::SwipeDialogActionLabel::Pin;
	} else if (action == Dialogs::Ui::SwipeDialogAction::Read) {
		return Window::IsUnreadThread(history)
			? Ui::SwipeDialogActionLabel::Read
			: Ui::SwipeDialogActionLabel::Unread;
	} else if (action == Dialogs::Ui::SwipeDialogAction::Archive) {
		return Window::IsArchived(history)
			? Ui::SwipeDialogActionLabel::Unarchive
			: Ui::SwipeDialogActionLabel::Archive;
	} else if (action == Dialogs::Ui::SwipeDialogAction::Delete) {
		return Ui::SwipeDialogActionLabel::Delete;
	}
	return Ui::SwipeDialogActionLabel::Disabled;
}

QString ResolveSwipeDialogLabel(Ui::SwipeDialogActionLabel action) {
	switch (action) {
	case Ui::SwipeDialogActionLabel::Mute:
		return tr::lng_settings_swipe_mute(tr::now);
	case Ui::SwipeDialogActionLabel::Unmute:
		return tr::lng_settings_swipe_unmute(tr::now);
	case Ui::SwipeDialogActionLabel::Pin:
		return tr::lng_settings_swipe_pin(tr::now);
	case Ui::SwipeDialogActionLabel::Unpin:
		return tr::lng_settings_swipe_unpin(tr::now);
	case Ui::SwipeDialogActionLabel::Read:
		return tr::lng_settings_swipe_read(tr::now);
	case Ui::SwipeDialogActionLabel::Unread:
		return tr::lng_settings_swipe_unread(tr::now);
	case Ui::SwipeDialogActionLabel::Archive:
		return tr::lng_settings_swipe_archive(tr::now);
	case Ui::SwipeDialogActionLabel::Unarchive:
		return tr::lng_settings_swipe_unarchive(tr::now);
	case Ui::SwipeDialogActionLabel::Delete:
		return tr::lng_settings_swipe_delete(tr::now);
	default:
		return tr::lng_settings_swipe_disabled(tr::now);
	};
}

} // namespace Dialogs
