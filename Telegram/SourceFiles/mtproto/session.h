/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/single_timer.h"
#include "mtproto/rpc_sender.h"

namespace MTP {

class Instance;
class AuthKey;
using AuthKeyPtr = std::shared_ptr<AuthKey>;

namespace internal {

class Dcenter;
class Connection;

using PreRequestMap = QMap<mtpRequestId, SecureRequest>;
using RequestMap = QMap<mtpMsgId, SecureRequest>;

class RequestIdsMap : public QMap<mtpMsgId, mtpRequestId> {
public:
	using ParentType = QMap<mtpMsgId, mtpRequestId>;

	mtpMsgId min() const {
		return size() ? cbegin().key() : 0;
	}

	mtpMsgId max() const {
		ParentType::const_iterator e(cend());
		return size() ? (--e).key() : 0;
	}

};

class ReceivedMsgIds {
public:
	bool registerMsgId(mtpMsgId msgId, bool needAck) {
		auto i = _idsNeedAck.constFind(msgId);
		if (i == _idsNeedAck.cend()) {
			if (_idsNeedAck.size() < MTPIdsBufferSize || msgId > min()) {
				_idsNeedAck.insert(msgId, needAck);
				return true;
			}
			MTP_LOG(-1, ("No need to handle - %1 < min = %2").arg(msgId).arg(min()));
		} else {
			MTP_LOG(-1, ("No need to handle - %1 already is in map").arg(msgId));
		}
		return false;
	}

	mtpMsgId min() const {
		return _idsNeedAck.isEmpty() ? 0 : _idsNeedAck.cbegin().key();
	}

	mtpMsgId max() const {
		auto end = _idsNeedAck.cend();
		return _idsNeedAck.isEmpty() ? 0 : (--end).key();
	}

	void shrink() {
		auto size = _idsNeedAck.size();
		while (size-- > MTPIdsBufferSize) {
			_idsNeedAck.erase(_idsNeedAck.begin());
		}
	}

	enum class State {
		NotFound,
		NeedsAck,
		NoAckNeeded,
	};
	State lookup(mtpMsgId msgId) const {
		auto i = _idsNeedAck.constFind(msgId);
		if (i == _idsNeedAck.cend()) {
			return State::NotFound;
		}
		return i.value() ? State::NeedsAck : State::NoAckNeeded;
	}

	void clear() {
		_idsNeedAck.clear();
	}

private:
	QMap<mtpMsgId, bool> _idsNeedAck;

};

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
		const ProxyData &proxy,
		bool useIPv4,
		bool useIPv6,
		bool useHttp,
		bool useTcp);
	ConnectionOptions(const ConnectionOptions &other) = default;
	ConnectionOptions &operator=(const ConnectionOptions &other) = default;

	QString systemLangCode;
	QString cloudLangCode;
	ProxyData proxy;
	bool useIPv4 = true;
	bool useIPv6 = true;
	bool useHttp = true;
	bool useTcp = true;
	bool inited = false;

};

class Session;
class SessionData {
public:
	SessionData(not_null<Session*> creator) : _owner(creator) {
	}

	void setSession(uint64 session) {
		DEBUG_LOG(("MTP Info: setting server_session: %1").arg(session));

		QWriteLocker locker(&_lock);
		if (_session != session) {
			_session = session;
			_messagesSent = 0;
		}
	}
	uint64 getSession() const {
		QReadLocker locker(&_lock);
		return _session;
	}
	void setConnectionInited(bool inited = true) {
		QWriteLocker locker(&_lock);
		_options.inited = inited;
	}
	void notifyConnectionInited(const ConnectionOptions &options);
	void applyConnectionOptions(ConnectionOptions options) {
		QWriteLocker locker(&_lock);
		const auto inited = _options.inited;
		_options = options;
		_options.inited = inited;
	}
	ConnectionOptions connectionOptions() const {
		QReadLocker locker(&_lock);
		return _options;
	}

	void setSalt(uint64 salt) {
		QWriteLocker locker(&_lock);
		_salt = salt;
	}
	uint64 getSalt() const {
		QReadLocker locker(&_lock);
		return _salt;
	}

	const AuthKeyPtr &getKey() const {
		return _authKey;
	}
	void setKey(const AuthKeyPtr &key);

	bool isCheckedKey() const {
		QReadLocker locker(&_lock);
		return _keyChecked;
	}
	void setCheckedKey(bool checked) {
		QWriteLocker locker(&_lock);
		_keyChecked = checked;
	}

	not_null<QReadWriteLock*> keyMutex() const;

