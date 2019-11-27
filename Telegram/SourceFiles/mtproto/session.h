/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "mtproto/mtproto_rpc_sender.h"
#include "mtproto/mtproto_proxy_data.h"
#include "mtproto/details/mtproto_serialized_request.h"

#include <QtCore/QTimer>

namespace MTP {

class Instance;
class AuthKey;
using AuthKeyPtr = std::shared_ptr<AuthKey>;

namespace internal {

class Dcenter;
class Connection;

enum class TemporaryKeyType;
enum class CreatingKeyType;

struct ConnectionOptions {
	ConnectionOptions() = default;
	ConnectionOptions(
		const QString &systemLangCode,
		const QString &cloudLangCode,
		const QString &langPackName,
		const ProxyData &proxy,
		bool useIPv4,
		bool useIPv6,
		bool useHttp,
		bool useTcp);
	ConnectionOptions(const ConnectionOptions &other) = default;
	ConnectionOptions &operator=(const ConnectionOptions &other) = default;

	QString systemLangCode;
	QString cloudLangCode;
	QString langPackName;
	ProxyData proxy;
	bool useIPv4 = true;
	bool useIPv6 = true;
	bool useHttp = true;
	bool useTcp = true;

};

class Session;
class SessionData {
public:
	explicit SessionData(not_null<Session*> creator) : _owner(creator) {
	}

	void notifyConnectionInited(const ConnectionOptions &options);
	void setConnectionOptions(ConnectionOptions options) {
		QWriteLocker locker(&_optionsLock);
		_options = options;
	}
	[[nodiscard]] ConnectionOptions connectionOptions() const {
		QReadLocker locker(&_optionsLock);
		return _options;
	}

	not_null<QReadWriteLock*> toSendMutex() {
		return &_toSendLock;
	}
	not_null<QReadWriteLock*> haveSentMutex() {
		return &_haveSentLock;
	}
	not_null<QReadWriteLock*> haveReceivedMutex() {
		return &_haveReceivedLock;
	}

	base::flat_map<mtpRequestId, details::SerializedRequest> &toSendMap() {
		return _toSend;
	}
	base::flat_map<mtpMsgId, details::SerializedRequest> &haveSentMap() {
		return _haveSent;
	}
	base::flat_map<mtpRequestId, mtpBuffer> &haveReceivedResponses() {
		return _receivedResponses;
	}
	std::vector<mtpBuffer> &haveReceivedUpdates() {
		return _receivedUpdates;
	}

	// Connection -> Session interface.
	void queueTryToReceive();
	void queueNeedToResumeAndSend();
	void queueConnectionStateChange(int newState);
	void queueResetDone();
	void queueSendAnything(crl::time msCanWait = 0);

	[[nodiscard]] bool connectionInited() const;
	[[nodiscard]] AuthKeyPtr getPersistentKey() const;
	[[nodiscard]] AuthKeyPtr getTemporaryKey(TemporaryKeyType type) const;
	[[nodiscard]] CreatingKeyType acquireKeyCreation(TemporaryKeyType type);
	[[nodiscard]] bool releaseKeyCreationOnDone(
		const AuthKeyPtr &temporaryKey,
		const AuthKeyPtr &persistentKeyUsedForBind);
	void releaseKeyCreationOnFail();
	void destroyTemporaryKey(uint64 keyId);

	void detach();

private:
	template <typename Callback>
	void withSession(Callback &&callback);

	Session *_owner = nullptr;
	mutable QMutex _ownerMutex;

	ConnectionOptions _options;
	mutable QReadWriteLock _optionsLock;

	base::flat_map<mtpRequestId, details::SerializedRequest> _toSend; // map of request_id -> request, that is waiting to be sent
	QReadWriteLock _toSendLock;

	base::flat_map<mtpMsgId, details::SerializedRequest> _haveSent; // map of msg_id -> request, that was sent
	QReadWriteLock _haveSentLock;

	base::flat_map<mtpRequestId, mtpBuffer> _receivedResponses; // map of request_id -> response that should be processed in the main thread
	std::vector<mtpBuffer> _receivedUpdates; // list of updates that should be processed in the main thread
	QReadWriteLock _haveReceivedLock;

};

class Session : public QObject {
public:
	// Main thread.
	Session(
		not_null<Instance*> instance,
		not_null<QThread*> thread,
		ShiftedDcId shiftedDcId,
		not_null<Dcenter*> dc);
	~Session();

	void start();
	void reInitConnection();

	void restart();
	void refreshOptions();
	void stop();
	void kill();

	void unpaused();

	// Thread-safe.
	[[nodiscard]] ShiftedDcId getDcWithShift() const;
	[[nodiscard]] AuthKeyPtr getPersistentKey() const;
	[[nodiscard]] AuthKeyPtr getTemporaryKey(TemporaryKeyType type) const;
	[[nodiscard]] bool connectionInited() const;
	void sendPrepared(
		const details::SerializedRequest &request,
		crl::time msCanWait = 0);

	// Connection thread.
	[[nodiscard]] CreatingKeyType acquireKeyCreation(TemporaryKeyType type);
	[[nodiscard]] bool releaseKeyCreationOnDone(
		const AuthKeyPtr &temporaryKey,
		const AuthKeyPtr &persistentKeyUsedForBind);
	void releaseKeyCreationOnFail();
	void destroyTemporaryKey(uint64 keyId);

	void notifyDcConnectionInited();

	void ping();
	void cancel(mtpRequestId requestId, mtpMsgId msgId);
	int requestState(mtpRequestId requestId) const;
	int getState() const;
	QString transport() const;

	void tryToReceive();
	void needToResumeAndSend();
	void connectionStateChange(int newState);
	void resetDone();
	void sendAnything(crl::time msCanWait = 0);

private:
	void watchDcKeyChanges();
	void watchDcOptionsChanges();

	void killConnection();

	bool rpcErrorOccured(
		mtpRequestId requestId,
		const RPCFailHandlerPtr &onFail,
		const RPCError &err);

	const not_null<Instance*> _instance;
	const ShiftedDcId _shiftedDcId = 0;
	const not_null<Dcenter*> _dc;
	const std::shared_ptr<SessionData> _data;
	const not_null<QThread*> _thread;

	Connection *_connection = nullptr;

	bool _killed = false;
	bool _needToReceive = false;

	AuthKeyPtr _dcKeyForCheck;
	CreatingKeyType _myKeyCreation = CreatingKeyType();

	crl::time _msSendCall = 0;
	crl::time _msWait = 0;

	bool _ping = false;

	base::Timer _timeouter;
	base::Timer _sender;

	rpl::lifetime _lifetime;

};

} // namespace internal
} // namespace MTP
