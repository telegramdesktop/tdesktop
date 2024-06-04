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

class CreditsStatus final {
public:
	CreditsStatus(not_null<PeerData*> peer);

	void request(
		const Data::CreditsStatusSlice::OffsetToken &token,
		Fn<void(Data::CreditsStatusSlice)> done);

private:
	const not_null<PeerData*> _peer;

	mtpRequestId _requestId = 0;

	MTP::Sender _api;

};

class CreditsHistory final {
public:
	CreditsHistory(not_null<PeerData*> peer, bool in, bool out);

	void request(
		const Data::CreditsStatusSlice::OffsetToken &token,
		Fn<void(Data::CreditsStatusSlice)> done);

private:
	using HistoryTL = MTPpayments_GetStarsTransactions;
	const not_null<PeerData*> _peer;
	const HistoryTL::Flags _flags;

	mtpRequestId _requestId = 0;

	MTP::Sender _api;

};

[[nodiscard]] rpl::producer<not_null<PeerData*>> PremiumPeerBot(
	not_null<Main::Session*> session);

} // namespace Api
