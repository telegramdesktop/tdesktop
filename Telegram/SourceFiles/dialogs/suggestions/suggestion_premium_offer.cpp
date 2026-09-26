/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/suggestions/suggestion.h"

#include "api/api_premium.h"
#include "apiwrap.h"
#include "data/components/promo_suggestions.h"
#include "dialogs/ui/dialogs_top_bar_suggestion_content.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/sections/settings_premium.h"
#include "ui/text/text_utilities.h"
#include "window/window_session_controller.h"

namespace Dialogs::TopBarSuggestions {
namespace {

constexpr auto kSugPremiumAnnual = "PREMIUM_ANNUAL"_cs;
constexpr auto kSugPremiumUpgrade = "PREMIUM_UPGRADE"_cs;
constexpr auto kSugPremiumRestore = "PREMIUM_RESTORE"_cs;

[[nodiscard]] QString CurrentPremiumKey(
		not_null<Data::PromoSuggestions*> promo) {
	if (promo->current(kSugPremiumAnnual.utf8())) {
		return kSugPremiumAnnual.utf8();
	} else if (promo->current(kSugPremiumRestore.utf8())) {
		return kSugPremiumRestore.utf8();
	} else if (promo->current(kSugPremiumUpgrade.utf8())) {
		return kSugPremiumUpgrade.utf8();
	}
	return QString();
}

bool Available(const Context &context) {
	const auto session = context.session.get();
	if (!session->premiumPossible() || session->premium()) {
		return false;
	}
	return !CurrentPremiumKey(&session->promoSuggestions()).isEmpty();
}

void Activate(ActivateArgs args) {
	const auto session = args.context.session.get();
	const auto promo = &session->promoSuggestions();
	const auto content = args.context.ensureContent();
	const auto findController = args.context.findController;
	const auto recompute = args.recompute;
	const auto done = args.done;
	const auto key = CurrentPremiumKey(promo);
	const auto isPremiumAnnual = (key == kSugPremiumAnnual.utf8());
	const auto isPremiumRestore = (key == kSugPremiumRestore.utf8());
	const auto premiumSub = args.lifetime->make_state<rpl::lifetime>();
	using RightIcon = TopBarSuggestionContent::RightIcon;
	content->setRightIcon(RightIcon::Arrow);
	content->setLeadingWidget(nullptr);

	const auto set = [=](QString discount) {
		constexpr auto kMinus = QChar(0x2212);
		const auto &title = isPremiumAnnual
			? tr::lng_dialogs_suggestions_premium_annual_title
			: isPremiumRestore
			? tr::lng_dialogs_suggestions_premium_restore_title
			: tr::lng_dialogs_suggestions_premium_upgrade_title;
		const auto &description = isPremiumAnnual
			? tr::lng_dialogs_suggestions_premium_annual_about
			: isPremiumRestore
			? tr::lng_dialogs_suggestions_premium_restore_about
			: tr::lng_dialogs_suggestions_premium_upgrade_about;
		content->setContent(
			title(
				tr::now,
				lt_text,
				{ discount.replace(kMinus, QChar()) },
				tr::bold),
			description(tr::now, TextWithEntities::Simple));
		content->setClickedCallback([=] {
			const auto controller = findController();
			Settings::ShowPremium(controller, "dialogs_hint");
			promo->dismiss(key);
			recompute();
		});
		done(content, [content] { content->prepareCollapseSnapshot(); });
	};

	const auto api = &session->api().premium();
	api->statusTextValue() | rpl::on_next([=] {
		for (const auto &o : api->subscriptionOptions()) {
			if (o.months == 12) {
				set(o.discount);
				premiumSub->destroy();
				return;
			}
		}
	}, *premiumSub);
	api->reload();
}

} // namespace

Spec MakePremiumOfferSpec() {
	return {
		.priority = Priority::PremiumOffer,
		.available = Available,
		.activate = Activate,
	};
}

} // namespace Dialogs::TopBarSuggestions
