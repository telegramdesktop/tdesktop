/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/current_geo_location.h"

#include "base/platform/base_platform_info.h"
#include "base/invoke_queued.h"
#include "base/timer.h"
#include "data/raw/raw_countries_bounds.h"
#include "platform/platform_current_geo_location.h"
#include "ui/ui_utility.h"

#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtCore/QCoreApplication>
#include <QtCore/QPointer>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>

namespace Core {
namespace {

constexpr auto kDestroyManagerTimeout = 20 * crl::time(1000);

[[nodiscard]] QString ChooseLanguage(const QString &language) {
	// https://docs.mapbox.com/api/search/geocoding#language-coverage
	auto result = language.toLower().replace('-', '_');
	static const auto kGood = std::array{
		// Global coverage.
		u"de"_q, u"en"_q, u"es"_q, u"fr"_q, u"it"_q, u"nl"_q, u"pl"_q,

		// Local coverage.
		u"az"_q, u"bn"_q, u"ca"_q, u"cs"_q, u"da"_q, u"el"_q, u"fa"_q,
		u"fi"_q, u"ga"_q, u"hu"_q, u"id"_q, u"is"_q, u"ja"_q, u"ka"_q,
		u"km"_q, u"ko"_q, u"lt"_q, u"lv"_q, u"mn"_q, u"pt"_q, u"ro"_q,
		u"sk"_q, u"sq"_q, u"sv"_q, u"th"_q, u"tl"_q, u"uk"_q, u"vi"_q,
		u"zh"_q, u"zh_Hans"_q, u"zh_TW"_q,

		// Limited coverage.
		u"ar"_q, u"bs"_q, u"gu"_q, u"he"_q, u"hi"_q, u"kk"_q, u"lo"_q,
		u"my"_q, u"nb"_q, u"ru"_q, u"sr"_q, u"te"_q, u"tk"_q, u"tr"_q,
		u"zh_Hant"_q,
	};
	for (const auto &known : kGood) {
		if (known.toLower() == result) {
			return known;
		}
	}
	if (const auto delimeter = result.indexOf('_'); delimeter > 0) {
		result = result.mid(0, delimeter);
		for (const auto &known : kGood) {
			if (known == result) {
				return known;
			}
		}
	}
	return u"en"_q;
}

void ResolveLocationAddressGeneric(
		const GeoLocation &location,
		const QString &language,
		const QString &token,
		Fn<void(GeoAddress)> callback) {
	const auto partialUrl = u"https://api.mapbox.com/search/geocode/v6"
		"/reverse?longitude=%1&latitude=%2&language=%3&access_token=%4"_q
		.arg(location.point.y())
		.arg(location.point.x())
		.arg(ChooseLanguage(language));
	static auto Cache = base::flat_map<QString, GeoAddress>();
	const auto i = Cache.find(partialUrl);
	if (i != end(Cache)) {
		callback(i->second);
		return;
	}
	const auto finishWith = [=](GeoAddress result) {
		Cache[partialUrl] = result;
		callback(result);
	};

	struct State final : QObject {
		explicit State(QObject *parent)
		: QObject(parent)
		, manager(this)
		, destroyer([=] { if (sent.empty()) delete this; }) {
		}

		QNetworkAccessManager manager;
		std::vector<QPointer<QNetworkReply>> sent;
		base::Timer destroyer;
	};

	static auto state = QPointer<State>();
	if (!state) {
		state = Ui::CreateChild<State>(qApp);
	}
	const auto destroyReplyDelayed = [](QNetworkReply *reply) {
		InvokeQueued(reply, [=] {
			for (auto i = begin(state->sent); i != end(state->sent);) {
				if (!*i || *i == reply) {
					i = state->sent.erase(i);
				} else {
					++i;
				}
			}
			delete reply;
			if (state->sent.empty()) {
				state->destroyer.callOnce(kDestroyManagerTimeout);
			}
		});
	};

	auto request = QNetworkRequest(partialUrl.arg(token));
	request.setRawHeader("Referer", "http://desktop-app-resource/");

	const auto reply = state->manager.get(request);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		destroyReplyDelayed(reply);

		const auto json = QJsonDocument::fromJson(reply->readAll());
		if (!json.isObject()) {
			finishWith({});
			return;
		}
		const auto features = json["features"].toArray();
		if (features.isEmpty()) {
			finishWith({});
			return;
		}
		const auto feature = features.at(0).toObject();
		const auto properties = feature["properties"].toObject();
		const auto context = properties["context"].toObject();
		auto names = QStringList();
		auto add = [&](std::vector<QString> keys) {
			for (const auto &key : keys) {
				const auto value = context[key];
				if (value.isObject()) {
					const auto name = value.toObject()["name"].toString();
					if (!name.isEmpty()) {
						names.push_back(name);
						break;
					}
				}
			}
		};
		add({ /*u"address"_q, u"street"_q, */u"neighborhood"_q });
		add({ u"place"_q, u"region"_q });
		add({ u"country"_q });
		finishWith({ .name = names.join(", ") });
	});
	QObject::connect(reply, &QNetworkReply::errorOccurred, [=] {
		destroyReplyDelayed(reply);

		finishWith({});
	});
}

} // namespace

