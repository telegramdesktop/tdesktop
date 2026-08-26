/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "gram/api/gram_api_stream.h"

#include "base/basic_types.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtCore/QUrl>

#include <algorithm>

namespace Gram {
namespace {

constexpr auto kSecurePort = 443;
constexpr auto kMaxPort = 65535;

const auto kType = u"type"_q;
const auto kAccount = u"account"_q;
const auto kState = u"state"_q;
const auto kBalance = u"balance"_q;
const auto kTransactions = u"transactions"_q;
const auto kTraceHash = u"trace_external_hash_norm"_q;
const auto kAccountStateChange = u"account_state_change"_q;
const auto kTraceInvalidated = u"trace_invalidated"_q;

void AppendAccount(
		std::vector<QString> &accounts,
		const QJsonValue &value) {
	if (!value.isString()) {
		return;
	}
	const auto account = value.toString();
	if (account.isEmpty()) {
		return;
	}
	const auto i = std::find(accounts.begin(), accounts.end(), account);
	if (i == accounts.end()) {
		accounts.push_back(account);
	}
}

[[nodiscard]] StreamEvent ReadAccountStateEvent(const QJsonObject &object) {
	const auto account = object.value(kAccount);
	if (!account.isString() || account.toString().isEmpty()) {
		return StreamEvent();
	}
	const auto state = object.value(kState);
	if (!state.isObject() || !state.toObject().value(kBalance).isString()) {
		return StreamEvent();
	}
	auto result = StreamEvent();
	result.kind = StreamEventKind::AccountState;
	AppendAccount(result.accounts, account);
	return result;
}

[[nodiscard]] StreamEvent ReadTransactionsEvent(const QJsonObject &object) {
	const auto transactions = object.value(kTransactions);
	if (!object.value(kTraceHash).isString() || !transactions.isArray()) {
		return StreamEvent();
	}
	auto result = StreamEvent();
	result.kind = StreamEventKind::Transactions;
	for (const auto &entry : transactions.toArray()) {
		AppendAccount(result.accounts, entry.toObject().value(kAccount));
	}
	return result;
}

[[nodiscard]] StreamEvent ReadTraceInvalidatedEvent(
		const QJsonObject &object) {
	if (!object.value(kTraceHash).isString()) {
		return StreamEvent();
	}
	auto result = StreamEvent();
	result.kind = StreamEventKind::TraceInvalidated;
	return result;
}

} // namespace

std::optional<StreamEndpoint> ParseStreamEndpoint(const QString &url) {
	const auto parsed = QUrl(url, QUrl::StrictMode);
	const auto scheme = parsed.scheme();
	if (!parsed.isValid()
		|| (scheme != u"wss"_q && scheme != u"https"_q)
		|| parsed.host().isEmpty()
		|| !parsed.userName().isEmpty()
		|| !parsed.password().isEmpty()) {
		return std::nullopt;
	}
	const auto port = parsed.port(kSecurePort);
	if (port < 1 || port > kMaxPort) {
		return std::nullopt;
	}
	const auto path = parsed.path(QUrl::FullyEncoded);
	auto result = StreamEndpoint();
	result.host = parsed.host();
	result.port = port;
	result.requestTarget = path.isEmpty() ? u"/"_q : path;
	if (parsed.hasQuery()) {
		result.requestTarget += u"?"_q + parsed.query(QUrl::FullyEncoded);
	}
	return result;
}

QString StreamEndpointLabel(const StreamEndpoint &endpoint) {
	return endpoint.host + u":"_q + QString::number(endpoint.port);
}

StreamEvent ParseStreamEvent(const QByteArray &frame) {
	const auto document = QJsonDocument::fromJson(frame);
	if (document.isNull() || !document.isObject()) {
		return StreamEvent();
	}
	const auto object = document.object();
	const auto type = object.value(kType);
	if (!type.isString()) {
		return StreamEvent();
	}
	const auto kind = type.toString();
	if (kind == kAccountStateChange) {
		return ReadAccountStateEvent(object);
	} else if (kind == kTransactions) {
		return ReadTransactionsEvent(object);
	} else if (kind == kTraceInvalidated) {
		return ReadTraceInvalidatedEvent(object);
	}
	return StreamEvent();
}

QByteArray StreamSubscribeMessage(const QString &account, quint32 id) {
	auto types = QJsonArray();
	types.append(kAccountStateChange);
	types.append(kTransactions);
	auto addresses = QJsonArray();
	addresses.append(account);
	auto object = QJsonObject();
	object.insert(u"operation"_q, u"subscribe"_q);
	object.insert(u"id"_q, u"sub-"_q + QString::number(id));
	object.insert(u"types"_q, types);
	object.insert(u"addresses"_q, addresses);
	object.insert(u"min_finality"_q, u"pending"_q);
	return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray StreamPingMessage(quint32 id) {
	auto object = QJsonObject();
	object.insert(u"operation"_q, u"ping"_q);
	object.insert(u"id"_q, u"ping-"_q + QString::number(id));
	return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

} // namespace Gram
