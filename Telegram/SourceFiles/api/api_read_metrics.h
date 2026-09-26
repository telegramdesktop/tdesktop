/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"
#include "base/timer.h"

class ApiWrap;
class PeerData;

namespace Api {

struct FinalizedReadMetric {
	MsgId msgId = 0;
	uint64 viewId = 0;
	int timeInViewMs = 0;
	int activeTimeInViewMs = 0;
	int heightToViewportRatioPermille = 0;
	int seenRangeRatioPermille = 0;
};

class ReadMetrics final {
public:
	explicit ReadMetrics(not_null<ApiWrap*> api);

	void add(not_null<PeerData*> peer, FinalizedReadMetric metric);

private:
	void send();

	MTP::Sender _api;
	base::flat_map<
		not_null<PeerData*>,
		std::vector<FinalizedReadMetric>> _pending;
	base::flat_map<not_null<PeerData*>, mtpRequestId> _requests;
	base::Timer _timer;

};

} // namespace Api
