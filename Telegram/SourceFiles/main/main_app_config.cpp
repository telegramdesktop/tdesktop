/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "main/main_app_config.h"

#include "main/main_account.h"
#include "base/call_delayed.h"
#include "apiwrap.h"

namespace Main {
namespace {

constexpr auto kRefreshTimeout = 3600 * crl::time(1000);

} // namespace

AppConfig::AppConfig(not_null<Account*> account) : _account(account) {
	account->mtpValue(
	) | rpl::start_with_next([=](not_null<MTP::Instance*> instance) {
		_api.emplace(instance);
		refresh();
	}, _lifetime);

	account->sessionChanges(
	) | rpl::filter([=](Session *session) {
		return (session != nullptr);
	}) | rpl::start_with_next([=] {
		refresh();
	}, _lifetime);
}

void AppConfig::refresh() {
	if (_requestId || !_api) {
		return;
	}
	_requestId = _api->request(MTPhelp_GetAppConfig(
	)).done([=](const MTPJSONValue &result) {
		_requestId = 0;
		refreshDelayed();
		if (result.type() == mtpc_jsonObject) {
			_data.clear();
			for (const auto &element : result.c_jsonObject().vvalue().v) {
				element.match([&](const MTPDjsonObjectValue &data) {
					_data.emplace_or_assign(qs(data.vkey()), data.vvalue());
				});
			}
			DEBUG_LOG(("getAppConfig result handled."));
		}
		_refreshed.fire({});
	}).fail([=](const MTP::Error &error) {
		_requestId = 0;
		refreshDelayed();
	}).send();
}

void AppConfig::refreshDelayed() {
	base::call_delayed(kRefreshTimeout, _account, [=] {
		refresh();
	});
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

std::vector<std::map<QString, QString>> AppConfig::getStringMapArray(
		const QString &key,
		std::vector<std::map<QString, QString>> &&fallback) const {
	return getValue(key, [&](const MTPJSONValue &value) {
		return value.match([&](const MTPDjsonArray &data) {
			auto result = std::vector<std::map<QString, QString>>();
			result.reserve(data.vvalue().v.size());
			for (const auto &entry : data.vvalue().v) {
				if (entry.type() != mtpc_jsonObject) {
					return std::move(fallback);
				}
				auto element = std::map<QString, QString>();
				for (const auto &field : entry.c_jsonObject().vvalue().v) {
					const auto &data = field.c_jsonObjectValue();
					if (data.vvalue().type() != mtpc_jsonString) {
						return std::move(fallback);
					}
					element.emplace(
						qs(data.vkey()),
						qs(data.vvalue().c_jsonString().vvalue()));
				}
				result.push_back(std::move(element));
			}
			return result;
		}, [&](const auto &data) {
			return std::move(fallback);
		});
	});
}

bool AppConfig::suggestionCurrent(const QString &key) const {
	return !_dismissedSuggestions.contains(key)
		&& ranges::contains(
			get<std::vector<QString>>(
				u"pending_suggestions"_q,
				std::vector<QString>()),
			key);
}

rpl::producer<> AppConfig::suggestionRequested(const QString &key) const {
	return value(
	) | rpl::filter([=] {
		return suggestionCurrent(key);
	});
}

void AppConfig::dismissSuggestion(const QString &key) {
	if (!_dismissedSuggestions.emplace(key).second) {
		return;
	}
	_api->request(MTPhelp_DismissSuggestion(
		MTP_inputPeerEmpty(),
		MTP_string(key)
	)).send();
}

} // namespace Main
