/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/components/promo_suggestions.h"

#include "api/api_text_entities.h"
#include "apiwrap.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_changes.h"
#include "data/data_histories.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "main/main_session.h"

namespace Data {
namespace {

using UserIds = std::vector<UserId>;

constexpr auto kTopPromotionInterval = TimeId(60 * 60);
constexpr auto kTopPromotionMinDelay = TimeId(10);

[[nodiscard]] CustomSuggestion CustomFromTL(
		not_null<Main::Session*> session,
		const MTPPendingSuggestion &r) {
	return CustomSuggestion({
		.suggestion = qs(r.data().vsuggestion()),
		.title = Api::ParseTextWithEntities(session, r.data().vtitle()),
		.description = Api::ParseTextWithEntities(
			session,
			r.data().vdescription()),
		.url = qs(r.data().vurl()),
	});
}

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
		topPromotionDelayed(now, next);
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
		topPromotionDelayed(now, next);
		return;
	}
	_topPromotionKey = key;
	_topPromotionRequestId = _session->api().request(MTPhelp_GetPromoData(
	)).done([=](const MTPhelp_PromoData &result) {
		_topPromotionRequestId = 0;

		_topPromotionNextRequestTime = result.match([&](const auto &data) {
			return data.vexpires().v;
		});
		topPromotionDelayed(
			base::unixtime::now(),
			_topPromotionNextRequestTime);

		result.match([&](const MTPDhelp_promoDataEmpty &data) {
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

			auto changedCustom = false;
			auto custom = data.vcustom_pending_suggestion()
				? std::make_optional(
					CustomFromTL(
						_session,
						*data.vcustom_pending_suggestion()))
				: std::nullopt;
			if (_custom != custom) {
				_custom = std::move(custom);
				changedCustom = true;
			}

			const auto changedContactBirthdaysLastDayRequest =
				_contactBirthdaysLastDayRequest != -1
					&& _contactBirthdaysLastDayRequest
						!= QDate::currentDate().day();

			if (changedPendingSuggestions
				|| changedDismissedSuggestions
				|| changedCustom
				|| changedContactBirthdaysLastDayRequest) {
				_refreshed.fire({});
			}
		});
	}).fail([=] {
		_topPromotionRequestId = 0;
		const auto now = base::unixtime::now();
		const auto next = _topPromotionNextRequestTime = now
			+ kTopPromotionInterval;
		if (!_topPromotionTimer.isActive()) {
			topPromotionDelayed(now, next);
		}
	}).send();
}

void PromoSuggestions::topPromotionDelayed(TimeId now, TimeId next) {
	_topPromotionTimer.callOnce(std::min(
		std::max(next - now, kTopPromotionMinDelay),
		kTopPromotionInterval) * crl::time(1000));
};

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
				= PromoSuggestions::knownBirthdaysToday();
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

void PromoSuggestions::invalidate() {
	if (_topPromotionRequestId) {
		_session->api().request(_topPromotionRequestId).cancel();
	}
	_topPromotionNextRequestTime = 0;
	_topPromotionTimer.callOnce(crl::time(200));
}

std::optional<CustomSuggestion> PromoSuggestions::custom() const {
	return (_custom && !_dismissedSuggestions.contains(_custom->suggestion))
		? _custom
		: std::nullopt;
}

void PromoSuggestions::requestContactBirthdays(Fn<void()> done, bool force) {
	if ((_contactBirthdaysLastDayRequest != -1)
		&& (_contactBirthdaysLastDayRequest == QDate::currentDate().day())
		&& !force) {
		return done();
	}
	if (_contactBirthdaysRequestId) {
		_session->api().request(_contactBirthdaysRequestId).cancel();
	}
	_contactBirthdaysRequestId = _session->api().request(
		MTPcontacts_GetBirthdays()
	).done([=](const MTPcontacts_ContactBirthdays &result) {
		_contactBirthdaysRequestId = 0;
		_contactBirthdaysLastDayRequest = QDate::currentDate().day();
		auto users = UserIds();
		auto today = UserIds();
		_session->data().processUsers(result.data().vusers());
		for (const auto &tlContact : result.data().vcontacts().v) {
			const auto peerId = tlContact.data().vcontact_id().v;
			if (const auto user = _session->data().user(peerId)) {
				const auto &data = tlContact.data().vbirthday().data();
				user->setBirthday(Data::Birthday(
					data.vday().v,
					data.vmonth().v,
					data.vyear().value_or_empty()));
				if (user->isSelf()
					|| user->isInaccessible()
					|| user->isBlocked()) {
					continue;
				}
				if (Data::IsBirthdayToday(user->birthday())) {
					today.push_back(peerToUser(user->id));
				}
				users.push_back(peerToUser(user->id));
			}
		}
		_contactBirthdays = std::move(users);
		_contactBirthdaysToday = std::move(today);
		done();
	}).fail([=](const MTP::Error &error) {
		_contactBirthdaysRequestId = 0;
		_contactBirthdaysLastDayRequest = QDate::currentDate().day();
		_contactBirthdays = {};
		_contactBirthdaysToday = {};
		done();
	}).send();
}

std::optional<UserIds> PromoSuggestions::knownContactBirthdays() const {
	if ((_contactBirthdaysLastDayRequest == -1)
		|| (_contactBirthdaysLastDayRequest != QDate::currentDate().day())) {
		return std::nullopt;
	}
	return _contactBirthdays;
}

std::optional<UserIds> PromoSuggestions::knownBirthdaysToday() const {
	if ((_contactBirthdaysLastDayRequest == -1)
		|| (_contactBirthdaysLastDayRequest != QDate::currentDate().day())) {
		return std::nullopt;
	}
	return _contactBirthdaysToday;
}

QString PromoSuggestions::SugValidatePassword() {
	static const auto key = u"VALIDATE_PASSWORD"_q;
	return key;
}

} // namespace Data
