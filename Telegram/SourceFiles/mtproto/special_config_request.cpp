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

constexpr auto kSendNextTimeout = TimeMs(1000);

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

QByteArray ParseDnsResponse(const QByteArray &response) {
	// Read and store to "entries" map all the data bytes from the response:
	// { ..,
	//   "Answer": [
	//     { .., "data": "bytes1", .. },
	//     { .., "data": "bytes2", .. }
	//   ],
	// .. }
	auto entries = QMap<int, QString>();
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
					if (dataIt == object.constEnd()) {
						LOG(("Config Error: Could not find data "
							"in Answer array entry in dns response JSON."));
					} else if (!(*dataIt).isString()) {
						LOG(("Config Error: Not a string data found "
							"in Answer array entry in dns response JSON."));
					} else {
						auto data = (*dataIt).toString();
						entries.insertMulti(INT_MAX - data.size(), data);
					}
				}
			}
		}
	}
	return QStringList(entries.values()).join(QString()).toLatin1();
}

} // namespace

SpecialConfigRequest::Request::Request(not_null<QNetworkReply*> reply)
: reply(reply.get()) {
}

SpecialConfigRequest::Request::Request(Request &&other)
: reply(base::take(other.reply)) {
}

auto SpecialConfigRequest::Request::operator=(Request &&other) -> Request& {
	if (reply != other.reply) {
		destroy();
		reply = base::take(other.reply);
	}
	return *this;
}

void SpecialConfigRequest::Request::destroy() {
	if (const auto value = base::take(reply)) {
		value->deleteLater();
		value->abort();
	}
}

SpecialConfigRequest::Request::~Request() {
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
	_attempts = {
		{ Type::App, qsl("software-download.microsoft.com") },
		{ Type::Dns, qsl("google.com") },
		{ Type::Dns, qsl("www.google.com") },
		{ Type::Dns, qsl("google.ru") },
		{ Type::Dns, qsl("www.google.ru") },
	};
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
			? qsl("/test/config.txt")
			: qsl("/prodv2/config.txt"));
		request.setRawHeader("Host", "tcdnb.azureedge.net");
	} break;
	case Type::Dns: {
		url.setPath(qsl("/resolve"));
		url.setQuery(
			qsl("name=%1.stel.com&type=16").arg(
				cTestMode() ? qsl("tap") : qsl("apv2")));
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
	case Type::Dns: handleResponse(ParseDnsResponse(result)); break;
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
		[](const Request &request) { return request.reply; });
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

} // namespace MTP
