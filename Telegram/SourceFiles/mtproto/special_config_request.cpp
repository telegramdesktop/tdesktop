/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "mtproto/special_config_request.h"

#include "mtproto/rsa_public_key.h"
#include "mtproto/dc_options.h"
#include "mtproto/auth_key.h"
#include "base/openssl_help.h"
#include <openssl/aes.h>

namespace MTP {
namespace {

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

} // namespace

SpecialConfigRequest::SpecialConfigRequest(base::lambda<void(DcId dcId, const std::string &ip, int port)> callback) : _callback(std::move(callback)) {
	App::setProxySettings(_manager);

	performAppRequest();
	performDnsRequest();
}

void SpecialConfigRequest::performAppRequest() {
	auto appUrl = QUrl();
	appUrl.setScheme(qsl("https"));
	appUrl.setHost(qsl("google.com"));
	if (cTestMode()) {
		appUrl.setPath(qsl("/test/"));
	}
	auto appRequest = QNetworkRequest(appUrl);
	appRequest.setRawHeader("Host", "dns-telegram.appspot.com");
	_appReply.reset(_manager.get(appRequest));
	connect(_appReply.get(), &QNetworkReply::finished, this, [this] { appFinished(); });
}

void SpecialConfigRequest::performDnsRequest() {
	auto dnsUrl = QUrl();
	dnsUrl.setScheme(qsl("https"));
	dnsUrl.setHost(qsl("google.com"));
	dnsUrl.setPath(qsl("/resolve"));
	dnsUrl.setQuery(qsl("name=%1.stel.com&type=16").arg(cTestMode() ? qsl("tap") : qsl("ap")));
	auto dnsRequest = QNetworkRequest(QUrl(dnsUrl));
	dnsRequest.setRawHeader("Host", "dns.google.com");
	_dnsReply.reset(_manager.get(dnsRequest));
	connect(_dnsReply.get(), &QNetworkReply::finished, this, [this] { dnsFinished(); });
}

void SpecialConfigRequest::appFinished() {
	if (!_appReply) {
		return;
	}
	auto result = _appReply->readAll();
	_appReply.release()->deleteLater();
	handleResponse(result);
}

void SpecialConfigRequest::dnsFinished() {
	if (!_dnsReply) {
		return;
	}
	if (_dnsReply->error() != QNetworkReply::NoError) {
		LOG(("Config Error: Failed to get dns response JSON, error: %1 (%2)").arg(_dnsReply->errorString()).arg(_dnsReply->error()));
	}
	auto result = _dnsReply->readAll();
	_dnsReply.release()->deleteLater();

	// Read and store to "entries" map all the data bytes from this response:
	// { .., "Answer": [ { .., "data": "bytes1", .. }, { .., "data": "bytes2", .. } ], .. }
	auto entries = QMap<int, QString>();
	auto error = QJsonParseError { 0, QJsonParseError::NoError };
	auto document = QJsonDocument::fromJson(result, &error);
	if (error.error != QJsonParseError::NoError) {
		LOG(("Config Error: Failed to parse dns response JSON, error: %1").arg(error.errorString()));
	} else if (!document.isObject()) {
		LOG(("Config Error: Not an object received in dns response JSON."));
	} else {
		auto response = document.object();
		auto answerIt = response.find(qsl("Answer"));
		if (answerIt == response.constEnd()) {
			LOG(("Config Error: Could not find Answer in dns response JSON."));
		} else if (!(*answerIt).isArray()) {
			LOG(("Config Error: Not an array received in Answer in dns response JSON."));
		} else {
			for (auto elem : (*answerIt).toArray()) {
				if (!elem.isObject()) {
					LOG(("Config Error: Not an object found in Answer array in dns response JSON."));
				} else {
					auto object = elem.toObject();
					auto dataIt = object.find(qsl("data"));
					if (dataIt == object.constEnd()) {
						LOG(("Config Error: Could not find data in Answer array entry in dns response JSON."));
					} else if (!(*dataIt).isString()) {
						LOG(("Config Error: Not a string data found in Answer array entry in dns response JSON."));
					} else {
						auto data = (*dataIt).toString();
						entries.insertMulti(INT_MAX - data.size(), data);
					}
				}
			}
		}
	}
	auto text = QStringList(entries.values()).join(QString());
	handleResponse(text.toLatin1());
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

	auto publicKey = internal::RSAPublicKey(gsl::as_bytes(gsl::make_span(kPublicKey.c_str(), kPublicKey.size())));
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
	if (config.vip_port_list.v.empty()) {
		LOG(("Config Error: Empty simple config received."));
		return;
	}
	for (auto &entry : config.vip_port_list.v) {
		Assert(entry.type() == mtpc_ipPort);
		auto &ipPort = entry.c_ipPort();
		auto ip = *reinterpret_cast<const uint32*>(&ipPort.vipv4.v);
		auto ipString = qsl("%1.%2.%3.%4").arg((ip >> 24) & 0xFF).arg((ip >> 16) & 0xFF).arg((ip >> 8) & 0xFF).arg(ip & 0xFF);
		_callback(config.vdc_id.v, ipString.toStdString(), ipPort.vport.v);
	}
}

SpecialConfigRequest::~SpecialConfigRequest() {
	if (_appReply) {
		_appReply->abort();
	}
	if (_dnsReply) {
		_dnsReply->abort();
	}
}

} // namespace MTP
