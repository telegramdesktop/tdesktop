/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_authorizations.h"

#include "apiwrap.h"
#include "base/unixtime.h"
#include "core/changelogs.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "lang/lang_keys.h"

namespace Api {
namespace {

constexpr auto TestApiId = 17349;
constexpr auto SnapApiId = 611335;
constexpr auto DesktopApiId = 2040;

Authorizations::Entry ParseEntry(const MTPDauthorization &data) {
	auto result = Authorizations::Entry();

	result.hash = data.is_current() ? 0 : data.vhash().v;
	result.incomplete = data.is_password_pending();

	const auto apiId = result.apiId = data.vapi_id().v;
	const auto isTest = (apiId == TestApiId);
	const auto isDesktop = (apiId == DesktopApiId)
		|| (apiId == SnapApiId)
		|| isTest;

	const auto appName = isDesktop
		? u"Telegram Desktop%1"_q.arg(isTest ? " (GitHub)" : QString())
		: qs(data.vapp_name());// + u" for "_q + qs(d.vplatform());
	const auto appVer = [&] {
		const auto version = qs(data.vapp_version());
		if (isDesktop) {
			const auto verInt = version.toInt();
			if (version == QString::number(verInt)) {
				return Core::FormatVersionDisplay(verInt);
			}
		} else {
			if (const auto index = version.indexOf('('); index >= 0) {
				return version.mid(index);
			}
		}
		return version;
	}();

	result.name = result.hash
		? qs(data.vdevice_model())
		: Core::App().settings().deviceModel();

	const auto country = qs(data.vcountry());
	//const auto platform = qs(data.vplatform());
	//const auto &countries = countriesByISO2();
	//const auto j = countries.constFind(country);
	//if (j != countries.cend()) {
	//	country = QString::fromUtf8(j.value()->name);
	//}
	result.system = qs(data.vsystem_version());
	result.platform = qs(data.vplatform());
	result.activeTime = data.vdate_active().v
		? data.vdate_active().v
		: data.vdate_created().v;
	result.info = QString("%1%2").arg(
		appName,
		appVer.isEmpty() ? QString() : (' ' + appVer));
	result.ip = qs(data.vip());
	if (!result.hash) {
		result.active = tr::lng_status_online(tr::now);
	} else {
		const auto now = QDateTime::currentDateTime();
		const auto lastTime = base::unixtime::parse(result.activeTime);
		const auto nowDate = now.date();
		const auto lastDate = lastTime.date();
		if (lastDate == nowDate) {
			result.active = QLocale().toString(lastTime, cTimeFormat());
		} else if (lastDate.year() == nowDate.year()
			&& lastDate.weekNumber() == nowDate.weekNumber()) {
			result.active = langDayOfWeek(lastDate);
		} else {
			result.active = QLocale().toString(lastDate, cDateFormat());
		}
	}
	result.location = country;

	return result;
}

} // namespace

Authorizations::Authorizations(not_null<ApiWrap*> api)
: _api(&api->instance()) {
	Core::App().settings().deviceModelChanges(
	) | rpl::start_with_next([=](const QString &model) {
		auto changed = false;
		for (auto &entry : _list) {
			if (!entry.hash) {
				entry.name = model;
				changed = true;
			}
		}
		if (changed) {
			_listChanges.fire({});
		}
	}, _lifetime);

	if (Core::App().settings().disableCallsLegacy()) {
		toggleCallsDisabledHere(true);
	}
}

void Authorizations::reload() {
	if (_requestId) {
		return;
	}

	_requestId = _api.request(MTPaccount_GetAuthorizations(
	)).done([=](const MTPaccount_Authorizations &result) {
		_requestId = 0;
		_lastReceived = crl::now();
		result.match([&](const MTPDaccount_authorizations &auths) {
			_ttlDays = auths.vauthorization_ttl_days().v;
			_list = (
				auths.vauthorizations().v
			) | ranges::views::transform([](const MTPAuthorization &d) {
				return ParseEntry(d.c_authorization());
			}) | ranges::to<List>;
			_listChanges.fire({});
		});
	}).fail([=] {
		_requestId = 0;
	}).send();
}

void Authorizations::cancelCurrentRequest() {
	_api.request(base::take(_requestId)).cancel();
}

void Authorizations::requestTerminate(
		Fn<void(const MTPBool &result)> &&done,
		Fn<void(const MTP::Error &error)> &&fail,
		std::optional<uint64> hash) {
	const auto send = [&](auto request) {
		_api.request(
			std::move(request)
		).done([=, done = std::move(done)](const MTPBool &result) {
			done(result);
			if (mtpIsTrue(result)) {
				if (hash) {
					_list.erase(
						ranges::remove(_list, *hash, &Entry::hash),
						end(_list));
				} else {
					_list.clear();
				}
				_listChanges.fire({});
			}
		}).fail(
			std::move(fail)
		).send();
	};
	if (hash) {
		send(MTPaccount_ResetAuthorization(MTP_long(*hash)));
	} else {
		send(MTPauth_ResetAuthorizations());
	}
}

Authorizations::List Authorizations::list() const {
	return _list;
}

auto Authorizations::listChanges() const
-> rpl::producer<Authorizations::List> {
	return rpl::single(
		list()
	) | rpl::then(
		_listChanges.events() | rpl::map([=] { return list(); }));
}

rpl::producer<int> Authorizations::totalChanges() const {
	return rpl::single(
		total()
	) | rpl::then(
		_listChanges.events() | rpl::map([=] { return total(); }));
}

void Authorizations::updateTTL(int days) {
	_api.request(_ttlRequestId).cancel();
	_ttlRequestId = _api.request(MTPaccount_SetAuthorizationTTL(
		MTP_int(days)
	)).done([=] {
		_ttlRequestId = 0;
	}).fail([=] {
		_ttlRequestId = 0;
	}).send();
	_ttlDays = days;
}

rpl::producer<int> Authorizations::ttlDays() const {
	return _ttlDays.value() | rpl::filter(rpl::mappers::_1 != 0);
}

void Authorizations::toggleCallsDisabled(uint64 hash, bool disabled) {
	if (const auto sent = _toggleCallsDisabledRequests.take(hash)) {
		_api.request(*sent).cancel();
	}
	using Flag = MTPaccount_ChangeAuthorizationSettings::Flag;
	const auto id = _api.request(MTPaccount_ChangeAuthorizationSettings(
		MTP_flags(Flag::f_call_requests_disabled),
		MTP_long(hash),
		MTPBool(), // encrypted_requests_disabled
		MTP_bool(disabled)
	)).done([=] {
		_toggleCallsDisabledRequests.remove(hash);
	}).fail([=] {
		_toggleCallsDisabledRequests.remove(hash);
	}).send();
	_toggleCallsDisabledRequests.emplace(hash, id);
	if (!hash) {
		_callsDisabledHere = disabled;
	}
}

bool Authorizations::callsDisabledHere() const {
	return _callsDisabledHere.current();
}

rpl::producer<bool> Authorizations::callsDisabledHereValue() const {
	return _callsDisabledHere.value();
}

rpl::producer<bool> Authorizations::callsDisabledHereChanges() const {
	return _callsDisabledHere.changes();
}

int Authorizations::total() const {
	return ranges::count_if(
		_list,
		ranges::not_fn(&Entry::incomplete));
}

crl::time Authorizations::lastReceivedTime() {
	return _lastReceived;
}

} // namespace Api
