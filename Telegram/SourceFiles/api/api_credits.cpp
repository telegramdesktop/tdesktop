/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_credits.h"

#include "api/api_credits_history_entry.h"
#include "api/api_premium.h"
#include "api/api_statistics_data_deserialize.h"
#include "api/api_updates.h"
#include "apiwrap.h"
#include "base/unixtime.h"
#include "data/components/credits.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_app_config.h"
#include "main/main_session.h"

namespace Api {
namespace {

constexpr auto kTransactionsLimit = 100;

[[nodiscard]] Data::SubscriptionEntry SubscriptionFromTL(
		const MTPStarsSubscription &tl,
		not_null<PeerData*> peer) {
	return Data::SubscriptionEntry{
		.id = qs(tl.data().vid()),
		.inviteHash = qs(tl.data().vchat_invite_hash().value_or_empty()),
		.title = qs(tl.data().vtitle().value_or_empty()),
		.slug = qs(tl.data().vinvoice_slug().value_or_empty()),
		.until = base::unixtime::parse(tl.data().vuntil_date().v),
		.subscription = Data::PeerSubscription{
			.credits = tl.data().vpricing().data().vamount().v,
			.period = tl.data().vpricing().data().vperiod().v,
		},
		.barePeerId = peerFromMTP(tl.data().vpeer()).value,
		.photoId = (tl.data().vphoto()
			? peer->owner().photoFromWeb(
				*tl.data().vphoto(),
				ImageLocation())->id
			: 0),
		.cancelled = tl.data().is_canceled(),
		.cancelledByBot = tl.data().is_bot_canceled(),
		.expired = (base::unixtime::now() > tl.data().vuntil_date().v),
		.canRefulfill = tl.data().is_can_refulfill(),
	};
}

[[nodiscard]] Data::CreditsStatusSlice StatusFromTL(
		const MTPpayments_StarsStatus &status,
		not_null<PeerData*> peer) {
	const auto &data = status.data();
	peer->owner().processUsers(data.vusers());
	peer->owner().processChats(data.vchats());
	auto entries = std::vector<Data::CreditsHistoryEntry>();
	if (const auto history = data.vhistory()) {
		entries.reserve(history->v.size());
		for (const auto &tl : history->v) {
			entries.push_back(CreditsHistoryEntryFromTL(tl, peer));
		}
	}
	auto subscriptions = std::vector<Data::SubscriptionEntry>();
	if (const auto history = data.vsubscriptions()) {
		subscriptions.reserve(history->v.size());
		for (const auto &tl : history->v) {
			subscriptions.push_back(SubscriptionFromTL(tl, peer));
		}
	}
	return Data::CreditsStatusSlice{
		.list = std::move(entries),
		.subscriptions = std::move(subscriptions),
		.balance = CreditsAmountFromTL(status.data().vbalance()),
		.subscriptionsMissingBalance
			= status.data().vsubscriptions_missing_balance().value_or_empty(),
		.allLoaded = !status.data().vnext_offset().has_value()
			&& !status.data().vsubscriptions_next_offset().has_value(),
		.token = qs(status.data().vnext_offset().value_or_empty()),
		.tokenSubscriptions = qs(
			status.data().vsubscriptions_next_offset().value_or_empty()),
	};
}

} // namespace

CreditsTopupOptions::CreditsTopupOptions(not_null<PeerData*> peer)
: _peer(peer)
, _api(&peer->session().api().instance()) {
}

rpl::producer<rpl::no_value, QString> CreditsTopupOptions::request() {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		const auto giftBarePeerId = !_peer->isSelf() ? _peer->id.value : 0;

		const auto optionsFromTL = [giftBarePeerId](const auto &options) {
			return ranges::views::all(
				options
			) | ranges::views::transform([=](const auto &option) {
				return Data::CreditTopupOption{
					.credits = option.data().vstars().v,
					.product = qs(
						option.data().vstore_product().value_or_empty()),
					.currency = qs(option.data().vcurrency()),
					.amount = option.data().vamount().v,
					.extended = option.data().is_extended(),
					.giftBarePeerId = giftBarePeerId,
				};
			}) | ranges::to_vector;
		};
		const auto fail = [=](const MTP::Error &error) {
			consumer.put_error_copy(error.type());
		};

		if (_peer->isSelf()) {
			using TLOption = MTPStarsTopupOption;
			_api.request(MTPpayments_GetStarsTopupOptions(
			)).done([=](const MTPVector<TLOption> &result) {
				_options = optionsFromTL(result.v);
				consumer.put_done();
			}).fail(fail).send();
		} else if (const auto user = _peer->asUser()) {
			using TLOption = MTPStarsGiftOption;
			_api.request(MTPpayments_GetStarsGiftOptions(
				MTP_flags(MTPpayments_GetStarsGiftOptions::Flag::f_user_id),
				user->inputUser
			)).done([=](const MTPVector<TLOption> &result) {
				_options = optionsFromTL(result.v);
				consumer.put_done();
			}).fail(fail).send();
		}

		return lifetime;
	};
}

