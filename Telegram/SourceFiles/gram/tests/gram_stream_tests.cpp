/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "gram/tests/gram_tests.h"

#include "gram/api/gram_api_stream.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

#include <vector>

namespace Gram::Tests {
namespace {

const auto kAccountRaw = u"0:9DA971AF38D2F03ABDF308D5F91636A9"
	u"7E5A2B07A66C39D71D7CBAE3B032EDDC"_q;
const auto kOtherRaw = u"0:CA0CFD519F763102B5BEC9D9E3AF4359"
	u"2EA362FB773FA319EA09C4F162C171E0"_q;

// The non-bounceable user-friendly forms of the two accounts above,
// derived from the TEP-2 encoding and recorded in the task evidence
// rather than computed here: the parser no longer converts between the
// two representations, so nothing in this file can produce them.
const auto kAccountFriendly =
	u"UQCdqXGvONLwOr3zCNX5FjapflorB6ZsOdcdfLrjsDLt3AF4"_q;
const auto kOtherFriendly =
	u"UQDKDP1Rn3YxArW-ydnjr0NZLqNi-3c_oxnqCcTxYsFx4NjV"_q;
const auto kTraceHash = u"HCSgz9CwJYQJXdDbo03QVft6KJ7RtG5js9Br8qPDCJ4="_q;
const auto kSecret = u"f4ke-streaming-secret-0123456789"_q;

[[nodiscard]] QString KindName(StreamEventKind kind) {
	switch (kind) {
	case StreamEventKind::Unknown: return u"Unknown"_q;
	case StreamEventKind::AccountState: return u"AccountState"_q;
	case StreamEventKind::Transactions: return u"Transactions"_q;
	case StreamEventKind::TraceInvalidated: return u"TraceInvalidated"_q;
	}
	return u"Invalid"_q;
}

[[nodiscard]] QString Printed(const std::vector<QString> &accounts) {
	auto result = QString();
	for (const auto &account : accounts) {
		if (!result.isEmpty()) {
			result += u", "_q;
		}
		result += account;
	}
	return result.isEmpty() ? u"none"_q : result;
}

[[nodiscard]] QString CheckEvent(
		const StreamEvent &event,
		StreamEventKind kind,
		const std::vector<QString> &accounts) {
	if (event.kind != kind) {
		return u"kind: got "_q
			+ KindName(event.kind)
			+ u", expected "_q
			+ KindName(kind);
	} else if (event.accounts != accounts) {
		return u"accounts: got "_q
			+ Printed(event.accounts)
			+ u", expected "_q
			+ Printed(accounts);
	}
	return QString();
}

[[nodiscard]] QString CheckUnknown(const QByteArray &frame) {
	const auto failure = CheckEvent(
		ParseStreamEvent(frame),
		StreamEventKind::Unknown,
		{});
	return failure.isEmpty()
		? QString()
		: (failure + u" for: "_q + QString::fromUtf8(frame));
}

[[nodiscard]] QString CheckEndpoint(
		const QString &url,
		const QString &host,
		int port,
		const QString &requestTarget) {
	const auto parsed = ParseStreamEndpoint(url);
	if (!parsed) {
		return u"parse failed: "_q + url;
	} else if (parsed->host != host) {
		return u"host: got "_q + parsed->host + u", expected "_q + host;
	} else if (parsed->port != port) {
		return u"port: got "_q
			+ QString::number(parsed->port)
			+ u", expected "_q
			+ QString::number(port);
	} else if (parsed->requestTarget != requestTarget) {
		return u"requestTarget: got "_q
			+ parsed->requestTarget
			+ u", expected "_q
			+ requestTarget;
	}
	return QString();
}

[[nodiscard]] QByteArray AccountStateFrame(const QString &account) {
	return (u"{\"type\":\"account_state_change\",\"account\":\""_q
		+ account
		+ u"\",\"state\":{\"balance\":\"1000000000\"}}"_q).toUtf8();
}

} // namespace

std::vector<Check> StreamChecks() {
	return {
		{ u"stream_endpoint_valid"_q, [] {
			const auto standard = CheckEndpoint(
				u"wss://toncenter.com/api/streaming/v2/ws?token=abc"_q,
				u"toncenter.com"_q,
				443,
				u"/api/streaming/v2/ws?token=abc"_q);
			if (!standard.isEmpty()) {
				return u"standard: "_q + standard;
			}
			const auto ported = CheckEndpoint(
				u"wss://toncenter.com:8443/x"_q,
				u"toncenter.com"_q,
				8443,
				u"/x"_q);
			if (!ported.isEmpty()) {
				return u"ported: "_q + ported;
			}
			const auto rootless = CheckEndpoint(
				u"wss://toncenter.com"_q,
				u"toncenter.com"_q,
				443,
				u"/"_q);
			if (!rootless.isEmpty()) {
				return u"rootless: "_q + rootless;
			}
			const auto cased = CheckEndpoint(
				u"WSS://TonCenter.com/x"_q,
				u"toncenter.com"_q,
				443,
				u"/x"_q);
			if (!cased.isEmpty()) {
				return u"cased: "_q + cased;
			}
			const auto multi = CheckEndpoint(
				u"wss://testnet.toncenter.com:443/api/streaming/v2/ws"
				u"?api_key=a&x=b"_q,
				u"testnet.toncenter.com"_q,
				443,
				u"/api/streaming/v2/ws?api_key=a&x=b"_q);
			return multi.isEmpty() ? QString() : (u"multi: "_q + multi);
		} },
		{ u"stream_endpoint_https"_q, [] {
			const auto issued = CheckEndpoint(
				u"https://ton.example.com/s/api/streaming/?token="_q
				+ kSecret,
				u"ton.example.com"_q,
				443,
				u"/s/api/streaming/?token="_q + kSecret);
			if (!issued.isEmpty()) {
				return u"issued: "_q + issued;
			}
			const auto ported = CheckEndpoint(
				u"https://ton.example.com:8443/x"_q,
				u"ton.example.com"_q,
				8443,
				u"/x"_q);
			if (!ported.isEmpty()) {
				return u"ported: "_q + ported;
			}
			const auto cased = CheckEndpoint(
				u"HTTPS://Ton.Example.com"_q,
				u"ton.example.com"_q,
				443,
				u"/"_q);
			if (!cased.isEmpty()) {
				return u"cased: "_q + cased;
			}
			const auto tail = u"//ton.example.com/ws?token="_q + kSecret;
			const auto secure = ParseStreamEndpoint(u"https:"_q + tail);
			const auto socket = ParseStreamEndpoint(u"wss:"_q + tail);
			if (!secure || !socket) {
				return u"both secure schemes must parse"_q;
			} else if (secure->host != socket->host
				|| secure->port != socket->port
				|| secure->requestTarget != socket->requestTarget) {
				return u"https must map onto the same tls endpoint as wss, "
					u"because a secure websocket upgrade is an https origin "
					u"get request"_q;
			}
			const auto insecure = std::vector<QString>{
				u"http://ton.example.com/x"_q,
				u"http://ton.example.com:443/x"_q,
				u"ws://ton.example.com/x"_q,
				u"ws://ton.example.com:443/x"_q,
			};
			for (const auto &url : insecure) {
				if (ParseStreamEndpoint(url)) {
					return u"a plaintext scheme must stay rejected: "_q + url;
				}
			}
			return QString();
		} },
		{ u"stream_endpoint_negative"_q, [] {
			const auto bad = std::vector<QString>{
				QString(),
				u"ws://toncenter.com/x"_q,
				u"http://toncenter.com/x"_q,
				u"ftp://toncenter.com/x"_q,
				u"wss:///x"_q,
				u"wss://"_q,
				u"https:///x"_q,
				u"https://"_q,
				u"toncenter.com/x"_q,
				u"/api/streaming/v2/ws"_q,
				u"not a url"_q,
				u"wss://user:pass@toncenter.com/x"_q,
				u"https://user:pass@toncenter.com/x"_q,
				u"wss://toncenter.com:0/x"_q,
				u"wss://toncenter.com:99999/x"_q,
				u"https://toncenter.com:0/x"_q,
				u"https://toncenter.com:99999/x"_q,
			};
			for (const auto &url : bad) {
				if (ParseStreamEndpoint(url)) {
					return u"expected nullopt for: "_q + url;
				}
			}
			return QString();
		} },
		{ u"stream_endpoint_label_redaction"_q, [] {
			const auto url =
				u"wss://toncenter.com/api/streaming/v2/ws?api_key="_q
				+ kSecret;
			const auto endpoint = ParseStreamEndpoint(url);
			if (!endpoint) {
				return u"parse failed for a secret carrying url"_q;
			}
			const auto label = StreamEndpointLabel(*endpoint);
			if (label != u"toncenter.com:443"_q) {
				return u"label: got "_q
					+ label
					+ u", expected toncenter.com:443"_q;
			} else if (label.contains(QChar('?'))
				|| label.contains(QChar('='))
				|| label.contains(QChar('&'))) {
				return u"label carries query punctuation: "_q + label;
			} else if (label.contains(kSecret)) {
				return u"label carries the secret"_q;
			}
			const auto expected =
				u"/api/streaming/v2/ws?api_key="_q + kSecret;
			if (endpoint->requestTarget != expected) {
				return u"requestTarget must stay the verbatim path and "
					u"query, because it is the only place the secret is "
					u"allowed to reach"_q;
			}
			const auto ported = ParseStreamEndpoint(
				u"wss://toncenter.com:8443/ws?api_key="_q + kSecret);
			if (!ported) {
				return u"parse failed for a ported secret carrying url"_q;
			}
			const auto portedLabel = StreamEndpointLabel(*ported);
			if (portedLabel != u"toncenter.com:8443"_q) {
				return u"ported label: got "_q
					+ portedLabel
					+ u", expected toncenter.com:8443"_q;
			}
			const auto secure = ParseStreamEndpoint(
				u"https://ton.example.com/s/api/streaming/?token="_q
				+ kSecret);
			if (!secure) {
				return u"parse failed for a secret carrying https url"_q;
			}
			const auto secureLabel = StreamEndpointLabel(*secure);
			if (secureLabel != u"ton.example.com:443"_q) {
				return u"https label: got "_q
					+ secureLabel
					+ u", expected ton.example.com:443"_q;
			} else if (secureLabel.contains(kSecret)) {
				return u"https label carries the secret"_q;
			}
			return QString();
		} },
		{ u"stream_event_account_state"_q, [] {
			// The claim this check makes: the parser carries the account
			// string the provider sent, verbatim. It does not parse it, it
			// does not fold a user-friendly form into the raw one, and it
			// does not reject a value that is not an address at all.
			// Deciding which of those strings is ours moved out of td_gram
			// and into Wallet::Stream::mine().
			const auto raw = CheckEvent(
				ParseStreamEvent(AccountStateFrame(kAccountRaw)),
				StreamEventKind::AccountState,
				{ kAccountRaw });
			if (!raw.isEmpty()) {
				return u"raw: "_q + raw;
			}
			const auto friendly = CheckEvent(
				ParseStreamEvent(AccountStateFrame(kAccountFriendly)),
				StreamEventKind::AccountState,
				{ kAccountFriendly });
			if (!friendly.isEmpty()) {
				return u"friendly: "_q + friendly;
			}
			const auto unparsed = CheckEvent(
				ParseStreamEvent(AccountStateFrame(u"address"_q)),
				StreamEventKind::AccountState,
				{ u"address"_q });
			return unparsed.isEmpty()
				? QString()
				: (u"unparsed: "_q + unparsed);
		} },
		{ u"stream_event_transactions"_q, [] {
			// The claim this check makes: accounts are deduplicated by
			// string and by nothing else. The same raw account twice
			// collapses to one entry, while an account named in raw and in
			// user-friendly form stays two entries, because the parser no
			// longer normalizes one representation into the other. That
			// normalization moved to Wallet::Stream::mine(). A value that
			// is not a non-empty string stays the only thing skipped.
			const auto frame = (u"{\"type\":\"transactions\","
				u"\"finality\":\"pending\","
				u"\"trace_external_hash_norm\":\""_q
				+ kTraceHash
				+ u"\",\"transactions\":[{\"account\":\""_q
				+ kAccountRaw
				+ u"\"},{\"account\":\""_q
				+ kAccountRaw
				+ u"\"},{\"account\":\""_q
				+ kAccountFriendly
				+ u"\"},{\"account\":\""_q
				+ kOtherRaw
				+ u"\"},{\"account\":\""_q
				+ kOtherFriendly
				+ u"\"},{\"account\":\"nonsense\"},"
				u"{\"account\":\"\"},{\"account\":42},{}]}"_q).toUtf8();
			const auto deduped = CheckEvent(
				ParseStreamEvent(frame),
				StreamEventKind::Transactions,
				{
					kAccountRaw,
					kAccountFriendly,
					kOtherRaw,
					kOtherFriendly,
					u"nonsense"_q,
				});
			if (!deduped.isEmpty()) {
				return u"deduped: "_q + deduped;
			}
			const auto empty = CheckEvent(
				ParseStreamEvent(
					(u"{\"type\":\"transactions\","
						u"\"trace_external_hash_norm\":\""_q
						+ kTraceHash
						+ u"\",\"transactions\":[]}"_q).toUtf8()),
				StreamEventKind::Transactions,
				{});
			return empty.isEmpty() ? QString() : (u"empty: "_q + empty);
		} },
		{ u"stream_event_trace_invalidated"_q, [] {
			const auto frame = (u"{\"type\":\"trace_invalidated\","
				u"\"trace_external_hash_norm\":\""_q
				+ kTraceHash
				+ u"\"}"_q).toUtf8();
			return CheckEvent(
				ParseStreamEvent(frame),
				StreamEventKind::TraceInvalidated,
				{});
		} },
		{ u"stream_event_negative"_q, [] {
			const auto bad = std::vector<QByteArray>{
				QByteArray(""),
				QByteArray("null"),
				QByteArray("42"),
				QByteArray("[]"),
				QByteArray("{"),
				QByteArray("{}"),
				QByteArray("not json"),
				QByteArray(R"("account_state_change")"),
				QByteArray(R"({"status":"pong"})"),
				QByteArray(R"({"status":"subscribed"})"),
				QByteArray(R"({"type":"wrong"})"),
				QByteArray(R"({"type":123})"),
				QByteArray(R"({"type":"account_state_change"})"),
				QByteArray(R"({"type":"account_state_change",)"
					R"("account":123})"),
				QByteArray(R"({"type":"account_state_change",)"
					R"("account":"addr","state":null})"),
				QByteArray(R"({"type":"account_state_change",)"
					R"("account":"addr","state":{}})"),
				QByteArray(R"({"type":"account_state_change",)"
					R"("account":"addr","state":{"balance":100}})"),
				QByteArray(R"({"type":"account_state_change",)"
					R"("account":"addr","state":[{"balance":"1"}]})"),
				QByteArray(R"({"type":"account_state_change",)"
					R"("account":"","state":{"balance":"1"}})"),
				QByteArray(R"({"type":"transactions"})"),
				QByteArray(R"({"type":"transactions",)"
					R"("trace_external_hash_norm":123})"),
				QByteArray(R"({"type":"transactions",)"
					R"("trace_external_hash_norm":"h","transactions":{}})"),
				QByteArray(R"({"type":"transactions",)"
					R"("trace_external_hash_norm":"h","transactions":[)"),
				QByteArray(R"({"type":"trace_invalidated"})"),
				QByteArray(R"({"type":"trace_invalidated",)"
					R"("trace_external_hash_norm":123})"),
				QByteArray(R"({"type":"jettons_change","jetton":)"
					R"({"address":"0:jetton","owner":"0:owner"},)"
					R"("jetton_wallets":[]})"),
				QByteArray(R"({"type":"jettons_change","jetton":null})"),
				QByteArray(R"({"type":"jettons_change","jetton":{}})"),
				QByteArray(R"({"type":"jettons_change",)"
					R"("jetton":{"address":"a"}})"),
				QByteArray(R"({"type":"jettons_change",)"
					R"("jetton":{"address":"a","owner":1}})"),
			};
			for (const auto &frame : bad) {
				const auto failure = CheckUnknown(frame);
				if (!failure.isEmpty()) {
					return failure;
				}
			}
			return QString();
		} },
		{ u"stream_subscribe_message"_q, [] {
			// The claim this check makes: the subscribe payload carries the
			// account string it is handed, verbatim. The wallet subscribes
			// with the derived non-bounceable user-friendly form, which is
			// also the form the provider echoes back in its frames, and
			// td_gram no longer converts between representations at all.
			const auto account = kAccountFriendly;
			const auto message = StreamSubscribeMessage(account, 7);
			if (message.contains('\n')) {
				return u"not compact: "_q + QString::fromUtf8(message);
			}
			const auto document = QJsonDocument::fromJson(message);
			if (!document.isObject()) {
				return u"not a json object: "_q
					+ QString::fromUtf8(message);
			}
			const auto object = document.object();
			if (object.value(u"operation"_q).toString() != u"subscribe"_q) {
				return u"operation: got "_q
					+ object.value(u"operation"_q).toString()
					+ u", expected subscribe"_q;
			} else if (object.value(u"id"_q).toString() != u"sub-7"_q) {
				return u"id: got "_q
					+ object.value(u"id"_q).toString()
					+ u", expected sub-7"_q;
			} else if (object.value(u"min_finality"_q).toString()
				!= u"pending"_q) {
				return u"min_finality: got "_q
					+ object.value(u"min_finality"_q).toString()
					+ u", expected pending, which is what makes an "
					u"outgoing transfer visible before confirmation"_q;
			}
			const auto types = object.value(u"types"_q).toArray();
			if (types.size() != 2
				|| !types.contains(u"account_state_change"_q)
				|| !types.contains(u"transactions"_q)) {
				return u"types: got "_q + QString::fromUtf8(message);
			}
			const auto addresses = object.value(u"addresses"_q).toArray();
			if (addresses.size() != 1
				|| addresses.at(0).toString() != account) {
				return u"addresses: got "_q + QString::fromUtf8(message);
			} else if (object.contains(u"include_metadata"_q)) {
				return u"include_metadata inflates every frame with detail "
					u"the wallet never reads"_q;
			} else if (object.size() != 5) {
				return u"fields: got "_q
					+ QString::number(object.size())
					+ u", expected 5: "_q
					+ QString::fromUtf8(message);
			}
			return QString();
		} },
		{ u"stream_ping_message"_q, [] {
			const auto message = StreamPingMessage(3);
			if (message.contains('\n')) {
				return u"not compact: "_q + QString::fromUtf8(message);
			}
			const auto document = QJsonDocument::fromJson(message);
			if (!document.isObject()) {
				return u"not a json object: "_q
					+ QString::fromUtf8(message);
			}
			const auto object = document.object();
			if (object.value(u"operation"_q).toString() != u"ping"_q) {
				return u"operation: got "_q
					+ object.value(u"operation"_q).toString()
					+ u", expected ping"_q;
			} else if (object.value(u"id"_q).toString() != u"ping-3"_q) {
				return u"id: got "_q
					+ object.value(u"id"_q).toString()
					+ u", expected ping-3"_q;
			} else if (object.size() != 2) {
				return u"fields: got "_q
					+ QString::number(object.size())
					+ u", expected 2: "_q
					+ QString::fromUtf8(message);
			}
			return QString();
		} },
	};
}

} // namespace Gram::Tests
