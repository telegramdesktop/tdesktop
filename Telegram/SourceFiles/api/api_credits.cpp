/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_credits.h"

#include "apiwrap.h"
#include "data/data_peer.h"
#include "main/main_session.h"

namespace Api {

CreditsTopupOptions::CreditsTopupOptions(not_null<PeerData*> peer)
: _peer(peer)
, _api(&peer->session().api().instance()) {
}

rpl::producer<rpl::no_value, QString> CreditsTopupOptions::request() {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		using TLOption = MTPStarsTopupOption;
		_api.request(MTPpayments_GetStarsTopupOptions(
		)).done([=](const MTPVector<TLOption> &result) {
			_options = ranges::views::all(
				result.v
			) | ranges::views::transform([](const TLOption &option) {
				return Data::CreditTopupOption{
					.credits = option.data().vstars().v,
					.product = qs(
						option.data().vstore_product().value_or_empty()),
					.currency = qs(option.data().vcurrency()),
					.amount = option.data().vamount().v,
				};
			}) | ranges::to_vector;
			consumer.put_done();
		}).fail([=](const MTP::Error &error) {
			consumer.put_error_copy(error.type());
		}).send();

		return lifetime;
	};
}

Data::CreditTopupOptions CreditsTopupOptions::options() const {
	return _options;
}

} // namespace Api
