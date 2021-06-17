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
#include "lang/lang_keys.h"

namespace Api {
namespace {

constexpr auto TestApiId = 17349;
constexpr auto DesktopApiId = 2040;

Authorizations::Entry ParseEntry(const MTPDauthorization &data) {
	auto result = Authorizations::Entry();

	result.hash = data.is_current() ? 0 : data.vhash().v;
	result.incomplete = data.is_password_pending();

	const auto apiId = data.vapi_id().v;
	const auto isTest = (apiId == TestApiId);
	const auto isDesktop = (apiId == DesktopApiId) || isTest;

	const auto appName = isDesktop
		? QString("Telegram Desktop%1").arg(isTest ? " (GitHub)" : QString())
		: qs(data.vapp_name());// +qsl(" for ") + qs(d.vplatform());
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

	result.name = QString("%1%2").arg(
		appName,
		appVer.isEmpty() ? QString() : (' ' + appVer));

	const auto country = qs(data.vcountry());
	const auto platform = qs(data.vplatform());
	//const auto &countries = countriesByISO2();
	//const auto j = countries.constFind(country);
	//if (j != countries.cend()) {
	//	country = QString::fromUtf8(j.value()->name);
	//}

	result.activeTime = data.vdate_active().v
		? data.vdate_active().v
		: data.vdate_created().v;
	result.info = QString("%1, %2%3").arg(
		qs(data.vdevice_model()),
		platform.isEmpty() ? QString() : platform + ' ',
		qs(data.vsystem_version()));
	result.ip = qs(data.vip())
		+ (country.isEmpty()
			? QString()
			: QString::fromUtf8(" \xe2\x80\x93 ") + country);
	if (!result.hash) {
		result.active = tr::lng_status_online(tr::now);
	} else {
		const auto now = QDateTime::currentDateTime();
		const auto lastTime = base::unixtime::parse(result.activeTime);
		const auto nowDate = now.date();
		const auto lastDate = lastTime.date();
		if (lastDate == nowDate) {
			result.active = lastTime.toString(cTimeFormat());
		} else if (lastDate.year() == nowDate.year()
			&& lastDate.weekNumber() == nowDate.weekNumber()) {
			result.active = langDayOfWeek(lastDate);
		} else {
			result.active = lastDate.toString(qsl("d.MM.yy"));
		}
	}

	return result;
}

} // namespace

Authorizations::Authorizations(not_null<ApiWrap*> api)
: _api(&api->instance()) {
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
			_list = (
				auths.vauthorizations().v
			) | ranges::views::transform([](const MTPAuthorization &d) {
				return ParseEntry(d.c_authorization());
			}) | ranges::to<List>;
			_listChanges.fire({});
		});
	}).fail([=](const MTP::Error &error) {
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

int Authorizations::total() const {
	return ranges::count_if(
		_list,
		ranges::not_fn(&Entry::incomplete));
}

crl::time Authorizations::lastReceivedTime() {
	return _lastReceived;
}

} // namespace Api
