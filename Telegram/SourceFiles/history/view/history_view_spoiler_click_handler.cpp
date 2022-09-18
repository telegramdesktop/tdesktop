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

namespace HistoryView {
namespace {

class AnimatedSpoilerClickHandler final : public ClickHandler {
public:
	explicit AnimatedSpoilerClickHandler(Ui::Text::String &text)
	: _text(text) {
	}

	void onClick(ClickContext context) const override;

private:
	Ui::Text::String &_text;

};

void AnimatedSpoilerClickHandler::onClick(ClickContext context) const {
	const auto button = context.button;
	if (button != Qt::LeftButton) {
		return;
	}
	const auto my = context.other.value<ClickHandlerContext>();
	if (const auto d = my.elementDelegate ? my.elementDelegate() : nullptr) {
		_text.setSpoilerRevealed(true, anim::type::normal);
		if (const auto controller = my.sessionWindow.get()) {
			controller->session().data().registerShownSpoiler(my.itemId);
		}
	}
}

} // namespace

void FillTextWithAnimatedSpoilers(Ui::Text::String &text) {
	if (text.hasSpoilers()) {
		text.setSpoilerLink(
			std::make_shared<AnimatedSpoilerClickHandler>(text));
	}
}

void HideSpoilers(Ui::Text::String &text) {
	if (text.hasSpoilers()) {
		text.setSpoilerRevealed(false, anim::type::instant);
	}
}

} // namespace HistoryView
