/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_text_helper.h"

#include "data/data_session.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "main/main_session.h"
#include "base/weak_ptr.h"

namespace HistoryView {

void InitElementTextPart(not_null<Element*> view, Ui::Text::String &text) {
	if (text.hasSpoilers()) {
		text.setSpoilerLinkFilter([weak = base::make_weak(view)](
				const ClickContext &context) {
			const auto button = context.button;
			const auto view = weak.get();
			if (button != Qt::LeftButton || !view) {
				return false;
			}
			view->history()->owner().registerShownSpoiler(view);
			return true;
		});
	}
	if (text.hasCollapsedBlockquots()) {
		const auto weak = base::make_weak(view);
		text.setBlockquoteExpandCallback([=](int quoteIndex, bool expanded) {
			if (const auto view = weak.get()) {
				view->blockquoteExpandChanged();
			}
		});
	}
}

} // namespace HistoryView
