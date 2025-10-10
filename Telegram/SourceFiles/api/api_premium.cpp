/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_premium.h"

#include "api/api_premium_option.h"
#include "api/api_text_entities.h"
#include "apiwrap.h"
#include "base/random.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_peer.h"
#include "data/data_peer_values.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "payments/payments_form.h"
#include "ui/chat/chat_style.h" // ColorCollectible
#include "ui/text/format_values.h"

namespace Api {
namespace {

[[nodiscard]] GiftCode Parse(const MTPDpayments_checkedGiftCode &data) {
	return {
		.from = data.vfrom_id() ? peerFromMTP(*data.vfrom_id()) : PeerId(),
		.to = data.vto_id() ? peerFromUser(*data.vto_id()) : PeerId(),
		.giveawayId = data.vgiveaway_msg_id().value_or_empty(),
		.date = data.vdate().v,
		.used = data.vused_date().value_or_empty(),
		.months = data.vmonths().v,
		.giveaway = data.is_via_giveaway(),
	};
}

[[nodiscard]] Data::PremiumSubscriptionOptions GiftCodesFromTL(
		const QVector<MTPPremiumGiftCodeOption> &tlOptions) {
	auto options = PremiumSubscriptionOptionsFromTL(tlOptions);
	for (auto i = 0; i < options.size(); i++) {
		const auto &tlOption = tlOptions[i].data();
		const auto currency = qs(tlOption.vcurrency());
		const auto perUserText = Ui::FillAmountAndCurrency(
			tlOption.vamount().v / float64(tlOption.vusers().v),
			currency,
			false);
		options[i].costPerMonth = perUserText
			+ ' '
			+ QChar(0x00D7)
			+ ' '
			+ QString::number(tlOption.vusers().v);
		options[i].currency = currency;
	}
	return options;
}

[[nodiscard]] int FindStarsForResale(const MTPVector<MTPStarsAmount> *list) {
	if (!list) {
		return 0;
	}
	for (const auto &amount : list->v) {
		if (amount.type() == mtpc_starsAmount) {
			return int(amount.c_starsAmount().vamount().v);
		}
	}
	return 0;
}

[[nodiscard]] int64 FindTonForResale(const MTPVector<MTPStarsAmount> *list) {
	if (!list) {
		return 0;
	}
	for (const auto &amount : list->v) {
		if (amount.type() == mtpc_starsTonAmount) {
			return int64(amount.c_starsTonAmount().vamount().v);
		}
	}
	return 0;
}

} // namespace

Premium::Premium(not_null<ApiWrap*> api)
: _session(&api->session())
, _api(&api->instance()) {
	crl::on_main(_session, [=] {
		// You can't use _session->user() in the constructor,
		// only queued, because it is not constructed yet.
		Data::AmPremiumValue(
			_session
		) | rpl::start_with_next([=] {
			reload();
			if (_session->premium()) {
				reloadCloudSet();
			}
		}, _session->lifetime());
	});
}

rpl::producer<TextWithEntities> Premium::statusTextValue() const {
	return _statusTextUpdates.events_starting_with_copy(
		_statusText.value_or(TextWithEntities()));
}

auto Premium::videos() const
-> const base::flat_map<QString, not_null<DocumentData*>> & {
	return _videos;
}

rpl::producer<> Premium::videosUpdated() const {
	return _videosUpdated.events();
}

auto Premium::stickers() const
-> const std::vector<not_null<DocumentData*>> & {
	return _stickers;
}

rpl::producer<> Premium::stickersUpdated() const {
	return _stickersUpdated.events();
}

auto Premium::cloudSet() const
-> const std::vector<not_null<DocumentData*>> & {
	return _cloudSet;
}

rpl::producer<> Premium::cloudSetUpdated() const {
	return _cloudSetUpdated.events();
}

auto Premium::helloStickers() const
-> const std::vector<not_null<DocumentData*>> & {
	if (_helloStickers.empty()) {
		const_cast<Premium*>(this)->reloadHelloStickers();
	}
	return _helloStickers;
}

rpl::producer<> Premium::helloStickersUpdated() const {
	return _helloStickersUpdated.events();
}

int64 Premium::monthlyAmount() const {
	return _monthlyAmount;
}

QString Premium::monthlyCurrency() const {
	return _monthlyCurrency;
}

void Premium::reload() {
	reloadPromo();
	reloadStickers();
}

void Premium::reloadPromo() {
	if (_promoRequestId) {
		return;
	}
	_promoRequestId = _api.request(MTPhelp_GetPremiumPromo(
	)).done([=](const MTPhelp_PremiumPromo &result) {
		_promoRequestId = 0;
		const auto &data = result.data();
		_session->data().processUsers(data.vusers());

		_subscriptionOptions = PremiumSubscriptionOptionsFromTL(
			data.vperiod_options().v);
		for (const auto &option : data.vperiod_options().v) {
			if (option.data().vmonths().v == 1) {
				_monthlyAmount = option.data().vamount().v;
				_monthlyCurrency = qs(option.data().vcurrency());
			}
		}
		auto text = TextWithEntities{
			qs(data.vstatus_text()),
			EntitiesFromMTP(_session, data.vstatus_entities().v),
		};
		_statusText = text;
		_statusTextUpdates.fire(std::move(text));
		auto videos = base::flat_map<QString, not_null<DocumentData*>>();
		const auto count = int(std::min(
			data.vvideo_sections().v.size(),
			data.vvideos().v.size()));
		videos.reserve(count);
		for (auto i = 0; i != count; ++i) {
			const auto document = _session->data().processDocument(
				data.vvideos().v[i]);
			if ((!document->isVideoFile() && !document->isGifv())
				|| !document->supportsStreaming()) {
				document->forceIsStreamedAnimation();
			}
			videos.emplace(
				qs(data.vvideo_sections().v[i]),
				document);
		}
		if (_videos != videos) {
			_videos = std::move(videos);
			_videosUpdated.fire({});
		}
	}).fail([=] {
		_promoRequestId = 0;
	}).send();
}

void Premium::reloadStickers() {
	if (_stickersRequestId) {
		return;
	}
	_stickersRequestId = _api.request(MTPmessages_GetStickers(
		MTP_string("\xe2\xad\x90\xef\xb8\x8f\xe2\xad\x90\xef\xb8\x8f"),
		MTP_long(_stickersHash)
	)).done([=](const MTPmessages_Stickers &result) {
		_stickersRequestId = 0;
		result.match([&](const MTPDmessages_stickersNotModified &) {
		}, [&](const MTPDmessages_stickers &data) {
			_stickersHash = data.vhash().v;
			const auto owner = &_session->data();
			_stickers.clear();
			for (const auto &sticker : data.vstickers().v) {
				const auto document = owner->processDocument(sticker);
				if (document->isPremiumSticker()) {
					_stickers.push_back(document);
				}
			}
			_stickersUpdated.fire({});
		});
	}).fail([=] {
		_stickersRequestId = 0;
	}).send();
}

void Premium::reloadCloudSet() {
	if (_cloudSetRequestId) {
		return;
	}
	_cloudSetRequestId = _api.request(MTPmessages_GetStickers(
		MTP_string("\xf0\x9f\x93\x82\xe2\xad\x90\xef\xb8\x8f"),
		MTP_long(_cloudSetHash)
	)).done([=](const MTPmessages_Stickers &result) {
		_cloudSetRequestId = 0;
		result.match([&](const MTPDmessages_stickersNotModified &) {
		}, [&](const MTPDmessages_stickers &data) {
			_cloudSetHash = data.vhash().v;
			const auto owner = &_session->data();
			_cloudSet.clear();
			for (const auto &sticker : data.vstickers().v) {
				const auto document = owner->processDocument(sticker);
				if (document->isPremiumSticker()) {
					_cloudSet.push_back(document);
				}
			}
			_cloudSetUpdated.fire({});
		});
	}).fail([=] {
		_cloudSetRequestId = 0;
	}).send();
}

void Premium::reloadHelloStickers() {
	if (_helloStickersRequestId) {
		return;
	}
	_helloStickersRequestId = _api.request(MTPmessages_GetStickers(
		MTP_string("\xf0\x9f\x91\x8b\xe2\xad\x90\xef\xb8\x8f"),
		MTP_long(_helloStickersHash)
	)).done([=](const MTPmessages_Stickers &result) {
		_helloStickersRequestId = 0;
		result.match([&](const MTPDmessages_stickersNotModified &) {
		}, [&](const MTPDmessages_stickers &data) {
			_helloStickersHash = data.vhash().v;
			const auto owner = &_session->data();
			_helloStickers.clear();
			for (const auto &sticker : data.vstickers().v) {
				const auto document = owner->processDocument(sticker);
				if (document->sticker()) {
					_helloStickers.push_back(document);
				}
			}
			_helloStickersUpdated.fire({});
		});
	}).fail([=] {
		_helloStickersRequestId = 0;
	}).send();
}

void Premium::checkGiftCode(
		const QString &slug,
		Fn<void(GiftCode)> done) {
	if (_giftCodeRequestId) {
		if (_giftCodeSlug == slug) {
			return;
		}
		_api.request(_giftCodeRequestId).cancel();
	}
	_giftCodeSlug = slug;
	_giftCodeRequestId = _api.request(MTPpayments_CheckGiftCode(
		MTP_string(slug)
	)).done([=](const MTPpayments_CheckedGiftCode &result) {
		_giftCodeRequestId = 0;

		const auto &data = result.data();
		_session->data().processUsers(data.vusers());
		_session->data().processChats(data.vchats());
		done(updateGiftCode(slug, Parse(data)));
	}).fail([=](const MTP::Error &error) {
		_giftCodeRequestId = 0;

		done(updateGiftCode(slug, {}));
	}).send();
}

GiftCode Premium::updateGiftCode(
		const QString &slug,
		const GiftCode &code) {
	auto &now = _giftCodes[slug];
	if (now != code) {
		now = code;
		_giftCodeUpdated.fire_copy(slug);
	}
	return code;
}

rpl::producer<GiftCode> Premium::giftCodeValue(const QString &slug) const {
	return _giftCodeUpdated.events_starting_with_copy(
		slug
	) | rpl::filter(rpl::mappers::_1 == slug) | rpl::map([=] {
		const auto i = _giftCodes.find(slug);
		return (i != end(_giftCodes)) ? i->second : GiftCode();
	});
}

void Premium::applyGiftCode(const QString &slug, Fn<void(QString)> done) {
	_api.request(MTPpayments_ApplyGiftCode(
		MTP_string(slug)
	)).done([=](const MTPUpdates &result) {
		_session->api().applyUpdates(result);
		done({});
	}).fail([=](const MTP::Error &error) {
		done(error.type());
	}).send();
}

void Premium::resolveGiveawayInfo(
		not_null<PeerData*> peer,
		MsgId messageId,
		Fn<void(GiveawayInfo)> done) {
	Expects(done != nullptr);

	_giveawayInfoDone = std::move(done);
	if (_giveawayInfoRequestId) {
		if (_giveawayInfoPeer == peer
			&& _giveawayInfoMessageId == messageId) {
			return;
		}
		_api.request(_giveawayInfoRequestId).cancel();
	}
	_giveawayInfoPeer = peer;
	_giveawayInfoMessageId = messageId;
	_giveawayInfoRequestId = _api.request(MTPpayments_GetGiveawayInfo(
		_giveawayInfoPeer->input,
		MTP_int(_giveawayInfoMessageId.bare)
	)).done([=](const MTPpayments_GiveawayInfo &result) {
		_giveawayInfoRequestId = 0;

		auto info = GiveawayInfo();
		result.match([&](const MTPDpayments_giveawayInfo &data) {
			info.participating = data.is_participating();
			info.state = data.is_preparing_results()
				? GiveawayState::Preparing
				: GiveawayState::Running;
			info.adminChannelId = data.vadmin_disallowed_chat_id()
				? ChannelId(*data.vadmin_disallowed_chat_id())
				: ChannelId();
			info.disallowedCountry = qs(
				data.vdisallowed_country().value_or_empty());
			info.tooEarlyDate
				= data.vjoined_too_early_date().value_or_empty();
			info.startDate = data.vstart_date().v;
		}, [&](const MTPDpayments_giveawayInfoResults &data) {
			info.state = data.is_refunded()
				? GiveawayState::Refunded
				: GiveawayState::Finished;
			info.giftCode = qs(data.vgift_code_slug().value_or_empty());
			info.activatedCount = data.vactivated_count().value_or_empty();
			info.finishDate = data.vfinish_date().v;
			info.startDate = data.vstart_date().v;
			info.credits = data.vstars_prize().value_or_empty();
		});
		_giveawayInfoDone(std::move(info));
	}).fail([=] {
		_giveawayInfoRequestId = 0;
		_giveawayInfoDone({});
	}).send();
}

const Data::PremiumSubscriptionOptions &Premium::subscriptionOptions() const {
	return _subscriptionOptions;
}

rpl::producer<> Premium::someMessageMoneyRestrictionsResolved() const {
	return _someMessageMoneyRestrictionsResolved.events();
}

void Premium::resolveMessageMoneyRestrictions(not_null<UserData*> user) {
	_resolveMessageMoneyRequiredUsers.emplace(user);
	if (!_messageMoneyRequestScheduled
		&& _resolveMessageMoneyRequestedUsers.empty()) {
		_messageMoneyRequestScheduled = true;
		crl::on_main(_session, [=] {
			requestPremiumRequiredSlice();
		});
	}
}

void Premium::requestPremiumRequiredSlice() {
	_messageMoneyRequestScheduled = false;
	if (!_resolveMessageMoneyRequestedUsers.empty()
		|| _resolveMessageMoneyRequiredUsers.empty()) {
		return;
	}
	constexpr auto kPerRequest = 100;
	auto users = MTP_vector_from_range(_resolveMessageMoneyRequiredUsers
		| ranges::views::transform(&UserData::inputUser));
	if (users.v.size() > kPerRequest) {
		auto shortened = users.v;
		shortened.resize(kPerRequest);
		users = MTP_vector<MTPInputUser>(std::move(shortened));
		const auto from = begin(_resolveMessageMoneyRequiredUsers);
		_resolveMessageMoneyRequestedUsers = { from, from + kPerRequest };
		_resolveMessageMoneyRequiredUsers.erase(from, from + kPerRequest);
	} else {
		_resolveMessageMoneyRequestedUsers
			= base::take(_resolveMessageMoneyRequiredUsers);
	}
	const auto finish = [=](const QVector<MTPRequirementToContact> &list) {

		auto index = 0;
		for (const auto &user : base::take(_resolveMessageMoneyRequestedUsers)) {
			const auto set = [&](bool requirePremium, int stars) {
				using Flag = UserDataFlag;
				constexpr auto me = Flag::RequiresPremiumToWrite;
				constexpr auto known = Flag::MessageMoneyRestrictionsKnown;
				constexpr auto hasPrem = Flag::HasRequirePremiumToWrite;
				constexpr auto hasStars = Flag::HasStarsPerMessage;
				user->setStarsPerMessage(stars);
				user->setFlags((user->flags() & ~me)
					| known
					| (requirePremium ? (me | hasPrem) : Flag())
					| (stars ? hasStars : Flag()));
			};
			if (index >= list.size()) {
				set(false, 0);
				continue;
			}
			list[index++].match([&](const MTPDrequirementToContactEmpty &) {
				set(false, 0);
			}, [&](const MTPDrequirementToContactPremium &) {
				set(true, 0);
			}, [&](const MTPDrequirementToContactPaidMessages &data) {
				set(false, data.vstars_amount().v);
			});
		}
		if (!_messageMoneyRequestScheduled
			&& !_resolveMessageMoneyRequiredUsers.empty()) {
			_messageMoneyRequestScheduled = true;
			crl::on_main(_session, [=] {
				requestPremiumRequiredSlice();
			});
		}
		_someMessageMoneyRestrictionsResolved.fire({});
	};
	_session->api().request(
		MTPusers_GetRequirementsToContact(std::move(users))
	).done([=](const MTPVector<MTPRequirementToContact> &result) {
		finish(result.v);
	}).fail([=] {
		finish({});
	}).send();
}

PremiumGiftCodeOptions::PremiumGiftCodeOptions(not_null<PeerData*> peer)
: _peer(peer)
, _api(&peer->session().api().instance()) {
}

rpl::producer<rpl::no_value, QString> PremiumGiftCodeOptions::request() {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		using TLOption = MTPPremiumGiftCodeOption;
		_api.request(MTPpayments_GetPremiumGiftCodeOptions(
			MTP_flags(_peer->isChannel()
				? MTPpayments_GetPremiumGiftCodeOptions::Flag::f_boost_peer
				: MTPpayments_GetPremiumGiftCodeOptions::Flag(0)),
			_peer->input
		)).done([=](const MTPVector<TLOption> &result) {
			auto tlMapOptions = base::flat_map<Amount, QVector<TLOption>>();
			for (const auto &tlOption : result.v) {
				const auto &data = tlOption.data();
				tlMapOptions[data.vusers().v].push_back(tlOption);
				if (qs(data.vcurrency()) == Ui::kCreditsCurrency) {
					continue;
				}

				const auto token = Token{ data.vusers().v, data.vmonths().v };
				_stores[token] = Store{
					.amount = data.vamount().v,
					.currency = qs(data.vcurrency()),
					.product = qs(data.vstore_product().value_or_empty()),
					.quantity = data.vstore_quantity().value_or_empty(),
				};
				if (!ranges::contains(_availablePresets, data.vusers().v)) {
					_availablePresets.push_back(data.vusers().v);
				}
			}
			for (const auto &[amount, tlOptions] : tlMapOptions) {
				if (amount == 1 && _optionsForOnePerson.currencies.empty()) {
					for (const auto &option : tlOptions) {
						_optionsForOnePerson.months.push_back(
							option.data().vmonths().v);
						_optionsForOnePerson.totalCosts.push_back(
							option.data().vamount().v);
						_optionsForOnePerson.currencies.push_back(
							qs(option.data().vcurrency()));
					}
				}
				_subscriptionOptions[amount] = GiftCodesFromTL(tlOptions);
			}
			consumer.put_done();
		}).fail([=](const MTP::Error &error) {
			consumer.put_error_copy(error.type());
		}).send();

		return lifetime;
	};
}

