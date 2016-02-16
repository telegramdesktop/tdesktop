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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "mtpConnection.h"
#include "mtpDC.h"
#include "mtpRPC.h"

class MTProtoSession;

class MTPSessionData {
public:
	
	MTPSessionData(MTProtoSession *creator)
	: _session(0), _salt(0)
	, _messagesSent(0), _fakeRequestId(-2000000000)
	, _owner(creator), _keyChecked(false), _layerInited(false) {
	}

	void setSession(uint64 session) {
		DEBUG_LOG(("MTP Info: setting server_session: %1").arg(session));

		QWriteLocker locker(&lock);
		if (_session != session) {
			_session = session;
			_messagesSent = 0;
		}
	}
	uint64 getSession() const {
		QReadLocker locker(&lock);
		return _session;
	}
	bool layerWasInited() const {
		QReadLocker locker(&lock);
		return _layerInited;
	}
	void setLayerWasInited(bool was) {
		QWriteLocker locker(&lock);
		_layerInited = was;
	}

	void setSalt(uint64 salt) {
		QWriteLocker locker(&lock);
		_salt = salt;
	}
	uint64 getSalt() const {
		QReadLocker locker(&lock);
		return _salt;
	}

	const mtpAuthKeyPtr &getKey() const {
		return _authKey;
	}
	void setKey(const mtpAuthKeyPtr &key) {
		if (_authKey != key) {
			uint64 session;
			memsetrnd(session);
			_authKey = key;

			DEBUG_LOG(("MTP Info: new auth key set in SessionData, id %1, setting random server_session %2").arg(key ? key->keyId() : 0).arg(session));
			QWriteLocker locker(&lock);
			if (_session != session) {
				_session = session;
				_messagesSent = 0;
			}
			_layerInited = false;
		}
	}

	bool isCheckedKey() const {
		QReadLocker locker(&lock);
		return _keyChecked;
	}
	void setCheckedKey(bool checked) {
		QWriteLocker locker(&lock);
		_keyChecked = checked;
	}

	QReadWriteLock *keyMutex() const;

	QReadWriteLock *toSendMutex() const {
		return &toSendLock;
	}
	QReadWriteLock *haveSentMutex() const {
		return &haveSentLock;
	}
	QReadWriteLock *toResendMutex() const {
		return &toResendLock;
	}
	QReadWriteLock *wereAckedMutex() const {
		return &wereAckedLock;
	}
	QReadWriteLock *receivedIdsMutex() const {
		return &receivedIdsLock;
	}
	QReadWriteLock *haveReceivedMutex() const {
		return &haveReceivedLock;
	}
	QReadWriteLock *stateRequestMutex() const {
		return &stateRequestLock;
	}

	mtpPreRequestMap &toSendMap() {
		return toSend;
	}
	const mtpPreRequestMap &toSendMap() const {
		return toSend;
	}
	mtpRequestMap &haveSentMap() {
		return haveSent;
	}
	const mtpRequestMap &haveSentMap() const {
		return haveSent;
	}
	mtpRequestIdsMap &toResendMap() { // msgId -> requestId, on which toSend: requestId -> request for resended requests
		return toResend;
	}
	const mtpRequestIdsMap &toResendMap() const {
		return toResend;
	}
	mtpMsgIdsMap &receivedIdsSet() {
		return receivedIds;
	}
	const mtpMsgIdsMap &receivedIdsSet() const {
		return receivedIds;
	}
	mtpRequestIdsMap &wereAckedMap() {
		return wereAcked;
	}
	const mtpRequestIdsMap &wereAckedMap() const {
		return wereAcked;
	}
	mtpResponseMap &haveReceivedMap() {
		return haveReceived;
	}
	const mtpResponseMap &haveReceivedMap() const {
		return haveReceived;
	}
	mtpMsgIdsSet &stateRequestMap() {
		return stateRequest;
	}
	const mtpMsgIdsSet &stateRequestMap() const {
		return stateRequest;
	}

	mtpRequestId nextFakeRequestId() { // must be locked by haveReceivedMutex()
		if (haveReceived.isEmpty() || haveReceived.cbegin().key() > 0) {
			_fakeRequestId = -2000000000;
		} else {
			++_fakeRequestId;
		}
		return _fakeRequestId;
	}

	MTProtoSession *owner() {
		return _owner;
	}
	const MTProtoSession *owner() const {
		return _owner;
	}

