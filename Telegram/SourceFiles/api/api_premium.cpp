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
#include "data/data_document.h"
#include "data/data_peer.h"
#include "data/data_peer_values.h"
#include "data/data_session.h"
#include "main/main_account.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "payments/payments_form.h"
#include "ui/text/format_values.h"

namespace Api {
namespace {

[[nodiscard]] GiftCode Parse(const MTPDpayments_checkedGiftCode &data) {
	return {
		.from = peerFromMTP(data.vfrom_id()),
		.to = data.vto_id() ? peerFromUser(*data.vto_id()) : PeerId(),
		.giveawayId = data.vgiveaway_msg_id().value_or_empty(),
		.date = data.vdate().v,
		.used = data.vused_date().value_or_empty(),
		.months = data.vmonths().v,
		.giveaway = data.is_via_giveaway(),
	};
}

[[nodiscard]] Data::SubscriptionOptions GiftCodesFromTL(
		const QVector<MTPPremiumGiftCodeOption> &tlOptions) {
	auto options = SubscriptionOptionsFromTL(tlOptions);
	for (auto i = 0; i < options.size(); i++) {
		const auto &tlOption = tlOptions[i].data();
		const auto perUserText = Ui::FillAmountAndCurrency(
			tlOption.vamount().v / float64(tlOption.vusers().v),
			qs(tlOption.vcurrency()),
			false);
		options[i].costPerMonth = perUserText
			+ ' '
			+ QChar(0x00D7)
			+ ' '
			+ QString::number(tlOption.vusers().v);
	}
	return options;
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

		_subscriptionOptions = SubscriptionOptionsFromTL(
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
			info.activatedCount = data.vactivated_count().v;
			info.finishDate = data.vfinish_date().v;
			info.startDate = data.vstart_date().v;
		});
		_giveawayInfoDone(std::move(info));
	}).fail([=] {
		_giveawayInfoRequestId = 0;
		_giveawayInfoDone({});
	}).send();
}

const Data::SubscriptionOptions &Premium::subscriptionOptions() const {
	return _subscriptionOptions;
}

PremiumGiftCodeOptions::PremiumGiftCodeOptions(not_null<PeerData*> peer)
: _peer(peer)
, _api(&peer->session().api().instance()) {
}

rpl::producer<rpl::no_value, QString> PremiumGiftCodeOptions::request() {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();
		const auto channel = _peer->asChannel();
		if (!channel) {
			return lifetime;
		}

		using TLOption = MTPPremiumGiftCodeOption;
		_api.request(MTPpayments_GetPremiumGiftCodeOptions(
			MTP_flags(
				MTPpayments_GetPremiumGiftCodeOptions::Flag::f_boost_peer),
			_peer->input
		)).done([=](const MTPVector<TLOption> &result) {
			auto tlMapOptions = base::flat_map<Amount, QVector<TLOption>>();
			for (const auto &tlOption : result.v) {
				const auto &data = tlOption.data();
				tlMapOptions[data.vusers().v].push_back(tlOption);

				const auto token = Token{ data.vusers().v, data.vmonths().v };
				_stores[token] = Store{
					.amount = data.vamount().v,
					.product = qs(data.vstore_product().value_or_empty()),
					.quantity = data.vstore_quantity().value_or_empty(),
				};
				if (!ranges::contains(_availablePresets, data.vusers().v)) {
					_availablePresets.push_back(data.vusers().v);
				}
			}
			for (const auto &[amount, tlOptions] : tlMapOptions) {
				if (amount == 1 && _optionsForOnePerson.currency.isEmpty()) {
					_optionsForOnePerson.currency = qs(
						tlOptions.front().data().vcurrency());
					for (const auto &option : tlOptions) {
						_optionsForOnePerson.months.push_back(
							option.data().vmonths().v);
						_optionsForOnePerson.totalCosts.push_back(
							option.data().vamount().v);
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
			Payments::InvoicePremiumGiftCodeGiveawayToTL(invoice)
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
	return _optionsForOnePerson.months[monthsIndex];
}

Payments::InvoicePremiumGiftCode PremiumGiftCodeOptions::invoice(
		int users,
		int months) {
	const auto randomId = base::RandomValue<uint64>();
	const auto token = Token{ users, months };
	const auto &store = _stores[token];
	return Payments::InvoicePremiumGiftCode{
		.randomId = randomId,
		.currency = _optionsForOnePerson.currency,
		.amount = store.amount,
		.storeProduct = store.product,
		.storeQuantity = store.quantity,
		.users = token.users,
		.months = token.months,
	};
}

Data::SubscriptionOptions PremiumGiftCodeOptions::options(int amount) {
	const auto it = _subscriptionOptions.find(amount);
	if (it != end(_subscriptionOptions)) {
		return it->second;
	} else {
		auto tlOptions = QVector<MTPPremiumGiftCodeOption>();
		for (auto i = 0; i < _optionsForOnePerson.months.size(); i++) {
			tlOptions.push_back(MTP_premiumGiftCodeOption(
				MTP_flags(MTPDpremiumGiftCodeOption::Flags(0)),
				MTP_int(amount),
				MTP_int(_optionsForOnePerson.months[i]),
				MTPstring(),
				MTPint(),
				MTP_string(_optionsForOnePerson.currency),
				MTP_long(_optionsForOnePerson.totalCosts[i] * amount)));
		}
		_subscriptionOptions[amount] = GiftCodesFromTL(tlOptions);
		return _subscriptionOptions[amount];
	}
}

int PremiumGiftCodeOptions::giveawayBoostsPerPremium() const {
	constexpr auto kFallbackCount = 4;
	return _peer->session().account().appConfig().get<int>(
		u"giveaway_boosts_per_premium"_q,
		kFallbackCount);
}

int PremiumGiftCodeOptions::giveawayCountriesMax() const {
	constexpr auto kFallbackCount = 10;
	return _peer->session().account().appConfig().get<int>(
		u"giveaway_countries_max"_q,
		kFallbackCount);
}

int PremiumGiftCodeOptions::giveawayAddPeersMax() const {
	constexpr auto kFallbackCount = 10;
	return _peer->session().account().appConfig().get<int>(
		u"giveaway_add_peers_max"_q,
		kFallbackCount);
}

int PremiumGiftCodeOptions::giveawayPeriodMax() const {
	constexpr auto kFallbackCount = 3600 * 24 * 7;
	return _peer->session().account().appConfig().get<int>(
		u"giveaway_period_max"_q,
		kFallbackCount);
}

bool PremiumGiftCodeOptions::giveawayGiftsPurchaseAvailable() const {
	return _peer->session().account().appConfig().get<bool>(
		u"giveaway_gifts_purchase_available"_q,
		false);
}

} // namespace Api
