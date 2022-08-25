/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_subscription_option.h"
#include "mtproto/sender.h"

class ApiWrap;

namespace Main {
class Session;
} // namespace Main

namespace Api {

class Premium final {
public:
	explicit Premium(not_null<ApiWrap*> api);

	void reload();
	[[nodiscard]] rpl::producer<TextWithEntities> statusTextValue() const;

	[[nodiscard]] auto videos() const
		-> const base::flat_map<QString, not_null<DocumentData*>> &;
	[[nodiscard]] rpl::producer<> videosUpdated() const;

	[[nodiscard]] auto stickers() const
		-> const std::vector<not_null<DocumentData*>> &;
	[[nodiscard]] rpl::producer<> stickersUpdated() const;

	[[nodiscard]] auto cloudSet() const
		-> const std::vector<not_null<DocumentData*>> &;
	[[nodiscard]] rpl::producer<> cloudSetUpdated() const;

	[[nodiscard]] int64 monthlyAmount() const;
	[[nodiscard]] QString monthlyCurrency() const;

	[[nodiscard]] auto subscriptionOptions() const
		-> const Data::SubscriptionOptions &;

private:
	void reloadPromo();
	void reloadStickers();
	void reloadCloudSet();

	const not_null<Main::Session*> _session;
	MTP::Sender _api;

	mtpRequestId _promoRequestId = 0;
	std::optional<TextWithEntities> _statusText;
	rpl::event_stream<TextWithEntities> _statusTextUpdates;

	base::flat_map<QString, not_null<DocumentData*>> _videos;
	rpl::event_stream<> _videosUpdated;

	mtpRequestId _stickersRequestId = 0;
	uint64 _stickersHash = 0;
	std::vector<not_null<DocumentData*>> _stickers;
	rpl::event_stream<> _stickersUpdated;

	mtpRequestId _cloudSetRequestId = 0;
	uint64 _cloudSetHash = 0;
	std::vector<not_null<DocumentData*>> _cloudSet;
	rpl::event_stream<> _cloudSetUpdated;

	int64 _monthlyAmount = 0;
	QString _monthlyCurrency;

	Data::SubscriptionOptions _subscriptionOptions;

};

} // namespace Api
