/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/mtproto_proxy_data.h"

#include "base/qthelp_url.h"
#include "base/qt/qt_string_view.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QMessageAuthenticationCode>
#include <QtCore/QUrl>
#include <QtNetwork/QHostAddress>

namespace MTP {
namespace {

[[nodiscard]] bool IsHexMtprotoPassword(const QString &password) {
	const auto size = password.size();
	if (size < 32 || size % 2 == 1) {
		return false;
	}
	const auto bad = [](QChar ch) {
		const auto code = ch.unicode();
		return (code < 'a' || code > 'f')
			&& (code < 'A' || code > 'F')
			&& (code < '0' || code > '9');
	};
	const auto i = std::find_if(password.begin(), password.end(), bad);
	return (i == password.end());
}

[[nodiscard]] ProxyData::Status HexMtprotoPasswordStatus(
		const QString &password) {
	const auto size = password.size() / 2;
	const auto type1 = password[0].toLower();
	const auto type2 = password[1].toLower();
	const auto valid = (size == 16)
		|| (size == 17 && (type1 == 'd') && (type2 == 'd'))
		|| (size >= 21 && (type1 == 'e') && (type2 == 'e'));
	if (valid) {
		return ProxyData::Status::Valid;
	} else if (size < 16) {
		return ProxyData::Status::Invalid;
	}
	return ProxyData::Status::Unsupported;
}

[[nodiscard]] bytes::vector SecretFromHexMtprotoPassword(
		const QString &password) {
	Expects(password.size() % 2 == 0);

	const auto size = password.size() / 2;
	const auto fromHex = [](QChar ch) -> int {
		const auto code = int(ch.unicode());
		if (code >= '0' && code <= '9') {
			return (code - '0');
		} else if (code >= 'A' && code <= 'F') {
			return 10 + (code - 'A');
		} else if (ch >= 'a' && ch <= 'f') {
			return 10 + (code - 'a');
		}
		Unexpected("Code in ProxyData fromHex.");
	};
	auto result = bytes::vector(size);
	for (auto i = 0; i != size; ++i) {
		const auto high = fromHex(password[2 * i]);
		const auto low = fromHex(password[2 * i + 1]);
		if (high < 0 || low < 0) {
			return {};
		}
		result[i] = static_cast<bytes::type>(high * 16 + low);
	}
	return result;
}

[[nodiscard]] QStringView Base64UrlInner(const QString &password) {
	Expects(password.size() > 2);

	// Skip one or two '=' at the end of the string.
	return base::StringViewMid(password, 0, [&] {
		auto result = password.size();
		for (auto i = 0; i != 2; ++i) {
			const auto prev = result - 1;
			if (password[prev] != '=') {
				break;
			}
			result = prev;
		}
		return result;
	}());
}

[[nodiscard]] bool IsBase64UrlMtprotoPassword(const QString &password) {
	const auto size = password.size();
	if (size < 22 || size % 4 == 1) {
		return false;
	}
	const auto bad = [](QChar ch) {
		const auto code = ch.unicode();
		return (code < 'a' || code > 'z')
			&& (code < 'A' || code > 'Z')
			&& (code < '0' || code > '9')
			&& (code != '_')
			&& (code != '-');
	};
	const auto inner = Base64UrlInner(password);
	const auto begin = inner.data();
	const auto end = begin + inner.size();
	return (std::find_if(begin, end, bad) == end);
}

[[nodiscard]] ProxyData::Status Base64UrlMtprotoPasswordStatus(
		const QString &password) {
	// IncorrectSecret
	const auto inner = Base64UrlInner(password);
	const auto size = (inner.size() * 3) / 4;
	const auto valid = (size == 16)
		|| (size == 17
			&& (password[0] == '3')
			&& ((password[1] >= 'Q' && password[1] <= 'Z')
				|| (password[1] >= 'a' && password[1] <= 'f')))
		|| (size >= 21
			&& (password[0] == '7')
			&& (password[1] >= 'g')
			&& (password[1] <= 'v'));
	const auto incorrect = (size >= 21
		&& password[0].toLower() == 'e'
		&& password[1].toLower() == 'e');
	if (size < 16) {
		return ProxyData::Status::Invalid;
	} else if (valid) {
		return ProxyData::Status::Valid;
	} else if (incorrect) {
		return ProxyData::Status::IncorrectSecret;
	}
	return ProxyData::Status::Unsupported;
}

[[nodiscard]] bytes::vector SecretFromBase64UrlMtprotoPassword(
		const QString &password) {
	const auto result = QByteArray::fromBase64(
		password.toLatin1(),
		QByteArray::Base64UrlEncoding);
	return bytes::make_vector(bytes::make_span(result));
}

// The WHATWG URL "ends in a number" rule: a host whose last label is
// all ASCII digits or 0x-prefixed hex is an IPv4 address (possibly in
// shorthand form like 127.1 or 0x7f.1), so the IP-literal policy below
// does not depend on QHostAddress recognising every shorthand.
[[nodiscard]] bool LastLabelIsNumeric(const QString &host) {
	const auto label = host.mid(host.lastIndexOf('.') + 1);
	if (label.isEmpty()) {
		return false;
	}
	const auto hex = label.startsWith(u"0x"_q, Qt::CaseInsensitive);
	const auto digits = hex ? label.mid(2) : label;
	for (const auto ch : digits) {
		const auto code = ch.unicode();
		const auto decimal = (code >= '0' && code <= '9');
		const auto alpha = (code >= 'a' && code <= 'f')
			|| (code >= 'A' && code <= 'F');
		if (!decimal && !(hex && alpha)) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] QString ComputeWebProxyBridgeCapability(
		const QString &host,
		const QByteArray &key) {
	const auto context = QByteArray("tdesktop-web-proxy-bridge-v1\n")
		+ host.toLatin1();
	return QString::fromLatin1(QMessageAuthenticationCode::hash(
		context,
		key,
		QCryptographicHash::Sha256
	).toBase64(
		QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

} // namespace

QString NormalizeWebProxyHost(const QString &value) {
	const auto input = value.trimmed();
	if (input.isEmpty()
		|| input.contains(':')
		|| input.contains('/')
		|| input.contains('?')
		|| input.contains('#')
		|| input.contains('@')
		|| input.endsWith('.')) {
		return QString();
	}
	const auto result = QString::fromLatin1(QUrl::toAce(input)).toLower();
	if (result.isEmpty()
		|| result.size() > 253
		|| !result.contains('.')) {
		return QString();
	}
	for (const auto &label : result.split('.')) {
		if (label.isEmpty()
			|| label.size() > 63
			|| label.front() == '-'
			|| label.back() == '-') {
			return QString();
		}
		for (const auto ch : label) {
			if (!ch.isLetterOrNumber() && ch != '-') {
				return QString();
			}
		}
	}
	if (LastLabelIsNumeric(result)) {
		return QString();
	}
	auto address = QHostAddress();
	return address.setAddress(result) ? QString() : result;
}

QString WebProxyBridgeCapability(const ProxyData &proxy) {
	Expects(proxy.type == ProxyData::Type::Web);
	Expects(proxy.host == NormalizeWebProxyHost(proxy.host));

#ifndef NDEBUG
	[[maybe_unused]] static const auto checked = [] {
		Assert(NormalizeWebProxyHost(u" Proxy.Example.COM "_q)
			== u"proxy.example.com"_q);
		Assert(NormalizeWebProxyHost(u"bücher.example"_q)
			== u"xn--bcher-kva.example"_q);
		Assert(NormalizeWebProxyHost(u"bücher.de"_q)
			== u"xn--bcher-kva.de"_q);
		Assert(NormalizeWebProxyHost(u"xn--strae-oqa.example"_q)
			== u"xn--strae-oqa.example"_q);
		Assert(NormalizeWebProxyHost(u"localhost"_q).isEmpty());
		Assert(NormalizeWebProxyHost(u"127.0.0.1"_q).isEmpty());
		Assert(NormalizeWebProxyHost(u"127.1"_q).isEmpty());
		Assert(NormalizeWebProxyHost(u"0x7f.1"_q).isEmpty());
		Assert(NormalizeWebProxyHost(u"0177.0.0.1"_q).isEmpty());
		Assert(NormalizeWebProxyHost(u"1.2.3"_q).isEmpty());
		Assert(NormalizeWebProxyHost(u"site.example:443"_q).isEmpty());
		Assert(NormalizeWebProxyHost(u"site..example"_q).isEmpty());
		const auto plain = QByteArray::fromHex(
			"000102030405060708090a0b0c0d0e0f");
		const auto padded = QByteArray::fromHex(
			"dd000102030405060708090a0b0c0d0e0f");
		Assert(ComputeWebProxyBridgeCapability(
			u"proxy.example.com"_q,
			plain) == u"MHLEY5PmW1GWqJkSrlmJpvJUiLhBH_QKy6yKg8a0JPk"_q);
		Assert(ComputeWebProxyBridgeCapability(
			u"proxy.example.com"_q,
			padded) == u"IpJrt3e7sKtzPyoXy6w-Zj6GGEvsvclN66JzQEfPYLA"_q);
		return true;
	}();
#endif // !NDEBUG

	const auto secret = proxy.secretFromMtprotoPassword();
	Expects(!secret.empty());
	const auto key = QByteArray(
		reinterpret_cast<const char*>(secret.data()),
		int(secret.size()));
	return ComputeWebProxyBridgeCapability(proxy.host, key);
}

bool ProxyData::valid() const {
	return status() == Status::Valid;
}

ProxyData::Status ProxyData::status() const {
	if (type == Type::Web) {
		if (host != NormalizeWebProxyHost(host)
			|| port != 443
			|| !user.isEmpty()) {
			return Status::Invalid;
		}
		const auto result = MtprotoPasswordStatus(password);
		if (result != Status::Valid) {
			return result;
		}
		const auto secret = secretFromMtprotoPassword();
		return (secret.size() >= 21 && secret[0] == bytes::type(0xEE))
			? Status::Unsupported
			: Status::Valid;
	} else if (type == Type::None || host.isEmpty() || !port) {
		return Status::Invalid;
	} else if (type == Type::Mtproto) {
		return MtprotoPasswordStatus(password);
	}
	return Status::Valid;
}

bool ProxyData::supportsCalls() const {
	return false;// (type == Type::Socks5);
}

bool ProxyData::tryCustomResolve() const {
	static const auto RegExp = QRegularExpression(
		QStringLiteral("^\\d+\\.\\d+\\.\\d+\\.\\d+$")
	);
	return (type == Type::Socks5 || type == Type::Mtproto)
		&& !qthelp::is_ipv6(host)
		&& !RegExp.match(host).hasMatch();
}

bytes::vector ProxyData::secretFromMtprotoPassword() const {
	Expects(type == Type::Mtproto || type == Type::Web);

	if (IsHexMtprotoPassword(password)) {
		return SecretFromHexMtprotoPassword(password);
	} else if (IsBase64UrlMtprotoPassword(password)) {
		return SecretFromBase64UrlMtprotoPassword(password);
	}
	return {};
}

ProxyData::operator bool() const {
	return valid();
}

bool ProxyData::operator==(const ProxyData &other) const {
	if (!valid()) {
		return !other.valid();
	}
	return (type == other.type)
		&& (host == other.host)
		&& (port == other.port)
		&& (user == other.user)
		&& (password == other.password);
}

bool ProxyData::operator!=(const ProxyData &other) const {
	return !(*this == other);
}

bool ProxyData::ValidMtprotoPassword(const QString &password) {
	return MtprotoPasswordStatus(password) == Status::Valid;
}

ProxyData::Status ProxyData::MtprotoPasswordStatus(const QString &password) {
	if (IsHexMtprotoPassword(password)) {
		return HexMtprotoPasswordStatus(password);
	} else if (IsBase64UrlMtprotoPassword(password)) {
		return Base64UrlMtprotoPasswordStatus(password);
	}
	return Status::Invalid;
}

ProxyData ToDirectIpProxy(const ProxyData &proxy, int ipIndex) {
	if (!proxy.tryCustomResolve()
		|| ipIndex < 0
		|| ipIndex >= proxy.resolvedIPs.size()) {
		return proxy;
	}
	return {
		proxy.type,
		proxy.resolvedIPs[ipIndex],
		proxy.port,
		proxy.user,
		proxy.password
	};
}

QNetworkProxy ToNetworkProxy(const ProxyData &proxy) {
	if (proxy.type == ProxyData::Type::None) {
		return QNetworkProxy::DefaultProxy;
	} else if (proxy.type == ProxyData::Type::Mtproto
		|| proxy.type == ProxyData::Type::Web) {
		return QNetworkProxy::NoProxy;
	}
	return QNetworkProxy(
		(proxy.type == ProxyData::Type::Socks5
			? QNetworkProxy::Socks5Proxy
			: QNetworkProxy::HttpProxy),
		proxy.host,
		proxy.port,
		proxy.user,
		proxy.password);
}

} // namespace MTP
