/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_statistics_sender.h"
#include "data/data_credits.h"
#include "data/data_credits_earn.h"
#include "mtproto/sender.h"

namespace Data {
class SavedStarGiftId;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

class UserData;

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

class CreditsGiveawayOptions final {
public:
	CreditsGiveawayOptions(not_null<PeerData*> peer);

	[[nodiscard]] rpl::producer<rpl::no_value, QString> request();
	[[nodiscard]] Data::CreditsGiveawayOptions options() const;

private:
	const not_null<PeerData*> _peer;

	Data::CreditsGiveawayOptions _options;

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
	CreditsHistory(
		not_null<PeerData*> peer,
		bool in,
		bool out,
		bool currency = false);

	void request(
		const Data::CreditsStatusSlice::OffsetToken &token,
		Fn<void(Data::CreditsStatusSlice)> done);
	void requestSubscriptions(
		const Data::CreditsStatusSlice::OffsetToken &token,
		Fn<void(Data::CreditsStatusSlice)> done,
		bool missingBalance = false);

private:
	using HistoryTL = MTPpayments_GetStarsTransactions;
	const not_null<PeerData*> _peer;
	const HistoryTL::Flags _flags;

	mtpRequestId _requestId = 0;

	MTP::Sender _api;

};

class CreditsEarnStatistics final : public StatisticsRequestSender {
public:
	explicit CreditsEarnStatistics(not_null<PeerData*>);

	[[nodiscard]] rpl::producer<rpl::no_value, QString> request();
	[[nodiscard]] Data::CreditsEarnStatistics data() const;

private:
	const bool _isUser = false;
	Data::CreditsEarnStatistics _data;

	mtpRequestId _requestId = 0;

};

[[nodiscard]] rpl::producer<not_null<PeerData*>> PremiumPeerBot(
	not_null<Main::Session*> session);

void EditCreditsSubscription(
	not_null<Main::Session*> session,
	const QString &id,
	bool cancel,
	Fn<void()> done,
	Fn<void(QString)> fail);

[[nodiscard]] MTPInputSavedStarGift InputSavedStarGiftId(
	const Data::SavedStarGiftId &id,
	const std::shared_ptr<Data::UniqueGift> &unique = nullptr);

} // namespace Api
