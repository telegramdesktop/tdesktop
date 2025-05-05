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
#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/main_session.h"
#include "menu/menu_mute.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_dialogs.h"

namespace Dialogs {
namespace {

const style::font &SwipeActionFont(
		Dialogs::Ui::QuickDialogActionLabel action,
		int availableWidth) {
	struct Entry final {
		Dialogs::Ui::QuickDialogActionLabel action;
		QString langId;
		style::font font;
	};
	static auto Fonts = std::vector<Entry>();
	for (auto &entry : Fonts) {
		if (entry.action == action) {
			if (entry.langId == Lang::GetInstance().id()) {
				return entry.font;
			}
		}
	}
	constexpr auto kNormalFontSize = 13;
	constexpr auto kMinFontSize = 5;
	for (auto i = kNormalFontSize; i >= kMinFontSize; --i) {
		auto font = style::font(
			style::ConvertScale(i, style::Scale()),
			st::semiboldFont->flags(),
			st::semiboldFont->family());
		if (font->width(ResolveQuickDialogLabel(action)) <= availableWidth
			|| i == kMinFontSize) {
			Fonts.emplace_back(Entry{
				.action = action,
				.langId = Lang::GetInstance().id(),
				.font = std::move(font),
			});
			return Fonts.back().font;
		}
	}
	Unexpected("SwipeActionFont: can't find font.");
}

} // namespace

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
		controller->showToast(isMuted
			? tr::lng_quick_dialog_action_toast_unmute_success(tr::now)
			: tr::lng_quick_dialog_action_toast_mute_success(tr::now));
	} else if (action == Dialogs::Ui::QuickDialogAction::Pin) {
		const auto entry = (Dialogs::Entry*)(history);
		const auto isPinned = entry->isPinnedDialog(filterId);
		const auto onToggled = isPinned
			? Fn<void()>(nullptr)
			: [=] {
				controller->showToast(
					tr::lng_quick_dialog_action_toast_pin_success(tr::now));
			};
		Window::TogglePinnedThread(controller, entry, filterId, onToggled);
		if (isPinned) {
			controller->showToast(
				tr::lng_quick_dialog_action_toast_unpin_success(tr::now));
		}
	} else if (action == Dialogs::Ui::QuickDialogAction::Read) {
		if (Window::IsUnreadThread(history)) {
			Window::MarkAsReadThread(history);
			controller->showToast(
				tr::lng_quick_dialog_action_toast_read_success(tr::now));
		} else if (history) {
			peer->owner().histories().changeDialogUnreadMark(history, true);
			controller->showToast(
				tr::lng_quick_dialog_action_toast_unread_success(tr::now));
		}
	} else if (action == Dialogs::Ui::QuickDialogAction::Archive) {
		const auto isArchived = Window::IsArchived(history);
		controller->showToast(isArchived
			? tr::lng_quick_dialog_action_toast_unarchive_success(tr::now)
			: tr::lng_quick_dialog_action_toast_archive_success(tr::now));
		history->session().api().toggleHistoryArchived(
			history,
			!isArchived,
			[] {});
	} else if (action == Dialogs::Ui::QuickDialogAction::Delete) {
		Window::DeleteAndLeaveHandler(controller, peer)();
	}
}

QString ResolveQuickDialogLottieIconName(Ui::QuickDialogActionLabel action) {
	switch (action) {
	case Ui::QuickDialogActionLabel::Mute:
		return u"swipe_mute"_q;
	case Ui::QuickDialogActionLabel::Unmute:
		return u"swipe_unmute"_q;
	case Ui::QuickDialogActionLabel::Pin:
		return u"swipe_pin"_q;
	case Ui::QuickDialogActionLabel::Unpin:
		return u"swipe_unpin"_q;
	case Ui::QuickDialogActionLabel::Read:
		return u"swipe_read"_q;
	case Ui::QuickDialogActionLabel::Unread:
		return u"swipe_unread"_q;
	case Ui::QuickDialogActionLabel::Archive:
		return u"swipe_archive"_q;
	case Ui::QuickDialogActionLabel::Unarchive:
		return u"swipe_unarchive"_q;
	case Ui::QuickDialogActionLabel::Delete:
		return u"swipe_delete"_q;
	default:
		return u"swipe_disabled"_q;
	}
}

Ui::QuickDialogActionLabel ResolveQuickDialogLabel(
		not_null<History*> history,
		Ui::QuickDialogAction action,
		FilterId filterId) {
	if (action == Dialogs::Ui::QuickDialogAction::Mute) {
		if (history->peer->isSelf()) {
			return Ui::QuickDialogActionLabel::Disabled;
		}
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
		const auto unread = Window::IsUnreadThread(history);
		if (history->isForum() && !unread) {
			return Ui::QuickDialogActionLabel::Disabled;
		}
		return unread
			? Ui::QuickDialogActionLabel::Read
			: Ui::QuickDialogActionLabel::Unread;
	} else if (action == Dialogs::Ui::QuickDialogAction::Archive) {
		if (!Window::CanArchive(history, history->peer)) {
			return Ui::QuickDialogActionLabel::Disabled;
		}
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
		return tr::lng_settings_quick_dialog_action_mute(tr::now);
	case Ui::QuickDialogActionLabel::Unmute:
		return tr::lng_settings_quick_dialog_action_unmute(tr::now);
	case Ui::QuickDialogActionLabel::Pin:
		return tr::lng_settings_quick_dialog_action_pin(tr::now);
	case Ui::QuickDialogActionLabel::Unpin:
		return tr::lng_settings_quick_dialog_action_unpin(tr::now);
	case Ui::QuickDialogActionLabel::Read:
		return tr::lng_settings_quick_dialog_action_read(tr::now);
	case Ui::QuickDialogActionLabel::Unread:
		return tr::lng_settings_quick_dialog_action_unread(tr::now);
	case Ui::QuickDialogActionLabel::Archive:
		return tr::lng_settings_quick_dialog_action_archive(tr::now);
	case Ui::QuickDialogActionLabel::Unarchive:
		return tr::lng_settings_quick_dialog_action_unarchive(tr::now);
	case Ui::QuickDialogActionLabel::Delete:
		return tr::lng_settings_quick_dialog_action_delete(tr::now);
	default:
		return tr::lng_settings_quick_dialog_action_disabled(tr::now);
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

void DrawQuickAction(
		QPainter &p,
		const QRect &rect,
		not_null<Lottie::Icon*> icon,
		Ui::QuickDialogActionLabel label,
		float64 iconRatio,
		bool twoLines) {
	const auto iconSize = st::dialogsQuickActionSize * iconRatio;
	const auto innerHeight = iconSize * 2;
	const auto top = (rect.height() - innerHeight) / 2;
	icon->paint(p, rect.x() + (rect.width() - iconSize) / 2, top);
	p.setPen(st::premiumButtonFg);
	p.setBrush(Qt::NoBrush);
	const auto availableWidth = rect.width();
	p.setFont(SwipeActionFont(label, availableWidth));
	if (twoLines) {
		auto text = ResolveQuickDialogLabel(label);
		const auto index = text.indexOf(' ');
		if (index != -1) {
			text = text.replace(index, 1, '\n');
		}
		p.drawText(
			QRect(rect.x(), top, availableWidth, innerHeight),
			std::move(text),
			style::al_bottom);
	} else {
		p.drawText(
			QRect(rect.x(), top, availableWidth, innerHeight),
			ResolveQuickDialogLabel(label),
			style::al_bottom);
	}
}

} // namespace Dialogs
