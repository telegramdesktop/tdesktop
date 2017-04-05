/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "mtproto/dc_options.h"

namespace MTP {

void DcOptions::constructFromBuiltIn() {
	QWriteLocker lock(&_mutex);
	_data.clear();

	auto bdcs = builtInDcs();
	for (auto i = 0, l = builtInDcsCount(); i != l; ++i) {
		auto flags = MTPDdcOption::Flags(0);
		auto idWithShift = MTP::shiftDcId(bdcs[i].id, flags);
		_data.emplace(idWithShift, Option(bdcs[i].id, flags, bdcs[i].ip, bdcs[i].port));
		DEBUG_LOG(("MTP Info: adding built in DC %1 connect option: %2:%3").arg(bdcs[i].id).arg(bdcs[i].ip).arg(bdcs[i].port));
	}

	auto bdcsipv6 = builtInDcsIPv6();
	for (auto i = 0, l = builtInDcsCountIPv6(); i != l; ++i) {
		auto flags = MTPDdcOption::Flags(MTPDdcOption::Flag::f_ipv6);
		auto idWithShift = MTP::shiftDcId(bdcsipv6[i].id, flags);
		_data.emplace(idWithShift, Option(bdcsipv6[i].id, flags, bdcsipv6[i].ip, bdcsipv6[i].port));
		DEBUG_LOG(("MTP Info: adding built in DC %1 IPv6 connect option: %2:%3").arg(bdcsipv6[i].id).arg(bdcsipv6[i].ip).arg(bdcsipv6[i].port));
	}
}

void DcOptions::processFromList(const QVector<MTPDcOption> &options, bool overwrite) {
	if (options.empty() || _immutable) {
		return;
	}

	auto idsChanged = std::vector<DcId>();
	idsChanged.reserve(options.size());
	auto shiftedIdsProcessed = std::vector<ShiftedDcId>();
	shiftedIdsProcessed.reserve(options.size());
	{
		QWriteLocker lock(&_mutex);
		if (overwrite) {
			idsChanged.reserve(_data.size());
		}
		for (auto &mtpOption : options) {
			if (mtpOption.type() != mtpc_dcOption) {
				LOG(("Wrong type in DcOptions: %1").arg(mtpOption.type()));
				continue;
			}

			auto &option = mtpOption.c_dcOption();
			auto dcId = option.vid.v;
			auto flags = option.vflags.v;
			auto dcIdWithShift = MTP::shiftDcId(dcId, flags);
			if (base::contains(shiftedIdsProcessed, dcIdWithShift)) {
				continue;
			}

			shiftedIdsProcessed.push_back(dcIdWithShift);
			auto ip = std::string(option.vip_address.v.constData(), option.vip_address.v.size());
			auto port = option.vport.v;
			if (applyOneGuarded(dcId, flags, ip, port)) {
				if (!base::contains(idsChanged, dcId)) {
					idsChanged.push_back(dcId);
				}
			}
		}
		if (overwrite && shiftedIdsProcessed.size() < _data.size()) {
			for (auto i = _data.begin(); i != _data.end();) {
				if (base::contains(shiftedIdsProcessed, i->first)) {
					++i;
				} else {
					if (!base::contains(idsChanged, i->second.id)) {
						idsChanged.push_back(i->second.id);
					}
					i = _data.erase(i);
				}
			}
		}
	}

	if (!idsChanged.empty()) {
		_changed.notify(std::move(idsChanged));
	}
}

void DcOptions::setFromList(const MTPVector<MTPDcOption> &options) {
	processFromList(options.v, true);
}

void DcOptions::addFromList(const MTPVector<MTPDcOption> &options) {
	processFromList(options.v, false);
}

void DcOptions::addFromOther(const DcOptions &options) {
	if (this == &options || _immutable) {
		return;
	}

	auto idsChanged = std::vector<DcId>();
	{
		QReadLocker lock(&options._mutex);
		if (options._data.empty()) {
			return;
		}

		idsChanged.reserve(options._data.size());
		{
			QWriteLocker lock(&_mutex);
			for (auto &item : options._data) {
				auto dcId = item.second.id;
				auto flags = item.second.flags;
				auto &ip = item.second.ip;
				auto port = item.second.port;
				if (applyOneGuarded(dcId, flags, ip, port)) {
					if (!base::contains(idsChanged, dcId)) {
						idsChanged.push_back(dcId);
					}
				}
			}
		}
	}

	if (!idsChanged.empty()) {
		_changed.notify(std::move(idsChanged));
	}
}

void DcOptions::constructAddOne(int id, MTPDdcOption::Flags flags, const std::string &ip, int port) {
	QWriteLocker lock(&_mutex);
	applyOneGuarded(bareDcId(id), flags, ip, port);
}

bool DcOptions::applyOneGuarded(DcId dcId, MTPDdcOption::Flags flags, const std::string &ip, int port) {
	auto dcIdWithShift = MTP::shiftDcId(dcId, flags);
	auto i = _data.find(dcIdWithShift);
	if (i != _data.cend()) {
		if (i->second.ip == ip && i->second.port == port) {
			return false;
		}
		i->second.ip = ip;
		i->second.port = port;
	} else {
		_data.emplace(dcIdWithShift, Option(dcId, flags, ip, port));
	}
	return true;
}

QByteArray DcOptions::serialize() const {
	if (_immutable) {
		// Don't write the overriden options to our settings.
		return DcOptions().serialize();
	}

	QReadLocker lock(&_mutex);

	auto size = sizeof(qint32);
	for (auto &item : _data) {
		size += sizeof(qint32) + sizeof(qint32) + sizeof(qint32); // id + flags + port
		size += sizeof(qint32) + item.second.ip.size();
	}

	auto result = QByteArray();
	result.reserve(size);
	{
		QBuffer buffer(&result);
		if (!buffer.open(QIODevice::WriteOnly)) {
			LOG(("MTP Error: Can't open data for DcOptions::serialize()"));
			return result;
		}

		QDataStream stream(&buffer);
		stream.setVersion(QDataStream::Qt_5_1);
		stream << qint32(_data.size());
		for (auto &item : _data) {
			stream << qint32(item.second.id) << qint32(item.second.flags) << qint32(item.second.port);
			stream << qint32(item.second.ip.size());
			stream.writeRawData(item.second.ip.data(), item.second.ip.size());
		}
	}
	return result;
}

void DcOptions::constructFromSerialized(const QByteArray &serialized) {
	auto readonly = serialized;
	QBuffer buffer(&readonly);
	if (!buffer.open(QIODevice::ReadOnly)) {
		LOG(("MTP Error: Can't open data for DcOptions::constructFromSerialized()"));
		return;
	}
	QDataStream stream(&buffer);
	stream.setVersion(QDataStream::Qt_5_1);
	qint32 count = 0;
	stream >> count;
	if (stream.status() != QDataStream::Ok) {
		LOG(("MTP Error: Bad data for DcOptions::constructFromSerialized()"));
		return;
	}

	QWriteLocker lock(&_mutex);
	_data.clear();
	for (auto i = 0; i != count; ++i) {
		qint32 id = 0, flags = 0, port = 0, ipSize = 0;
		stream >> id >> flags >> port >> ipSize;
		std::string ip(ipSize, ' ');
		stream.readRawData(&ip[0], ipSize);

		if (stream.status() != QDataStream::Ok) {
			LOG(("MTP Error: Bad data inside DcOptions::constructFromSerialized()"));
			return;
		}

		applyOneGuarded(DcId(id), MTPDdcOption::Flags(flags), ip, port);
	}
}

DcOptions::Ids DcOptions::sortedDcIds() const {
	auto result = Ids();
	{
		QReadLocker lock(&_mutex);
		result.reserve(_data.size());
		for (auto &item : _data) {
			if (!base::contains(result, item.second.id)) {
				result.push_back(item.second.id);
			}
		}
	}
	std::sort(result.begin(), result.end());
	return result;
}

DcId DcOptions::getDefaultDcId() const {
	auto result = sortedDcIds();
	t_assert(!result.empty());

	return result[0];
}

DcOptions::Variants DcOptions::lookup(DcId dcId, DcType type) const {
	auto isMediaDownload = (type == DcType::MediaDownload);
	int shifts[2][2][4] = {
		{ // IPv4
			{ // TCP IPv4
				isMediaDownload ? (MTPDdcOption::Flag::f_media_only | MTPDdcOption::Flag::f_tcpo_only) : -1,
				qFlags(MTPDdcOption::Flag::f_tcpo_only),
				isMediaDownload ? qFlags(MTPDdcOption::Flag::f_media_only) : -1,
				0
			}, { // HTTP IPv4
				-1,
				-1,
				isMediaDownload ? qFlags(MTPDdcOption::Flag::f_media_only) : -1,
				0
			},
		}, { // IPv6
			{ // TCP IPv6
				isMediaDownload ? (MTPDdcOption::Flag::f_media_only | MTPDdcOption::Flag::f_tcpo_only | MTPDdcOption::Flag::f_ipv6) : -1,
				MTPDdcOption::Flag::f_tcpo_only | MTPDdcOption::Flag::f_ipv6,
				isMediaDownload ? (MTPDdcOption::Flag::f_media_only | MTPDdcOption::Flag::f_ipv6) : -1,
				qFlags(MTPDdcOption::Flag::f_ipv6)
			}, { // HTTP IPv6
				-1,
				-1,
				isMediaDownload ? (MTPDdcOption::Flag::f_media_only | MTPDdcOption::Flag::f_ipv6) : -1,
				qFlags(MTPDdcOption::Flag::f_ipv6)
			},
		},
	};

	auto result = Variants();
	{
		QReadLocker lock(&_mutex);
		for (auto address = 0; address != Variants::AddressTypeCount; ++address) {
			for (auto protocol = 0; protocol != Variants::ProtocolCount; ++protocol) {
				for (auto variant = 0; variant != base::array_size(shifts[address][protocol]); ++variant) {
					auto shift = shifts[address][protocol][variant];
					if (shift < 0) continue;

					auto it = _data.find(shiftDcId(dcId, shift));
					if (it != _data.cend()) {
						result.data[address][protocol].ip = it->second.ip;
						result.data[address][protocol].flags = it->second.flags;
						result.data[address][protocol].port = it->second.port;
						break;
					}
				}
			}
		}
	}
	return result;
}

bool DcOptions::loadFromFile(const QString &path) {
	QVector<MTPDcOption> options;

	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) {
		LOG(("MTP Error: could not read '%1'").arg(path));
		return false;
	}
	QTextStream stream(&f);
	stream.setCodec("UTF-8");
	while (!stream.atEnd()) {
		auto line = stream.readLine();
		auto components = line.split(QRegularExpression(R"(\s)"), QString::SkipEmptyParts);
		if (components.isEmpty() || components[0].startsWith('#')) {
			continue;
		}

		auto error = [line] {
			LOG(("MTP Error: in .tdesktop-endpoints expected 'dcId host port [tcpo_only] [media_only]', got '%1'").arg(line));
			return false;
		};
		if (components.size() < 3) {
			return error();
		}
		auto dcId = components[0].toInt();
		auto ip = components[1];
		auto port = components[2].toInt();
		auto host = QHostAddress();
		if (dcId <= 0 || dcId >= internal::kDcShift || !host.setAddress(ip) || port <= 0) {
			return error();
		}
		auto flags = MTPDdcOption::Flags(0);
		if (host.protocol() == QAbstractSocket::IPv6Protocol) {
			flags |= MTPDdcOption::Flag::f_ipv6;
		}
		for (auto &option : components.mid(3)) {
			if (option.startsWith('#')) {
				break;
			} else if (option == qstr("tcpo_only")) {
				flags |= MTPDdcOption::Flag::f_tcpo_only;
			} else if (option == qstr("media_only")) {
				flags |= MTPDdcOption::Flag::f_media_only;
			} else {
				return error();
			}
		}
		options.push_back(MTP_dcOption(MTP_flags(flags), MTP_int(dcId), MTP_string(ip), MTP_int(port)));
	}
	if (options.isEmpty()) {
		LOG(("MTP Error: in .tdesktop-endpoints expected at least one endpoint being provided."));
		return false;
	}

	_immutable = false;
	setFromList(MTP_vector<MTPDcOption>(options));
	_immutable = true;

	return true;
}

bool DcOptions::writeToFile(const QString &path) const {
	QFile f(path);
	if (!f.open(QIODevice::WriteOnly)) {
		return false;
	}
	QTextStream stream(&f);
	stream.setCodec("UTF-8");

	QReadLocker lock(&_mutex);
	for (auto &item : _data) {
		auto &endpoint = item.second;
		stream << endpoint.id << ' ' << QString::fromStdString(endpoint.ip) << ' ' << endpoint.port;
		if (endpoint.flags & MTPDdcOption::Flag::f_tcpo_only) {
			stream << " tcpo_only";
		}
		if (endpoint.flags & MTPDdcOption::Flag::f_media_only) {
			stream << " media_only";
		}
		stream << '\n';
	}
	return true;
}

} // namespace MTP
