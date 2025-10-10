/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "main/main_app_config.h"

#include "api/api_authorizations.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "ui/chat/chat_style.h"

namespace Main {
namespace {

constexpr auto kRefreshTimeout = 3600 * crl::time(1000);

} // namespace

AppConfig::AppConfig(not_null<Account*> account) : _account(account) {
	account->sessionChanges(
	) | rpl::filter([=](Session *session) {
		return (session != nullptr);
	}) | rpl::start_with_next([=] {
		_lastFrozenRefresh = 0;
		refresh();
	}, _lifetime);
}

AppConfig::~AppConfig() = default;

void AppConfig::start() {
	_account->mtpMainSessionValue(
	) | rpl::start_with_next([=](not_null<MTP::Instance*> instance) {
		_api.emplace(instance);
		refresh();

		_frozenTrackLifetime = instance->frozenErrorReceived(
		) | rpl::start_with_next([=] {
			if (!get<int>(u"freeze_since_date"_q, 0)) {
				const auto now = crl::now();
				if (!_lastFrozenRefresh
					|| now > _lastFrozenRefresh + kRefreshTimeout) {
					_lastFrozenRefresh = now;
					refresh();
				}
			}
		});
	}, _lifetime);
}

int AppConfig::quoteLengthMax() const {
	return get<int>(u"quote_length_max"_q, 1024);
}

int AppConfig::stargiftConvertPeriodMax() const {
	return get<int>(
		u"stargifts_convert_period_max"_q,
		_account->mtp().isTestMode() ? 300 : (90 * 86400));
}

const std::vector<QString> &AppConfig::startRefPrefixes() {
	if (_startRefPrefixes.empty()) {
		_startRefPrefixes = get<std::vector<QString>>(
			u"starref_start_param_prefixes"_q,
			std::vector<QString>());
	}
	return _startRefPrefixes;
}

bool AppConfig::starrefSetupAllowed() const {
	return get<bool>(u"starref_program_allowed"_q, false);
}

bool AppConfig::starrefJoinAllowed() const {
	return get<bool>(u"starref_connect_allowed"_q, false);
}

int AppConfig::starrefCommissionMin() const {
	return get<int>(u"starref_min_commission_permille"_q, 1);
}

int AppConfig::starrefCommissionMax() const {
	return get<int>(u"starref_max_commission_permille"_q, 900);
}

int AppConfig::starsWithdrawMax() const {
	return get<int>(u"stars_revenue_withdrawal_max"_q, 100);
}

float64 AppConfig::starsWithdrawRate() const {
	return get<float64>(u"stars_usd_withdraw_rate_x1000"_q, 1300) / 1000.;
}

float64 AppConfig::currencyWithdrawRate() const {
	return get<float64>(u"ton_usd_rate"_q, 1);
}

bool AppConfig::paidMessagesAvailable() const {
	return get<bool>(u"stars_paid_messages_available"_q, false);
}

int AppConfig::paidMessageStarsMax() const {
	return get<int>(u"stars_paid_message_amount_max"_q, 10'000);
}

int AppConfig::paidMessageCommission() const {
	return get<int>(u"stars_paid_message_commission_permille"_q, 850);
}

int AppConfig::paidMessageChannelStarsDefault() const {
	return get<int>(u"stars_paid_messages_channel_amount_default"_q, 10);
}

int AppConfig::pinnedGiftsLimit() const {
	return get<int>(u"stargifts_pinned_to_top_limit"_q, 6);
}

int AppConfig::giftCollectionsLimit() const {
	return get<int>(u"stargifts_collections_limit"_q, 10);
}

int AppConfig::giftCollectionGiftsLimit() const {
	return get<int>(u"stargifts_collection_gifts_limit"_q, 500);
}

bool AppConfig::callsDisabledForSession() const {
	const auto authorizations = _account->sessionExists()
		? &_account->session().api().authorizations()
		: nullptr;
	return get<bool>(
		u"call_requests_disabled"_q,
		authorizations->callsDisabledHere());
}

int AppConfig::confcallSizeLimit() const {
	return get<int>(
		u"conference_call_size_limit"_q,
		_account->mtp().isTestMode() ? 5 : 100);
}

bool AppConfig::confcallPrioritizeVP8() const {
	return get<bool>(u"confcall_use_vp8"_q, false);
}

int AppConfig::giftResaleStarsMin() const {
	return get<int>(u"stars_stargift_resale_amount_min"_q, 125);
}

int AppConfig::giftResaleStarsMax() const {
	return get<int>(u"stars_stargift_resale_amount_max"_q, 35000);
}

int AppConfig::giftResaleStarsThousandths() const {
	return get<int>(u"stars_stargift_resale_commission_permille"_q, 800);
}

int64 AppConfig::giftResaleNanoTonMin() const {
	return get<int64>(u"ton_stargift_resale_amount_min"_q, 250'000'000LL);
}

int64 AppConfig::giftResaleNanoTonMax() const {
	return get<int64>(
		u"ton_stargift_resale_amount_max"_q,
		1'000'000'000'000'000LL);
}

int AppConfig::giftResaleNanoTonThousandths() const {
	return get<int>(u"ton_stargift_resale_commission_permille"_q, 800);
}

int AppConfig::pollOptionsLimit() const {
	return get<int>(u"poll_answers_max"_q, 12);
}

int AppConfig::todoListItemsLimit() const {
	return get<int>(
		u"todo_items_max"_q,
		_account->mtp().isTestMode() ? 10 : 30);
}

int AppConfig::todoListTitleLimit() const {
	return get<int>(u"todo_title_length_max"_q, 32);
}

int AppConfig::todoListItemTextLimit() const {
	return get<int>(u"todo_item_length_max"_q, 64);
}

int AppConfig::suggestedPostCommissionStars() const {
	return get<int>(u"stars_suggested_post_commission_permille"_q, 850);
}

int AppConfig::suggestedPostCommissionTon() const {
	return get<int>(u"ton_suggested_post_commission_permille"_q, 850);
}

int AppConfig::suggestedPostStarsMin() const {
	return get<int>(u"stars_suggested_post_amount_min"_q, 5);
}

int AppConfig::suggestedPostStarsMax() const {
	return get<int>(u"stars_suggested_post_amount_max"_q, 100'000);
}

int64 AppConfig::suggestedPostNanoTonMin() const {
	return get<int64>(u"ton_suggested_post_amount_min"_q, 10'000'000LL);
}

int64 AppConfig::suggestedPostNanoTonMax() const {
	return get<int64>(
		u"ton_suggested_post_amount_max"_q,
		10'000'000'000'000LL);
}

int AppConfig::suggestedPostDelayMin() const {
	return get<int>(u"stars_suggested_post_future_min"_q, 300);
}

int AppConfig::suggestedPostDelayMax() const {
	return get<int>(u"appConfig.stars_suggested_post_future_max"_q, 2678400);
}

TimeId AppConfig::suggestedPostAgeMin() const {
	return get<int>(u"stars_suggested_post_age_min"_q, 86400);
}

bool AppConfig::ageVerifyNeeded() const {
	return get<bool>(u"need_age_video_verification"_q, false);
}

QString AppConfig::ageVerifyCountry() const {
	return get<QString>(u"verify_age_country"_q, QString());
}

int AppConfig::ageVerifyMinAge() const {
	return get<int>(u"verify_age_min"_q, 18);
}

QString AppConfig::ageVerifyBotUsername() const {
	return get<QString>(u"verify_age_bot_username"_q, QString());
}

int AppConfig::storiesAlbumsLimit() const {
	return get<int>(u"stories_albums_limit"_q, 100);
}

int AppConfig::storiesAlbumLimit() const {
	return get<int>(u"stories_album_stories_limit"_q, 1000);
}

int AppConfig::groupCallMessageLengthLimit() const {
	return get<int>(u"group_call_message_length_limit"_q, 128);
}

TimeId AppConfig::groupCallMessageTTL() const {
	return get<int>(u"group_call_message_ttl"_q, 10);
}

void AppConfig::refresh(bool force) {
	if (_requestId || !_api) {
		if (force) {
			_pendingRefresh = true;
		}
		return;
	}
	_pendingRefresh = false;
	_requestId = _api->request(MTPhelp_GetAppConfig(
		MTP_int(_hash)
	)).done([=](const MTPhelp_AppConfig &result) {
		_requestId = 0;
		result.match([&](const MTPDhelp_appConfig &data) {
			_hash = data.vhash().v;

			const auto &config = data.vconfig();
			if (config.type() != mtpc_jsonObject) {
				LOG(("API Error: Unexpected config type."));
				return;
			}
			auto was = ignoredRestrictionReasons();

			_data.clear();
			for (const auto &element : config.c_jsonObject().vvalue().v) {
				element.match([&](const MTPDjsonObjectValue &data) {
					_data.emplace_or_assign(qs(data.vkey()), data.vvalue());
				});
			}
			updateIgnoredRestrictionReasons(std::move(was));

			DEBUG_LOG(("getAppConfig result handled."));
			_refreshed.fire({});
		}, [](const MTPDhelp_appConfigNotModified &) {});

		if (base::take(_pendingRefresh)) {
			refresh();
		} else {
			refreshDelayed();
		}
	}).fail([=] {
		_requestId = 0;
		refreshDelayed();
	}).send();
}

void AppConfig::refreshDelayed() {
	base::call_delayed(kRefreshTimeout, _account, [=] {
		refresh();
	});
}

void AppConfig::updateIgnoredRestrictionReasons(std::vector<QString> was) {
	_ignoreRestrictionReasons = get<std::vector<QString>>(
		u"ignore_restriction_reasons"_q,
		std::vector<QString>());
	ranges::sort(_ignoreRestrictionReasons);
	if (_ignoreRestrictionReasons != was) {
		for (const auto &reason : _ignoreRestrictionReasons) {
			const auto i = ranges::remove(was, reason);
			if (i != end(was)) {
				was.erase(i, end(was));
			} else {
				was.push_back(reason);
			}
		}
		_ignoreRestrictionChanges.fire(std::move(was));
	}
}

rpl::producer<> AppConfig::refreshed() const {
	return _refreshed.events();
}

rpl::producer<> AppConfig::value() const {
	return _refreshed.events_starting_with({});
}

template <typename Extractor>
auto AppConfig::getValue(const QString &key, Extractor &&extractor) const {
	const auto i = _data.find(key);
	return extractor((i != end(_data))
		? i->second
		: MTPJSONValue(MTP_jsonNull()));
}

bool AppConfig::getBool(const QString &key, bool fallback) const {
	return getValue(key, [&](const MTPJSONValue &value) {
		return value.match([&](const MTPDjsonBool &data) {
			return mtpIsTrue(data.vvalue());
		}, [&](const auto &data) {
			return fallback;
		});
	});
}

double AppConfig::getDouble(const QString &key, double fallback) const {
	return getValue(key, [&](const MTPJSONValue &value) {
		return value.match([&](const MTPDjsonNumber &data) {
			return data.vvalue().v;
		}, [&](const auto &data) {
			return fallback;
		});
	});
}

QString AppConfig::getString(
		const QString &key,
		const QString &fallback) const {
	return getValue(key, [&](const MTPJSONValue &value) {
		return value.match([&](const MTPDjsonString &data) {
			return qs(data.vvalue());
		}, [&](const auto &data) {
			return fallback;
		});
	});
}

std::vector<QString> AppConfig::getStringArray(
		const QString &key,
		std::vector<QString> &&fallback) const {
	return getValue(key, [&](const MTPJSONValue &value) {
		return value.match([&](const MTPDjsonArray &data) {
			auto result = std::vector<QString>();
			result.reserve(data.vvalue().v.size());
			for (const auto &entry : data.vvalue().v) {
				if (entry.type() != mtpc_jsonString) {
					return std::move(fallback);
				}
				result.push_back(qs(entry.c_jsonString().vvalue()));
			}
			return result;
		}, [&](const auto &data) {
			return std::move(fallback);
		});
	});
}

base::flat_map<QString, QString> AppConfig::getStringMap(
		const QString &key,
		base::flat_map<QString, QString> &&fallback) const {
	return getValue(key, [&](const MTPJSONValue &value) {
		return value.match([&](const MTPDjsonObject &data) {
			auto result = base::flat_map<QString, QString>();
			result.reserve(data.vvalue().v.size());
			for (const auto &entry : data.vvalue().v) {
				const auto &data = entry.data();
				const auto &value = data.vvalue();
				if (value.type() != mtpc_jsonString) {
					return std::move(fallback);
				}
				result.emplace(
					qs(data.vkey()),
					qs(value.c_jsonString().vvalue()));
			}
			return result;
		}, [&](const auto &data) {
			return std::move(fallback);
		});
	});
}

std::vector<int> AppConfig::getIntArray(
		const QString &key,
		std::vector<int> &&fallback) const {
	return getValue(key, [&](const MTPJSONValue &value) {
		return value.match([&](const MTPDjsonArray &data) {
			auto result = std::vector<int>();
			result.reserve(data.vvalue().v.size());
			for (const auto &entry : data.vvalue().v) {
				if (entry.type() != mtpc_jsonNumber) {
					return std::move(fallback);
				}
				result.push_back(
					int(base::SafeRound(entry.c_jsonNumber().vvalue().v)));
			}
			return result;
		}, [&](const auto &data) {
			return std::move(fallback);
		});
	});
}

bool AppConfig::newRequirePremiumFree() const {
	return get<bool>(
		u"new_noncontact_peers_require_premium_without_ownpremium"_q,
		false);
}

} // namespace Main
