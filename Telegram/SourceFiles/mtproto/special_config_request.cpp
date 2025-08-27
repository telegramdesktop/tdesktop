/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/special_config_request.h"

#include "mtproto/details/mtproto_rsa_public_key.h"
#include "mtproto/mtproto_dc_options.h"
#include "mtproto/mtproto_auth_key.h"
#include "base/unixtime.h"
#include "base/openssl_help.h"
#include "base/call_delayed.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>

namespace MTP::details {
namespace {

constexpr auto kSendNextTimeout = crl::time(800);

constexpr auto kPublicKey = "\
-----BEGIN RSA PUBLIC KEY-----\n\
MIIBCgKCAQEAyr+18Rex2ohtVy8sroGPBwXD3DOoKCSpjDqYoXgCqB7ioln4eDCF\n\
fOBUlfXUEvM/fnKCpF46VkAftlb4VuPDeQSS/ZxZYEGqHaywlroVnXHIjgqoxiAd\n\
192xRGreuXIaUKmkwlM9JID9WS2jUsTpzQ91L8MEPLJ/4zrBwZua8W5fECwCCh2c\n\
9G5IzzBm+otMS/YKwmR1olzRCyEkyAEjXWqBI9Ftv5eG8m0VkBzOG655WIYdyV0H\n\
fDK/NWcvGqa0w/nriMD6mDjKOryamw0OP9QuYgMN0C9xMW9y8SmP4h92OAWodTYg\n\
Y1hZCxdv6cs5UnW9+PWvS+WIbkh+GaWYxwIDAQAB\n\
-----END RSA PUBLIC KEY-----\
"_cs;

const auto kRemoteProject = "peak-vista-421";
const auto kFireProject = "reserve-5a846";
const auto kConfigKey = "ipconfig";
const auto kConfigSubKey = "v3";
const auto kApiKey = "AIzaSyC2-kAkpDsroixRXw-sTw-Wfqo4NxjMwwM";
const auto kAppId = "1:560508485281:web:4ee13a6af4e84d49e67ae0";

QString ApiDomain(const QString &service) {
	return service + ".googleapis.com";
}

QString GenerateInstanceId() {
	auto fid = bytes::array<17>();
	bytes::set_random(fid);
	fid[0] = (bytes::type(0xF0) & fid[0]) | bytes::type(0x07);
	return QString::fromLatin1(
		QByteArray::fromRawData(
			reinterpret_cast<const char*>(fid.data()),
			fid.size()
		).toBase64(QByteArray::Base64UrlEncoding).mid(0, 22));
}

QString InstanceId() {
	static const auto result = GenerateInstanceId();
	return result;
}

bool CheckPhoneByPrefixesRules(const QString &phone, const QString &rules) {
	static const auto RegExp = QRegularExpression("[^0-9]");
	const auto check = QString(phone).replace(
		RegExp,
		QString());
	auto result = false;
	for (const auto &prefix : rules.split(',')) {
		if (prefix.isEmpty()) {
			result = true;
		} else if (prefix[0] == '+' && check.startsWith(prefix.mid(1))) {
			result = true;
		} else if (prefix[0] == '-' && check.startsWith(prefix.mid(1))) {
			return false;
		}
	}
	return result;
}

QByteArray ConcatenateDnsTxtFields(const std::vector<DnsEntry> &response) {
	auto entries = QMultiMap<int, QString>();
	for (const auto &entry : response) {
		entries.insert(INT_MAX - entry.data.size(), entry.data);
	}
	return QStringList(entries.values()).join(QString()).toLatin1();
}

QByteArray ParseRemoteConfigResponse(const QByteArray &bytes) {
	auto error = QJsonParseError{ 0, QJsonParseError::NoError };
	const auto document = QJsonDocument::fromJson(bytes, &error);
	if (error.error != QJsonParseError::NoError) {
		LOG(("Config Error: Failed to parse fire response JSON, error: %1"
			).arg(error.errorString()));
		return {};
	} else if (!document.isObject()) {
		LOG(("Config Error: Not an object received in fire response JSON."));
		return {};
	}
	return document.object().value(
		"entries"
	).toObject().value(
		u"%1%2"_q.arg(kConfigKey, kConfigSubKey)
	).toString().toLatin1();
}

QByteArray ParseFireStoreResponse(const QByteArray &bytes) {
	auto error = QJsonParseError{ 0, QJsonParseError::NoError };
	const auto document = QJsonDocument::fromJson(bytes, &error);
	if (error.error != QJsonParseError::NoError) {
		LOG(("Config Error: Failed to parse fire response JSON, error: %1"
			).arg(error.errorString()));
		return {};
	} else if (!document.isObject()) {
		LOG(("Config Error: Not an object received in fire response JSON."));
		return {};
	}
	return document.object().value(
		"fields"
	).toObject().value(
		"data"
	).toObject().value(
		"stringValue"
	).toString().toLatin1();
}

QByteArray ParseRealtimeResponse(const QByteArray &bytes) {
	if (bytes.size() < 2
		|| bytes[0] != '"'
		|| bytes[bytes.size() - 1] != '"') {
		return QByteArray();
	}
	return bytes.mid(1, bytes.size() - 2);
}

[[nodiscard]] QDateTime ParseHttpDate(const QString &date) {
	// Wed, 10 Jul 2019 14:33:38 GMT
	static const auto expression = QRegularExpression(
		R"(\w\w\w, (\d\d) (\w\w\w) (\d\d\d\d) (\d\d):(\d\d):(\d\d) GMT)");
	const auto match = expression.match(date);
	if (!match.hasMatch()) {
		return QDateTime();
	}

	const auto number = [&](int index) {
		return match.capturedView(index).toInt();
	};
	const auto day = number(1);
	const auto month = [&] {
		static const auto months = {
			"Jan",
			"Feb",
			"Mar",
			"Apr",
			"May",
			"Jun",
			"Jul",
			"Aug",
			"Sep",
			"Oct",
			"Nov",
			"Dec"
		};
		const auto captured = match.capturedView(2);
		for (auto i = begin(months); i != end(months); ++i) {
			if (captured == QString(*i)) {
				return 1 + int(i - begin(months));
			}
		}
		return 0;
	}();
	const auto year = number(3);
	const auto hour = number(4);
	const auto minute = number(5);
	const auto second = number(6);
	return QDateTime(
		QDate(year, month, day),
		QTime(hour, minute, second),
		Qt::UTC);
}

} // namespace

SpecialConfigRequest::SpecialConfigRequest(
	Fn<void(
		DcId dcId,
		const std::string &ip,
		int port,
		bytes::const_span secret)> callback,
	Fn<void()> timeDoneCallback,
	bool isTestMode,
	const QString &domainString,
	const QString &phone)
: _callback(std::move(callback))
, _timeDoneCallback(std::move(timeDoneCallback))
, _domainString(domainString)
, _phone(phone) {
	Expects((_callback == nullptr) != (_timeDoneCallback == nullptr));

	_manager.setProxy(QNetworkProxy::NoProxy);

	std::random_device rd;
	const auto shuffle = [&](int from, int till) {
		Expects(till > from);

		ranges::shuffle(
			begin(_attempts) + from,
			begin(_attempts) + till,
			std::mt19937(rd()));
	};

	_attempts = {};
	_attempts.push_back({ Type::Google, "dns.google.com" });
	_attempts.push_back({ Type::Mozilla, "mozilla.cloudflare-dns.com" });
	_attempts.push_back({ Type::RemoteConfig, "firebaseremoteconfig" });
	if (!_timeDoneCallback) {
		_attempts.push_back({ Type::FireStore, "firestore" });
		for (const auto &domain : DnsDomains()) {
			_attempts.push_back({ Type::FireStore, domain, "firestore" });
		}
	}

	shuffle(0, 2);
	if (!_timeDoneCallback) {
		shuffle(_attempts.size() - (int(DnsDomains().size()) + 1), _attempts.size());
	}
	if (isTestMode) {
		_attempts.erase(ranges::remove_if(_attempts, [](
				const Attempt &attempt) {
			return (attempt.type != Type::Google)
				&& (attempt.type != Type::Mozilla);
		}), _attempts.end());
	}
	ranges::reverse(_attempts); // We go from last to first.

	sendNextRequest();
}

SpecialConfigRequest::SpecialConfigRequest(
	Fn<void(
		DcId dcId,
		const std::string &ip,
		int port,
		bytes::const_span secret)> callback,
	bool isTestMode,
	const QString &domainString,
	const QString &phone)
: SpecialConfigRequest(
	std::move(callback),
	nullptr,
	isTestMode,
	domainString,
	phone) {
}

SpecialConfigRequest::SpecialConfigRequest(
	Fn<void()> timeDoneCallback,
	bool isTestMode,
	const QString &domainString)
: SpecialConfigRequest(
	nullptr,
	std::move(timeDoneCallback),
	isTestMode,
	domainString,
	QString()) {
}

void SpecialConfigRequest::sendNextRequest() {
	Expects(!_attempts.empty());

	const auto attempt = _attempts.back();
	_attempts.pop_back();
	if (!_attempts.empty()) {
		base::call_delayed(kSendNextTimeout, this, [=] {
			sendNextRequest();
		});
	}
	performRequest(attempt);
}

void SpecialConfigRequest::performRequest(const Attempt &attempt) {
	const auto type = attempt.type;
	auto url = QUrl();
	url.setScheme(u"https"_q);
	auto request = QNetworkRequest();
	auto payload = QByteArray();
	switch (type) {
	case Type::Mozilla: {
		url.setHost(attempt.data);
		url.setPath(u"/dns-query"_q);
		url.setQuery(u"name=%1&type=16&random_padding=%2"_q.arg(
			_domainString,
			GenerateDnsRandomPadding()));
		request.setRawHeader("accept", "application/dns-json");
	} break;
	case Type::Google: {
		url.setHost(attempt.data);
		url.setPath(u"/resolve"_q);
		url.setQuery(u"name=%1&type=ANY&random_padding=%2"_q.arg(
			_domainString,
			GenerateDnsRandomPadding()));
		if (!attempt.host.isEmpty()) {
			const auto host = attempt.host + ".google.com";
			request.setRawHeader("Host", host.toLatin1());
		}
	} break;
	case Type::RemoteConfig: {
		url.setHost(ApiDomain(attempt.data));
		url.setPath((u"/v1/projects/%1/namespaces/firebase:fetch"_q
		).arg(kRemoteProject));
		url.setQuery(u"key=%1"_q.arg(kApiKey));
		payload = u"{\"app_id\":\"%1\",\"app_instance_id\":\"%2\"}"_q.arg(
			kAppId,
			InstanceId()).toLatin1();
		request.setRawHeader("Content-Type", "application/json");
	} break;
	case Type::Realtime: {
		url.setHost(kFireProject + u".%1"_q.arg(attempt.data));
		url.setPath(u"/%1%2.json"_q.arg(kConfigKey, kConfigSubKey));
	} break;
	case Type::FireStore: {
		url.setHost(attempt.host.isEmpty()
			? ApiDomain(attempt.data)
			: attempt.data);
		url.setPath((u"/v1/projects/%1/databases/(default)/documents/%2/%3"_q
		).arg(
			kFireProject,
			kConfigKey,
			kConfigSubKey));
		if (!attempt.host.isEmpty()) {
			const auto host = ApiDomain(attempt.host);
			request.setRawHeader("Host", host.toLatin1());
		}
	} break;
	default: Unexpected("Type in SpecialConfigRequest::performRequest.");
	}
	request.setUrl(url);
	request.setRawHeader("User-Agent", DnsUserAgent());
	const auto reply = _requests.emplace_back(payload.isEmpty()
		? _manager.get(request)
		: _manager.post(request, payload)
	).reply;
	connect(reply, &QNetworkReply::finished, this, [=] {
		requestFinished(type, reply);
	});
}

void SpecialConfigRequest::handleHeaderUnixtime(
		not_null<QNetworkReply*> reply) {
	if (reply->error() != QNetworkReply::NoError) {
		return;
	}
	const auto date = QString::fromLatin1([&] {
		for (const auto &pair : reply->rawHeaderPairs()) {
			if (pair.first == "Date") {
				return pair.second;
			}
		}
		return QByteArray();
	}());
	if (date.isEmpty()) {
		LOG(("Config Error: No 'Date' header received."));
		return;
	}
	const auto parsed = ParseHttpDate(date);
	if (!parsed.isValid()) {
		LOG(("Config Error: Bad 'Date' header received: %1").arg(date));
		return;
	}
	base::unixtime::http_update(parsed.toSecsSinceEpoch());
	if (_timeDoneCallback) {
		_timeDoneCallback();
	}
}

void SpecialConfigRequest::requestFinished(
		Type type,
		not_null<QNetworkReply*> reply) {
	handleHeaderUnixtime(reply);
	const auto result = finalizeRequest(reply);
	if (!_callback || result.isEmpty()) {
		return;
	}

	switch (type) {
	case Type::Mozilla:
	case Type::Google: {
		constexpr auto kTypeRestriction = 16; // TXT
		handleResponse(ConcatenateDnsTxtFields(
			ParseDnsResponse(result, kTypeRestriction)));
	} break;
	case Type::RemoteConfig: {
		handleResponse(ParseRemoteConfigResponse(result));
	} break;
	case Type::Realtime: {
		handleResponse(ParseRealtimeResponse(result));
	} break;
	case Type::FireStore: {
		handleResponse(ParseFireStoreResponse(result));
	} break;
	default: Unexpected("Type in SpecialConfigRequest::requestFinished.");
	}
}

QByteArray SpecialConfigRequest::finalizeRequest(
		not_null<QNetworkReply*> reply) {
	if (reply->error() != QNetworkReply::NoError) {
		DEBUG_LOG(("Config Error: Failed to get response, error: %2 (%3)"
			).arg(reply->errorString()
			).arg(reply->error()));
	}
	const auto result = reply->readAll();
	const auto from = ranges::remove(
		_requests,
		reply,
		[](const ServiceWebRequest &request) { return request.reply; });
	_requests.erase(from, end(_requests));
	return result;
}

bool SpecialConfigRequest::decryptSimpleConfig(const QByteArray &bytes) {
	auto cleanBytes = bytes;
	auto removeFrom = std::remove_if(cleanBytes.begin(), cleanBytes.end(), [](char ch) {
		auto isGoodBase64 = (ch == '+') || (ch == '=') || (ch == '/')
			|| (ch >= 'a' && ch <= 'z')
			|| (ch >= 'A' && ch <= 'Z')
			|| (ch >= '0' && ch <= '9');
		return !isGoodBase64;
	});
	if (removeFrom != cleanBytes.end()) {
		cleanBytes.remove(removeFrom - cleanBytes.begin(), cleanBytes.end() - removeFrom);
	}

	constexpr auto kGoodSizeBase64 = 344;
	if (cleanBytes.size() != kGoodSizeBase64) {
		LOG(("Config Error: Bad data size %1 required %2").arg(cleanBytes.size()).arg(kGoodSizeBase64));
		return false;
	}
	constexpr auto kGoodSizeData = 256;
	auto decodedBytes = QByteArray::fromBase64(cleanBytes, QByteArray::Base64Encoding);
	if (decodedBytes.size() != kGoodSizeData) {
		LOG(("Config Error: Bad data size %1 required %2").arg(decodedBytes.size()).arg(kGoodSizeData));
		return false;
	}

	auto publicKey = details::RSAPublicKey(bytes::make_span(kPublicKey));
	auto decrypted = publicKey.decrypt(bytes::make_span(decodedBytes));
	auto decryptedBytes = gsl::make_span(decrypted);

	auto aesEncryptedBytes = decryptedBytes.subspan(CTRState::KeySize);
	auto aesivec = bytes::make_vector(decryptedBytes.subspan(CTRState::KeySize - CTRState::IvecSize, CTRState::IvecSize));
	AES_KEY aeskey;
	AES_set_decrypt_key(reinterpret_cast<const unsigned char*>(decryptedBytes.data()), CTRState::KeySize * CHAR_BIT, &aeskey);
	AES_cbc_encrypt(reinterpret_cast<const unsigned char*>(aesEncryptedBytes.data()), reinterpret_cast<unsigned char*>(aesEncryptedBytes.data()), aesEncryptedBytes.size(), &aeskey, reinterpret_cast<unsigned char*>(aesivec.data()), AES_DECRYPT);

	constexpr auto kDigestSize = 16;
	auto dataSize = aesEncryptedBytes.size() - kDigestSize;
	auto data = aesEncryptedBytes.subspan(0, dataSize);
	auto hash = openssl::Sha256(data);
	if (bytes::compare(gsl::make_span(hash).subspan(0, kDigestSize), aesEncryptedBytes.subspan(dataSize)) != 0) {
		LOG(("Config Error: Bad digest."));
		return false;
	}

	mtpBuffer buffer;
	buffer.resize(data.size() / sizeof(mtpPrime));
	bytes::copy(bytes::make_span(buffer), data);
	auto from = &*buffer.cbegin();
	auto end = from + buffer.size();
	auto realLength = *from++;
	if (realLength <= 0 || realLength > dataSize || (realLength & 0x03)) {
		LOG(("Config Error: Bad length %1.").arg(realLength));
		return false;
	}

	if (!_simpleConfig.read(from, end)) {
		LOG(("Config Error: Could not read configSimple."));
		return false;
	}
	if ((end - from) * sizeof(mtpPrime) != (dataSize - realLength)) {
		LOG(("Config Error: Bad read length %1, should be %2.").arg((end - from) * sizeof(mtpPrime)).arg(dataSize - realLength));
		return false;
	}
	return true;
}

void SpecialConfigRequest::handleResponse(const QByteArray &bytes) {
	if (!decryptSimpleConfig(bytes)) {
		return;
	}
	Assert(_simpleConfig.type() == mtpc_help_configSimple);
	const auto &config = _simpleConfig.c_help_configSimple();
	const auto now = base::unixtime::http_now();
	if (now > config.vexpires().v) {
		LOG(("Config Error: "
			"Bad date frame for simple config: %1-%2, our time is %3."
			).arg(config.vdate().v
			).arg(config.vexpires().v
			).arg(now));
		return;
	}
	if (config.vrules().v.empty()) {
		LOG(("Config Error: Empty simple config received."));
		return;
	}
	for (const auto &rule : config.vrules().v) {
		Assert(rule.type() == mtpc_accessPointRule);
		const auto &data = rule.c_accessPointRule();
		const auto phoneRules = qs(data.vphone_prefix_rules());
		if (!CheckPhoneByPrefixesRules(_phone, phoneRules)) {
			continue;
		}

		const auto dcId = data.vdc_id().v;
		for (const auto &address : data.vips().v) {
			const auto parseIp = [](const MTPint &ipv4) {
				const auto ip = *reinterpret_cast<const uint32*>(&ipv4.v);
				return (u"%1.%2.%3.%4"_q
				).arg((ip >> 24) & 0xFF
				).arg((ip >> 16) & 0xFF
				).arg((ip >> 8) & 0xFF
				).arg(ip & 0xFF).toStdString();
			};
			switch (address.type()) {
			case mtpc_ipPort: {
				const auto &fields = address.c_ipPort();
				const auto ip = parseIp(fields.vipv4());
				if (!ip.empty()) {
					_callback(dcId, ip, fields.vport().v, {});
				}
			} break;
			case mtpc_ipPortSecret: {
				const auto &fields = address.c_ipPortSecret();
				const auto ip = parseIp(fields.vipv4());
				if (!ip.empty()) {
					_callback(
						dcId,
						ip,
						fields.vport().v,
						bytes::make_span(fields.vsecret().v));
				}
			} break;
			default: Unexpected("Type in simpleConfig ips.");
			}
		}
	}
	_callback(0, std::string(), 0, {});
}

} // namespace MTP::details
