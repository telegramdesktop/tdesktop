/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_statistics.h"
#include "mtproto/sender.h"

class ApiWrap;
class PeerData;

namespace Api {

class Statistics final {
public:
	explicit Statistics(not_null<ApiWrap*> api);

	[[nodiscard]] rpl::producer<rpl::no_value, QString> request(
		not_null<PeerData*> peer);

	[[nodiscard]] Data::ChannelStatistics channelStats() const;

private:
	Data::ChannelStatistics _channelStats;
	MTP::Sender _api;

};

} // namespace Api
