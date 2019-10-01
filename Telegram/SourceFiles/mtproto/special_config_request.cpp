/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/special_config_request.h"

#include "mtproto/rsa_public_key.h"
#include "mtproto/dc_options.h"
#include "mtproto/auth_key.h"
#include "base/unixtime.h"
#include "base/openssl_help.h"
#include "facades.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>

extern "C" {
#include <openssl/aes.h>
} // extern "C"

namespace MTP {
namespace {

constexpr auto kSendNextTimeout = crl::time(800);
constexpr auto kMinTimeToLive = 10 * crl::time(1000);
constexpr auto kMaxTimeToLive = 300 * crl::time(1000);

constexpr auto kPublicKey = str_const("\
-----BEGIN RSA PUBLIC KEY-----\n\
MIIBCgKCAQEAyr+18Rex2ohtVy8sroGPBwXD3DOoKCSpjDqYoXgCqB7ioln4eDCF\n\
fOBUlfXUEvM/fnKCpF46VkAftlb4VuPDeQSS/ZxZYEGqHaywlroVnXHIjgqoxiAd\n\
192xRGreuXIaUKmkwlM9JID9WS2jUsTpzQ91L8MEPLJ/4zrBwZua8W5fECwCCh2c\n\
9G5IzzBm+otMS/YKwmR1olzRCyEkyAEjXWqBI9Ftv5eG8m0VkBzOG655WIYdyV0H\n\
fDK/NWcvGqa0w/nriMD6mDjKOryamw0OP9QuYgMN0C9xMW9y8SmP4h92OAWodTYg\n\
Y1hZCxdv6cs5UnW9+PWvS+WIbkh+GaWYxwIDAQAB\n\
-----END RSA PUBLIC KEY-----\
");

constexpr auto kUserAgent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
"AppleWebKit/537.36 (KHTML, like Gecko) Chrome/77.0.3865.90 Safari/537.36";

const auto kRemoteProject = "peak-vista-421";
const auto kFireProject = "reserve-5a846";
const auto kConfigKey = "ipconfig";
const auto kConfigSubKey = "v3";
const auto kApiKey = "AIzaSyC2-kAkpDsroixRXw-sTw-Wfqo4NxjMwwM";
const auto kAppId = "1:560508485281:web:4ee13a6af4e84d49e67ae0";

struct DnsEntry {
	QString data;
	crl::time TTL = 0;
};

const std::vector<QString> &DnsDomains() {
	static auto result = std::vector<QString>{
		qsl("google.com"),
		qsl("www.google.com"),
		qsl("google.ru"),
		qsl("www.google.ru"),
	};
	return result;
}

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
	const auto check = QString(phone).replace(
		QRegularExpression("[^0-9]"),
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

QString GenerateRandomPadding() {
	constexpr char kValid[] = "abcdefghijklmnopqrstuvwxyz"
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

	auto result = QString();
	const auto count = [&] {
		constexpr auto kMinPadding = 13;
		constexpr auto kMaxPadding = 128;
		while (true) {
			const auto result = 1 + (rand_value<uchar>() / 2);
			Assert(result <= kMaxPadding);
			if (result >= kMinPadding) {
				return result;
			}
		}
	}();
	result.resize(count);
	for (auto &ch : result) {
		ch = kValid[rand_value<uchar>() % (sizeof(kValid) - 1)];
	}
	return result;
}

std::vector<DnsEntry> ParseDnsResponse(
		const QByteArray &bytes,
		std::optional<int> typeRestriction = std::nullopt) {
	// Read and store to "result" all the data bytes from the response:
	// { ..,
	//   "Answer": [
	//     { .., "data": "bytes1", "TTL": int, .. },
	//     { .., "data": "bytes2", "TTL": int, .. }
	//   ],
	// .. }
	auto error = QJsonParseError{ 0, QJsonParseError::NoError };
	const auto document = QJsonDocument::fromJson(bytes, &error);
	if (error.error != QJsonParseError::NoError) {
		LOG(("Config Error: Failed to parse dns response JSON, error: %1"
			).arg(error.errorString()));
		return {};
	} else if (!document.isObject()) {
		LOG(("Config Error: Not an object received in dns response JSON."));
		return {};
	}
	const auto response = document.object();
	const auto answerIt = response.find(qsl("Answer"));
	if (answerIt == response.constEnd()) {
		LOG(("Config Error: Could not find Answer in dns response JSON."));
		return {};
	} else if (!(*answerIt).isArray()) {
		LOG(("Config Error: Not an array received "
			"in Answer in dns response JSON."));
		return {};
	}

	auto result = std::vector<DnsEntry>();
	for (const auto elem : (*answerIt).toArray()) {
		if (!elem.isObject()) {
			LOG(("Config Error: Not an object found "
				"in Answer array in dns response JSON."));
			continue;
		}
		const auto object = elem.toObject();
		if (typeRestriction) {
			const auto typeIt = object.find(qsl("type"));
			const auto type = int(std::round((*typeIt).toDouble()));
			if (!(*typeIt).isDouble()) {
				LOG(("Config Error: Not a number in type field "
					"in Answer array in dns response JSON."));
				continue;
			} else if (type != *typeRestriction) {
				continue;
			}
		}
		const auto dataIt = object.find(qsl("data"));
		if (dataIt == object.constEnd()) {
			LOG(("Config Error: Could not find data "
				"in Answer array entry in dns response JSON."));
			continue;
		} else if (!(*dataIt).isString()) {
			LOG(("Config Error: Not a string data found "
				"in Answer array entry in dns response JSON."));
			continue;
		}

		const auto ttlIt = object.find(qsl("TTL"));
		const auto ttl = (ttlIt != object.constEnd())
			? crl::time(std::round((*ttlIt).toDouble()))
			: crl::time(0);
		result.push_back({ (*dataIt).toString(), ttl });
	}
	return result;
}

QByteArray ConcatenateDnsTxtFields(const std::vector<DnsEntry> &response) {
	auto entries = QMap<int, QString>();
	for (const auto &entry : response) {
		entries.insertMulti(INT_MAX - entry.data.size(), entry.data);
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
		qsl("%1%2").arg(kConfigKey).arg(kConfigSubKey)
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
		return match.capturedRef(index).toInt();
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
		const auto captured = match.capturedRef(2);
		for (auto i = begin(months); i != end(months); ++i) {
			if (captured == (*i)) {
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

ServiceWebRequest::ServiceWebRequest(not_null<QNetworkReply*> reply)
: reply(reply.get()) {
}

ServiceWebRequest::ServiceWebRequest(ServiceWebRequest &&other)
: reply(base::take(other.reply)) {
}

ServiceWebRequest &ServiceWebRequest::operator=(ServiceWebRequest &&other) {
	if (reply != other.reply) {
		destroy();
		reply = base::take(other.reply);
	}
	return *this;
}

void ServiceWebRequest::destroy() {
	if (const auto value = base::take(reply)) {
		value->disconnect(
			value,
			&QNetworkReply::finished,
			nullptr,
			nullptr);
		value->abort();
		value->deleteLater();
	}
}

ServiceWebRequest::~ServiceWebRequest() {
	if (reply) {
		reply->deleteLater();
	}
}

SpecialConfigRequest::SpecialConfigRequest(
	Fn<void(
		DcId dcId,
		const std::string &ip,
		int port,
		bytes::const_span secret)> callback,
	Fn<void()> timeDoneCallback,
	const QString &phone)
: _callback(std::move(callback))
, _timeDoneCallback(std::move(timeDoneCallback))
, _phone(phone) {
	Expects((_callback == nullptr) != (_timeDoneCallback == nullptr));

	_manager.setProxy(QNetworkProxy::NoProxy);

	auto domains = DnsDomains();
	const auto domainsCount = domains.size();

	std::random_device rd;
	ranges::shuffle(domains, std::mt19937(rd()));
	const auto takeDomain = [&] {
		const auto result = domains.back();
		domains.pop_back();
		return result;
	};
	const auto shuffle = [&](int from, int till) {
		Expects(till > from);

		ranges::shuffle(
			begin(_attempts) + from,
			begin(_attempts) + till,
			std::mt19937(rd()));
	};

	_attempts = {};
	_attempts.push_back({ Type::Google, "dns.google.com" });
	_attempts.push_back({ Type::Google, takeDomain(), "dns" });
	_attempts.push_back({ Type::Mozilla, "mozilla.cloudflare-dns.com" });
	_attempts.push_back({ Type::RemoteConfig, "firebaseremoteconfig" });
	while (!domains.empty()) {
		_attempts.push_back({ Type::Google, takeDomain(), "dns" });
	}
	_attempts.push_back({ Type::Realtime, "firebaseio.com" });
	_attempts.push_back({ Type::FireStore, "firestore" });
	for (const auto &domain : DnsDomains()) {
		_attempts.push_back({ Type::FireStore, domain, "firestore" });
	}

	shuffle(0, 2);
	shuffle(2, 4);
	shuffle(
		_attempts.size() - (2 + domainsCount),
		_attempts.size() - domainsCount);
	shuffle(_attempts.size() - domainsCount, _attempts.size());

	ranges::reverse(_attempts); // We go from last to first.

	sendNextRequest();
}

SpecialConfigRequest::SpecialConfigRequest(
	Fn<void(
		DcId dcId,
		const std::string &ip,
		int port,
		bytes::const_span secret)> callback,
	const QString &phone)
: SpecialConfigRequest(std::move(callback), nullptr, phone) {
}

SpecialConfigRequest::SpecialConfigRequest(Fn<void()> timeDoneCallback)
: SpecialConfigRequest(nullptr, std::move(timeDoneCallback), QString()) {
}

void SpecialConfigRequest::sendNextRequest() {
	Expects(!_attempts.empty());

	const auto attempt = _attempts.back();
	_attempts.pop_back();
	if (!_attempts.empty()) {
		App::CallDelayed(kSendNextTimeout, this, [=] {
			sendNextRequest();
		});
	}
	performRequest(attempt);
}

void SpecialConfigRequest::performRequest(const Attempt &attempt) {
	const auto type = attempt.type;
	auto url = QUrl();
	url.setScheme(qsl("https"));
	auto request = QNetworkRequest();
	auto payload = QByteArray();
	switch (type) {
	case Type::Mozilla: {
		url.setHost(attempt.data);
		url.setPath(qsl("/dns-query"));
		url.setQuery(qsl("name=%1&type=16&random_padding=%2"
		).arg(Global::TxtDomainString()
		).arg(GenerateRandomPadding()));
		request.setRawHeader("accept", "application/dns-json");
	} break;
	case Type::Google: {
		url.setHost(attempt.data);
		url.setPath(qsl("/resolve"));
		url.setQuery(qsl("name=%1&type=ANY&random_padding=%2"
		).arg(Global::TxtDomainString()
		).arg(GenerateRandomPadding()));
		if (!attempt.host.isEmpty()) {
			const auto host = attempt.host + ".google.com";
			request.setRawHeader("Host", host.toLatin1());
		}
	} break;
	case Type::RemoteConfig: {
		url.setHost(ApiDomain(attempt.data));
		url.setPath(qsl("/v1/projects/%1/namespaces/firebase:fetch"
		).arg(kRemoteProject));
		url.setQuery(qsl("key=%1").arg(kApiKey));
		payload = qsl("{\"app_id\":\"%1\",\"app_instance_id\":\"%2\"}"
		).arg(kAppId
		).arg(InstanceId()).toLatin1();
		request.setRawHeader("Content-Type", "application/json");
	} break;
	case Type::Realtime: {
		url.setHost(kFireProject + qsl(".%1").arg(attempt.data));
		url.setPath(qsl("/%1%2.json").arg(kConfigKey).arg(kConfigSubKey));
	} break;
	case Type::FireStore: {
		url.setHost(attempt.host.isEmpty()
			? ApiDomain(attempt.data)
			: attempt.data);
		url.setPath(qsl("/v1/projects/%1/databases/(default)/documents/%2/%3"
		).arg(kFireProject
		).arg(kConfigKey
		).arg(kConfigSubKey));
		if (!attempt.host.isEmpty()) {
			const auto host = ApiDomain(attempt.host);
			request.setRawHeader("Host", host.toLatin1());
		}
	} break;
	default: Unexpected("Type in SpecialConfigRequest::performRequest.");
	}
	request.setUrl(url);
	request.setRawHeader("User-Agent", kUserAgent);
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
	base::unixtime::http_update(parsed.toTime_t());
	if (_timeDoneCallback) {
		_timeDoneCallback();
	}
}

void SpecialConfigRequest::requestFinished(
		Type type,
		not_null<QNetworkReply*> reply) {
	handleHeaderUnixtime(reply);
	const auto result = finalizeRequest(reply);
	if (!_callback) {
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
		LOG(("Config Error: Failed to get response, error: %2 (%3)"
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

	auto publicKey = internal::RSAPublicKey(bytes::make_span(
		kPublicKey.c_str(),
		kPublicKey.size()));
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
				return qsl("%1.%2.%3.%4"
				).arg((ip >> 24) & 0xFF
				).arg((ip >> 16) & 0xFF
				).arg((ip >> 8) & 0xFF
				).arg(ip & 0xFF).toStdString();
			};
			switch (address.type()) {
			case mtpc_ipPort: {
				const auto &fields = address.c_ipPort();
				_callback(dcId, parseIp(fields.vipv4()), fields.vport().v, {});
			} break;
			case mtpc_ipPortSecret: {
				const auto &fields = address.c_ipPortSecret();
				_callback(
					dcId,
					parseIp(fields.vipv4()),
					fields.vport().v,
					bytes::make_span(fields.vsecret().v));
			} break;
			default: Unexpected("Type in simpleConfig ips.");
			}
		}
	}
}

DomainResolver::DomainResolver(Fn<void(
	const QString &host,
	const QStringList &ips,
	crl::time expireAt)> callback)
: _callback(std::move(callback)) {
	_manager.setProxy(QNetworkProxy::NoProxy);
}

void DomainResolver::resolve(const QString &domain) {
	resolve({ domain, false });
	resolve({ domain, true });
}

void DomainResolver::resolve(const AttemptKey &key) {
	if (_attempts.find(key) != end(_attempts)) {
		return;
	} else if (_requests.find(key) != end(_requests)) {
		return;
	}
	const auto i = _cache.find(key);
	_lastTimestamp = crl::now();
	if (i != end(_cache) && i->second.expireAt > _lastTimestamp) {
		checkExpireAndPushResult(key.domain);
		return;
	}

	auto attempts = std::vector<Attempt>();
	auto domains = DnsDomains();
	std::random_device rd;
	ranges::shuffle(domains, std::mt19937(rd()));
	const auto takeDomain = [&] {
		const auto result = domains.back();
		domains.pop_back();
		return result;
	};
	const auto shuffle = [&](int from, int till) {
		Expects(till > from);

		ranges::shuffle(
			begin(attempts) + from,
			begin(attempts) + till,
			std::mt19937(rd()));
	};

	attempts.push_back({ Type::Google, "dns.google.com" });
	attempts.push_back({ Type::Google, takeDomain(), "dns" });
	attempts.push_back({ Type::Mozilla, "mozilla.cloudflare-dns.com" });
	while (!domains.empty()) {
		attempts.push_back({ Type::Google, takeDomain(), "dns" });
	}

	shuffle(0, 2);

	ranges::reverse(attempts); // We go from last to first.

	_attempts.emplace(key, Attempts{ std::move(attempts) });
	sendNextRequest(key);
}

void DomainResolver::checkExpireAndPushResult(const QString &domain) {
	const auto ipv4 = _cache.find({ domain, false });
	if (ipv4 == end(_cache) || ipv4->second.expireAt <= _lastTimestamp) {
		return;
	}
	auto result = ipv4->second;
	const auto ipv6 = _cache.find({ domain, true });
	if (ipv6 != end(_cache) && ipv6->second.expireAt > _lastTimestamp) {
		result.ips.append(ipv6->second.ips);
		accumulate_min(result.expireAt, ipv6->second.expireAt);
	}
	InvokeQueued(this, [=] {
		_callback(domain, result.ips, result.expireAt);
	});
}

void DomainResolver::sendNextRequest(const AttemptKey &key) {
	auto i = _attempts.find(key);
	if (i == end(_attempts)) {
		return;
	}
	auto &attempts = i->second;
	auto &list = attempts.list;
	const auto attempt = list.back();
	list.pop_back();

	if (!list.empty()) {
		App::CallDelayed(kSendNextTimeout, &attempts.guard, [=] {
			sendNextRequest(key);
		});
	}
	performRequest(key, attempt);
}

void DomainResolver::performRequest(
		const AttemptKey &key,
		const Attempt &attempt) {
	auto url = QUrl();
	url.setScheme(qsl("https"));
	auto request = QNetworkRequest();
	switch (attempt.type) {
	case Type::Mozilla: {
		url.setHost(attempt.data);
		url.setPath(qsl("/dns-query"));
		url.setQuery(qsl("name=%1&type=%2&random_padding=%3"
		).arg(key.domain
		).arg(key.ipv6 ? 28 : 1
		).arg(GenerateRandomPadding()));
		request.setRawHeader("accept", "application/dns-json");
	} break;
	case Type::Google: {
		url.setHost(attempt.data);
		url.setPath(qsl("/resolve"));
		url.setQuery(qsl("name=%1&type=%2&random_padding=%3"
		).arg(key.domain
		).arg(key.ipv6 ? 28 : 1
		).arg(GenerateRandomPadding()));
		if (!attempt.host.isEmpty()) {
			const auto host = attempt.host + ".google.com";
			request.setRawHeader("Host", host.toLatin1());
		}
	} break;
	default: Unexpected("Type in SpecialConfigRequest::performRequest.");
	}
	request.setUrl(url);
	request.setRawHeader("User-Agent", kUserAgent);
	const auto i = _requests.emplace(
		key,
		std::vector<ServiceWebRequest>()).first;
	const auto reply = i->second.emplace_back(
		_manager.get(request)
	).reply;
	connect(reply, &QNetworkReply::finished, this, [=] {
		requestFinished(key, reply);
	});
}

void DomainResolver::requestFinished(
		const AttemptKey &key,
		not_null<QNetworkReply*> reply) {
	const auto result = finalizeRequest(key, reply);
	const auto response = ParseDnsResponse(result);
	if (response.empty()) {
		return;
	}
	_requests.erase(key);
	_attempts.erase(key);

	auto entry = CacheEntry();
	auto ttl = kMaxTimeToLive;
	for (const auto &item : response) {
		entry.ips.push_back(item.data);
		accumulate_min(ttl, std::max(
			item.TTL * crl::time(1000),
			kMinTimeToLive));
	}
	_lastTimestamp = crl::now();
	entry.expireAt = _lastTimestamp + ttl;
	_cache[key] = std::move(entry);

	checkExpireAndPushResult(key.domain);
}

QByteArray DomainResolver::finalizeRequest(
		const AttemptKey &key,
		not_null<QNetworkReply*> reply) {
	if (reply->error() != QNetworkReply::NoError) {
		LOG(("Resolve Error: Failed to get response, error: %2 (%3)"
			).arg(reply->errorString()
			).arg(reply->error()));
	}
	const auto result = reply->readAll();
	const auto i = _requests.find(key);
	if (i != end(_requests)) {
		auto &requests = i->second;
		const auto from = ranges::remove(
			requests,
			reply,
			[](const ServiceWebRequest &request) { return request.reply; });
		requests.erase(from, end(requests));
		if (requests.empty()) {
			_requests.erase(i);
		}
	}
	return result;
}

} // namespace MTP
