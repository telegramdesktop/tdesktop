/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/details/mtproto_domain_resolver.h"

#include "base/random.h"
#include "base/invoke_queued.h"
#include "base/call_delayed.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <range/v3/algorithm/shuffle.hpp>
#include <range/v3/algorithm/reverse.hpp>
#include <range/v3/algorithm/remove.hpp>
#include <random>

namespace MTP::details {
namespace {

constexpr auto kSendNextTimeout = crl::time(800);
constexpr auto kMinTimeToLive = 10 * crl::time(1000);
constexpr auto kMaxTimeToLive = 300 * crl::time(1000);

} // namespace

const std::vector<QString> &DnsDomains() {
	static const auto kResult = std::vector<QString>{
		"google.com",
		"www.google.com",
		"google.ru",
		"www.google.ru",
	};
	return kResult;
}

QString GenerateDnsRandomPadding() {
	constexpr char kValid[] = "abcdefghijklmnopqrstuvwxyz"
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

	auto result = QString();
	const auto count = [&] {
		constexpr auto kMinPadding = 13;
		constexpr auto kMaxPadding = 128;
		while (true) {
			const auto result = 1 + (base::RandomValue<uchar>() / 2);
			Assert(result <= kMaxPadding);
			if (result >= kMinPadding) {
				return result;
			}
		}
	}();
	result.resize(count);
	for (auto &ch : result) {
		ch = kValid[base::RandomValue<uchar>() % (sizeof(kValid) - 1)];
	}
	return result;
}

QByteArray DnsUserAgent() {
	static const auto kResult = QByteArray(
		"Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
		"AppleWebKit/537.36 (KHTML, like Gecko) "
		"Chrome/106.0.5249.119 Safari/537.36");
	return kResult;
}

std::vector<DnsEntry> ParseDnsResponse(
		const QByteArray &bytes,
		std::optional<int> typeRestriction) {
	if (bytes.isEmpty()) {
		return {};
	}

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
	const auto answerIt = response.find("Answer");
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
			const auto typeIt = object.find("type");
			const auto type = int(base::SafeRound((*typeIt).toDouble()));
			if (!(*typeIt).isDouble()) {
				LOG(("Config Error: Not a number in type field "
					"in Answer array in dns response JSON."));
				continue;
			} else if (type != *typeRestriction) {
				continue;
			}
		}
		const auto dataIt = object.find("data");
		if (dataIt == object.constEnd()) {
			LOG(("Config Error: Could not find data "
				"in Answer array entry in dns response JSON."));
			continue;
		} else if (!(*dataIt).isString()) {
			LOG(("Config Error: Not a string data found "
				"in Answer array entry in dns response JSON."));
			continue;
		}

		const auto ttlIt = object.find("TTL");
		const auto ttl = (ttlIt != object.constEnd())
			? crl::time(base::SafeRound((*ttlIt).toDouble()))
			: crl::time(0);
		result.push_back({ (*dataIt).toString(), ttl });
	}
	return result;
}

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
		base::call_delayed(kSendNextTimeout, &attempts.guard, [=] {
			sendNextRequest(key);
		});
	}
	performRequest(key, attempt);
}

void DomainResolver::performRequest(
		const AttemptKey &key,
		const Attempt &attempt) {
	auto url = QUrl();
	url.setScheme("https");
	auto request = QNetworkRequest();
	switch (attempt.type) {
	case Type::Mozilla: {
		url.setHost(attempt.data);
		url.setPath("/dns-query");
		url.setQuery(QStringLiteral("name=%1&type=%2&random_padding=%3"
		).arg(key.domain
		).arg(key.ipv6 ? 28 : 1
		).arg(GenerateDnsRandomPadding()));
		request.setRawHeader("accept", "application/dns-json");
	} break;
	case Type::Google: {
		url.setHost(attempt.data);
		url.setPath("/resolve");
		url.setQuery(QStringLiteral("name=%1&type=%2&random_padding=%3"
		).arg(key.domain
		).arg(key.ipv6 ? 28 : 1
		).arg(GenerateDnsRandomPadding()));
		if (!attempt.host.isEmpty()) {
			const auto host = attempt.host + ".google.com";
			request.setRawHeader("Host", host.toLatin1());
		}
	} break;
	default: Unexpected("Type in DomainResolver::performRequest.");
	}
	request.setUrl(url);
	request.setRawHeader("User-Agent", DnsUserAgent());
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
		ttl = std::min(
			ttl,
			std::max(item.TTL * crl::time(1000), kMinTimeToLive));
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
		DEBUG_LOG(("Resolve Error: Failed to get response, error: %2 (%3)"
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

} // namespace MTP::details
