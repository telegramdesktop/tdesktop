/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/editor_weather.h"

#include "apiwrap.h"
#include "base/algorithm.h"
#include "base/platform/base_platform_info.h"
#include "base/weak_ptr.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/current_geo_location.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_app_config.h"
#include "main/main_session.h"

#include <QtCore/QDateTime>

namespace Editor {
namespace {

constexpr auto kCacheKeyScale = 1000;
constexpr auto kCacheHours = 3600;

struct WeatherCache {
	QString key;
	WeatherInfo value;
};

WeatherCache &Cache() {
	static auto result = WeatherCache();
	return result;
}

struct WeatherRequest : base::has_weak_ptr {
	QString key;
	QPointF point;
};

[[nodiscard]] std::optional<WeatherInfo> ParseWeatherInfo(
		const MTPmessages_BotResults &result) {
	const auto &list = result.data().vresults().v;
	if (list.isEmpty()) {
		return std::nullopt;
	}
	auto emoji = QString();
	auto celsius = 0.;
	auto ok = false;
	list.front().match([&](const MTPDbotInlineResult &data) {
		emoji = qs(data.vtitle().value_or_empty());
		celsius = qs(data.vdescription().value_or_empty()).toDouble(&ok);
	}, [&](const MTPDbotInlineMediaResult &data) {
		emoji = qs(data.vtitle().value_or_empty());
		celsius = qs(data.vdescription().value_or_empty()).toDouble(&ok);
	});
	if (!ok || emoji.isEmpty()) {
		return std::nullopt;
	}
	return WeatherInfo{ std::move(emoji), celsius };
}

void RequestInlineResults(
		not_null<Main::Session*> session,
		not_null<UserData*> bot,
		std::shared_ptr<WeatherRequest> state,
		Fn<void(WeatherInfo)> done,
		Fn<void()> fail) {
	session->api().request(MTPmessages_GetInlineBotResults(
		MTP_flags(MTPmessages_GetInlineBotResults::Flag::f_geo_point),
		bot->inputUser(),
		MTP_inputPeerEmpty(),
		MTP_inputGeoPoint(
			MTP_flags(0),
			MTP_double(state->point.x()),
			MTP_double(state->point.y()),
			MTP_int(0)),
		MTP_string(),
		MTP_string()
	)).done(crl::guard(state.get(), [=,
		done = std::move(done)](
				const MTPmessages_BotResults &result) mutable {
		auto info = ParseWeatherInfo(result);
		if (!info) {
			fail();
			return;
		}
		Cache() = { state->key, *info };
		done(std::move(*info));
	})).fail(crl::guard(state.get(), [=, fail = std::move(fail)] {
		fail();
	})).send();
}

void RequestWeatherBot(
		not_null<Main::Session*> session,
		std::shared_ptr<WeatherRequest> state,
		Fn<void(WeatherInfo)> done,
		Fn<void()> fail) {
	const auto username = session->appConfig().get<QString>(
		u"weather_search_username"_q,
		u"izweatherbot"_q);
	session->api().request(MTPcontacts_ResolveUsername(
		MTP_flags(0),
		MTP_string(username),
		MTP_string()
	)).done(crl::guard(state.get(), [=,
		done = std::move(done)](
				const MTPcontacts_ResolvedPeer &result) mutable {
		const auto &data = result.data();
		session->data().processUsers(data.vusers());
		session->data().processChats(data.vchats());
		const auto peer = session->data().peerLoaded(
			peerFromMTP(data.vpeer()));
		const auto user = peer ? peer->asUser() : nullptr;
		if (!user || !user->isBotInlineGeo()) {
			LOG(("API Error: Bad weather bot returned by: %1").arg(username));
			fail();
			return;
		}
		RequestInlineResults(
			session,
			user,
			std::move(state),
			std::move(done),
			std::move(fail));
	})).fail(crl::guard(state.get(), [=, fail = std::move(fail)] {
		fail();
	})).send();
}

} // namespace

std::optional<WeatherInfo> CachedWeather() {
	auto &cache = Cache();
	return cache.key.isEmpty()
		? std::nullopt
		: std::make_optional(cache.value);
}

bool WeatherInCelsius() {
	const auto saved = Core::App().settings().weatherInCelsius();
	return saved.value_or(!ranges::contains(
		std::array{ u"US"_q, u"BS"_q, u"KY"_q, u"LR"_q, u"BZ"_q },
		Platform::SystemCountry().toUpper()));
}

QString WeatherTemperatureText(float64 celsius) {
	const auto format = [](float64 value) {
		return QString::number(int(base::SafeRound(value)));
	};
	return WeatherInCelsius()
		? format(celsius) + QChar(0xb0) + QChar('C')
		: format(celsius * 9. / 5. + 32) + QChar(0xb0) + QChar('F');
}

QColor WeatherTextColor(QColor background) {
	const auto luminance = (0.2126 * background.redF())
		+ (0.7152 * background.greenF())
		+ (0.0722 * background.blueF());
	return (luminance > 0.705) ? QColor(0, 0, 0) : QColor(255, 255, 255);
}

void FetchWeather(
		not_null<Main::Session*> session,
		QPointF point,
		Fn<void(WeatherInfo)> done,
		Fn<void()> fail) {
	const auto hours = int(QDateTime::currentSecsSinceEpoch() / kCacheHours);
	const auto key = u"%1:%2at%3"_q
		.arg(int(base::SafeRound(point.x() * kCacheKeyScale)))
		.arg(int(base::SafeRound(point.y() * kCacheKeyScale)))
		.arg(hours);
	auto &cache = Cache();
	if (cache.key == key) {
		done(cache.value);
		return;
	}
	const auto state = std::make_shared<WeatherRequest>(
		WeatherRequest{ .key = key, .point = point });
	RequestWeatherBot(
		session,
		std::move(state),
		std::move(done),
		std::move(fail));
}

void FetchWeatherForCurrentLocation(
		not_null<Main::Session*> session,
		Fn<void(WeatherInfo)> done,
		Fn<void()> fail) {
	Core::ResolveCurrentGeoLocation(crl::guard(
		session,
		[=](Core::GeoLocation location) {
			if (!location) {
				fail();
				return;
			}
			FetchWeather(session, location.point, done, fail);
		}));
}

} // namespace Editor
