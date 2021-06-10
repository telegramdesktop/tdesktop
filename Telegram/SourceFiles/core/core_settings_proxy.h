/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/mtproto_proxy_data.h"

namespace Core {

class SettingsProxy final {
public:
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

	[[nodiscard]] MTP::ProxyData::Settings settings() const;
	void setSettings(MTP::ProxyData::Settings value);

	[[nodiscard]] MTP::ProxyData selected() const;
	void setSelected(MTP::ProxyData value);

	[[nodiscard]] const std::vector<MTP::ProxyData> &list() const;
	[[nodiscard]] std::vector<MTP::ProxyData> &list();

	[[nodiscard]] QByteArray serialize() const;
	bool setFromSerialized(const QByteArray &serialized);

private:
	bool _tryIPv6 = false;
	bool _useProxyForCalls = false;
	MTP::ProxyData::Settings _settings = MTP::ProxyData::Settings::System;
	MTP::ProxyData _selected;
	std::vector<MTP::ProxyData> _list;

	rpl::event_stream<> _connectionTypeChanges;

};

} // namespace Core