rpl::producer<rpl::no_value, QString> PremiumGiftCodeOptions::applyPrepaid(
		const Payments::InvoicePremiumGiftCode &invoice,
		uint64 prepaidId) {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();
		const auto channel = _peer->asChannel();
		if (!channel) {
			return lifetime;
		}

		_api.request(MTPpayments_LaunchPrepaidGiveaway(
			_peer->input,
			MTP_long(prepaidId),
			invoice.giveawayCredits
				? Payments::InvoiceCreditsGiveawayToTL(invoice)
				: Payments::InvoicePremiumGiftCodeGiveawayToTL(invoice)
		)).done([=](const MTPUpdates &result) {
			_peer->session().api().applyUpdates(result);
			consumer.put_done();
		}).fail([=](const MTP::Error &error) {
			consumer.put_error_copy(error.type());
		}).send();

		return lifetime;
	};
}

const std::vector<int> &PremiumGiftCodeOptions::availablePresets() const {
	return _availablePresets;
}

[[nodiscard]] int PremiumGiftCodeOptions::monthsFromPreset(int monthsIndex) {
	Expects(monthsIndex >= 0 && monthsIndex < _availablePresets.size());

	return _optionsForOnePerson.months[monthsIndex];
}

Payments::InvoicePremiumGiftCode PremiumGiftCodeOptions::invoice(
		int users,
		int months) {
	const auto randomId = base::RandomValue<uint64>();
	const auto token = Token{ users, months };
	const auto &store = _stores[token];
	return Payments::InvoicePremiumGiftCode{
		.currency = store.currency,
		.storeProduct = store.product,
		.randomId = randomId,
		.amount = store.amount,
		.storeQuantity = store.quantity,
		.users = token.users,
		.months = token.months,
	};
}

