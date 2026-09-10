/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtNetwork/QNetworkProxy>

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
		Web,
	};
	enum class Status {
		Valid,
		Unsupported,
		IncorrectSecret,
		Invalid,
	};

	Type type = Type::None;
	QString host;
	uint32 port = 0;
	QString user, password;

	std::vector<QString> resolvedIPs;
	crl::time resolvedExpireAt = 0;

	// A WEB proxy address is a hostname with an optional base path, like
	// `proxy.example.com` or `proxy.example.com/my-super-app`, so that a
	// production site can keep serving everything outside that prefix. The
	// path lives in the otherwise unused `user` field: that keeps the
	// serialized proxy format unchanged and makes a client without this
	// feature reject the entry instead of silently using the host root.
	[[nodiscard]] QString webAddress() const;
	void setWebAddress(const QString &value);
	[[nodiscard]] QString webBasePath() const;

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

[[nodiscard]] QString NormalizeWebProxyHost(const QString &value);
[[nodiscard]] QString WebProxyBridgeCapability(const ProxyData &proxy);
[[nodiscard]] QString EncodeWebProxyLinkSecret(const ProxyData &proxy);
[[nodiscard]] QString DecodeWebProxyLinkSecret(
	const QString &value,
	bool hasBasePath);
[[nodiscard]] QString WebProxyBridgePath(const ProxyData &proxy);
[[nodiscard]] QString WebProxyBridgeUrl(const ProxyData &proxy);
[[nodiscard]] ProxyData ToDirectIpProxy(
	const ProxyData &proxy,
	int ipIndex = 0);
[[nodiscard]] QNetworkProxy ToNetworkProxy(const ProxyData &proxy);

} // namespace MTP