GeoLocation ResolveCurrentCountryLocation() {
	const auto iso2 = Platform::SystemCountry().toUpper();
	const auto &bounds = Raw::CountryBounds();
	const auto i = bounds.find(iso2);
	if (i == end(bounds)) {
		return {
			.accuracy = GeoLocationAccuracy::Failed,
		};
	}
	return {
		.point = {
			(i->second.minLat + i->second.maxLat) / 2.,
			(i->second.minLon + i->second.maxLon) / 2.,
		},
		.bounds = {
			i->second.minLat,
			i->second.minLon,
			i->second.maxLat - i->second.minLat,
			i->second.maxLon - i->second.minLon,
		},
		.accuracy = GeoLocationAccuracy::Country,
	};
}

void ResolveCurrentGeoLocation(Fn<void(GeoLocation)> callback) {
	using namespace Platform;
	return ResolveCurrentExactLocation([done = std::move(callback)](
			GeoLocation result) {
		done(result.accuracy != GeoLocationAccuracy::Failed
			? result
			: ResolveCurrentCountryLocation());
	});
}

void ResolveLocationAddress(
		const GeoLocation &location,
		const QString &language,
		const QString &token,
		Fn<void(GeoAddress)> callback) {
	auto done = [=, done = std::move(callback)](GeoAddress result) mutable {
		if (!result && !token.isEmpty()) {
			ResolveLocationAddressGeneric(
				location,
				language,
				token,
				std::move(done));
		} else {
			done(result);
		}
	};
	Platform::ResolveLocationAddress(location, language, std::move(done));
}

bool AreTheSame(const GeoLocation &a, const GeoLocation &b) {
	if (a.accuracy != GeoLocationAccuracy::Exact
		|| b.accuracy != GeoLocationAccuracy::Exact) {
		return false;
	}
	const auto normalize = [](float64 value) {
		value = std::fmod(value + 180., 360.);
		return (value + (value < 0. ? 360. : 0.)) - 180.;
	};
	constexpr auto kEpsilon = 0.0001;
	const auto lon1 = normalize(a.point.y());
	const auto lon2 = normalize(b.point.y());
	const auto diffLat = std::abs(a.point.x() - b.point.x());
	if (std::abs(a.point.x()) >= (90. - kEpsilon)
		|| std::abs(b.point.x()) >= (90. - kEpsilon)) {
		return diffLat <= kEpsilon;
	}
	auto diffLon = std::abs(lon1 - lon2);
	if (diffLon > 180.) {
		diffLon = 360. - diffLon;
	}

	return diffLat <= kEpsilon && diffLon <= kEpsilon;
}

} // namespace Core
