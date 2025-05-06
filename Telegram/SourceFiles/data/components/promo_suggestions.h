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

class PromoSuggestions final {
public:
	explicit PromoSuggestions(not_null<Main::Session*> session);
	~PromoSuggestions();

	[[nodiscard]] bool current(const QString &key) const;
	[[nodiscard]] rpl::producer<> requested(
		const QString &key) const;
	void dismiss(const QString &key);

	void refreshTopPromotion();

	rpl::producer<> value() const;
	// Create rpl::producer<> refreshed() const; on memand.

private:
	void setTopPromoted(
		History *promoted,
		const QString &type,
		const QString &message);

	void getTopPromotionDelayed(TimeId now, TimeId next);
	void topPromotionDone(const MTPhelp_PromoData &proxy);

	const not_null<Main::Session*> _session;
	base::flat_set<QString> _dismissedSuggestions;
	std::vector<QString> _pendingSuggestions;

	History *_topPromoted = nullptr;

	mtpRequestId _topPromotionRequestId = 0;
	std::pair<QString, uint32> _topPromotionKey;
	TimeId _topPromotionNextRequestTime = TimeId(0);
	base::Timer _topPromotionTimer;

	rpl::event_stream<> _refreshed;

	rpl::lifetime _lifetime;

};

} // namespace Data
