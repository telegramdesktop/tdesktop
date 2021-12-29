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
#include "ui/spoiler_click_handler.h"

namespace HistoryView {
namespace {

class AnimatedSpoilerClickHandler final : public SpoilerClickHandler {
public:
	AnimatedSpoilerClickHandler() = default;

	void onClick(ClickContext context) const override;

};

void AnimatedSpoilerClickHandler::onClick(ClickContext context) const {
	const auto button = context.button;
	if (button != Qt::LeftButton) {
		return;
	}
	const auto my = context.other.value<ClickHandlerContext>();
	if (const auto d = my.elementDelegate ? my.elementDelegate() : nullptr) {
		d->elementShowSpoilerAnimation();
		const auto nonconst = const_cast<AnimatedSpoilerClickHandler*>(this);
		nonconst->setStartMs(crl::now());
		SpoilerClickHandler::onClick({});

		if (const auto controller = my.sessionWindow.get()) {
			controller->session().data().registerShownSpoiler(my.itemId);
		}
	}
}

} // namespace

void FillTextWithAnimatedSpoilers(Ui::Text::String &text) {
	for (auto i = 0; i < text.spoilersCount(); i++) {
		text.setSpoiler(
			i + 1,
			std::make_shared<AnimatedSpoilerClickHandler>());
	}
}

void HideSpoilers(Ui::Text::String &text) {
	for (auto i = 0; i < text.spoilersCount(); i++) {
		text.setSpoilerShown(i + 1, false);
	}
}

} // namespace HistoryView