std::vector<GiftOptionData> PremiumGiftCodeOptions::optionsForPeer() const {
	auto result = std::vector<GiftOptionData>();

	if (!_optionsForOnePerson.currencies.empty()) {
		const auto count = int(_optionsForOnePerson.months.size());
		result.reserve(count);
		for (auto i = 0; i != count; ++i) {
			Assert(i < _optionsForOnePerson.totalCosts.size());
			Assert(i < _optionsForOnePerson.currencies.size());
			result.push_back({
				.cost = _optionsForOnePerson.totalCosts[i],
				.currency = _optionsForOnePerson.currencies[i],
				.months = _optionsForOnePerson.months[i],
			});
		}
	}
	return result;
}

Data::PremiumSubscriptionOptions PremiumGiftCodeOptions::optionsForGiveaway(
		int usersCount) {
	const auto skipForStars = [&](Data::PremiumSubscriptionOptions options) {
		const auto proj = &Data::PremiumSubscriptionOption::currency;
		options.erase(
			ranges::remove(options, Ui::kCreditsCurrency, proj),
			end(options));
		return options;
	};
	const auto it = _subscriptionOptions.find(usersCount);
	if (it != end(_subscriptionOptions)) {
		return skipForStars(it->second);
	} else {
		auto tlOptions = QVector<MTPPremiumGiftCodeOption>();
		for (auto i = 0; i < _optionsForOnePerson.months.size(); i++) {
			tlOptions.push_back(MTP_premiumGiftCodeOption(
				MTP_flags(MTPDpremiumGiftCodeOption::Flags(0)),
				MTP_int(usersCount),
				MTP_int(_optionsForOnePerson.months[i]),
				MTPstring(),
				MTPint(),
				MTP_string(_optionsForOnePerson.currencies[i]),
				MTP_long(_optionsForOnePerson.totalCosts[i] * usersCount)));
		}
		_subscriptionOptions[usersCount] = GiftCodesFromTL(tlOptions);
		return skipForStars(_subscriptionOptions[usersCount]);
	}
}

