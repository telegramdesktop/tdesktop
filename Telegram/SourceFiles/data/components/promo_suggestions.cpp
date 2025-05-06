/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/components/promo_suggestions.h"

#include "apiwrap.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_changes.h"
#include "data/data_histories.h"
#include "data/data_session.h"
#include "history/history.h"
#include "main/main_session.h"

namespace Data {
namespace {

constexpr auto kTopPromotionInterval = TimeId(60 * 60);
constexpr auto kTopPromotionMinDelay = TimeId(10);

} // namespace

PromoSuggestions::PromoSuggestions(not_null<Main::Session*> session)
: _session(session)
, _topPromotionTimer([=] { refreshTopPromotion(); }) {
	Core::App().settings().proxy().connectionTypeValue(
	) | rpl::start_with_next([=] {
		refreshTopPromotion();
	}, _lifetime);
}

PromoSuggestions::~PromoSuggestions() = default;

void PromoSuggestions::refreshTopPromotion() {
	const auto now = base::unixtime::now();
	const auto next = (_topPromotionNextRequestTime != 0)
		? _topPromotionNextRequestTime
		: now;
	if (_topPromotionRequestId) {
		getTopPromotionDelayed(now, next);
		return;
	}
	const auto key = [&]() -> std::pair<QString, uint32> {
		if (!Core::App().settings().proxy().isEnabled()) {
			return {};
		}
		const auto &proxy = Core::App().settings().proxy().selected();
		if (proxy.type != MTP::ProxyData::Type::Mtproto) {
			return {};
		}
		return { proxy.host, proxy.port };
	}();
	if (_topPromotionKey == key && now < next) {
		getTopPromotionDelayed(now, next);
		return;
	}
	_topPromotionKey = key;
	_topPromotionRequestId = _session->api().request(MTPhelp_GetPromoData(
	)).done([=](const MTPhelp_PromoData &result) {
		_topPromotionRequestId = 0;
		topPromotionDone(result);
	}).fail([=] {
		_topPromotionRequestId = 0;
		const auto now = base::unixtime::now();
		const auto next = _topPromotionNextRequestTime = now
			+ kTopPromotionInterval;
		if (!_topPromotionTimer.isActive()) {
			getTopPromotionDelayed(now, next);
		}
	}).send();
}

void PromoSuggestions::getTopPromotionDelayed(TimeId now, TimeId next) {
	_topPromotionTimer.callOnce(std::min(
		std::max(next - now, kTopPromotionMinDelay),
		kTopPromotionInterval) * crl::time(1000));
};

void PromoSuggestions::topPromotionDone(const MTPhelp_PromoData &proxy) {
	_topPromotionNextRequestTime = proxy.match([&](const auto &data) {
		return data.vexpires().v;
	});
	getTopPromotionDelayed(
		base::unixtime::now(),
		_topPromotionNextRequestTime);

	proxy.match([&](const MTPDhelp_promoDataEmpty &data) {
		setTopPromoted(nullptr, QString(), QString());
	}, [&](const MTPDhelp_promoData &data) {
		_session->data().processChats(data.vchats());
		_session->data().processUsers(data.vusers());

		auto changedPendingSuggestions = false;
		auto pendingSuggestions = ranges::views::all(
			data.vpending_suggestions().v
		) | ranges::views::transform([](const auto &suggestion) {
			return qs(suggestion);
		}) | ranges::to_vector;
		if (!ranges::equal(_pendingSuggestions, pendingSuggestions)) {
			_pendingSuggestions = std::move(pendingSuggestions);
			changedPendingSuggestions = true;
		}

		auto changedDismissedSuggestions = false;
		for (const auto &suggestion : data.vdismissed_suggestions().v) {
			changedDismissedSuggestions
				|= _dismissedSuggestions.emplace(qs(suggestion)).second;
		}

		if (changedPendingSuggestions || changedDismissedSuggestions) {
			_refreshed.fire({});
		}

		if (const auto peer = data.vpeer()) {
			const auto peerId = peerFromMTP(*peer);
			const auto history = _session->data().history(peerId);
			setTopPromoted(
				history,
				data.vpsa_type().value_or_empty(),
				data.vpsa_message().value_or_empty());
		} else {
			setTopPromoted(nullptr, QString(), QString());
		}
	});
}

rpl::producer<> PromoSuggestions::value() const {
	return _refreshed.events_starting_with({});
}

void PromoSuggestions::setTopPromoted(
		History *promoted,
		const QString &type,
		const QString &message) {
	const auto changed = (_topPromoted != promoted);
	if (!changed
		&& (!promoted || promoted->topPromotionMessage() == message)) {
		return;
	}
	if (changed) {
		if (_topPromoted) {
			_topPromoted->cacheTopPromotion(false, QString(), QString());
		}
	}
	const auto old = std::exchange(_topPromoted, promoted);
	if (_topPromoted) {
		_session->data().histories().requestDialogEntry(_topPromoted);
		_topPromoted->cacheTopPromotion(true, type, message);
		_topPromoted->requestChatListMessage();
		_session->changes().historyUpdated(
			_topPromoted,
			HistoryUpdate::Flag::TopPromoted);
	}
	if (changed && old) {
		_session->changes().historyUpdated(
			old,
			HistoryUpdate::Flag::TopPromoted);
	}
}

bool PromoSuggestions::current(const QString &key) const {
	if (key == u"BIRTHDAY_CONTACTS_TODAY"_q) {
		if (_dismissedSuggestions.contains(key)) {
			return false;
		} else {
			const auto known
				= _session->data().knownBirthdaysToday();
			if (!known) {
				return true;
			}
			return !known->empty();
		}
	}
	return !_dismissedSuggestions.contains(key)
		&& ranges::contains(_pendingSuggestions, key);
}

rpl::producer<> PromoSuggestions::requested(const QString &key) const {
	return value() | rpl::filter([=] { return current(key); });
}

void PromoSuggestions::dismiss(const QString &key) {
	if (!_dismissedSuggestions.emplace(key).second) {
		return;
	}
	_session->api().request(MTPhelp_DismissSuggestion(
		MTP_inputPeerEmpty(),
		MTP_string(key)
	)).send();
}

} // namespace Data
