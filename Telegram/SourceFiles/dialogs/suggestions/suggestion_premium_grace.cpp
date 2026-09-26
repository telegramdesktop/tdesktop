/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/suggestions/suggestion.h"

#include "core/click_handler_types.h"
#include "data/components/promo_suggestions.h"
#include "dialogs/ui/dialogs_top_bar_suggestion_content.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/text/text_utilities.h"
#include "window/window_session_controller.h"

namespace Dialogs::TopBarSuggestions {
namespace {

constexpr auto kSugPremiumGrace = "PREMIUM_GRACE"_cs;

bool Available(const Context &context) {
	const auto session = context.session.get();
	return session->premiumCanBuy()
		&& session->promoSuggestions().current(kSugPremiumGrace.utf8());
}

void Activate(ActivateArgs args) {
	const auto session = args.context.session.get();
	const auto promo = &session->promoSuggestions();
	const auto content = args.context.ensureContent();
	const auto findController = args.context.findController;
	const auto recompute = args.recompute;
	using RightIcon = TopBarSuggestionContent::RightIcon;
	content->setRightIcon(RightIcon::Close);
	content->setLeadingWidget(nullptr);
	content->setClickedCallback([=] {
		const auto controller = findController();
		UrlClickHandler::Open(
			u"https://t.me/premiumbot?start=status"_q,
			QVariant::fromValue(ClickHandlerContext{
				.sessionWindow = base::make_weak(controller),
			}));
	});
	content->setHideCallback([=] {
		promo->dismiss(kSugPremiumGrace.utf8());
		recompute();
	});
	content->setContent(
		tr::lng_dialogs_suggestions_premium_grace_title(
			tr::now,
			tr::bold),
		tr::lng_dialogs_suggestions_premium_grace_about(
			tr::now,
			TextWithEntities::Simple));
	args.done(content, [content] { content->prepareCollapseSnapshot(); });
}

} // namespace

Spec MakePremiumGraceSpec() {
	return {
		.priority = Priority::PremiumGrace,
		.available = Available,
		.activate = Activate,
	};
}

} // namespace Dialogs::TopBarSuggestions
