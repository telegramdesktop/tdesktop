/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_credits.h"
#include "mtproto/sender.h"

namespace Main {
class Session;
} // namespace Main

namespace Api {

class CreditsTopupOptions final {
public:
	CreditsTopupOptions(not_null<PeerData*> peer);

	[[nodiscard]] rpl::producer<rpl::no_value, QString> request();
	[[nodiscard]] Data::CreditTopupOptions options() const;

private:
	const not_null<PeerData*> _peer;

	Data::CreditTopupOptions _options;

	MTP::Sender _api;

};

} // namespace Api
