/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "payments/payments_reaction_process.h"

#include "api/api_credits.h"
#include "boxes/send_credits_box.h" // CreditsEmojiSmall.
#include "core/ui_integration.h" // MarkedTextContext.
#include "data/components/credits.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "payments/ui/payments_reaction_box.h"
#include "settings/settings_credits_graphics.h"
#include "ui/effects/reaction_fly_animation.h"
#include "ui/layers/box_content.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/text/text_utilities.h"

namespace Payments {
namespace {

constexpr auto kMaxPerReactionFallback = 2'500;
constexpr auto kDefaultPerReaction = 20;

void TryAddingPaidReaction(
		not_null<Main::Session*> session,
		FullMsgId itemId,
		base::weak_ptr<HistoryView::Element> weakView,
		int count,
		std::shared_ptr<Ui::Show> show,
		Fn<void(bool)> finished) {
	const auto checkItem = [=] {
		const auto item = session->data().message(itemId);
		if (!item) {
			if (const auto onstack = finished) {
				onstack(false);
			}
		}
		return item;
	};

	const auto item = checkItem();
	if (!item) {
		return;
	}
	const auto done = [=](Settings::SmallBalanceResult result) {
		if (result == Settings::SmallBalanceResult::Success) {
			if (const auto item = checkItem()) {
				item->addPaidReaction(count);
				if (const auto view = weakView.get()) {
					view->animateReaction({
						.id = Data::ReactionId::Paid(),
					});
				}
				if (const auto onstack = finished) {
					onstack(true);
				}
			}
		} else if (const auto onstack = finished) {
			onstack(false);
		}
	};
	const auto channelId = peerToChannel(itemId.peer);
	Settings::MaybeRequestBalanceIncrease(
		Main::MakeSessionShow(show, session),
		count,
		Settings::SmallBalanceReaction{ .channelId = channelId },
		done);
}

} // namespace

void TryAddingPaidReaction(
		not_null<HistoryItem*> item,
		HistoryView::Element *view,
		int count,
		std::shared_ptr<Ui::Show> show,
		Fn<void(bool)> finished) {
	TryAddingPaidReaction(
		&item->history()->session(),
		item->fullId(),
		view,
		count,
		std::move(show),
		std::move(finished));
}

void ShowPaidReactionDetails(
		std::shared_ptr<Ui::Show> show,
		not_null<HistoryItem*> item,
		HistoryView::Element *view,
		HistoryReactionSource source) {
	Expects(item->history()->peer->isBroadcast());

	const auto itemId = item->fullId();
	const auto session = &item->history()->session();
	const auto appConfig = &session->appConfig();

	const auto min = 1;
	const auto max = std::max(
		appConfig->get<int>(
			u"stars_paid_reaction_amount_max"_q,
			kMaxPerReactionFallback),
		min);
	const auto chosen = std::clamp(kDefaultPerReaction, min, max);

	struct State {
		QPointer<Ui::BoxContent> selectBox;
		bool sending = false;
	};
	const auto state = std::make_shared<State>();
	session->credits().load(true);

	const auto weakView = base::make_weak(view);
	const auto send = [=](int count, auto resend) -> void {
		Expects(count > 0);

		const auto finish = [=](bool success) {
			state->sending = false;
			if (success) {
				if (const auto strong = state->selectBox.data()) {
					strong->closeBox();
				}
			}
		};
		if (state->sending) {
			return;
		} else if (const auto item = session->data().message(itemId)) {
			state->sending = true;
			TryAddingPaidReaction(
				item,
				weakView.get(),
				count,
				show,
				finish);
		}
	};

	auto submitText = [=](rpl::producer<int> amount) {
		auto nice = std::move(amount) | rpl::map([=](int count) {
			return Ui::CreditsEmojiSmall(session).append(
				Lang::FormatCountDecimal(count));
		});
		return tr::lng_paid_react_send(
			lt_price,
			std::move(nice),
			Ui::Text::RichLangValue
		) | rpl::map([=](TextWithEntities &&text) {
			return Ui::TextWithContext{
				.text = std::move(text),
				.context = Core::MarkedTextContext{
					.session = session,
					.customEmojiRepaint = [] {},
				},
			};
		});
	};
	state->selectBox = show->show(Ui::MakePaidReactionBox({
		.min = min,
		.max = max,
		.chosen = chosen,
		.channel = item->history()->peer->name(),
		.submit = std::move(submitText),
		.balanceValue = session->credits().balanceValue(),
		.send = [=](int count) { send(count, send); },
	}));

	if (const auto strong = state->selectBox.data()) {
		session->data().itemRemoved(
		) | rpl::start_with_next([=](not_null<const HistoryItem*> removed) {
			if (removed == item) {
				strong->closeBox();
			}
		}, strong->lifetime());
	}
}

} // namespace Payments
