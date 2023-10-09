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
class ChannelData;
class PeerData;

namespace Api {

class Statistics final {
public:
	explicit Statistics(not_null<ApiWrap*> api);

	[[nodiscard]] rpl::producer<rpl::no_value, QString> request(
		not_null<PeerData*> peer);
	using GraphResult = rpl::producer<Data::StatisticalGraph, QString>;
	[[nodiscard]] GraphResult requestZoom(
		not_null<PeerData*> peer,
		const QString &token,
		float64 x);
	[[nodiscard]] GraphResult requestMessage(
		not_null<PeerData*> peer,
		MsgId msgId);

	[[nodiscard]] Data::ChannelStatistics channelStats() const;
	[[nodiscard]] Data::SupergroupStatistics supergroupStats() const;

private:
	Data::ChannelStatistics _channelStats;
	Data::SupergroupStatistics _supergroupStats;
	MTP::Sender _api;

};

class PublicForwards final {
public:
	struct OffsetToken final {
		int rate = 0;
		FullMsgId fullId;
	};

	struct Slice {
		QVector<FullMsgId> list;
		int total = 0;
		bool allLoaded = false;
		OffsetToken token;
	};

	explicit PublicForwards(not_null<ChannelData*> channel, FullMsgId fullId);

	void request(const OffsetToken &token, Fn<void(Slice)> done);

private:
	const not_null<ChannelData*> _channel;
	const FullMsgId _fullId;
	mtpRequestId _requestId = 0;
	int _lastTotal = 0;

	MTP::Sender _api;

};

class MessageStatistics final {
public:
	explicit MessageStatistics(
		not_null<ChannelData*> channel,
		FullMsgId fullId);

	void request(Fn<void(Data::MessageStatistics)> done);

	[[nodiscard]] PublicForwards::Slice firstSlice() const;

private:
	PublicForwards _publicForwards;
	const not_null<ChannelData*> _channel;
	const FullMsgId _fullId;

	PublicForwards::Slice _firstSlice;

	mtpRequestId _requestId = 0;
	MTP::Sender _api;

};

} // namespace Api
