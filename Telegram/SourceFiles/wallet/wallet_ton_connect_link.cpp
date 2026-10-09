/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_ton_connect_link.h"

#include "base/qthelp_regex.h"
#include "base/qthelp_url.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QUrl>

namespace Wallet {
namespace {

constexpr auto kMaxQueryLength = 8192;

// \z, not $: $ also matches before a trailing newline.
[[nodiscard]] bool IsClientId(const QString &value) {
	return qthelp::regex_match(
		u"\\A[0-9a-fA-F]{64}\\z"_q,
		value,
		{}).valid();
}

[[nodiscard]] bool IsTraceId(const QString &value) {
	return qthelp::regex_match(
		u"\\A[0-9a-fA-F]{8}(-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}\\z"_q,
		value,
		{}).valid();
}

[[nodiscard]] QMap<QString, QString> ParseQuery(QString query) {
	const auto fragment = query.indexOf('#');
	if (fragment >= 0) {
		query.truncate(fragment);
	}
	auto result = QMap<QString, QString>();
	const auto pairs = query.split('&', Qt::SkipEmptyParts);
	for (const auto &pair : pairs) {
		const auto separator = pair.indexOf('=');
		const auto name = qthelp::url_decode(
			(separator < 0) ? pair : pair.mid(0, separator));
		if (!name.isEmpty() && !result.contains(name)) {
			result.insert(
				name,
				((separator < 0)
					? QString()
					: qthelp::url_decode(pair.mid(separator + 1))));
		}
	}
	return result;
}

[[nodiscard]] bool ParseConnectRequest(
		const QString &json,
		TonConnectLink &link) {
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(json.toUtf8(), &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		return false;
	}
	const auto object = document.object();
	const auto manifestUrl = object.value(u"manifestUrl"_q).toString();
	const auto items = object.value(u"items"_q);
	if (!ValidHttpsUrl(manifestUrl) || !items.isArray()) {
		return false;
	}
	auto address = false;
	const auto array = items.toArray();
	for (const auto &value : array) {
		const auto item = value.toObject();
		const auto name = item.value(u"name"_q).toString();
		if (name == u"ton_addr"_q) {
			address = true;
		} else if (name == u"ton_proof"_q && !link.proofPayload) {
			const auto payload = item.value(u"payload"_q);
			if (!payload.isString()) {
				return false;
			}
			link.proofPayload = payload.toString();
		}
	}
	if (!address) {
		return false;
	}
	link.manifestUrl = manifestUrl;
	return true;
}

} // namespace

std::optional<QString> TonConnectStartParamQuery(const QString &startapp) {
	const auto prefix = u"tonconnect-"_q;
	if (!startapp.startsWith(prefix)) {
		return std::nullopt;
	}
	auto result = startapp.mid(prefix.size());
	// WHY: the SDK decoder restores '%' before '=' and '&'; any other
	// order would split the "--" of an escaped byte into two separators.
	result.replace(u"--"_q, u"%"_q);
	result.replace(u"__"_q, u"="_q);
	result.replace('-', '&');
	return result;
}

QString TonConnectStartParam(QString query) {
	const auto fragment = query.indexOf('#');
	if (fragment >= 0) {
		query.truncate(fragment);
	}
	auto parts = QStringList();
	query.replace('+', u"%20"_q);
	const auto pairs = query.split('&', Qt::SkipEmptyParts);
	for (const auto &pair : pairs) {
		// toPercentEncoding escapes '%' even when excluded, so split on it.
		auto chunks = pair.split('%');
		for (auto &chunk : chunks) {
			chunk = QString::fromLatin1(
				QUrl::toPercentEncoding(chunk, "=", "-._~"));
		}
		const auto part = chunks.join('%');
		// WHY: a pair that starts with an escaped byte would follow its
		// separator as "---", which decodes to "%&" instead of "&%".
		if (!part.startsWith('%')) {
			parts.push_back(part);
		}
	}
	auto result = parts.join('-');
	result.replace(u"="_q, u"__"_q);
	result.replace(u"%"_q, u"--"_q);
	return u"tonconnect-"_q + result;
}

std::optional<TonConnectLink> ParseTonConnectLink(const QString &query) {
	if (query.size() > kMaxQueryLength) {
		return std::nullopt;
	}
	const auto params = ParseQuery(query);
	const auto hasVersion = params.contains(u"v"_q);
	const auto hasId = params.contains(u"id"_q);
	const auto hasRequest = params.contains(u"r"_q);
	const auto id = params.value(u"id"_q);
	if ((hasVersion && params.value(u"v"_q) != u"2"_q)
		|| (hasId && !IsClientId(id))
		|| (hasRequest && (!hasVersion || !hasId))) {
		return std::nullopt;
	}
	auto result = TonConnectLink{
		.kind = (hasRequest
			? TonConnectLinkKind::Connect
			: hasId
			? TonConnectLinkKind::OpenPending
			: TonConnectLinkKind::Bare),
		.clientId = id,
		.ret = params.value(u"ret"_q),
	};
	if (hasRequest
		&& !ParseConnectRequest(params.value(u"r"_q), result)) {
		return std::nullopt;
	}
	const auto traceId = params.value(u"trace_id"_q);
	if (IsTraceId(traceId)) {
		result.traceId = traceId;
	}
	return result;
}

bool ValidHttpsUrl(const QString &url) {
	const auto parsed = QUrl(url, QUrl::StrictMode);
	return parsed.isValid()
		&& (parsed.scheme() == u"https"_q)
		&& !parsed.host().isEmpty()
		&& parsed.userInfo().isEmpty();
}

} // namespace Wallet
