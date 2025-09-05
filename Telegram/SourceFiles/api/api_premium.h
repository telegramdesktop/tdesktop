/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_premium_subscription_option.h"
#include "data/data_star_gift.h"
#include "mtproto/sender.h"

class History;
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
	uint64 credits = 0;
	int winnersCount = 0;
	int activatedCount = 0;
	bool participating = false;

	explicit operator bool() const {
		return state != GiveawayState::Invalid;
	}
};

struct GiftOptionData {
	int64 cost = 0;
	QString currency;
	int months = 0;
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

	[[nodiscard]] auto helloStickers() const
		-> const std::vector<not_null<DocumentData*>> &;
	[[nodiscard]] rpl::producer<> helloStickersUpdated() const;

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
		-> const Data::PremiumSubscriptionOptions &;

	[[nodiscard]] auto someMessageMoneyRestrictionsResolved() const
		-> rpl::producer<>;
	void resolveMessageMoneyRestrictions(not_null<UserData*> user);

private:
	void reloadPromo();
	void reloadStickers();
	void reloadCloudSet();
	void reloadHelloStickers();
	void requestPremiumRequiredSlice();

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

	mtpRequestId _helloStickersRequestId = 0;
	uint64 _helloStickersHash = 0;
	std::vector<not_null<DocumentData*>> _helloStickers;
	rpl::event_stream<> _helloStickersUpdated;

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

	Data::PremiumSubscriptionOptions _subscriptionOptions;

	rpl::event_stream<> _someMessageMoneyRestrictionsResolved;
	base::flat_set<not_null<UserData*>> _resolveMessageMoneyRequiredUsers;
	base::flat_set<not_null<UserData*>> _resolveMessageMoneyRequestedUsers;
	bool _messageMoneyRequestScheduled = false;

};

class PremiumGiftCodeOptions final {
public:
	PremiumGiftCodeOptions(not_null<PeerData*> peer);

	[[nodiscard]] rpl::producer<rpl::no_value, QString> request();
	[[nodiscard]] std::vector<GiftOptionData> optionsForPeer() const;
	[[nodiscard]] Data::PremiumSubscriptionOptions optionsForGiveaway(
			int usersCount);
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

	[[nodiscard]] rpl::producer<rpl::no_value, QString> requestStarGifts();
	[[nodiscard]] const std::vector<Data::StarGift> &starGifts() const;

private:
	struct Token final {
		int users = 0;
		int months = 0;

		friend inline constexpr auto operator<=>(Token, Token) = default;

	};
	struct Store final {
		uint64 amount = 0;
		QString currency;
		QString product;
		int quantity = 0;
	};
	using Amount = int;
	using PremiumSubscriptionOptions = Data::PremiumSubscriptionOptions;
	const not_null<PeerData*> _peer;
	base::flat_map<Amount, PremiumSubscriptionOptions> _subscriptionOptions;
	struct {
		std::vector<int> months;
		std::vector<int64> totalCosts;
		std::vector<QString> currencies;
	} _optionsForOnePerson;

	std::vector<int> _availablePresets;

	base::flat_map<Token, Store> _stores;

	int32 _giftsHash = 0;
	std::vector<Data::StarGift> _gifts;

	MTP::Sender _api;

};

class SponsoredToggle final {
public:
	explicit SponsoredToggle(not_null<Main::Session*> session);

	[[nodiscard]] rpl::producer<bool> toggled();
	[[nodiscard]] rpl::producer<rpl::no_value, QString> setToggled(bool);

private:
	MTP::Sender _api;

};

struct MessageMoneyRestriction {
	int starsPerMessage = 0;
	bool premiumRequired = false;
	bool known = false;

	explicit operator bool() const {
		return starsPerMessage != 0 || premiumRequired;
	}

	friend inline bool operator==(
		const MessageMoneyRestriction &,
		const MessageMoneyRestriction &) = default;
};
[[nodiscard]] MessageMoneyRestriction ResolveMessageMoneyRestrictions(
	not_null<PeerData*> peer,
	History *maybeHistory);

[[nodiscard]] rpl::producer<DocumentData*> RandomHelloStickerValue(
	not_null<Main::Session*> session);

[[nodiscard]] std::optional<Data::StarGift> FromTL(
	not_null<Main::Session*> session,
	const MTPstarGift &gift);
[[nodiscard]] std::optional<Data::SavedStarGift> FromTL(
	not_null<PeerData*> to,
	const MTPsavedStarGift &gift);

[[nodiscard]] Data::UniqueGiftModel FromTL(
	not_null<Main::Session*> session,
	const MTPDstarGiftAttributeModel &data);
[[nodiscard]] Data::UniqueGiftPattern FromTL(
	not_null<Main::Session*> session,
	const MTPDstarGiftAttributePattern &data);
[[nodiscard]] Data::UniqueGiftBackdrop FromTL(
	const MTPDstarGiftAttributeBackdrop &data);
[[nodiscard]] Data::UniqueGiftOriginalDetails FromTL(
	not_null<Main::Session*> session,
	const MTPDstarGiftAttributeOriginalDetails &data);

} // namespace Api