Data::CreditTopupOptions CreditsTopupOptions::options() const {
	return _options;
}

CreditsStatus::CreditsStatus(not_null<PeerData*> peer)
: _peer(peer)
, _api(&peer->session().api().instance()) {
}

void CreditsStatus::request(
		const Data::CreditsStatusSlice::OffsetToken &token,
		Fn<void(Data::CreditsStatusSlice)> done) {
	if (_requestId) {
		return;
	}

	using TLResult = MTPpayments_StarsStatus;

	_requestId = _api.request(MTPpayments_GetStarsStatus(
		MTP_flags(0),
		_peer->isSelf() ? MTP_inputPeerSelf() : _peer->input
	)).done([=](const TLResult &result) {
		_requestId = 0;
		const auto &balance = result.data().vbalance();
		_peer->session().credits().apply(
			_peer->id,
			CreditsAmountFromTL(balance));
		if (const auto onstack = done) {
			onstack(StatusFromTL(result, _peer));
		}
	}).fail([=] {
		_requestId = 0;
		if (const auto onstack = done) {
			onstack({});
		}
	}).send();
}

CreditsHistory::CreditsHistory(
	not_null<PeerData*> peer,
	bool in,
	bool out,
	bool currency)
: _peer(peer)
, _flags(((in == out)
	? HistoryTL::Flags(0)
	: HistoryTL::Flags(0)
		| (in ? HistoryTL::Flag::f_inbound : HistoryTL::Flags(0))
		| (out ? HistoryTL::Flag::f_outbound : HistoryTL::Flags(0)))
	| (currency ? HistoryTL::Flag::f_ton : HistoryTL::Flags(0)))
, _api(&peer->session().api().instance()) {
}

void CreditsHistory::request(
		const Data::CreditsStatusSlice::OffsetToken &token,
		Fn<void(Data::CreditsStatusSlice)> done) {
	if (_requestId) {
		return;
	}
	_requestId = _api.request(MTPpayments_GetStarsTransactions(
		MTP_flags(_flags),
		MTPstring(), // subscription_id
		_peer->isSelf() ? MTP_inputPeerSelf() : _peer->input,
		MTP_string(token),
		MTP_int(kTransactionsLimit)
	)).done([=](const MTPpayments_StarsStatus &result) {
		_requestId = 0;
		done(StatusFromTL(result, _peer));
	}).fail([=] {
		_requestId = 0;
		done({});
	}).send();
}

void CreditsHistory::requestSubscriptions(
		const Data::CreditsStatusSlice::OffsetToken &token,
		Fn<void(Data::CreditsStatusSlice)> done,
		bool missingBalance) {
	if (_requestId) {
		return;
	}
	_requestId = _api.request(MTPpayments_GetStarsSubscriptions(
		MTP_flags(missingBalance
			? MTPpayments_getStarsSubscriptions::Flag::f_missing_balance
			: MTPpayments_getStarsSubscriptions::Flags(0)),
		_peer->isSelf() ? MTP_inputPeerSelf() : _peer->input,
		MTP_string(token)
	)).done([=](const MTPpayments_StarsStatus &result) {
		_requestId = 0;
		done(StatusFromTL(result, _peer));
	}).fail([=] {
		_requestId = 0;
		done({});
	}).send();
}

rpl::producer<not_null<PeerData*>> PremiumPeerBot(
		not_null<Main::Session*> session) {
	const auto username = session->appConfig().get<QString>(
		u"premium_bot_username"_q,
		QString());
	if (username.isEmpty()) {
		return rpl::never<not_null<PeerData*>>();
	}
	if (const auto p = session->data().peerByUsername(username)) {
		return rpl::single<not_null<PeerData*>>(p);
	}
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		const auto api = lifetime.make_state<MTP::Sender>(&session->mtp());

		api->request(MTPcontacts_ResolveUsername(
			MTP_flags(0),
			MTP_string(username),
			MTP_string()
		)).done([=](const MTPcontacts_ResolvedPeer &result) {
			session->data().processUsers(result.data().vusers());
			session->data().processChats(result.data().vchats());
			const auto botPeer = session->data().peerLoaded(
				peerFromMTP(result.data().vpeer()));
			if (!botPeer) {
				return consumer.put_done();
			}
			consumer.put_next(not_null{ botPeer });
		}).send();

		return lifetime;
	};
}

CreditsEarnStatistics::CreditsEarnStatistics(not_null<PeerData*> peer)
: StatisticsRequestSender(peer)
, _isUser(peer->isUser()) {
}

