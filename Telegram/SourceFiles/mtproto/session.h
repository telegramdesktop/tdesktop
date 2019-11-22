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

using PreRequestMap = QMap<mtpRequestId, SecureRequest>;
using RequestMap = QMap<mtpMsgId, SecureRequest>;
using SerializedMessage = mtpBuffer;

inline bool ResponseNeedsAck(const SerializedMessage &response) {
	if (response.size() < 8) {
		return false;
	}
	auto seqNo = *(uint32*)(response.constData() + 6);
	return (seqNo & 0x01) ? true : false;
}

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
	SessionData(not_null<Session*> creator) : _owner(creator) {
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

	not_null<QReadWriteLock*> toSendMutex() const {
		return &_toSendLock;
	}
	not_null<QReadWriteLock*> haveSentMutex() const {
		return &_haveSentLock;
	}
	not_null<QReadWriteLock*> haveReceivedMutex() const {
		return &_haveReceivedLock;
	}

	PreRequestMap &toSendMap() {
		return _toSend;
	}
	const PreRequestMap &toSendMap() const {
		return _toSend;
	}
	RequestMap &haveSentMap() {
		return _haveSent;
	}
	const RequestMap &haveSentMap() const {
		return _haveSent;
	}
	QMap<mtpRequestId, SerializedMessage> &haveReceivedResponses() {
		return _receivedResponses;
	}
	const QMap<mtpRequestId, SerializedMessage> &haveReceivedResponses() const {
		return _receivedResponses;
	}
	QList<SerializedMessage> &haveReceivedUpdates() {
		return _receivedUpdates;
	}
	const QList<SerializedMessage> &haveReceivedUpdates() const {
		return _receivedUpdates;
	}

	// Warning! Valid only in constructor, _owner is guaranteed != null.
	[[nodiscard]] not_null<Session*> owner() {
		return _owner;
	}

	// Connection -> Session interface.
	void queueTryToReceive();
	void queueNeedToResumeAndSend();
	void queueConnectionStateChange(int newState);
	void queueResetDone();
	void queueSendAnything(crl::time msCanWait = 0);
	void queueSendMsgsStateInfo(quint64 msgId, QByteArray data);

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

	PreRequestMap _toSend; // map of request_id -> request, that is waiting to be sent
	RequestMap _haveSent; // map of msg_id -> request, that was sent, msDate = 0 for msgs_state_req (no resend / state req), msDate = 0, seqNo = 0 for containers

	QMap<mtpRequestId, SerializedMessage> _receivedResponses; // map of request_id -> response that should be processed in the main thread
	QList<SerializedMessage> _receivedUpdates; // list of updates that should be processed in the main thread

	// mutexes
	mutable QReadWriteLock _optionsLock;
	mutable QReadWriteLock _toSendLock;
	mutable QReadWriteLock _haveSentLock;
	mutable QReadWriteLock _haveReceivedLock;

};

class Session : public QObject {
	Q_OBJECT

public:
	// Main thread.
	Session(
		not_null<Instance*> instance,
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
	void sendPrepared(const SecureRequest &request, crl::time msCanWait = 0);

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
	int32 requestState(mtpRequestId requestId) const;
	int32 getState() const;
	QString transport() const;

	void tryToReceive();
	void needToResumeAndSend();
	void connectionStateChange(int newState);
	void resetDone();
	void sendAnything(crl::time msCanWait = 0);
	void sendMsgsStateInfo(quint64 msgId, QByteArray data);

signals:
	void authKeyChanged();
	void needToSend();
	void needToPing();
	void needToRestart();

private:
	void watchDcKeyChanges();

	bool rpcErrorOccured(mtpRequestId requestId, const RPCFailHandlerPtr &onFail, const RPCError &err);

	const not_null<Instance*> _instance;
	const ShiftedDcId _shiftedDcId = 0;
	const not_null<Dcenter*> _dc;
	const std::shared_ptr<SessionData> _data;

	std::unique_ptr<Connection> _connection;

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
