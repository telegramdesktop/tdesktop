/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_premium.h"

#include "api/api_text_entities.h"
#include "main/main_session.h"
#include "data/data_peer_values.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_peer.h"
#include "apiwrap.h"

namespace Api {

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
		result.match([&](const MTPDhelp_premiumPromo &data) {
			_session->data().processUsers(data.vusers());
			_monthlyAmount = data.vmonthly_amount().v;
			_monthlyCurrency = qs(data.vcurrency());
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
		});
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

} // namespace Api
