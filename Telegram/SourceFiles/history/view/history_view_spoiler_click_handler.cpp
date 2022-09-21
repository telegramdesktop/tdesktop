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
namespace {

class AnimatedSpoilerClickHandler final : public ClickHandler {
public:
	AnimatedSpoilerClickHandler(
		not_null<Element*> view,
		Ui::Text::String &text);

	void onClick(ClickContext context) const override;

private:
	base::weak_ptr<Element> _weak;
	Ui::Text::String &_text;

};

AnimatedSpoilerClickHandler::AnimatedSpoilerClickHandler(
	not_null<Element*> view,
	Ui::Text::String &text)
: _weak(view)
, _text(text) {
}

void AnimatedSpoilerClickHandler::onClick(ClickContext context) const {
	const auto button = context.button;
	const auto view = _weak.get();
	if (button != Qt::LeftButton || !view) {
		return;
	}
	const auto my = context.other.value<ClickHandlerContext>();
	if (const auto d = my.elementDelegate ? my.elementDelegate() : nullptr) {
		_text.setSpoilerRevealed(true, anim::type::normal);
		if (const auto controller = my.sessionWindow.get()) {
			controller->session().data().registerShownSpoiler(view);
		}
	}
}

} // namespace

void FillTextWithAnimatedSpoilers(
		not_null<Element*> view,
		Ui::Text::String &text) {
	if (text.hasSpoilers()) {
		text.setSpoilerLink(
			std::make_shared<AnimatedSpoilerClickHandler>(view, text));
	}
}

} // namespace HistoryView
