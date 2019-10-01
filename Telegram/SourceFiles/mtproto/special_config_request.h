/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/bytes.h"

#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkAccessManager>

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
		Fn<void(
			DcId dcId,
			const std::string &ip,
			int port,
			bytes::const_span secret)> callback,
		const QString &phone);
	explicit SpecialConfigRequest(Fn<void()> timeDoneCallback);

private:
	enum class Type {
		Mozilla,
		Google,
		RemoteConfig,
		Realtime,
		FireStore,
	};
	struct Attempt {
		Type type;
		QString data;
		QString host;
	};

	SpecialConfigRequest(
		Fn<void(
			DcId dcId,
			const std::string &ip,
			int port,
			bytes::const_span secret)> callback,
		Fn<void()> timeDoneCallback,
		const QString &phone);

	void sendNextRequest();
	void performRequest(const Attempt &attempt);
	void requestFinished(Type type, not_null<QNetworkReply*> reply);
	void handleHeaderUnixtime(not_null<QNetworkReply*> reply);
	QByteArray finalizeRequest(not_null<QNetworkReply*> reply);
	void handleResponse(const QByteArray &bytes);
	bool decryptSimpleConfig(const QByteArray &bytes);

	Fn<void(
		DcId dcId,
		const std::string &ip,
		int port,
		bytes::const_span secret)> _callback;
	Fn<void()> _timeDoneCallback;
	QString _phone;
	MTPhelp_ConfigSimple _simpleConfig;

	QNetworkAccessManager _manager;
	std::vector<Attempt> _attempts;
	std::vector<ServiceWebRequest> _requests;

};

class DomainResolver : public QObject {
public:
	DomainResolver(Fn<void(
		const QString &domain,
		const QStringList &ips,
		crl::time expireAt)> callback);

	void resolve(const QString &domain);

private:
	enum class Type {
		Mozilla,
		Google,
	};
	struct Attempt {
		Type type;
		QString data;
		QString host;
	};
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
		crl::time expireAt = 0;

	};
	struct Attempts {
		std::vector<Attempt> list;
		base::has_weak_ptr guard;

	};

	void resolve(const AttemptKey &key);
	void sendNextRequest(const AttemptKey &key);
	void performRequest(const AttemptKey &key, const Attempt &attempt);
	void checkExpireAndPushResult(const QString &domain);
	void requestFinished(
		const AttemptKey &key,
		not_null<QNetworkReply*> reply);
	QByteArray finalizeRequest(
		const AttemptKey &key,
		not_null<QNetworkReply*> reply);

	Fn<void(
		const QString &domain,
		const QStringList &ips,
		crl::time expireAt)> _callback;

	QNetworkAccessManager _manager;
	std::map<AttemptKey, Attempts> _attempts;
	std::map<AttemptKey, std::vector<ServiceWebRequest>> _requests;
	std::map<AttemptKey, CacheEntry> _cache;
	crl::time _lastTimestamp = 0;

};

} // namespace MTP