auto PremiumGiftCodeOptions::requestStarGifts()
-> rpl::producer<rpl::no_value, QString> {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		_api.request(MTPpayments_GetStarGifts(
			MTP_int(0)
		)).done([=](const MTPpayments_StarGifts &result) {
			result.match([&](const MTPDpayments_starGifts &data) {
				_peer->owner().processUsers(data.vusers());
				_peer->owner().processChats(data.vchats());
				_giftsHash = data.vhash().v;
				const auto &list = data.vgifts().v;
				const auto session = &_peer->session();
				auto gifts = std::vector<Data::StarGift>();
				gifts.reserve(list.size());
				for (const auto &gift : list) {
					if (auto parsed = FromTL(session, gift)) {
						gifts.push_back(std::move(*parsed));
					}
				}
				_gifts = std::move(gifts);
			}, [&](const MTPDpayments_starGiftsNotModified &) {
			});
			consumer.put_done();
		}).fail([=](const MTP::Error &error) {
			consumer.put_error_copy(error.type());
		}).send();

		return lifetime;
	};
}

auto PremiumGiftCodeOptions::starGifts() const
-> const std::vector<Data::StarGift> & {
	return _gifts;
}

int PremiumGiftCodeOptions::giveawayBoostsPerPremium() const {
	constexpr auto kFallbackCount = 4;
	return _peer->session().appConfig().get<int>(
		u"giveaway_boosts_per_premium"_q,
		kFallbackCount);
}

