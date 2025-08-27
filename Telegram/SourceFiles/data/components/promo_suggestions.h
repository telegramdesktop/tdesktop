/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"

class History;

namespace Main {
class Session;
} // namespace Main

namespace Data {

struct CustomSuggestion final {
	QString suggestion;
	TextWithEntities title;
	TextWithEntities description;
	QString url;

	friend inline auto operator<=>(
		const CustomSuggestion &,
		const CustomSuggestion &) = default;
};

class PromoSuggestions final {
public:
	explicit PromoSuggestions(not_null<Main::Session*> session);
	~PromoSuggestions();

	[[nodiscard]] bool current(const QString &key) const;
	[[nodiscard]] std::optional<CustomSuggestion> custom() const;
	[[nodiscard]] rpl::producer<> requested(const QString &key) const;
	void dismiss(const QString &key);

	void refreshTopPromotion();

	void invalidate();

	rpl::producer<> value() const;
	// Create rpl::producer<> refreshed() const; on memand.

	void requestContactBirthdays(Fn<void()> done, bool force = false);
	[[nodiscard]] auto knownContactBirthdays() const
		-> std::optional<std::vector<UserId>>;
	[[nodiscard]] auto knownBirthdaysToday() const
		-> std::optional<std::vector<UserId>>;

	[[nodiscard]] static QString SugValidatePassword();

private:
	void setTopPromoted(
		History *promoted,
		const QString &type,
		const QString &message);

	void topPromotionDelayed(TimeId now, TimeId next);

	const not_null<Main::Session*> _session;
	base::flat_set<QString> _dismissedSuggestions;
	std::vector<QString> _pendingSuggestions;
	std::optional<CustomSuggestion> _custom;

	History *_topPromoted = nullptr;

	mtpRequestId _contactBirthdaysRequestId = 0;
	int _contactBirthdaysLastDayRequest = -1;
	std::vector<UserId> _contactBirthdays;
	std::vector<UserId> _contactBirthdaysToday;

	mtpRequestId _topPromotionRequestId = 0;
	std::pair<QString, uint32> _topPromotionKey;
	TimeId _topPromotionNextRequestTime = TimeId(0);
	base::Timer _topPromotionTimer;

	rpl::event_stream<> _refreshed;

	rpl::lifetime _lifetime;

};

} // namespace Data