	not_null<QReadWriteLock*> toSendMutex() const {
		return &_toSendLock;
	}
	not_null<QReadWriteLock*> haveSentMutex() const {
		return &_haveSentLock;
	}
	not_null<QReadWriteLock*> toResendMutex() const {
		return &_toResendLock;
	}
	not_null<QReadWriteLock*> wereAckedMutex() const {
		return &_wereAckedLock;
	}
	not_null<QReadWriteLock*> receivedIdsMutex() const {
		return &_receivedIdsLock;
	}
	not_null<QReadWriteLock*> haveReceivedMutex() const {
		return &_haveReceivedLock;
	}
	not_null<QReadWriteLock*> stateRequestMutex() const {
		return &_stateRequestLock;
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
	RequestIdsMap &toResendMap() { // msgId -> requestId, on which toSend: requestId -> request for resended requests
		return _toResend;
	}
	const RequestIdsMap &toResendMap() const {
		return _toResend;
	}
	ReceivedMsgIds &receivedIdsSet() {
		return _receivedIds;
	}
	const ReceivedMsgIds &receivedIdsSet() const {
		return _receivedIds;
	}
	RequestIdsMap &wereAckedMap() {
		return _wereAcked;
	}
	const RequestIdsMap &wereAckedMap() const {
		return _wereAcked;
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
	QMap<mtpMsgId, bool> &stateRequestMap() {
		return _stateRequest;
	}
	const QMap<mtpMsgId, bool> &stateRequestMap() const {
		return _stateRequest;
	}

	not_null<Session*> owner() {
		return _owner;
	}
	not_null<const Session*> owner() const {
		return _owner;
	}

	uint32 nextRequestSeqNumber(bool needAck = true) {
		QWriteLocker locker(&_lock);
		auto result = _messagesSent;
		_messagesSent += (needAck ? 1 : 0);
		return result * 2 + (needAck ? 1 : 0);
	}

	void clear(Instance *instance);

private:
	uint64 _session = 0;
	uint64 _salt = 0;

	uint32 _messagesSent = 0;

	not_null<Session*> _owner;

	AuthKeyPtr _authKey;
	bool _keyChecked = false;
	bool _layerInited = false;
	ConnectionOptions _options;

	PreRequestMap _toSend; // map of request_id -> request, that is waiting to be sent
	RequestMap _haveSent; // map of msg_id -> request, that was sent, msDate = 0 for msgs_state_req (no resend / state req), msDate = 0, seqNo = 0 for containers
	RequestIdsMap _toResend; // map of msg_id -> request_id, that request_id -> request lies in toSend and is waiting to be resent
	ReceivedMsgIds _receivedIds; // set of received msg_id's, for checking new msg_ids
	RequestIdsMap _wereAcked; // map of msg_id -> request_id, this msg_ids already were acked or do not need ack
	QMap<mtpMsgId, bool> _stateRequest; // set of msg_id's, whose state should be requested

	QMap<mtpRequestId, SerializedMessage> _receivedResponses; // map of request_id -> response that should be processed in the main thread
	QList<SerializedMessage> _receivedUpdates; // list of updates that should be processed in the main thread

	// mutexes
	mutable QReadWriteLock _lock;
	mutable QReadWriteLock _toSendLock;
	mutable QReadWriteLock _haveSentLock;
	mutable QReadWriteLock _toResendLock;
	mutable QReadWriteLock _receivedIdsLock;
	mutable QReadWriteLock _wereAckedLock;
	mutable QReadWriteLock _haveReceivedLock;
	mutable QReadWriteLock _stateRequestLock;

};

class Session : public QObject {
	Q_OBJECT

public:
	Session(not_null<Instance*> instance, ShiftedDcId shiftedDcId);

	void start();
	void restart();
	void refreshOptions();
	void reInitConnection();
	void stop();
	void kill();

	void unpaused();

	ShiftedDcId getDcWithShift() const;

	QReadWriteLock *keyMutex() const;
	void notifyKeyCreated(AuthKeyPtr &&key);
	void destroyKey();
	void notifyDcConnectionInited();

	void ping();
	void cancel(mtpRequestId requestId, mtpMsgId msgId);
	int32 requestState(mtpRequestId requestId) const;
	int32 getState() const;
	QString transport() const;

	// Nulls msgId and seqNo in request, if newRequest = true.
	void sendPrepared(
		const SecureRequest &request,
		TimeMs msCanWait = 0,
		bool newRequest = true);

	~Session();

signals:
	void authKeyCreated();
	void needToSend();
	void needToPing();
	void needToRestart();

public slots:
	void needToResumeAndSend();

	mtpRequestId resend(quint64 msgId, qint64 msCanWait = 0, bool forceContainer = false, bool sendMsgStateInfo = false);
	void resendMany(QVector<quint64> msgIds, qint64 msCanWait, bool forceContainer, bool sendMsgStateInfo);
	void resendAll(); // after connection restart

	void authKeyCreatedForDC();
	void connectionWasInitedForDC();

	void tryToReceive();
	void checkRequestsByTimer();
	void onConnectionStateChange(qint32 newState);
	void onResetDone();

	void sendAnything(qint64 msCanWait = 0);
	void sendPong(quint64 msgId, quint64 pingId);
	void sendMsgsStateInfo(quint64 msgId, QByteArray data);

private:
	void createDcData();

	bool rpcErrorOccured(mtpRequestId requestId, const RPCFailHandlerPtr &onFail, const RPCError &err);

	not_null<Instance*> _instance;
	std::unique_ptr<Connection> _connection;

	bool _killed = false;
	bool _needToReceive = false;

	SessionData data;

	ShiftedDcId dcWithShift = 0;
	std::shared_ptr<Dcenter> dc;

	TimeMs msSendCall = 0;
	TimeMs msWait = 0;

	bool _ping = false;

	QTimer timeouter;
	SingleTimer sender;

};

inline not_null<QReadWriteLock*> SessionData::keyMutex() const {
	return _owner->keyMutex();
}

MTPrpcError rpcClientError(const QString &type, const QString &description = QString());

} // namespace internal
} // namespace MTP