int PremiumGiftCodeOptions::giveawayCountriesMax() const {
	constexpr auto kFallbackCount = 10;
	return _peer->session().appConfig().get<int>(
		u"giveaway_countries_max"_q,
		kFallbackCount);
}

int PremiumGiftCodeOptions::giveawayAddPeersMax() const {
	constexpr auto kFallbackCount = 10;
	return _peer->session().appConfig().get<int>(
		u"giveaway_add_peers_max"_q,
		kFallbackCount);
}

int PremiumGiftCodeOptions::giveawayPeriodMax() const {
	constexpr auto kFallbackCount = 3600 * 24 * 7;
	return _peer->session().appConfig().get<int>(
		u"giveaway_period_max"_q,
		kFallbackCount);
}

bool PremiumGiftCodeOptions::giveawayGiftsPurchaseAvailable() const {
	return _peer->session().appConfig().get<bool>(
		u"giveaway_gifts_purchase_available"_q,
		false);
}

SponsoredToggle::SponsoredToggle(not_null<Main::Session*> session)
: _api(&session->api().instance()) {
}

rpl::producer<bool> SponsoredToggle::toggled() {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		_api.request(MTPusers_GetFullUser(
			MTP_inputUserSelf()
		)).done([=](const MTPusers_UserFull &result) {
			consumer.put_next_copy(
				result.data().vfull_user().data().is_sponsored_enabled());
		}).fail([=] { consumer.put_next(false); }).send();

		return lifetime;
	};
}