rpl::producer<rpl::no_value, QString> CreditsEarnStatistics::request() {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		const auto finish = [=](const QString &url) {
			api().request(MTPpayments_GetStarsRevenueStats(
				MTP_flags(0),
				(_isUser ? user()->input : channel()->input)
			)).done([=](const MTPpayments_StarsRevenueStats &result) {
				const auto &data = result.data();
				const auto &status = data.vstatus().data();
				_data = Data::CreditsEarnStatistics{
					.revenueGraph = StatisticalGraphFromTL(
						data.vrevenue_graph()),
					.currentBalance = CreditsAmountFromTL(
						status.vcurrent_balance()),
					.availableBalance = CreditsAmountFromTL(
						status.vavailable_balance()),
					.overallRevenue = CreditsAmountFromTL(
						status.voverall_revenue()),
					.usdRate = data.vusd_rate().v,
					.isWithdrawalEnabled = status.is_withdrawal_enabled(),
					.nextWithdrawalAt = status.vnext_withdrawal_at()
						? base::unixtime::parse(
							status.vnext_withdrawal_at()->v)
						: QDateTime(),
					.buyAdsUrl = url,
				};

				consumer.put_done();
			}).fail([=](const MTP::Error &error) {
				consumer.put_error_copy(error.type());
			}).send();
		};

		api().request(
			MTPpayments_GetStarsRevenueAdsAccountUrl(
				(_isUser ? user()->input : channel()->input))
		).done([=](const MTPpayments_StarsRevenueAdsAccountUrl &result) {
			finish(qs(result.data().vurl()));
		}).fail([=](const MTP::Error &error) {
			finish({});
		}).send();

		return lifetime;
	};
}

Data::CreditsEarnStatistics CreditsEarnStatistics::data() const {
	return _data;
}

CreditsGiveawayOptions::CreditsGiveawayOptions(not_null<PeerData*> peer)
: _peer(peer)
, _api(&peer->session().api().instance()) {
}

rpl::producer<rpl::no_value, QString> CreditsGiveawayOptions::request() {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		using TLOption = MTPStarsGiveawayOption;

		const auto optionsFromTL = [=](const auto &options) {
			return ranges::views::all(
				options
			) | ranges::views::transform([=](const auto &option) {
				return Data::CreditsGiveawayOption{
					.winners = ranges::views::all(
						option.data().vwinners().v
					) | ranges::views::transform([](const auto &winner) {
						return Data::CreditsGiveawayOption::Winner{
							.users = winner.data().vusers().v,
							.perUserStars = winner.data().vper_user_stars().v,
							.isDefault = winner.data().is_default(),
						};
					}) | ranges::to_vector,
					.storeProduct = qs(
						option.data().vstore_product().value_or_empty()),
					.currency = qs(option.data().vcurrency()),
					.amount = option.data().vamount().v,
					.credits = option.data().vstars().v,
					.yearlyBoosts = option.data().vyearly_boosts().v,
					.isExtended = option.data().is_extended(),
					.isDefault = option.data().is_default(),
				};
			}) | ranges::to_vector;
		};
		const auto fail = [=](const MTP::Error &error) {
			consumer.put_error_copy(error.type());
		};

		_api.request(MTPpayments_GetStarsGiveawayOptions(
		)).done([=](const MTPVector<TLOption> &result) {
			_options = optionsFromTL(result.v);
			consumer.put_done();
		}).fail(fail).send();

		return lifetime;
	};
}

Data::CreditsGiveawayOptions CreditsGiveawayOptions::options() const {
	return _options;
}

void EditCreditsSubscription(
		not_null<Main::Session*> session,
		const QString &id,
		bool cancel,
		Fn<void()> done,
		Fn<void(QString)> fail) {
	using Flag = MTPpayments_ChangeStarsSubscription::Flag;
	session->api().request(
		MTPpayments_ChangeStarsSubscription(
			MTP_flags(Flag::f_canceled),
			MTP_inputPeerSelf(),
			MTP_string(id),
			MTP_bool(cancel)
	)).done(done).fail([=](const MTP::Error &e) { fail(e.type()); }).send();
}

MTPInputSavedStarGift InputSavedStarGiftId(
		const Data::SavedStarGiftId &id,
		const std::shared_ptr<Data::UniqueGift> &unique) {
	return (!id && unique)
		? MTP_inputSavedStarGiftSlug(MTP_string(unique->slug))
		: id.isUser()
		? MTP_inputSavedStarGiftUser(MTP_int(id.userMessageId().bare))
		: MTP_inputSavedStarGiftChat(
			id.chat()->input,
			MTP_long(id.chatSavedId()));
}

} // namespace Api
