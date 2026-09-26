/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/controls/button_context_menu.h"

#include "base/event_filter.h"
#include "base/unique_qptr.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/ui_utility.h"

namespace Ui {
namespace {

struct State {
	~State() {
		closing = true;
	}

	bool closing = false;
	base::unique_qptr<PopupMenu> menu;
};

} // namespace

void KeepHoveredWhileShown(
		not_null<RippleButton*> button,
		not_null<PopupMenu*> menu) {
	base::install_event_filter(button, [](not_null<QEvent*> e) {
		return (e->type() == QEvent::Leave)
			? base::EventFilterResult::Cancel
			: base::EventFilterResult::Continue;
	}, menu->lifetime());
}

void SetupButtonContextMenu(
		not_null<RippleButton*> button,
		not_null<const style::PopupMenu*> st,
		Fn<void(not_null<PopupMenu*>)> fill) {
	Expects(fill != nullptr);

	const auto state = button->lifetime().make_state<State>();

	button->setAcceptBoth(true, true);
	button->clicks(
	) | rpl::on_next([=](Qt::MouseButton which) {
		if (which != Qt::RightButton || state->menu) {
			return;
		}
		state->menu = base::make_unique_q<PopupMenu>(button, *st);
		fill(state->menu.get());
		if (state->menu->empty()) {
			state->menu = nullptr;
			return;
		}
		button->setForceRippled(true);
		KeepHoveredWhileShown(button, state->menu.get());
		state->menu->setDestroyedCallback([=] {
			if (!state->closing) {
				button->setForceRippled(false);
				SendSynteticMouseEvent(
					button,
					QEvent::MouseMove,
					Qt::NoButton);
			}
		});
		state->menu->popup(QCursor::pos());
	}, button->lifetime());
}

} // namespace Ui