rpl::producer<rpl::no_value, QString> SponsoredToggle::setToggled(bool v) {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		_api.request(MTPaccount_ToggleSponsoredMessages(
			MTP_bool(v)
		)).done([=] {
			consumer.put_done();
		}).fail([=](const MTP::Error &error) {
			consumer.put_error_copy(error.type());
		}).send();

		return lifetime;
	};
}

MessageMoneyRestriction ResolveMessageMoneyRestrictions(
		not_null<PeerData*> peer,
		History *maybeHistory) {
	if (const auto channel = peer->asChannel()) {
		return {
			.starsPerMessage = channel->starsPerMessageChecked(),
			.known = true,
		};
	}
	const auto user = peer->asUser();
	if (!user) {
		return { .known = true };
	} else if (user->messageMoneyRestrictionsKnown()) {
		return {
			.starsPerMessage = user->starsPerMessageChecked(),
			.premiumRequired = (user->requiresPremiumToWrite()
				&& !user->session().premium()),
			.known = true,
		};
	} else if (user->hasStarsPerMessage()) {
		return {};
	} else if (!user->hasRequirePremiumToWrite()) {
		return { .known = true };
	} else if (user->flags() & UserDataFlag::MutualContact) {
		return { .known = true };
	} else if (!maybeHistory) {
		return {};
	}
	const auto update = [&](bool require) {
		using Flag = UserDataFlag;
		constexpr auto known = Flag::MessageMoneyRestrictionsKnown;
		constexpr auto me = Flag::RequiresPremiumToWrite;
		user->setFlags((user->flags() & ~me)
			| known
			| (require ? me : Flag()));
	};
	// We allow this potentially-heavy loop because in case we've opened
	// the chat and have a lot of messages `requires_premium` will be known.
	for (const auto &block : maybeHistory->blocks) {
		for (const auto &view : block->messages) {
			const auto item = view->data();
			if (!item->out() && !item->isService()) {
				update(false);
				return { .known = true };
			}
		}
	}
	if (user->isContact() // Here we know, that we're not in his contacts.
		&& maybeHistory->loadedAtTop() // And no incoming messages.
		&& maybeHistory->loadedAtBottom()) {
		return {
			.premiumRequired = !user->session().premium(),
			.known = true,
		};
	}
	return {};
}

rpl::producer<DocumentData*> RandomHelloStickerValue(
		not_null<Main::Session*> session) {
	const auto premium = &session->api().premium();
	const auto random = [=] {
		const auto &v = premium->helloStickers();
		Assert(!v.empty());
		return v[base::RandomIndex(v.size())].get();
	};
	const auto &v = premium->helloStickers();
	if (!v.empty()) {
		return rpl::single(random());
	}
	return rpl::single<DocumentData*>(
		nullptr
	) | rpl::then(premium->helloStickersUpdated(
	) | rpl::filter([=] {
		return !premium->helloStickers().empty();
	}) | rpl::take(1) | rpl::map(random));
}

