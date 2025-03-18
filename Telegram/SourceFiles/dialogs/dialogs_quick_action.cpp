/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_quick_action.h"

#include "dialogs/ui/dialogs_quick_action_context.h"
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

void PerformQuickDialogAction(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		Ui::QuickDialogAction action,
		FilterId filterId) {
	const auto history = peer->owner().history(peer);
	if (action == Dialogs::Ui::QuickDialogAction::Mute) {
		const auto isMuted = rpl::variable<bool>(
			MuteMenu::ThreadDescriptor(history).isMutedValue()).current();
		MuteMenu::ThreadDescriptor(history).updateMutePeriod(isMuted
			? 0
			: std::numeric_limits<TimeId>::max());
	} else if (action == Dialogs::Ui::QuickDialogAction::Pin) {
		const auto entry = (Dialogs::Entry*)(history);
		Window::TogglePinnedThread(controller, entry, filterId);
	} else if (action == Dialogs::Ui::QuickDialogAction::Read) {
		if (Window::IsUnreadThread(history)) {
			Window::MarkAsReadThread(history);
		} else if (history) {
			peer->owner().histories().changeDialogUnreadMark(history, true);
		}
	} else if (action == Dialogs::Ui::QuickDialogAction::Archive) {
		history->session().api().toggleHistoryArchived(
			history,
			!Window::IsArchived(history),
			[] {});
	} else if (action == Dialogs::Ui::QuickDialogAction::Delete) {
		Window::DeleteAndLeaveHandler(controller, peer)();
	}
}

QString ResolveQuickDialogLottieIconName(
		not_null<PeerData*> peer,
		Ui::QuickDialogAction action,
		FilterId filterId) {
	if (action == Dialogs::Ui::QuickDialogAction::Mute) {
		const auto history = peer->owner().history(peer);
		const auto isMuted = rpl::variable<bool>(
			MuteMenu::ThreadDescriptor(history).isMutedValue()).current();
		return isMuted ? u"swipe_unmute"_q : u"swipe_mute"_q;
	} else if (action == Dialogs::Ui::QuickDialogAction::Pin) {
		const auto history = peer->owner().history(peer);
		const auto entry = (Dialogs::Entry*)(history);
		return entry->isPinnedDialog(filterId)
			? u"swipe_unpin"_q
			: u"swipe_pin"_q;
	} else if (action == Dialogs::Ui::QuickDialogAction::Read) {
		const auto history = peer->owner().history(peer);
		return Window::IsUnreadThread(history)
			? u"swipe_read"_q
			: u"swipe_unread"_q;
	} else if (action == Dialogs::Ui::QuickDialogAction::Archive) {
		const auto history = peer->owner().history(peer);
		return Window::IsArchived(history)
			? u"swipe_unarchive"_q
			: u"swipe_archive"_q;
	} else if (action == Dialogs::Ui::QuickDialogAction::Delete) {
		return u"swipe_delete"_q;
	}
	return u"swipe_disabled"_q;
}

Ui::QuickDialogActionLabel ResolveQuickDialogLabel(
		not_null<History*> history,
		Ui::QuickDialogAction action,
		FilterId filterId) {
	if (action == Dialogs::Ui::QuickDialogAction::Mute) {
		const auto isMuted = rpl::variable<bool>(
			MuteMenu::ThreadDescriptor(history).isMutedValue()).current();
		return isMuted
			? Ui::QuickDialogActionLabel::Unmute
			: Ui::QuickDialogActionLabel::Mute;
	} else if (action == Dialogs::Ui::QuickDialogAction::Pin) {
		const auto entry = (Dialogs::Entry*)(history);
		return entry->isPinnedDialog(filterId)
			? Ui::QuickDialogActionLabel::Unpin
			: Ui::QuickDialogActionLabel::Pin;
	} else if (action == Dialogs::Ui::QuickDialogAction::Read) {
		return Window::IsUnreadThread(history)
			? Ui::QuickDialogActionLabel::Read
			: Ui::QuickDialogActionLabel::Unread;
	} else if (action == Dialogs::Ui::QuickDialogAction::Archive) {
		return Window::IsArchived(history)
			? Ui::QuickDialogActionLabel::Unarchive
			: Ui::QuickDialogActionLabel::Archive;
	} else if (action == Dialogs::Ui::QuickDialogAction::Delete) {
		return Ui::QuickDialogActionLabel::Delete;
	}
	return Ui::QuickDialogActionLabel::Disabled;
}

QString ResolveQuickDialogLabel(Ui::QuickDialogActionLabel action) {
	switch (action) {
	case Ui::QuickDialogActionLabel::Mute:
		return tr::lng_settings_swipe_mute(tr::now);
	case Ui::QuickDialogActionLabel::Unmute:
		return tr::lng_settings_swipe_unmute(tr::now);
	case Ui::QuickDialogActionLabel::Pin:
		return tr::lng_settings_swipe_pin(tr::now);
	case Ui::QuickDialogActionLabel::Unpin:
		return tr::lng_settings_swipe_unpin(tr::now);
	case Ui::QuickDialogActionLabel::Read:
		return tr::lng_settings_swipe_read(tr::now);
	case Ui::QuickDialogActionLabel::Unread:
		return tr::lng_settings_swipe_unread(tr::now);
	case Ui::QuickDialogActionLabel::Archive:
		return tr::lng_settings_swipe_archive(tr::now);
	case Ui::QuickDialogActionLabel::Unarchive:
		return tr::lng_settings_swipe_unarchive(tr::now);
	case Ui::QuickDialogActionLabel::Delete:
		return tr::lng_settings_swipe_delete(tr::now);
	default:
		return tr::lng_settings_swipe_disabled(tr::now);
	};
}

const style::color &ResolveQuickActionBg(
		Ui::QuickDialogActionLabel action) {
	switch (action) {
	case Ui::QuickDialogActionLabel::Delete:
		return st::attentionButtonFg;
	case Ui::QuickDialogActionLabel::Disabled:
		return st::windowSubTextFgOver;
	case Ui::QuickDialogActionLabel::Mute:
	case Ui::QuickDialogActionLabel::Unmute:
	case Ui::QuickDialogActionLabel::Pin:
	case Ui::QuickDialogActionLabel::Unpin:
	case Ui::QuickDialogActionLabel::Read:
	case Ui::QuickDialogActionLabel::Unread:
	case Ui::QuickDialogActionLabel::Archive:
	case Ui::QuickDialogActionLabel::Unarchive:
	default:
		return st::windowBgActive;
	};
}

const style::color &ResolveQuickActionBgActive(
		Ui::QuickDialogActionLabel action) {
	return st::windowSubTextFgOver;
}

} // namespace Dialogs
