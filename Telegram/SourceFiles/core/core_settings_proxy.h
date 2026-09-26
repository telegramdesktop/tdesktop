/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/mtproto_proxy_data.h"

#include <array>

namespace Core {

class SettingsProxy final {
public:
	static constexpr auto kProxyRotationTimeouts = std::array{
		5,
		10,
		15,
		30,
		60,
	};
	static constexpr auto kDefaultProxyRotationTimeout = 10;

	SettingsProxy();

	[[nodiscard]] bool isEnabled() const;
	[[nodiscard]] bool isSystem() const;
	[[nodiscard]] bool isDisabled() const;

	[[nodiscard]] rpl::producer<> connectionTypeChanges() const;
	[[nodiscard]] rpl::producer<> connectionTypeValue() const;
	void connectionTypeChangesNotify();

	[[nodiscard]] bool tryIPv6() const;
	void setTryIPv6(bool value);

	[[nodiscard]] bool useProxyForCalls() const;
	void setUseProxyForCalls(bool value);

	[[nodiscard]] bool proxyRotationEnabled() const;
	void setProxyRotationEnabled(bool value);

	[[nodiscard]] int proxyRotationTimeout() const;
	void setProxyRotationTimeout(int value);

	[[nodiscard]] MTP::ProxyData::Settings settings() const;
	void setSettings(MTP::ProxyData::Settings value);

	[[nodiscard]] MTP::ProxyData selected() const;
	void setSelected(MTP::ProxyData value);

	[[nodiscard]] bool checkIpWarningShown() const;
	void setCheckIpWarningShown(bool value);

	[[nodiscard]] const std::vector<int> &proxyRotationPreferredIndices() const;
	void setProxyRotationPreferredIndices(std::vector<int> value);
	[[nodiscard]] bool promoteProxyRotationPreferredIndex(int index);

	[[nodiscard]] const std::vector<MTP::ProxyData> &list() const;
	[[nodiscard]] std::vector<MTP::ProxyData> &list();
	void setList(std::vector<MTP::ProxyData> value);
	void addToList(MTP::ProxyData value);
	void insertToList(int index, MTP::ProxyData value);
	[[nodiscard]] bool removeFromList(const MTP::ProxyData &value);
	[[nodiscard]] bool replaceInList(
		const MTP::ProxyData &was,
		MTP::ProxyData value);
	[[nodiscard]] int indexInList(const MTP::ProxyData &value) const;

	[[nodiscard]] QByteArray serialize() const;
	bool setFromSerialized(const QByteArray &serialized);

private:
	bool _tryIPv6 = false;
	bool _useProxyForCalls = false;
	bool _proxyRotationEnabled = false;
	bool _checkIpWarningShown = false;
	int _proxyRotationTimeout = kDefaultProxyRotationTimeout;
	MTP::ProxyData::Settings _settings = MTP::ProxyData::Settings::System;
	MTP::ProxyData _selected;
	std::vector<MTP::ProxyData> _list;
	std::vector<int> _proxyRotationPreferredIndices;

	rpl::event_stream<> _connectionTypeChanges;

};

} // namespace Core