std::optional<Data::StarGift> FromTL(
		not_null<Main::Session*> session,
		const MTPstarGift &gift) {
	return gift.match([&](const MTPDstarGift &data) {
		const auto document = session->data().processDocument(
			data.vsticker());
		const auto resellPrice = data.vresell_min_stars().value_or_empty();
		const auto remaining = data.vavailability_remains();
		const auto total = data.vavailability_total();
		if (!document->sticker()) {
			return std::optional<Data::StarGift>();
		}
		const auto releasedById = data.vreleased_by()
			? peerFromMTP(*data.vreleased_by())
			: PeerId();
		const auto releasedBy = releasedById
			? session->data().peer(releasedById).get()
			: nullptr;
		return std::optional<Data::StarGift>(Data::StarGift{
			.id = uint64(data.vid().v),
			.stars = int64(data.vstars().v),
			.starsConverted = int64(data.vconvert_stars().v),
			.starsToUpgrade = int64(data.vupgrade_stars().value_or_empty()),
			.starsResellMin = int64(resellPrice),
			.document = document,
			.releasedBy = releasedBy,
			.resellTitle = qs(data.vtitle().value_or_empty()),
			.resellCount = int(data.vavailability_resale().value_or_empty()),
			.limitedLeft = remaining.value_or_empty(),
			.limitedCount = total.value_or_empty(),
			.perUserTotal = data.vper_user_total().value_or_empty(),
			.perUserRemains = data.vper_user_remains().value_or_empty(),
			.firstSaleDate = data.vfirst_sale_date().value_or_empty(),
			.lastSaleDate = data.vlast_sale_date().value_or_empty(),
			.lockedUntilDate = data.vlocked_until_date().value_or_empty(),
			.requirePremium = data.is_require_premium(),
			.peerColorAvailable = data.is_peer_color_available(),
			.upgradable = data.vupgrade_stars().has_value(),
			.birthday = data.is_birthday(),
			.soldOut = data.is_sold_out(),
		});
	}, [&](const MTPDstarGiftUnique &data) {
		const auto total = data.vavailability_total().v;
		auto model = std::optional<Data::UniqueGiftModel>();
		auto pattern = std::optional<Data::UniqueGiftPattern>();
		for (const auto &attribute : data.vattributes().v) {
			attribute.match([&](const MTPDstarGiftAttributeModel &data) {
				model = FromTL(session, data);
			}, [&](const MTPDstarGiftAttributePattern &data) {
				pattern = FromTL(session, data);
			}, [&](const MTPDstarGiftAttributeBackdrop &data) {
			}, [&](const MTPDstarGiftAttributeOriginalDetails &data) {
			});
		}
		if (!model
			|| !model->document->sticker()
			|| !pattern
			|| !pattern->document->sticker()) {
			return std::optional<Data::StarGift>();
		}
		const auto releasedById = data.vreleased_by()
			? peerFromMTP(*data.vreleased_by())
			: PeerId();
		const auto themeUserId = data.vtheme_peer()
			? peerFromMTP(*data.vtheme_peer())
			: PeerId();
		const auto releasedBy = releasedById
			? session->data().peer(releasedById).get()
			: nullptr;
		const auto themeUser = themeUserId
			? session->data().peer(themeUserId).get()
			: nullptr;
		const auto colorCollectible = (data.vpeer_color()
			&& data.vpeer_color()->type() == mtpc_peerColorCollectible)
			? std::make_shared<Ui::ColorCollectible>(
				Data::ParseColorCollectible(
					data.vpeer_color()->c_peerColorCollectible()))
			: nullptr;
		auto result = Data::StarGift{
			.id = data.vid().v,
			.unique = std::make_shared<Data::UniqueGift>(Data::UniqueGift{
				.id = data.vid().v,
				.initialGiftId = data.vgift_id().v,
				.slug = qs(data.vslug()),
				.title = qs(data.vtitle()),
				.giftAddress = qs(data.vgift_address().value_or_empty()),
				.ownerAddress = qs(data.vowner_address().value_or_empty()),
				.ownerName = qs(data.vowner_name().value_or_empty()),
				.ownerId = (data.vowner_id()
					? peerFromMTP(*data.vowner_id())
					: PeerId()),
				.hostId = (data.vhost_id()
					? peerFromMTP(*data.vhost_id())
					: PeerId()),
				.releasedBy = releasedBy,
				.themeUser = themeUser,
				.nanoTonForResale = FindTonForResale(data.vresell_amount()),
				.starsForResale = FindStarsForResale(data.vresell_amount()),
				.number = data.vnum().v,
				.onlyAcceptTon = data.is_resale_ton_only(),
				.canBeTheme = data.is_theme_available(),
				.model = *model,
				.pattern = *pattern,
				.value = (data.vvalue_amount()
					? std::make_shared<Data::UniqueGiftValue>(
						Data::UniqueGiftValue{
							.currency = qs(
								data.vvalue_currency().value_or_empty()),
							.valuePrice = int64(
								data.vvalue_amount().value_or_empty()),
						})
					: nullptr),
				.peerColor = colorCollectible,
			}),
			.document = model->document,
			.releasedBy = releasedBy,
			.limitedLeft = (total - data.vavailability_issued().v),
			.limitedCount = total,
			.resellTonOnly = data.is_resale_ton_only(),
			.requirePremium = data.is_require_premium(),
		};
		const auto unique = result.unique.get();
		for (const auto &attribute : data.vattributes().v) {
			attribute.match([&](const MTPDstarGiftAttributeModel &data) {
			}, [&](const MTPDstarGiftAttributePattern &data) {
			}, [&](const MTPDstarGiftAttributeBackdrop &data) {
				unique->backdrop = FromTL(data);
			}, [&](const MTPDstarGiftAttributeOriginalDetails &data) {
				unique->originalDetails = FromTL(session, data);
			});
		}
		return std::make_optional(result);
	});
}

