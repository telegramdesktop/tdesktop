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
		) | rpl::skip(1) | rpl::start_with_next([=] {
			reload();
		}, _session->lifetime());
	});
}

rpl::producer<TextWithEntities> Premium::statusTextValue() const {
	return _statusTextUpdates.events_starting_with_copy(
		_statusText.value_or(TextWithEntities()));
}

void Premium::reload() {
	if (_statusRequestId) {
		return;
	}
	_statusRequestId = _api.request(MTPhelp_GetPremiumPromo(
	)).done([=](const MTPhelp_PremiumPromo &result) {
		_statusRequestId = 0;
		result.match([&](const MTPDhelp_premiumPromo &data) {
			auto text = TextWithEntities{
				qs(data.vstatus_text()),
				EntitiesFromMTP(_session, data.vstatus_entities().v),
			};
			_statusText = text;
			_statusTextUpdates.fire(std::move(text));
		});
	}).fail([=] {
		_statusRequestId = 0;
	}).send();
}

} // namespace Api
