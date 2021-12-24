/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_spoiler_click_handler.h"

#include "core/click_handler_types.h" // ClickHandlerContext
#include "history/view/history_view_element.h"
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
	}
}

} // namespace

void FillTextWithAnimatedSpoilers(
		Ui::Text::String &text,
		const TextWithEntities &textWithEntities) {
	for (auto i = 0; i < textWithEntities.entities.size(); i++) {
		if (textWithEntities.entities[i].type() == EntityType::Spoiler) {
			text.setSpoiler(
				i + 1,
				std::make_shared<AnimatedSpoilerClickHandler>());
		}
	}
}

} // namespace HistoryView