	uint32 nextRequestSeqNumber(bool needAck = true) {
		QWriteLocker locker(&lock);
		uint32 result(_messagesSent);
		_messagesSent += (needAck ? 1 : 0);
		return result * 2 + (needAck ? 1 : 0);
	}

	void clear();

private:
	uint64 _session, _salt;

	uint32 _messagesSent;
	mtpRequestId _fakeRequestId;

	MTProtoSession *_owner;

	mtpAuthKeyPtr _authKey;
	bool _keyChecked, _layerInited;

	mtpPreRequestMap toSend; // map of request_id -> request, that is waiting to be sent
	mtpRequestMap haveSent; // map of msg_id -> request, that was sent, msDate = 0 for msgs_state_req (no resend / state req), msDate = 0, seqNo = 0 for containers
	mtpRequestIdsMap toResend; // map of msg_id -> request_id, that request_id -> request lies in toSend and is waiting to be resent
	mtpMsgIdsMap receivedIds; // set of received msg_id's, for checking new msg_ids
	mtpRequestIdsMap wereAcked; // map of msg_id -> request_id, this msg_ids already were acked or do not need ack
	mtpResponseMap haveReceived; // map of request_id -> response, that should be processed in other thread
	mtpMsgIdsSet stateRequest; // set of msg_id's, whose state should be requested

	// mutexes
	mutable QReadWriteLock lock;
	mutable QReadWriteLock toSendLock;
	mutable QReadWriteLock haveSentLock;
	mutable QReadWriteLock toResendLock;
	mutable QReadWriteLock receivedIdsLock;
	mutable QReadWriteLock wereAckedLock;
	mutable QReadWriteLock haveReceivedLock;
	mutable QReadWriteLock stateRequestLock;

};

class MTProtoSession : public QObject {
	Q_OBJECT

public:

	MTProtoSession();

	void start(int32 dcenter = 0);
	void restart();
	void stop();
	void kill();

	void unpaused();

	int32 getDcWithShift() const;
	~MTProtoSession();

	QReadWriteLock *keyMutex() const;
	void notifyKeyCreated(const mtpAuthKeyPtr &key);
	void destroyKey();
	void notifyLayerInited(bool wasInited);

	template <typename TRequest>
	mtpRequestId send(const TRequest &request, RPCResponseHandler callbacks = RPCResponseHandler(), uint64 msCanWait = 0, bool needsLayer = false, bool toMainDC = false, mtpRequestId after = 0); // send mtp request

	void ping();
	void cancel(mtpRequestId requestId, mtpMsgId msgId);
	int32 requestState(mtpRequestId requestId) const;
	int32 getState() const;
	QString transport() const;

	void sendPrepared(const mtpRequest &request, uint64 msCanWait = 0, bool newRequest = true); // nulls msgId and seqNo in request, if newRequest = true

signals:

	void authKeyCreated();
	void needToSend();
	void needToPing();
	void needToRestart();

public slots:

	void needToResumeAndSend();

	mtpRequestId resend(quint64 msgId, quint64 msCanWait = 0, bool forceContainer = false, bool sendMsgStateInfo = false);
	void resendMany(QVector<quint64> msgIds, quint64 msCanWait, bool forceContainer, bool sendMsgStateInfo);
	void resendAll(); // after connection restart

	void authKeyCreatedForDC();
	void layerWasInitedForDC(bool wasInited);

	void tryToReceive();
	void checkRequestsByTimer();
	void onConnectionStateChange(qint32 newState);
	void onResetDone();

	void sendAnything(quint64 msCanWait = 0);
	void sendPong(quint64 msgId, quint64 pingId);
	void sendMsgsStateInfo(quint64 msgId, QByteArray data);

private:
	
	typedef QList<MTProtoConnection*> MTProtoConnections;
	MTProtoConnections connections;

	bool _killed;
	bool _needToReceive;
	
	MTPSessionData data;

	int32 dcWithShift;
	MTProtoDCPtr dc;

	uint64 msSendCall, msWait;

	bool _ping;

	QTimer timeouter;
	SingleTimer sender;

};

inline QReadWriteLock *MTPSessionData::keyMutex() const {
	return _owner->keyMutex();
}

MTPrpcError rpcClientError(const QString &type, const QString &description = QString());

// here

typedef QSharedPointer<MTProtoSession> MTProtoSessionPtr;
