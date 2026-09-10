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
#include <QtCore/QUrlQuery>
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

// The root deployment keeps the frozen v1 context byte-for-byte; a base
// path binds the capability to the (host, path) pair with its own context,
// so a capability minted for one prefix is useless on another.
[[nodiscard]] QString ComputeWebProxyBridgeCapability(
		const QString &host,
		const QString &basePath,
		const QByteArray &key) {
	const auto context = basePath.isEmpty()
		? (QByteArray("tdesktop-web-proxy-bridge-v1\n") + host.toLatin1())
		: (QByteArray("tdesktop-web-proxy-bridge-v2\n")
			+ host.toLatin1()
			+ '\n'
			+ basePath.toLatin1());
	return QString::fromLatin1(QMessageAuthenticationCode::hash(
		context,
		key,
		QCryptographicHash::Sha256
	).toBase64(
		QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

// A base path needs a client that understands one, so a link that carries a
// path encodes its secret as base64url of this marker byte followed by the real
// secret. A client without path support decodes 17 bytes whose first byte is not
// the 0xDD of a padded secret, so it reports the link as an unsupported proxy
// type and asks the user to update, instead of accepting a pathless entry.
// 0xDD is the one byte that must never be used here: the older parser reads a
// 17-byte secret starting with it as an ordinary valid one.
constexpr auto kWebProxyLinkSecretMarker = uchar(0x70);

// The address field accepts a pasted URL, so the scheme is removed before the
// address is parsed and only the canonical form is stored. `http://` is left in
// place, and therefore rejected: HTTPS and port 443 are fixed for a WEB proxy.
[[nodiscard]] QStringView StripWebProxyScheme(const QString &value) {
	const auto scheme = u"https://"_q;
	auto result = QStringView(value).trimmed();
	if (result.startsWith(scheme, Qt::CaseInsensitive)) {
		result = result.mid(scheme.size());
	}
	return result;
}

[[nodiscard]] QStringView TrimWebProxyBasePath(const QString &value) {
	auto result = QStringView(value).trimmed();
	if (result.startsWith('/')) {
		result = result.mid(1);
	}
	if (result.endsWith('/')) {
		result = result.chopped(1);
	}
	return result;
}

// One or more `/`-separated segments, each starting with an ASCII letter or
// digit and continuing with those, `-` or `_`. `.` is not in the alphabet at
// all, so `.` and `..` segments cannot appear and the value never needs dot
// segment resolution. Empty segments are rejected, so the canonical value and
// the wire path stay the same string.
[[nodiscard]] QString NormalizeWebProxyBasePath(const QString &value) {
	const auto result = TrimWebProxyBasePath(value);
	if (result.isEmpty() || result.size() > 128) {
		return QString();
	}
	auto segmentStart = true;
	for (const auto character : result) {
		const auto code = character.unicode();
		if (code == '/') {
			if (segmentStart) {
				return QString();
			}
			segmentStart = true;
			continue;
		}
		const auto letter = (code >= 'a' && code <= 'z')
			|| (code >= 'A' && code <= 'Z');
		const auto digit = (code >= '0' && code <= '9');
		const auto extra = (code == '-' || code == '_');
		if (!letter && !digit && !(extra && !segmentStart)) {
			return QString();
		}
		segmentStart = false;
	}
	return segmentStart ? QString() : result.toString();
}

[[nodiscard]] bool ValidWebProxyBasePath(const QString &value) {
	return TrimWebProxyBasePath(value).isEmpty()
		|| !NormalizeWebProxyBasePath(value).isEmpty();
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
	Expects(proxy.webBasePath()
		== NormalizeWebProxyBasePath(proxy.webBasePath()));

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
		Assert(NormalizeWebProxyBasePath(u" /dobry-cola-super-app/ "_q)
			== u"dobry-cola-super-app"_q);
		Assert(NormalizeWebProxyBasePath(u"a"_q) == u"a"_q);
		Assert(NormalizeWebProxyBasePath(QString()).isEmpty());
		Assert(NormalizeWebProxyBasePath(u"-lead"_q).isEmpty());
		Assert(NormalizeWebProxyBasePath(u"MixedCase"_q)
			== u"MixedCase"_q);
		Assert(NormalizeWebProxyBasePath(u" /two/segments/ "_q)
			== u"two/segments"_q);
		Assert(NormalizeWebProxyBasePath(u"with space"_q).isEmpty());
		Assert(NormalizeWebProxyBasePath(u"empty//segment"_q).isEmpty());
		Assert(NormalizeWebProxyBasePath(u"trailing//"_q).isEmpty());
		Assert(NormalizeWebProxyBasePath(u"a/-lead"_q).isEmpty());
		Assert(StripWebProxyScheme(u" HTTPS://Proxy.Example.COM/App/ "_q)
			== u"Proxy.Example.COM/App/"_q);
		Assert(StripWebProxyScheme(u"http://proxy.example.com"_q)
			== u"http://proxy.example.com"_q);
		const auto plainSecret = u"8561944064fc730cbfa4473562d8ec59"_q;
		const auto markedSecret = u"cIVhlEBk_HMMv6RHNWLY7Fk"_q;
		Assert(DecodeWebProxyLinkSecret(markedSecret, true) == plainSecret);
		Assert(DecodeWebProxyLinkSecret(markedSecret, false) == plainSecret);
		Assert(DecodeWebProxyLinkSecret(plainSecret, false) == plainSecret);
		Assert(DecodeWebProxyLinkSecret(plainSecret, true).isEmpty());
		Assert(NormalizeWebProxyBasePath(u"per%20cent"_q).isEmpty());
		Assert(NormalizeWebProxyBasePath(u"dot.ted"_q).isEmpty());
		Assert(NormalizeWebProxyBasePath(u".."_q).isEmpty());
		Assert(NormalizeWebProxyBasePath(QString(129, QChar('a'))).isEmpty());
		Assert(!NormalizeWebProxyBasePath(QString(128, QChar('a'))).isEmpty());
		Assert(ValidWebProxyBasePath(QString()));
		Assert(ValidWebProxyBasePath(u"/"_q));
		Assert(!ValidWebProxyBasePath(u"empty//segment"_q));
		const auto plain = QByteArray::fromHex(
			"000102030405060708090a0b0c0d0e0f");
		const auto padded = QByteArray::fromHex(
			"dd000102030405060708090a0b0c0d0e0f");
		const auto path = u"dobry-cola-super-app"_q;
		Assert(ComputeWebProxyBridgeCapability(
			u"proxy.example.com"_q,
			QString(),
			plain) == u"MHLEY5PmW1GWqJkSrlmJpvJUiLhBH_QKy6yKg8a0JPk"_q);
		Assert(ComputeWebProxyBridgeCapability(
			u"proxy.example.com"_q,
			QString(),
			padded) == u"IpJrt3e7sKtzPyoXy6w-Zj6GGEvsvclN66JzQEfPYLA"_q);
		Assert(ComputeWebProxyBridgeCapability(
			u"proxy.example.com"_q,
			path,
			plain) == u"hHz99Xs93EN1j91G9gpNepXwGNNt5YdAFkEVk_LlqdQ"_q);
		Assert(ComputeWebProxyBridgeCapability(
			u"proxy.example.com"_q,
			path,
			padded) == u"TGUkZaevsavLbHvlNWipnRoYxgzZ51ioWvbxgGT3wHo"_q);
		return true;
	}();
#endif // !NDEBUG

	const auto secret = proxy.secretFromMtprotoPassword();
	Expects(!secret.empty());
	const auto key = QByteArray(
		reinterpret_cast<const char*>(secret.data()),
		int(secret.size()));
	return ComputeWebProxyBridgeCapability(
		proxy.host,
		proxy.webBasePath(),
		key);
}

QString EncodeWebProxyLinkSecret(const ProxyData &proxy) {
	Expects(proxy.type == ProxyData::Type::Web);

	const auto secret = proxy.secretFromMtprotoPassword();
	if (proxy.webBasePath().isEmpty() || secret.empty()) {
		return proxy.password;
	}
	auto marked = QByteArray(1, char(kWebProxyLinkSecretMarker));
	marked.append(
		reinterpret_cast<const char*>(secret.data()),
		int(secret.size()));
	return QString::fromLatin1(marked.toBase64(
		QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString DecodeWebProxyLinkSecret(const QString &value, bool hasBasePath) {
	const auto decoded = QByteArray::fromBase64(
		value.toLatin1(),
		QByteArray::Base64UrlEncoding
			| QByteArray::AbortOnBase64DecodingErrors);

	// A canonical secret is 16 bytes, 17 starting with 0xDD, or 21+ starting
	// with 0xEE, so a longer value behind this marker is never ambiguous.
	const auto marked = (decoded.size() >= 17)
		&& (uchar(decoded[0]) == kWebProxyLinkSecretMarker);
	if (marked) {
		return QString::fromLatin1(decoded.mid(1).toHex());
	}

	// An unmarked secret on a link that carries a base path is rejected rather
	// than accepted: that is exactly the link a client without path support
	// would take for a pathless proxy on an empty host, so the marked form is
	// required once a path is present. A root link keeps the plain secret and
	// still works in those clients.
	return hasBasePath ? QString() : value;
}

QString WebProxyBridgePath(const ProxyData &proxy) {
	Expects(proxy.type == ProxyData::Type::Web);

	const auto path = proxy.webBasePath();
	return path.isEmpty() ? u"/"_q : ('/' + path + '/');
}

QString WebProxyBridgeUrl(const ProxyData &proxy) {
	auto result = QUrl(u"https://"_q + proxy.host);
	result.setPath(WebProxyBridgePath(proxy));
	auto query = QUrlQuery();
	query.addQueryItem(u"bridge"_q, WebProxyBridgeCapability(proxy));
	result.setQuery(query);
	return result.toString(QUrl::FullyEncoded);
}

QString ProxyData::webAddress() const {
	const auto path = webBasePath();
	return path.isEmpty() ? host : (host + '/' + path);
}

void ProxyData::setWebAddress(const QString &value) {
	Expects(type == Type::Web);

	const auto trimmed = StripWebProxyScheme(value).toString();
	const auto slash = trimmed.indexOf('/');
	const auto path = (slash < 0)
		? QString()
		: trimmed.mid(slash + 1);
	host = NormalizeWebProxyHost((slash < 0)
		? trimmed
		: trimmed.mid(0, slash));
	if (host.isEmpty() || !ValidWebProxyBasePath(path)) {
		host = QString();
		user = QString();
	} else {
		user = NormalizeWebProxyBasePath(path);
	}
}

QString ProxyData::webBasePath() const {
	return (type == Type::Web) ? user : QString();
}

bool ProxyData::valid() const {
	return status() == Status::Valid;
}

ProxyData::Status ProxyData::status() const {
	if (type == Type::Web) {
		if (host.isEmpty()
			|| host != NormalizeWebProxyHost(host)
			|| port != 443
			|| user != NormalizeWebProxyBasePath(user)) {
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
