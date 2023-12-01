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

namespace Payments {
struct InvoicePremiumGiftCode;
} // namespace Payments

namespace Api {

struct GiftCode {
	PeerId from = 0;
	PeerId to = 0;
	MsgId giveawayId = 0;
	TimeId date = 0;
	TimeId used = 0; // 0 if not used.
	int months = 0;
	bool giveaway = false;

	explicit operator bool() const {
		return months != 0;
	}

	friend inline bool operator==(
		const GiftCode&,
		const GiftCode&) = default;
};

enum class GiveawayState {
	Invalid,
	Running,
	Preparing,
	Finished,
	Refunded,
};

struct GiveawayInfo {
	QString giftCode;
	QString disallowedCountry;
	ChannelId adminChannelId = 0;
	GiveawayState state = GiveawayState::Invalid;
	TimeId tooEarlyDate = 0;
	TimeId finishDate = 0;
	TimeId startDate = 0;
	int winnersCount = 0;
	int activatedCount = 0;
	bool participating = false;

	explicit operator bool() const {
		return state != GiveawayState::Invalid;
	}
};

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

	void checkGiftCode(
		const QString &slug,
		Fn<void(GiftCode)> done);
	GiftCode updateGiftCode(const QString &slug, const GiftCode &code);
	[[nodiscard]] rpl::producer<GiftCode> giftCodeValue(
		const QString &slug) const;
	void applyGiftCode(const QString &slug, Fn<void(QString)> done);

	void resolveGiveawayInfo(
		not_null<PeerData*> peer,
		MsgId messageId,
		Fn<void(GiveawayInfo)> done);

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

	mtpRequestId _giftCodeRequestId = 0;
	QString _giftCodeSlug;
	base::flat_map<QString, GiftCode> _giftCodes;
	rpl::event_stream<QString> _giftCodeUpdated;

	mtpRequestId _giveawayInfoRequestId = 0;
	PeerData *_giveawayInfoPeer = nullptr;
	MsgId _giveawayInfoMessageId = 0;
	Fn<void(GiveawayInfo)> _giveawayInfoDone;

	Data::SubscriptionOptions _subscriptionOptions;

};

class PremiumGiftCodeOptions final {
public:
	PremiumGiftCodeOptions(not_null<PeerData*> peer);

	[[nodiscard]] rpl::producer<rpl::no_value, QString> request();
	[[nodiscard]] Data::SubscriptionOptions options(int amount);
	[[nodiscard]] const std::vector<int> &availablePresets() const;
	[[nodiscard]] int monthsFromPreset(int monthsIndex);
	[[nodiscard]] Payments::InvoicePremiumGiftCode invoice(
		int users,
		int months);
	[[nodiscard]] rpl::producer<rpl::no_value, QString> applyPrepaid(
		const Payments::InvoicePremiumGiftCode &invoice,
		uint64 prepaidId);

	[[nodiscard]] int giveawayBoostsPerPremium() const;
	[[nodiscard]] int giveawayCountriesMax() const;
	[[nodiscard]] int giveawayAddPeersMax() const;
	[[nodiscard]] int giveawayPeriodMax() const;
	[[nodiscard]] bool giveawayGiftsPurchaseAvailable() const;

private:
	struct Token final {
		int users = 0;
		int months = 0;

		friend inline constexpr auto operator<=>(Token, Token) = default;

	};
	struct Store final {
		uint64 amount = 0;
		QString product;
		int quantity = 0;
	};
	using Amount = int;
	const not_null<PeerData*> _peer;
	base::flat_map<Amount, Data::SubscriptionOptions> _subscriptionOptions;
	struct {
		std::vector<int> months;
		std::vector<float64> totalCosts;
		QString currency;
	} _optionsForOnePerson;

	std::vector<int> _availablePresets;

	base::flat_map<Token, Store> _stores;

	MTP::Sender _api;

};

} // namespace Api
