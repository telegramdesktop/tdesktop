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
#include "base/openssl_help.h"
#include <openssl/aes.h>

namespace MTP {
namespace {

struct DnsEntry {
	QString data;
	int64 TTL = 0;
};

constexpr auto kSendNextTimeout = TimeMs(1000);
constexpr auto kMinTimeToLive = 10 * TimeMs(1000);
constexpr auto kMaxTimeToLive = 300 * TimeMs(1000);

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
"AppleWebKit/537.36 (KHTML, like Gecko) Chrome/66.0.3359.117 Safari/537.36";

const auto &DnsDomains() {
	static auto result = std::vector<QString>{
		qsl("google.com"),
		qsl("www.google.com"),
		qsl("google.ru"),
		qsl("www.google.ru"),
	};
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

std::vector<DnsEntry> ParseDnsResponse(const QByteArray &response) {
	// Read and store to "result" all the data bytes from the response:
	// { ..,
	//   "Answer": [
	//     { .., "data": "bytes1", "TTL": int, .. },
	//     { .., "data": "bytes2", "TTL": int, .. }
	//   ],
	// .. }
	auto result = std::vector<DnsEntry>();
	auto error = QJsonParseError{ 0, QJsonParseError::NoError };
	auto document = QJsonDocument::fromJson(response, &error);
	if (error.error != QJsonParseError::NoError) {
		LOG(("Config Error: Failed to parse dns response JSON, error: %1"
			).arg(error.errorString()));
	} else if (!document.isObject()) {
		LOG(("Config Error: Not an object received in dns response JSON."));
	} else {
		auto response = document.object();
		auto answerIt = response.find(qsl("Answer"));
		if (answerIt == response.constEnd()) {
			LOG(("Config Error: Could not find Answer "
				"in dns response JSON."));
		} else if (!(*answerIt).isArray()) {
			LOG(("Config Error: Not an array received "
				"in Answer in dns response JSON."));
		} else {
			for (auto elem : (*answerIt).toArray()) {
				if (!elem.isObject()) {
					LOG(("Config Error: Not an object found "
						"in Answer array in dns response JSON."));
				} else {
					auto object = elem.toObject();
					auto dataIt = object.find(qsl("data"));
					auto ttlIt = object.find(qsl("TTL"));
					auto ttl = (ttlIt != object.constEnd())
						? int64(std::round((*ttlIt).toDouble()))
						: int64(0);
					if (dataIt == object.constEnd()) {
						LOG(("Config Error: Could not find data "
							"in Answer array entry in dns response JSON."));
					} else if (!(*dataIt).isString()) {
						LOG(("Config Error: Not a string data found "
							"in Answer array entry in dns response JSON."));
					} else {
						result.push_back({ (*dataIt).toString(), ttl });
					}
				}
			}
		}
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
		value->deleteLater();
		value->abort();
	}
}

ServiceWebRequest::~ServiceWebRequest() {
	destroy();
}

SpecialConfigRequest::SpecialConfigRequest(
	base::lambda<void(
		DcId dcId,
		const std::string &ip,
		int port,
		bytes::const_span secret)> callback,
	const QString &phone)
: _callback(std::move(callback))
, _phone(phone) {
	_manager.setProxy(QNetworkProxy::NoProxy);
	_attempts = {
		{ Type::App, qsl("software-download.microsoft.com") },
	};
	for (const auto &domain : DnsDomains()) {
		_attempts.push_back({ Type::Dns, domain });
	}
	std::random_device rd;
	ranges::shuffle(_attempts, std::mt19937(rd()));
	sendNextRequest();
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
	url.setHost(attempt.domain);
	auto request = QNetworkRequest();
	switch (type) {
	case Type::App: {
		url.setPath(cTestMode()
			? qsl("/testv2/config.txt")
			: qsl("/prodv2/config.txt"));
		request.setRawHeader("Host", "tcdnb.azureedge.net");
	} break;
	case Type::Dns: {
		url.setPath(qsl("/resolve"));
		url.setQuery(
			qsl("name=%1.stel.com&type=16").arg(
				cTestMode() ? qsl("testapv2") : qsl("apv2")));
		request.setRawHeader("Host", "dns.google.com");
	} break;
	default: Unexpected("Type in SpecialConfigRequest::performRequest.");
	}
	request.setUrl(url);
	request.setRawHeader("User-Agent", kUserAgent);
	const auto reply = _requests.emplace_back(
		_manager.get(request)
	).reply;
	connect(reply, &QNetworkReply::finished, this, [=] {
		requestFinished(type, reply);
	});
}

void SpecialConfigRequest::requestFinished(
		Type type,
		not_null<QNetworkReply*> reply) {
	const auto result = finalizeRequest(reply);
	switch (type) {
	case Type::App: handleResponse(result); break;
	case Type::Dns: handleResponse(
		ConcatenateDnsTxtFields(ParseDnsResponse(result))); break;
	default: Unexpected("Type in SpecialConfigRequest::requestFinished.");
	}
}

QByteArray SpecialConfigRequest::finalizeRequest(
		not_null<QNetworkReply*> reply) {
	if (reply->error() != QNetworkReply::NoError) {
		LOG(("Config Error: Failed to get response from %1, error: %2 (%3)"
			).arg(reply->request().url().toDisplayString()
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

	auto publicKey = internal::RSAPublicKey(gsl::as_bytes(gsl::make_span(
		kPublicKey.c_str(),
		kPublicKey.size())));
	auto decrypted = publicKey.decrypt(gsl::as_bytes(gsl::make_span(decodedBytes)));
	auto decryptedBytes = gsl::make_span(decrypted);

	constexpr auto kAesKeySize = CTRState::KeySize;
	constexpr auto kAesIvecSize = CTRState::IvecSize;
	auto aesEncryptedBytes = decryptedBytes.subspan(kAesKeySize);
	base::byte_array<kAesIvecSize> aesivec;
	base::copy_bytes(aesivec, decryptedBytes.subspan(CTRState::KeySize - CTRState::IvecSize, CTRState::IvecSize));
	AES_KEY aeskey;
	AES_set_decrypt_key(reinterpret_cast<const unsigned char*>(decryptedBytes.data()), kAesKeySize * CHAR_BIT, &aeskey);
	AES_cbc_encrypt(reinterpret_cast<const unsigned char*>(aesEncryptedBytes.data()), reinterpret_cast<unsigned char*>(aesEncryptedBytes.data()), aesEncryptedBytes.size(), &aeskey, reinterpret_cast<unsigned char*>(aesivec.data()), AES_DECRYPT);

	constexpr auto kDigestSize = 16;
	auto dataSize = aesEncryptedBytes.size() - kDigestSize;
	auto data = aesEncryptedBytes.subspan(0, dataSize);
	auto hash = openssl::Sha256(data);
	if (base::compare_bytes(gsl::make_span(hash).subspan(0, kDigestSize), aesEncryptedBytes.subspan(dataSize)) != 0) {
		LOG(("Config Error: Bad digest."));
		return false;
	}

	mtpBuffer buffer;
	buffer.resize(data.size() / sizeof(mtpPrime));
	base::copy_bytes(gsl::as_writeable_bytes(gsl::make_span(buffer)), data);
	auto from = &*buffer.cbegin();
	auto end = from + buffer.size();
	auto realLength = *from++;
	if (realLength <= 0 || realLength > dataSize || (realLength & 0x03)) {
		LOG(("Config Error: Bad length %1.").arg(realLength));
		return false;
	}

	try {
		_simpleConfig.read(from, end);
	} catch (...) {
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
	auto &config = _simpleConfig.c_help_configSimple();
	auto now = unixtime();
	if (now < config.vdate.v || now > config.vexpires.v) {
		LOG(("Config Error: Bad date frame for simple config: %1-%2, our time is %3.").arg(config.vdate.v).arg(config.vexpires.v).arg(now));
		return;
	}
	if (config.vrules.v.empty()) {
		LOG(("Config Error: Empty simple config received."));
		return;
	}
	for (auto &rule : config.vrules.v) {
		Assert(rule.type() == mtpc_accessPointRule);
		auto &data = rule.c_accessPointRule();
		const auto phoneRules = qs(data.vphone_prefix_rules);
		if (!CheckPhoneByPrefixesRules(_phone, phoneRules)) {
			continue;
		}

		const auto dcId = data.vdc_id.v;
		for (const auto &address : data.vips.v) {
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
				_callback(dcId, parseIp(fields.vipv4), fields.vport.v, {});
			} break;
			case mtpc_ipPortSecret: {
				const auto &fields = address.c_ipPortSecret();
				_callback(
					dcId,
					parseIp(fields.vipv4),
					fields.vport.v,
					bytes::make_span(fields.vsecret.v));
			} break;
			default: Unexpected("Type in simpleConfig ips.");
			}
		}
	}
}

DomainResolver::DomainResolver(base::lambda<void(
	const QString &host,
	const QStringList &ips,
	TimeMs expireAt)> callback)
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
	_lastTimestamp = getms(true);
	if (i != end(_cache) && i->second.expireAt > _lastTimestamp) {
		checkExpireAndPushResult(key.domain);
		return;
	}
	auto hosts = DnsDomains();
	std::random_device rd;
	ranges::shuffle(hosts, std::mt19937(rd()));
	_attempts.emplace(key, std::move(hosts));
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
	auto &hosts = i->second;
	const auto host = hosts.back();
	hosts.pop_back();

	if (!hosts.empty()) {
		App::CallDelayed(kSendNextTimeout, this, [=] {
			sendNextRequest(key);
		});
	}
	performRequest(key, host);
}

void DomainResolver::performRequest(
		const AttemptKey &key,
		const QString &host) {
	auto url = QUrl();
	url.setScheme(qsl("https"));
	url.setHost(host);
	url.setPath(qsl("/resolve"));
	url.setQuery(
		qsl("name=%1&type=%2").arg(key.domain).arg(key.ipv6 ? 28 : 1));
	auto request = QNetworkRequest();
	request.setRawHeader("Host", "dns.google.com");
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
			item.TTL * TimeMs(1000),
			kMinTimeToLive));
	}
	_lastTimestamp = getms(true);
	entry.expireAt = _lastTimestamp + ttl;
	_cache[key] = std::move(entry);

	checkExpireAndPushResult(key.domain);
}

QByteArray DomainResolver::finalizeRequest(
		const AttemptKey &key,
		not_null<QNetworkReply*> reply) {
	if (reply->error() != QNetworkReply::NoError) {
		LOG(("Resolve Error: Failed to get response from %1, error: %2 (%3)"
			).arg(reply->request().url().toDisplayString()
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
