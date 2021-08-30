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

struct FileData {
	int hash = 0;
	std::vector<Info> infos;
};

auto ProcessAlternativeName(Info &&info) {
	if (info.name == u"USA"_q) {
		info.alternativeName = u"United States of America"_q;
	}
	return std::move(info);
}

[[nodiscard]] QByteArray SerializeCodeInfo(const CallingCodeInfo &info) {
	auto result = QByteArray();
	auto stream = QDataStream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_3);
	stream
		<< info.callingCode
		<< int(info.prefixes.size())
		<< int(info.patterns.size());
	for (const auto &prefix : info.prefixes) {
		stream << prefix;
	}
	for (const auto &pattern : info.patterns) {
		stream << pattern;
	}
	stream.device()->close();

	return result;
}

[[nodiscard]] CallingCodeInfo DeserializeCodeInfo(const QByteArray &data) {
	auto stream = QDataStream(data);
	auto result = CallingCodeInfo();
	auto prefixesCount = qint32(0);
	auto patternsCount = qint32(0);
	stream
		>> result.callingCode
		>> prefixesCount
		>> patternsCount;
	for (auto i = 0; i < prefixesCount; i++) {
		auto prefix = QString();
		stream >> prefix;
		result.prefixes.push_back(std::move(prefix));
	}
	for (auto i = 0; i < patternsCount; i++) {
		auto pattern = QString();
		stream >> pattern;
		result.patterns.push_back(std::move(pattern));
	}
	return (stream.status() != QDataStream::Ok)
		? CallingCodeInfo()
		: result;
}

[[nodiscard]] QByteArray SerializeInfo(const Info &info) {
	auto result = QByteArray();
	auto stream = QDataStream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_3);
	stream
		<< info.name
		<< info.iso2
		<< info.alternativeName
		<< info.isHidden
		<< int(info.codes.size());
	for (const auto &code : info.codes) {
		stream << SerializeCodeInfo(code);
	}
	stream.device()->close();

	return result;
}

[[nodiscard]] Info DeserializeInfo(const QByteArray &data) {
	auto stream = QDataStream(data);
	auto result = Info();
	auto codesCount = qint32(0);
	stream
		>> result.name
		>> result.iso2
		>> result.alternativeName
		>> result.isHidden
		>> codesCount;
	for (auto i = 0; i < codesCount; i++) {
		auto code = QByteArray();
		stream >> code;
		result.codes.push_back(DeserializeCodeInfo(code));
	}
	return (stream.status() != QDataStream::Ok)
		? Info()
		: result;
}

[[nodiscard]] QByteArray Serialize(const FileData &data) {
	auto result = QByteArray();
	auto stream = QDataStream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_3);
	stream
		<< data.hash
		<< int(data.infos.size());
	for (const auto &info : data.infos) {
		stream << SerializeInfo(info);
	}
	stream.device()->close();

	return result;
}

[[nodiscard]] FileData Deserialize(const QByteArray &data) {
	auto stream = QDataStream(data);
	auto hash = int(0);
	auto infosCount = qint32(0);
	auto infos = std::vector<Info>();
	stream >> hash >> infosCount;
	for (auto i = 0; i < infosCount; i++) {
		auto info = QByteArray();
		stream >> info;
		infos.push_back(DeserializeInfo(info));
	}
	return (stream.status() != QDataStream::Ok)
		? FileData()
		: FileData{ .hash = hash, .infos = std::move(infos) };
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
		return account
				? account->mtpMainSessionValue()
				: rpl::never<not_null<MTP::Instance*>>();
	}) | rpl::flatten_latest(
	) | rpl::start_with_next([=](not_null<MTP::Instance*> instance) {
		_api.emplace(instance);
		request();
	}, _lifetime);
}

void Manager::read() {
	auto file = QFile(_path);
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}

	auto stream = QDataStream(&file);
	auto data = QByteArray();
	stream >> data;
	auto fileData = Deserialize(data);

	_hash = fileData.hash;
	Instance().setList(base::take(fileData.infos));
}

void Manager::write() const {
	auto file = QFile(_path);
	if (!file.open(QIODevice::WriteOnly)) {
		return;
	}

	auto stream = QDataStream(&file);
	stream << Serialize({ .hash = _hash, .infos = Instance().list() });
}

void Manager::request() {
	Expects(_api.has_value());

	const auto convertMTP = [](const auto &vector, bool force = false) {
		if (!vector) {
			return std::vector<QString>(force ? 1 : 0);
		}
		return ranges::views::all(
			vector->v
		) | ranges::views::transform([](const MTPstring &s) -> QString {
			return qs(s);
		}) | ranges::to_vector;
	};

	_api->request(MTPhelp_GetCountriesList(
		MTP_string(),
		MTP_int(_hash)
	)).done([=](const MTPhelp_CountriesList &result) {
		result.match([&](const MTPDhelp_countriesList &data) {
			_hash = data.vhash().v;

			auto infos = std::vector<Info>();

			for (const auto &country : data.vcountries().v) {

				const auto &countryData = country.c_help_country();
				if (countryData.is_hidden()) {
					continue;
				}

				auto info = Info(ProcessAlternativeName({
					.name = countryData.vdefault_name().v,
					.iso2 = countryData.viso2().v,
					.isHidden = countryData.is_hidden(),
				}));
				for (const auto &code : countryData.vcountry_codes().v) {
					const auto &codeData = code.c_help_countryCode();
					info.codes.push_back(CallingCodeInfo{
						.callingCode = codeData.vcountry_code().v,
						.prefixes = convertMTP(codeData.vprefixes(), true),
						.patterns = convertMTP(codeData.vpatterns()),
					});
				}

				infos.push_back(std::move(info));
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
