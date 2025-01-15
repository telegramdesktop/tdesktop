/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_credits.h"

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

[[nodiscard]] Data::CreditsHistoryEntry HistoryFromTL(
		const MTPStarsTransaction &tl,
		not_null<PeerData*> peer) {
	using HistoryPeerTL = MTPDstarsTransactionPeer;
	using namespace Data;
	const auto owner = &peer->owner();
	const auto photo = tl.data().vphoto()
		? owner->photoFromWeb(*tl.data().vphoto(), ImageLocation())
		: nullptr;
	auto extended = std::vector<CreditsHistoryMedia>();
	if (const auto list = tl.data().vextended_media()) {
		extended.reserve(list->v.size());
		for (const auto &media : list->v) {
			media.match([&](const MTPDmessageMediaPhoto &data) {
				if (const auto inner = data.vphoto()) {
					const auto photo = owner->processPhoto(*inner);
					if (!photo->isNull()) {
						extended.push_back(CreditsHistoryMedia{
							.type = CreditsHistoryMediaType::Photo,
							.id = photo->id,
						});
					}
				}
			}, [&](const MTPDmessageMediaDocument &data) {
				if (const auto inner = data.vdocument()) {
					const auto document = owner->processDocument(
						*inner,
						data.valt_documents());
					if (document->isAnimation()
						|| document->isVideoFile()
						|| document->isGifv()) {
						extended.push_back(CreditsHistoryMedia{
							.type = CreditsHistoryMediaType::Video,
							.id = document->id,
						});
					}
				}
			}, [&](const auto &) {});
		}
	}
	const auto barePeerId = tl.data().vpeer().match([](
			const HistoryPeerTL &p) {
		return peerFromMTP(p.vpeer());
	}, [](const auto &) {
		return PeerId(0);
	}).value;
	const auto stargift = tl.data().vstargift();
	const auto nonUniqueGift = stargift
		? stargift->match([&](const MTPDstarGift &data) {
			return &data;
		}, [](const auto &) { return (const MTPDstarGift*)nullptr; })
		: nullptr;
	const auto reaction = tl.data().is_reaction();
	const auto amount = Data::FromTL(tl.data().vstars());
	const auto starrefAmount = tl.data().vstarref_amount()
		? Data::FromTL(*tl.data().vstarref_amount())
		: StarsAmount();
	const auto starrefCommission
		= tl.data().vstarref_commission_permille().value_or_empty();
	const auto starrefBarePeerId = tl.data().vstarref_peer()
		? peerFromMTP(*tl.data().vstarref_peer()).value
		: 0;
	const auto incoming = (amount >= StarsAmount());
	const auto saveActorId = (reaction || !extended.empty()) && incoming;
	const auto parsedGift = stargift
		? FromTL(&peer->session(), *stargift)
		: std::optional<Data::StarGift>();
	const auto giftStickerId = parsedGift ? parsedGift->document->id : 0;
	return Data::CreditsHistoryEntry{
		.id = qs(tl.data().vid()),
		.title = qs(tl.data().vtitle().value_or_empty()),
		.description = { qs(tl.data().vdescription().value_or_empty()) },
		.date = base::unixtime::parse(tl.data().vdate().v),
		.photoId = photo ? photo->id : 0,
		.extended = std::move(extended),
		.credits = Data::FromTL(tl.data().vstars()),
		.bareMsgId = uint64(tl.data().vmsg_id().value_or_empty()),
		.barePeerId = saveActorId ? peer->id.value : barePeerId,
		.bareGiveawayMsgId = uint64(
			tl.data().vgiveaway_post_id().value_or_empty()),
		.bareGiftStickerId = giftStickerId,
		.bareActorId = saveActorId ? barePeerId : uint64(0),
		.uniqueGift = parsedGift ? parsedGift->unique : nullptr,
		.starrefAmount = starrefAmount,
		.starrefCommission = starrefCommission,
		.starrefRecipientId = starrefBarePeerId,
		.peerType = tl.data().vpeer().match([](const HistoryPeerTL &) {
			return Data::CreditsHistoryEntry::PeerType::Peer;
		}, [](const MTPDstarsTransactionPeerPlayMarket &) {
			return Data::CreditsHistoryEntry::PeerType::PlayMarket;
		}, [](const MTPDstarsTransactionPeerFragment &) {
			return Data::CreditsHistoryEntry::PeerType::Fragment;
		}, [](const MTPDstarsTransactionPeerAppStore &) {
			return Data::CreditsHistoryEntry::PeerType::AppStore;
		}, [](const MTPDstarsTransactionPeerUnsupported &) {
			return Data::CreditsHistoryEntry::PeerType::Unsupported;
		}, [](const MTPDstarsTransactionPeerPremiumBot &) {
			return Data::CreditsHistoryEntry::PeerType::PremiumBot;
		}, [](const MTPDstarsTransactionPeerAds &) {
			return Data::CreditsHistoryEntry::PeerType::Ads;
		}, [](const MTPDstarsTransactionPeerAPI &) {
			return Data::CreditsHistoryEntry::PeerType::API;
		}),
		.subscriptionUntil = tl.data().vsubscription_period()
			? base::unixtime::parse(base::unixtime::now()
				+ tl.data().vsubscription_period()->v)
			: QDateTime(),
		.successDate = tl.data().vtransaction_date()
			? base::unixtime::parse(tl.data().vtransaction_date()->v)
			: QDateTime(),
		.successLink = qs(tl.data().vtransaction_url().value_or_empty()),
		.starsConverted = int(nonUniqueGift
			? nonUniqueGift->vconvert_stars().v
			: 0),
		.floodSkip = int(tl.data().vfloodskip_number().value_or(0)),
		.converted = stargift && incoming,
		.stargift = stargift.has_value(),
		.giftUpgraded = tl.data().is_stargift_upgrade(),
		.reaction = tl.data().is_reaction(),
		.refunded = tl.data().is_refund(),
		.pending = tl.data().is_pending(),
		.failed = tl.data().is_failed(),
		.in = incoming,
		.gift = tl.data().is_gift() || stargift.has_value(),
	};
}

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
			entries.push_back(HistoryFromTL(tl, peer));
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
		.balance = Data::FromTL(status.data().vbalance()),
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
		_peer->isSelf() ? MTP_inputPeerSelf() : _peer->input
	)).done([=](const TLResult &result) {
		_requestId = 0;
		const auto &balance = result.data().vbalance();
		_peer->session().credits().apply(_peer->id, Data::FromTL(balance));
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

CreditsHistory::CreditsHistory(not_null<PeerData*> peer, bool in, bool out)
: _peer(peer)
, _flags((in == out)
	? HistoryTL::Flags(0)
	: HistoryTL::Flags(0)
		| (in ? HistoryTL::Flag::f_inbound : HistoryTL::Flags(0))
		| (out ? HistoryTL::Flag::f_outbound : HistoryTL::Flags(0)))
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
		Fn<void(Data::CreditsStatusSlice)> done) {
	if (_requestId) {
		return;
	}
	_requestId = _api.request(MTPpayments_GetStarsSubscriptions(
		MTP_flags(0),
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
			makeRequest(MTPpayments_GetStarsRevenueStats(
				MTP_flags(0),
				(_isUser ? user()->input : channel()->input)
			)).done([=](const MTPpayments_StarsRevenueStats &result) {
				const auto &data = result.data();
				const auto &status = data.vstatus().data();
				using Data::FromTL;
				_data = Data::CreditsEarnStatistics{
					.revenueGraph = StatisticalGraphFromTL(
						data.vrevenue_graph()),
					.currentBalance = FromTL(status.vcurrent_balance()),
					.availableBalance = FromTL(status.vavailable_balance()),
					.overallRevenue = FromTL(status.voverall_revenue()),
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

		makeRequest(
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

MTPInputSavedStarGift InputSavedStarGiftId(const Data::SavedStarGiftId &id) {
	return id.isUser()
		? MTP_inputSavedStarGiftUser(MTP_int(id.userMessageId().bare))
		: MTP_inputSavedStarGiftChat(
			id.chat()->input,
			MTP_long(id.chatSavedId()));
}

} // namespace Api
