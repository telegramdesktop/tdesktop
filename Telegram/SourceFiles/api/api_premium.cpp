/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_premium.h"

#include "api/api_premium_option.h"
#include "api/api_text_entities.h"
#include "main/main_session.h"
#include "data/data_peer_values.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_peer.h"
#include "apiwrap.h"

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

} // namespace Api
