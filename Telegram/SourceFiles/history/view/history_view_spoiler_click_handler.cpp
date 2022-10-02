/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_spoiler_click_handler.h"

#include "core/click_handler_types.h" // ClickHandlerContext
#include "data/data_session.h"
#include "history/view/history_view_element.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "base/weak_ptr.h"

namespace HistoryView {

void FillTextWithAnimatedSpoilers(
		not_null<Element*> view,
		Ui::Text::String &text) {
	if (text.hasSpoilers()) {
		text.setSpoilerLinkFilter([weak = base::make_weak(view.get())](
				const ClickContext &context) {
			const auto my = context.other.value<ClickHandlerContext>();
			const auto button = context.button;
			const auto view = weak.get();
			if (button != Qt::LeftButton || !view || !my.elementDelegate) {
				return false;
			} else if (const auto d = my.elementDelegate()) {
				if (const auto controller = my.sessionWindow.get()) {
					controller->session().data().registerShownSpoiler(view);
				}
				return true;
			}
			return false;
		});
	}
}

} // namespace HistoryView
