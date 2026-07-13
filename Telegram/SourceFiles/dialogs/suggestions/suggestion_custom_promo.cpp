/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/suggestions/suggestion.h"

#include "core/click_handler_types.h"
#include "core/ui_integration.h"
#include "data/components/promo_suggestions.h"
#include "dialogs/ui/dialogs_top_bar_suggestion_content.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"

namespace Dialogs::TopBarSuggestions {
namespace {

bool Available(const Context &context) {
	return context.session->promoSuggestions().custom().has_value();
}

void Activate(ActivateArgs args) {
	const auto session = args.context.session.get();
	const auto promo = &session->promoSuggestions();
	const auto custom = promo->custom();
	if (!custom) {
		return;
	}
	const auto content = args.context.ensureContent();
	const auto findController = args.context.findController;
	const auto recompute = args.recompute;
	using RightIcon = TopBarSuggestionContent::RightIcon;
	content->setRightIcon(RightIcon::Close);
	content->setLeadingWidget(nullptr);
	content->setClickedCallback([=] {
		const auto controller = findController();
		UrlClickHandler::Open(
			custom->url,
			QVariant::fromValue(ClickHandlerContext{
				.sessionWindow = base::make_weak(controller),
			}));
	});
	content->setHideCallback([=] {
		promo->dismiss(custom->suggestion);
		recompute();
	});
	content->setContent(
		custom->title,
		custom->description,
		Core::TextContext({ .session = session }));
	args.done(content, [content] { content->prepareCollapseSnapshot(); });
}

} // namespace

Spec MakeCustomPromoSpec() {
	return {
		.priority = Priority::CustomPromo,
		.available = Available,
		.activate = Activate,
	};
}

} // namespace Dialogs::TopBarSuggestions
