/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace MTP {

struct ProxyData {
	enum class Settings {
		System,
		Enabled,
		Disabled,
	};
	enum class Type {
		None,
		Socks5,
		Http,
		Mtproto,
	};
	enum class Status {
		Valid,
		Unsupported,
		Invalid,
	};

	Type type = Type::None;
	QString host;
	uint32 port = 0;
	QString user, password;

	std::vector<QString> resolvedIPs;
	crl::time resolvedExpireAt = 0;

	[[nodiscard]] bool valid() const;
	[[nodiscard]] Status status() const;
	[[nodiscard]] bool supportsCalls() const;
	[[nodiscard]] bool tryCustomResolve() const;
	[[nodiscard]] bytes::vector secretFromMtprotoPassword() const;
	[[nodiscard]] explicit operator bool() const;
	[[nodiscard]] bool operator==(const ProxyData &other) const;
	[[nodiscard]] bool operator!=(const ProxyData &other) const;

	[[nodiscard]] static bool ValidMtprotoPassword(const QString &password);
	[[nodiscard]] static Status MtprotoPasswordStatus(
		const QString &password);

};

[[nodiscard]] ProxyData ToDirectIpProxy(
	const ProxyData &proxy,
	int ipIndex = 0);
[[nodiscard]] QNetworkProxy ToNetworkProxy(const ProxyData &proxy);

} // namespace MTP
