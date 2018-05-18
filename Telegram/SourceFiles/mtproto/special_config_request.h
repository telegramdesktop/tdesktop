/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/bytes.h"

namespace MTP {

struct ServiceWebRequest {
        ServiceWebRequest(not_null<QNetworkReply*> reply);
        ServiceWebRequest(ServiceWebRequest &&other);
        ServiceWebRequest &operator=(ServiceWebRequest &&other);
        ~ServiceWebRequest();

        void destroy();

        QPointer<QNetworkReply> reply;

};

class SpecialConfigRequest : public QObject {
public:
	SpecialConfigRequest(
		base::lambda<void(
			DcId dcId,
			const std::string &ip,
			int port,
			bytes::const_span secret)> callback,
		const QString &phone);

private:
	enum class Type {
		App,
		Dns,
	};
	struct Attempt {
		Type type;
		QString domain;
	};

	void sendNextRequest();
	void performRequest(const Attempt &attempt);
	void requestFinished(Type type, not_null<QNetworkReply*> reply);
	QByteArray finalizeRequest(not_null<QNetworkReply*> reply);
	void handleResponse(const QByteArray &bytes);
	bool decryptSimpleConfig(const QByteArray &bytes);

	base::lambda<void(
		DcId dcId,
		const std::string &ip,
		int port,
		bytes::const_span secret)> _callback;
	QString _phone;
	MTPhelp_ConfigSimple _simpleConfig;

	QNetworkAccessManager _manager;
	std::vector<Attempt> _attempts;
	std::vector<ServiceWebRequest> _requests;

};

class DomainResolver : public QObject {
public:
	DomainResolver(base::lambda<void(
		const QString &domain,
		const QStringList &ips,
		TimeMs expireAt)> callback);

	void resolve(const QString &domain);

private:
	struct AttemptKey {
		QString domain;
		bool ipv6 = false;

		inline bool operator<(const AttemptKey &other) const {
			return (domain < other.domain)
				|| (domain == other.domain && !ipv6 && other.ipv6);
		}
		inline bool operator==(const AttemptKey &other) const {
			return (domain == other.domain) && (ipv6 == other.ipv6);
		}

	};
	struct CacheEntry {
		QStringList ips;
		TimeMs expireAt = 0;

	};

	void resolve(const AttemptKey &key);
	void sendNextRequest(const AttemptKey &key);
	void performRequest(const AttemptKey &key, const QString &host);
	void checkExpireAndPushResult(const QString &domain);
	void requestFinished(
		const AttemptKey &key,
		not_null<QNetworkReply*> reply);
	QByteArray finalizeRequest(
		const AttemptKey &key,
		not_null<QNetworkReply*> reply);

	base::lambda<void(
		const QString &domain,
		const QStringList &ips,
		TimeMs expireAt)> _callback;

	QNetworkAccessManager _manager;
	std::map<AttemptKey, std::vector<QString>> _attempts;
	std::map<AttemptKey, std::vector<ServiceWebRequest>> _requests;
	std::map<AttemptKey, CacheEntry> _cache;
	TimeMs _lastTimestamp = 0;

};

} // namespace MTP
