/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "countries/countries_manager.h"

#include "core/application.h"
#include "countries/countries_instance.h"
#include "main/main_app_config.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "mtproto/mtp_instance.h"

#include <QtCore/QFile>

namespace Countries {
namespace {

auto ProcessAlternativeName(Info &&info) {
	if (info.name == u"USA"_q) {
		info.alternativeName = u"United States of America"_q;
	}
	return std::move(info);
}

} // namespace

Manager::Manager(not_null<Main::Domain*> domain)
: _path(cWorkingDir() + "tdata/countries") {
	read();
	domain->activeValue(
	) | rpl::map([=](Main::Account *account) {
		if (!account) {
			_api.reset();
		}
		return rpl::combine(
			account
				? account->appConfig().refreshed() | rpl::map_to(true)
				: rpl::never<>() | rpl::map_to(false),
			account
				? account->mtpValue()
				: rpl::never<not_null<MTP::Instance*>>());
	}) | rpl::flatten_latest(
	) | rpl::start_with_next([=](bool, not_null<MTP::Instance*> instance) {
		_api.emplace(instance);
		request();
	}, _lifetime);
}

void Manager::read() {
	auto file = QFile(_path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		return;
	}

	_hash = QString(file.readLine()).toInt();

	auto infos = std::vector<Info>();
	while (!file.atEnd()) {
		const auto parts = QString(file.readLine()).split(";");
		if (parts.size() != 3) {
			continue;
		}
		infos.push_back(ProcessAlternativeName({
			.name = parts.at(0),
			.iso2 = parts.at(1),
			.code = parts.at(2),
		}));
	}
	Instance().setList(std::move(infos));
}

void Manager::write() const {
	auto file = QFile(_path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		return;
	}

	auto s = QTextStream(&file);
	s << QString::number(_hash) << "\n";

	for (const auto &info : Instance().list()) {
		s << info.name << ";" << info.iso2 << ";" << info.code << "\n";
	}
}

void Manager::request() {
	Expects(_api);

	_api->request(MTPhelp_GetCountriesList(
		MTP_string(),
		MTP_int(_hash)
	)).done([=](const MTPhelp_CountriesList &result) {
		result.match([&](const MTPDhelp_countriesList &data) {
			_hash = data.vhash().v;

			auto infos = std::vector<Info>();

			for (const auto &country : data.vcountries().v) {

				const auto &countryData = country.c_help_country();
				const auto &first = countryData.vcountry_codes().v.front()
					.c_help_countryCode();

				const auto name = countryData.vdefault_name().v;

				infos.push_back(ProcessAlternativeName({
					.name = name,
					.iso2 = countryData.viso2().v,
					.code = first.vcountry_code().v,
				}));
			}

			Instance().setList(std::move(infos));
			write();
		}, [](const MTPDhelp_countriesListNotModified &data) {
		});
		_lifetime.destroy();
	}).fail([=](const MTP::Error &error) {
		LOG(("API Error: getting countries failed with error %1"
			).arg(error.type()));
		_lifetime.destroy();
	}).send();
}

rpl::lifetime &Manager::lifetime() {
	return _lifetime;
}

Manager::~Manager() {
}

} // namespace Countries
