/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace MTP {

class Instance;
class AuthKey;
using AuthKeyPtr = std::shared_ptr<AuthKey>;

namespace internal {

class Dcenter : public QObject {
	Q_OBJECT

public:
	Dcenter(not_null<Instance*> instance, DcId dcId, AuthKeyPtr &&key);

	QReadWriteLock *keyMutex() const;
	const AuthKeyPtr &getKey() const;
	void setKey(AuthKeyPtr &&key);
	void destroyKey();

	bool connectionInited() const {
		QMutexLocker lock(&initLock);
		return _connectionInited;
	}
	void setConnectionInited(bool connectionInited = true) {
		QMutexLocker lock(&initLock);
		_connectionInited = connectionInited;
	}

signals:
	void authKeyCreated();
	void connectionWasInited();

private slots:
	void authKeyWrite();

private:
	mutable QReadWriteLock keyLock;
	mutable QMutex initLock;
	not_null<Instance*> _instance;
	DcId _id = 0;
	AuthKeyPtr _key;
	bool _connectionInited = false;

};

} // namespace internal
} // namespace MTP
