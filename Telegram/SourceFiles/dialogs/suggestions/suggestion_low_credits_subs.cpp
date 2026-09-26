/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/suggestions/suggestion.h"

#include "api/api_credits.h"
#include "data/components/credits.h"
#include "data/components/promo_suggestions.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/ui/dialogs_top_bar_suggestion_content.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_credits_graphics.h"
#include "ui/effects/credits_graphics.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "window/window_session_controller.h"

namespace Dialogs::TopBarSuggestions {
namespace {

constexpr auto kSugLowCreditsSubs = "STARS_SUBSCRIPTION_LOW_BALANCE"_cs;

bool Available(const Context &context) {
	const auto session = context.session.get();
	return session->premiumCanBuy()
		&& session->promoSuggestions().current(kSugLowCreditsSubs.utf8());
}

void Activate(ActivateArgs args) {
	const auto session = args.context.session.get();
	const auto promo = &session->promoSuggestions();
	const auto content = args.context.ensureContent();
	const auto findController = args.context.findController;
	const auto recompute = args.recompute;
	const auto done = args.done;

	const auto history = args.lifetime->make_state<
		std::unique_ptr<Api::CreditsHistory>>(
			std::make_unique<Api::CreditsHistory>(
				session->user(),
				false,
				false));
	const auto balanceSub = args.lifetime->make_state<rpl::lifetime>();

	const auto show = [=](const QString &peers, uint64 needed, uint64 whole) {
		if (whole > needed) {
			return;
		}
		using RightIcon = TopBarSuggestionContent::RightIcon;
		content->setRightIcon(RightIcon::Close);
		content->setLeadingWidget(nullptr);
		content->setClickedCallback([=] {
			const auto controller = findController();
			controller->uiShow()->show(Box(
				Settings::SmallBalanceBox,
				controller->uiShow(),
				needed,
				Settings::SmallBalanceSubscription{ peers },
				[=] {
					promo->dismiss(kSugLowCreditsSubs.utf8());
					recompute();
				}));
		});
		content->setHideCallback([=] {
			promo->dismiss(kSugLowCreditsSubs.utf8());
			recompute();
		});
		content->setContent(
			tr::lng_dialogs_suggestions_credits_sub_low_title(
				tr::now,
				lt_count,
				float64(needed - whole),
				lt_emoji,
				Ui::MakeCreditsIconEntity(),
				lt_channels,
				{ peers },
				tr::bold),
			tr::lng_dialogs_suggestions_credits_sub_low_about(
				tr::now,
				TextWithEntities::Simple),
			Ui::MakeCreditsIconContext(
				content->contentTitleSt().font->height,
				1));
		done(content, [content] { content->prepareCollapseSnapshot(); });
	};

	session->credits().load();
	session->credits().balanceValue() | rpl::on_next([=] {
		balanceSub->destroy();
		(*history)->requestSubscriptions(
			Data::CreditsStatusSlice::OffsetToken(),
			[=](Data::CreditsStatusSlice slice) {
				history->reset();
				auto peers = QStringList();
				auto credits = uint64(0);
				for (const auto &entry : slice.subscriptions) {
					if (entry.barePeerId) {
						const auto peer = session->data().peer(
							PeerId(entry.barePeerId));
						peers.append(peer->name());
						credits += entry.subscription.credits;
					}
				}
				show(
					peers.join(", "),
					credits,
					session->credits().balance().whole());
			},
			true);
	}, *balanceSub);
}

} // namespace

Spec MakeLowCreditsSubsSpec() {
	return {
		.priority = Priority::LowCreditsSubs,
		.available = Available,
		.activate = Activate,
	};
}

} // namespace Dialogs::TopBarSuggestions