std::optional<Data::SavedStarGift> FromTL(
		not_null<PeerData*> to,
		const MTPsavedStarGift &gift) {
	const auto session = &to->session();
	const auto &data = gift.data();
	auto parsed = FromTL(session, data.vgift());
	if (!parsed) {
		return {};
	} else if (const auto unique = parsed->unique.get()) {
		unique->starsForTransfer = data.vtransfer_stars().value_or(-1);
		unique->exportAt = data.vcan_export_at().value_or_empty();
		unique->canTransferAt = data.vcan_transfer_at().value_or_empty();
		unique->canResellAt = data.vcan_resell_at().value_or_empty();
	}
	using Id = Data::SavedStarGiftId;
	const auto hasUnique = parsed->unique != nullptr;
	return Data::SavedStarGift{
		.info = std::move(*parsed),
		.manageId = (to->isUser()
			? Id::User(data.vmsg_id().value_or_empty())
			: Id::Chat(to, data.vsaved_id().value_or_empty())),
		.collectionIds = (data.vcollection_id()
			? (data.vcollection_id()->v
				| ranges::views::transform(&MTPint::v)
				| ranges::to_vector)
			: std::vector<int>()),
		.message = (data.vmessage()
			? Api::ParseTextWithEntities(
				session,
				*data.vmessage())
			: TextWithEntities()),
		.starsConverted = int64(data.vconvert_stars().value_or_empty()),
		.starsUpgradedBySender = int64(
			data.vupgrade_stars().value_or_empty()),
		.starsForDetailsRemove = int64(
			data.vdrop_original_details_stars().value_or_empty()),
		.giftPrepayUpgradeHash = qs(
			data.vprepaid_upgrade_hash().value_or_empty()),
		.fromId = (data.vfrom_id()
			? peerFromMTP(*data.vfrom_id())
			: PeerId()),
		.date = data.vdate().v,
		.upgradeSeparate = data.is_upgrade_separate(),
		.upgradable = data.is_can_upgrade(),
		.anonymous = data.is_name_hidden(),
		.pinned = data.is_pinned_to_top() && hasUnique,
		.hidden = data.is_unsaved(),
		.mine = to->isSelf(),
	};
}

Data::UniqueGiftModel FromTL(
		not_null<Main::Session*> session,
		const MTPDstarGiftAttributeModel &data) {
	auto result = Data::UniqueGiftModel{
		.document = session->data().processDocument(data.vdocument()),
	};
	result.name = qs(data.vname());
	result.rarityPermille = data.vrarity_permille().v;
	return result;
}

Data::UniqueGiftPattern FromTL(
		not_null<Main::Session*> session,
		const MTPDstarGiftAttributePattern &data) {
	auto result = Data::UniqueGiftPattern{
		.document = session->data().processDocument(data.vdocument()),
	};
	result.document->overrideEmojiUsesTextColor(true);
	result.name = qs(data.vname());
	result.rarityPermille = data.vrarity_permille().v;
	return result;
}

Data::UniqueGiftBackdrop FromTL(const MTPDstarGiftAttributeBackdrop &data) {
	auto result = Data::UniqueGiftBackdrop{ .id = data.vbackdrop_id().v };
	result.name = qs(data.vname());
	result.rarityPermille = data.vrarity_permille().v;
	result.centerColor = Ui::ColorFromSerialized(
		data.vcenter_color());
	result.edgeColor = Ui::ColorFromSerialized(
		data.vedge_color());
	result.patternColor = Ui::ColorFromSerialized(
		data.vpattern_color());
	result.textColor = Ui::ColorFromSerialized(
		data.vtext_color());
	return result;
}

Data::UniqueGiftOriginalDetails FromTL(
		not_null<Main::Session*> session,
		const MTPDstarGiftAttributeOriginalDetails &data) {
	auto result = Data::UniqueGiftOriginalDetails();
	result.date = data.vdate().v;
	result.senderId = data.vsender_id()
		? peerFromMTP(*data.vsender_id())
		: PeerId();
	result.recipientId = peerFromMTP(data.vrecipient_id());
	result.message = data.vmessage()
		? ParseTextWithEntities(session, *data.vmessage())
		: TextWithEntities();
	return result;
}

} // namespace Api
