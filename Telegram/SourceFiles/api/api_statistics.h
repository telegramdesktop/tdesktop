/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "data/data_boosts.h"
#include "data/data_statistics.h"
#include "mtproto/sender.h"

class ChannelData;
class PeerData;

namespace Api {

class StatisticsRequestSender {
protected:
	explicit StatisticsRequestSender(not_null<ChannelData*> channel);
	~StatisticsRequestSender();

	template <
		typename Request,
		typename = std::enable_if_t<!std::is_reference_v<Request>>,
		typename = typename Request::Unboxed>
	[[nodiscard]] auto makeRequest(Request &&request);

	[[nodiscard]] MTP::Sender &api() {
		return _api;
	}
	[[nodiscard]] not_null<ChannelData*> channel() {
		return _channel;
	}

private:
	void checkRequests();

	const not_null<ChannelData*> _channel;
	MTP::Sender _api;
	base::Timer _timer;
	base::flat_map<MTP::DcId, base::flat_set<mtpRequestId>> _requests;

};

class Statistics final : public StatisticsRequestSender {
public:
	explicit Statistics(not_null<ChannelData*> channel);

	[[nodiscard]] rpl::producer<rpl::no_value, QString> request();
	using GraphResult = rpl::producer<Data::StatisticalGraph, QString>;
	[[nodiscard]] GraphResult requestZoom(
		const QString &token,
		float64 x);

	[[nodiscard]] Data::ChannelStatistics channelStats() const;
	[[nodiscard]] Data::SupergroupStatistics supergroupStats() const;

private:
	Data::ChannelStatistics _channelStats;
	Data::SupergroupStatistics _supergroupStats;

	std::deque<Fn<void()>> _zoomDeque;

};

class PublicForwards final : public StatisticsRequestSender {
public:
	PublicForwards(not_null<ChannelData*> channel, FullMsgId fullId);

	void request(
		const Data::PublicForwardsSlice::OffsetToken &token,
		Fn<void(Data::PublicForwardsSlice)> done);

private:
	const FullMsgId _fullId;
	mtpRequestId _requestId = 0;
	int _lastTotal = 0;

};

class MessageStatistics final : public StatisticsRequestSender {
public:
	explicit MessageStatistics(
		not_null<ChannelData*> channel,
		FullMsgId fullId);

	void request(Fn<void(Data::MessageStatistics)> done);

	[[nodiscard]] Data::PublicForwardsSlice firstSlice() const;

private:
	PublicForwards _publicForwards;
	const FullMsgId _fullId;

	Data::PublicForwardsSlice _firstSlice;

	mtpRequestId _requestId = 0;

};

class Boosts final {
public:
	explicit Boosts(not_null<PeerData*> peer);

	[[nodiscard]] rpl::producer<rpl::no_value, QString> request();
	void requestBoosts(
		const Data::BoostsListSlice::OffsetToken &token,
		Fn<void(Data::BoostsListSlice)> done);

	[[nodiscard]] Data::BoostStatus boostStatus() const;

	static constexpr auto kFirstSlice = int(10);
	static constexpr auto kLimit = int(40);

private:
	const not_null<PeerData*> _peer;
	Data::BoostStatus _boostStatus;

	MTP::Sender _api;
	mtpRequestId _requestId = 0;

};

} // namespace Api
