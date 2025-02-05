/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "payments/payments_reaction_process.h"

#include "api/api_credits.h"
#include "api/api_global_privacy.h"
#include "apiwrap.h"
#include "boxes/send_credits_box.h" // CreditsEmojiSmall.
#include "core/ui_integration.h" // MarkedTextContext.
#include "data/components/credits.h"
#include "data/data_channel.h"
#include "data/data_message_reactions.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "main/session/send_as_peers.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "payments/ui/payments_reaction_box.h"
#include "settings/settings_credits_graphics.h"
#include "ui/effects/reaction_fly_animation.h"
#include "ui/layers/box_content.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/text/text_utilities.h"
#include "ui/dynamic_thumbnails.h"
#include "window/window_session_controller.h"

namespace Payments {
namespace {

constexpr auto kMaxPerReactionFallback = 2'500;
constexpr auto kDefaultPerReaction = 50;

void TryAddingPaidReaction(
		not_null<Main::Session*> session,
		FullMsgId itemId,
		base::weak_ptr<HistoryView::Element> weakView,
		int count,
		std::optional<PeerId> shownPeer,
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
		if (result == Settings::SmallBalanceResult::Success
			|| result == Settings::SmallBalanceResult::Already) {
			if (const auto item = checkItem()) {
				item->addPaidReaction(count, shownPeer);
				if (const auto view = count ? weakView.get() : nullptr) {
					const auto history = view->history();
					history->owner().notifyViewPaidReactionSent(view);
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

bool LookupMyPaidAnonymous(not_null<HistoryItem*> item) {
	for (const auto &entry : item->topPaidReactionsWithLocal()) {
		if (entry.my) {
			return !entry.peer;
		}
	}
	return false;
}

void TryAddingPaidReaction(
		not_null<HistoryItem*> item,
		HistoryView::Element *view,
		int count,
		std::optional<PeerId> shownPeer,
		std::shared_ptr<Ui::Show> show,
		Fn<void(bool)> finished) {
	TryAddingPaidReaction(
		&item->history()->session(),
		item->fullId(),
		view,
		count,
		shownPeer,
		std::move(show),
		std::move(finished));
}

void ShowPaidReactionDetails(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item,
		HistoryView::Element *view,
		HistoryReactionSource source) {
	Expects(item->history()->peer->isBroadcast()
		|| item->isDiscussionPost());

	const auto show = controller->uiShow();
	const auto itemId = item->fullId();
	const auto session = &item->history()->session();
	const auto appConfig = &session->appConfig();

	const auto max = std::max(
		appConfig->get<int>(
			u"stars_paid_reaction_amount_max"_q,
			kMaxPerReactionFallback),
		2);
	const auto chosen = std::clamp(kDefaultPerReaction, 1, max);

	struct State {
		QPointer<Ui::BoxContent> selectBox;
		bool ignoreShownPeerSwitch = false;
		bool sending = false;
	};
	const auto state = std::make_shared<State>();
	session->credits().load(true);

	const auto weakView = base::make_weak(view);
	const auto send = [=](int count, PeerId shownPeer, auto resend) -> void {
		Expects(count >= 0);

		const auto finish = [=](bool success) {
			state->sending = false;
			if (success && count > 0) {
				state->ignoreShownPeerSwitch = true;
				if (const auto strong = state->selectBox.data()) {
					strong->closeBox();
				}
			}
		};
		if (state->sending || (!count && state->ignoreShownPeerSwitch)) {
			return;
		} else if (const auto item = session->data().message(itemId)) {
			state->sending = true;
			TryAddingPaidReaction(
				item,
				weakView.get(),
				count,
				shownPeer,
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
	auto top = std::vector<Ui::PaidReactionTop>();
	const auto add = [&](const Data::MessageReactionsTopPaid &entry) {
		const auto peer = entry.peer;
		const auto name = peer
			? peer->shortName()
			: tr::lng_paid_react_anonymous(tr::now);
		const auto open = [=] {
			controller->showPeerInfo(peer);
		};
		top.push_back({
			.name = name,
			.photo = (peer
				? Ui::MakeUserpicThumbnail(peer)
				: Ui::MakeHiddenAuthorThumbnail()),
			.barePeerId = peer ? uint64(peer->id.value) : 0,
			.count = int(entry.count),
			.click = peer ? open : Fn<void()>(),
			.my = (entry.my == 1),
		});
	};
	const auto linked = item->discussionPostOriginalSender();
	const auto channel = (linked ? linked : item->history()->peer.get());
	const auto channels = session->sendAsPeers().paidReactionList(channel);
	const auto topPaid = item->topPaidReactionsWithLocal();
	top.reserve(topPaid.size() + 2 + channels.size());
	for (const auto &entry : topPaid) {
		add(entry);
	}
	auto myAdded = base::flat_set<uint64>();
	const auto i = ranges::find(top, true, &Ui::PaidReactionTop::my);
	if (i != end(top)) {
		myAdded.emplace(i->barePeerId);
	}
	const auto myCount = uint32((i != end(top)) ? i->count : 0);
	const auto myAdd = [&](PeerData *peer) {
		const auto barePeerId = peer ? uint64(peer->id.value) : 0;
		if (!myAdded.emplace(barePeerId).second) {
			return;
		}
		add(Data::MessageReactionsTopPaid{
			.peer = peer,
			.count = myCount,
			.my = true,
		});
	};
	const auto globalPrivacy = &session->api().globalPrivacy();
	const auto shown = globalPrivacy->paidReactionShownPeerCurrent();
	const auto owner = &session->data();
	const auto shownPeer = shown ? owner->peer(shown).get() : nullptr;
	myAdd(shownPeer);
	myAdd(session->user());
	myAdd(nullptr);
	for (const auto &channel : channels) {
		myAdd(channel);
	}
	ranges::stable_sort(top, ranges::greater(), &Ui::PaidReactionTop::count);

	state->selectBox = show->show(Ui::MakePaidReactionBox({
		.chosen = chosen,
		.max = max,
		.top = std::move(top),
		.channel = channel->name(),
		.submit = std::move(submitText),
		.balanceValue = session->credits().balanceValue(),
		.send = [=](int count, uint64 barePeerId) {
			send(count, PeerId(barePeerId), send);
		},
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
