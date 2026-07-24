/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/core_settings_proxy.h"

#include "base/platform/base_platform_info.h"
#include "storage/serialize_common.h"

#include <algorithm>

namespace Core {
namespace {

[[nodiscard]] qint32 ProxySettingsToInt(MTP::ProxyData::Settings settings) {
	switch(settings) {
	case MTP::ProxyData::Settings::System: return 0;
	case MTP::ProxyData::Settings::Enabled: return 1;
	case MTP::ProxyData::Settings::Disabled: return 2;
	}
	Unexpected("Bad type in ProxySettingsToInt");
}

[[nodiscard]] MTP::ProxyData::Settings IntToProxySettings(qint32 value) {
	switch(value) {
	case 0: return MTP::ProxyData::Settings::System;
	case 1: return MTP::ProxyData::Settings::Enabled;
	case 2: return MTP::ProxyData::Settings::Disabled;
	}
	Unexpected("Bad type in IntToProxySettings");
}

[[nodiscard]] MTP::ProxyData DeserializeProxyData(const QByteArray &data) {
	QDataStream stream(data);
	stream.setVersion(QDataStream::Qt_5_1);

	qint32 proxyType, port;
	MTP::ProxyData proxy;
	stream
		>> proxyType
		>> proxy.host
		>> port
		>> proxy.user
		>> proxy.password;
	proxy.port = port;
	proxy.type = [&] {
		switch(proxyType) {
		case 0: return MTP::ProxyData::Type::None;
		case 1: return MTP::ProxyData::Type::Socks5;
		case 2: return MTP::ProxyData::Type::Http;
		case 3: return MTP::ProxyData::Type::Mtproto;
		}
		Unexpected("Bad type in DeserializeProxyData");
	}();
	return proxy;
}

[[nodiscard]] QByteArray SerializeProxyData(const MTP::ProxyData &proxy) {
	auto result = QByteArray();
	const auto size = 1 * sizeof(qint32)
		+ Serialize::stringSize(proxy.host)
		+ 1 * sizeof(qint32)
		+ Serialize::stringSize(proxy.user)
		+ Serialize::stringSize(proxy.password);

	result.reserve(size);
	{
		const auto proxyType = [&] {
			switch(proxy.type) {
			case MTP::ProxyData::Type::None: return 0;
			case MTP::ProxyData::Type::Socks5: return 1;
			case MTP::ProxyData::Type::Http: return 2;
			case MTP::ProxyData::Type::Mtproto: return 3;
			}
			Unexpected("Bad type in SerializeProxyData");
		}();

		QDataStream stream(&result, QIODevice::WriteOnly);
		stream.setVersion(QDataStream::Qt_5_1);
		stream
			<< qint32(proxyType)
			<< proxy.host
			<< qint32(proxy.port)
			<< proxy.user
			<< proxy.password;
	}
	return result;
}

std::vector<int> NormalizeProxyRotationPreferredIndices(
		std::vector<int> indices,
		int listSize) {
	auto filtered = std::vector<int>();
	filtered.reserve(indices.size());
	for (const auto index : indices) {
		if (index < 0
			|| index >= listSize
			|| ranges::contains(filtered, index)) {
			continue;
		}
		filtered.push_back(index);
	}
	return filtered;
}

} // namespace

SettingsProxy::SettingsProxy()
: _tryIPv6(!Platform::IsWindows()) {
}

QByteArray SettingsProxy::serialize() const {
	const auto serializedSelected = SerializeProxyData(_selected);
	const auto serializedList = ranges::views::all(
		_list
	) | ranges::views::transform(SerializeProxyData) | ranges::to_vector;

	const auto size = 3 * sizeof(qint32)
		+ Serialize::bytearraySize(serializedSelected)
		+ 1 * sizeof(qint32)
		+ ranges::accumulate(
			serializedList,
			0,
			ranges::plus(),
			&Serialize::bytearraySize)
		+ (4 + int(_proxyRotationPreferredIndices.size())) * sizeof(qint32);
	auto stream = Serialize::ByteArrayWriter(size);
	stream
		<< qint32(_tryIPv6 ? 1 : 0)
		<< qint32(_useProxyForCalls ? 1 : 0)
		<< ProxySettingsToInt(_settings)
		<< serializedSelected
		<< qint32(_list.size());
	for (const auto &i : serializedList) {
		stream << i;
	}
	stream
		<< qint32(_checkIpWarningShown ? 1 : 0)
		<< qint32(_proxyRotationEnabled ? 1 : 0)
		<< qint32(_proxyRotationTimeout)
		<< qint32(_proxyRotationPreferredIndices.size());
	for (const auto index : _proxyRotationPreferredIndices) {
		stream << qint32(index);
	}
	return std::move(stream).result();
}

bool SettingsProxy::setFromSerialized(const QByteArray &serialized) {
	if (serialized.isEmpty()) {
		return true;
	}

	auto stream = Serialize::ByteArrayReader(serialized);

	auto tryIPv6 = qint32(_tryIPv6 ? 1 : 0);
	auto useProxyForCalls = qint32(_useProxyForCalls ? 1 : 0);
	auto settings = ProxySettingsToInt(_settings);
	auto listCount = qint32(_list.size());
	auto selectedProxy = QByteArray();
	auto list = std::vector<MTP::ProxyData>();

	if (!stream.atEnd()) {
		stream
			>> tryIPv6
			>> useProxyForCalls
			>> settings
			>> selectedProxy
			>> listCount;
		if (stream.ok()) {
			if (listCount < 0) {
				return false;
			}
			list.reserve(listCount);
			for (auto i = 0; i != listCount; ++i) {
				QByteArray data;
				stream >> data;
				list.push_back(DeserializeProxyData(data));
			}
		}
	}

	auto checkIpWarningShown = qint32(0);
	if (!stream.atEnd()) {
		stream >> checkIpWarningShown;
	}
	auto proxyRotationEnabled = qint32(_proxyRotationEnabled ? 1 : 0);
	if (!stream.atEnd()) {
		stream >> proxyRotationEnabled;
	}
	auto proxyRotationTimeout = qint32(_proxyRotationTimeout);
	if (!stream.atEnd()) {
		stream >> proxyRotationTimeout;
	}
	auto preferredCount = qint32(0);
	auto preferredIndices = std::vector<int>();
	if (!stream.atEnd()) {
		stream >> preferredCount;
		if (stream.ok()) {
			if (preferredCount < 0) {
				return false;
			}
			preferredIndices.reserve(preferredCount);
			for (auto i = 0; i != preferredCount; ++i) {
				auto index = qint32(0);
				stream >> index;
				preferredIndices.push_back(index);
			}
		}
	}

	if (!stream.ok()) {
		LOG(("App Error: "
			"Bad data for Core::SettingsProxy::setFromSerialized()"));
		return false;
	}

	_tryIPv6 = (tryIPv6 == 1);
	_useProxyForCalls = (useProxyForCalls == 1);
	_checkIpWarningShown = (checkIpWarningShown == 1);
	_proxyRotationEnabled = (proxyRotationEnabled == 1);
	setProxyRotationTimeout(proxyRotationTimeout);
	_settings = IntToProxySettings(settings);
	_selected = DeserializeProxyData(selectedProxy);
	_list = std::move(list);
	setProxyRotationPreferredIndices(std::move(preferredIndices));

	return true;
}

bool SettingsProxy::isEnabled() const {
	return _settings == MTP::ProxyData::Settings::Enabled;
}

bool SettingsProxy::isSystem() const {
	return _settings == MTP::ProxyData::Settings::System;
}

bool SettingsProxy::isDisabled() const {
	return _settings == MTP::ProxyData::Settings::Disabled;
}

bool SettingsProxy::checkIpWarningShown() const {
	return _checkIpWarningShown;
}

void SettingsProxy::setCheckIpWarningShown(bool value) {
	_checkIpWarningShown = value;
}

const std::vector<int> &SettingsProxy::proxyRotationPreferredIndices() const {
	return _proxyRotationPreferredIndices;
}

void SettingsProxy::setProxyRotationPreferredIndices(std::vector<int> value) {
	_proxyRotationPreferredIndices = NormalizeProxyRotationPreferredIndices(
		std::move(value),
		int(_list.size()));
}

bool SettingsProxy::promoteProxyRotationPreferredIndex(int index) {
	if (index < 0 || index >= int(_list.size())) {
		return false;
	}
	auto &indices = _proxyRotationPreferredIndices;
	const auto i = ranges::find(indices, index);
	if (i == begin(indices)) {
		return false;
	} else if (i != end(indices)) {
		std::rotate(begin(indices), i, std::next(i));
	} else {
		indices.insert(begin(indices), index);
	}
	return true;
}

bool SettingsProxy::tryIPv6() const {
	return _tryIPv6;
}

void SettingsProxy::setTryIPv6(bool value) {
	_tryIPv6 = value;
}

bool SettingsProxy::useProxyForCalls() const {
	return _useProxyForCalls;
}

void SettingsProxy::setUseProxyForCalls(bool value) {
	_useProxyForCalls = value;
}

bool SettingsProxy::proxyRotationEnabled() const {
	return _proxyRotationEnabled;
}

void SettingsProxy::setProxyRotationEnabled(bool value) {
	_proxyRotationEnabled = value;
}

int SettingsProxy::proxyRotationTimeout() const {
	return _proxyRotationTimeout;
}

void SettingsProxy::setProxyRotationTimeout(int value) {
	_proxyRotationTimeout = (value > 0)
		? value
		: kDefaultProxyRotationTimeout;
}

MTP::ProxyData::Settings SettingsProxy::settings() const {
	return _settings;
}

void SettingsProxy::setSettings(MTP::ProxyData::Settings value) {
	_settings = value;
}

MTP::ProxyData SettingsProxy::selected() const {
	return _selected;
}

void SettingsProxy::setSelected(MTP::ProxyData value) {
	_selected = value;
}

const std::vector<MTP::ProxyData> &SettingsProxy::list() const {
	return _list;
}

std::vector<MTP::ProxyData> &SettingsProxy::list() {
	return _list;
}

void SettingsProxy::setList(std::vector<MTP::ProxyData> value) {
	_list = std::move(value);
	_proxyRotationPreferredIndices.clear();
}

void SettingsProxy::addToList(MTP::ProxyData value) {
	_list.push_back(std::move(value));
}

void SettingsProxy::insertToList(int index, MTP::ProxyData value) {
	index = std::clamp(index, 0, int(_list.size()));
	for (auto &existing : _proxyRotationPreferredIndices) {
		if (existing >= index) {
			++existing;
		}
	}
	_list.insert(begin(_list) + index, std::move(value));
}

bool SettingsProxy::removeFromList(const MTP::ProxyData &value) {
	const auto i = ranges::find(_list, value);
	if (i == end(_list)) {
		return false;
	}
	const auto index = int(i - begin(_list));
	_list.erase(i);
	for (auto &existing : _proxyRotationPreferredIndices) {
		if (existing > index) {
			--existing;
		}
	}
	_proxyRotationPreferredIndices.erase(
		std::remove(
			begin(_proxyRotationPreferredIndices),
			end(_proxyRotationPreferredIndices),
			index),
		end(_proxyRotationPreferredIndices));
	return true;
}

bool SettingsProxy::replaceInList(
		const MTP::ProxyData &was,
		MTP::ProxyData value) {
	const auto i = ranges::find(_list, was);
	if (i == end(_list)) {
		return false;
	}
	*i = std::move(value);
	return true;
}

int SettingsProxy::indexInList(const MTP::ProxyData &value) const {
	const auto i = ranges::find(_list, value);
	return (i == end(_list)) ? -1 : int(i - begin(_list));
}

rpl::producer<> SettingsProxy::connectionTypeValue() const {
	return _connectionTypeChanges.events_starting_with({});
}

rpl::producer<> SettingsProxy::connectionTypeChanges() const {
	return _connectionTypeChanges.events();
}

void SettingsProxy::connectionTypeChangesNotify() {
	_connectionTypeChanges.fire({});
}

} // namespace Core
