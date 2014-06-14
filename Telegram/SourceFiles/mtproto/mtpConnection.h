/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#pragma once

#include "mtproto/mtpCoreTypes.h"
#include "mtproto/mtpScheme.h"
#include "mtproto/mtpPublicRSA.h"
#include "mtproto/mtpAuthKey.h"

inline bool mtpRequestData::isSentContainer(const mtpRequest &request) { // "request-like" wrap for msgIds vector
	if (request->size() < 9) return false;
	return (!request->msDate && !(*request)[6]); // msDate = 0, seqNo = 0
}
inline bool mtpRequestData::isStateRequest(const mtpRequest &request) {
	if (request->size() < 9) return false;
	return (mtpTypeId((*request)[8]) == mtpc_msgs_state_req);
}
inline bool mtpRequestData::needAck(const mtpRequest &request) {
	if (request->size() < 9) return false;
	return mtpRequestData::needAckByType((*request)[8]);
}
inline bool mtpRequestData::needAckByType(mtpTypeId type) {
	switch (type) {
	case mtpc_msg_container:
	case mtpc_msgs_ack:
	case mtpc_http_wait:
	case mtpc_bad_msg_notification:
	case mtpc_msgs_all_info:
	case mtpc_msg_detailed_info:
	case mtpc_msg_new_detailed_info:
		return false;
	}
	return true;
}

class MTProtoConnectionPrivate;
class MTPSessionData;

class MTPThread : public QThread {
public:
	MTPThread(QObject *parent = 0);
	uint32 getThreadId() const;

private:
	uint32 threadId;
};

class MTProtoConnection {
public:

	enum ConnectionType {
		TcpConnection,
		HttpConnection
	};

	MTProtoConnection();
	int32 start(MTPSessionData *data, int32 dc = 0); // return dc
	void restart();
	void stop();
	void stopped();
	~MTProtoConnection();

	enum {
		Disconnected = 0,
		Connecting = 1,
		Connected = 2,

		UpdateAlways = 666
	};

	int32 state() const;
	QString transport() const;

    /*template <typename TRequest> // not used
    uint64 sendAsync(const TRequest &request) {
        return data->sendAsync(request);
    }*/

private:

	QThread *thread;
	MTProtoConnectionPrivate *data;

};

class MTPabstractConnection : public QObject {
	Q_OBJECT

	typedef QList<mtpBuffer> BuffersQueue;

public:

	virtual void sendData(mtpBuffer &buffer) = 0; // has size + 3, buffer[0] = len, buffer[1] = packetnum, buffer[last] = crc32
	virtual void disconnectFromServer() = 0;
	virtual void connectToServer(const QString &addr, int32 port) = 0;
	virtual bool isConnected() = 0;
	virtual bool needHttpWait() {
		return false;
	}

	virtual int32 debugState() const = 0;

	virtual QString transport() const = 0;

	BuffersQueue &received() {
		return receivedQueue;
	}

signals:

	void receivedData();
	void receivedSome(); // to stop restart timer

	void error(bool maybeBadKey = false);

	void connected();
	void disconnected();

protected:

    BuffersQueue receivedQueue; // list of received packets, not processed yet

};

class MTPabstractTcpConnection : public MTPabstractConnection {
	Q_OBJECT

public:

	MTPabstractTcpConnection();

public slots:

	void socketRead();

protected:

	QTcpSocket sock;
	uint32 packetNum; // sent packet number

	uint32 packetRead, packetLeft; // reading from socket
	bool readingToShort;
	char *currentPos;
	mtpBuffer longBuffer;
	mtpPrime shortBuffer[MTPShortBufferSize];
	virtual void socketPacket(mtpPrime *packet, uint32 packetSize) = 0;

};

class MTPautoConnection : public MTPabstractTcpConnection {
	Q_OBJECT

public:

	MTPautoConnection(QThread *thread);

	void sendData(mtpBuffer &buffer);
	void disconnectFromServer();
	void connectToServer(const QString &addr, int32 port);
	bool isConnected();
	bool needHttpWait();

	int32 debugState() const;

	QString transport() const;

public slots:

	void socketError(QAbstractSocket::SocketError e);
	void requestFinished(QNetworkReply *reply);

	void onSocketConnected();
	void onSocketDisconnected();
	void onHttpStart();

protected:

	void socketPacket(mtpPrime *packet, uint32 packetSize);

private:

	void tcpSend(mtpBuffer &buffer);
	void httpSend(mtpBuffer &buffer);
	enum Status {
		WaitingBoth = 0,
		WaitingHttp,
		WaitingTcp,
		HttpReady,
		UsingHttp,
		UsingTcp,
	};
	Status status;
	MTPint128 tcpNonce, httpNonce;
	QTimer httpStartTimer;

	QNetworkAccessManager manager;
	QUrl address;

	typedef QSet<QNetworkReply*> Requests;
	Requests requests;

};

class MTPtcpConnection : public MTPabstractTcpConnection {
	Q_OBJECT

public:

	MTPtcpConnection(QThread *thread);

	void sendData(mtpBuffer &buffer);
	void disconnectFromServer();
	void connectToServer(const QString &addr, int32 port);
	bool isConnected();

	int32 debugState() const;

	QString transport() const;

public slots:

	void socketError(QAbstractSocket::SocketError e);

protected:

	void socketPacket(mtpPrime *packet, uint32 packetSize);

};

class MTPhttpConnection : public MTPabstractConnection {
	Q_OBJECT

public:

	MTPhttpConnection(QThread *thread);
	
	void sendData(mtpBuffer &buffer);
	void disconnectFromServer();
	void connectToServer(const QString &addr, int32 port);
	bool isConnected();
	bool needHttpWait();

	int32 debugState() const;

	QString transport() const;

public slots:

	void requestFinished(QNetworkReply *reply);

private:

	QNetworkAccessManager manager;
	QUrl address;

	typedef QSet<QNetworkReply*> Requests;
	Requests requests;

};

class MTProtoConnectionPrivate : public QObject {
	Q_OBJECT

public:

	MTProtoConnectionPrivate(QThread *thread, MTProtoConnection *owner, MTPSessionData *data, uint32 dc);
	~MTProtoConnectionPrivate();

	int32 getDC() const;

	int32 getState() const;
	QString transport() const;

signals:

	void needToReceive();
	void needToRestart();
	void stateChanged(qint32 newState);

public slots:

	void retryByTimer();
	void restartNow();
	void restart(bool maybeBadKey = false);

	void onBadConnection();
	void onOldConnection();
	void onSentSome(uint64 size);
	void onReceivedSome();

	void onError(bool maybeBadKey = false);
	void onReadyData();
	void socketStart(bool afterConfig = false);
	void onConnected();
	void doDisconnect();
	void doFinish();

	// Auth key creation packet receive slots
	void pqAnswered();
	void dhParamsAnswered();
	void dhClientParamsAnswered();

	// General packet receive slot, connected to conn->receivedData signal
	void handleReceived();

	// Sessions signals, when we need to send something
	void tryToSend();

	bool updateAuthKey();

	void sendPing();

	void onConfigLoaded();

private:

	void createConn();

	mtpMsgId prepareToSend(mtpRequest &request);

	bool sendRequest(mtpRequest &request, bool needAnyResponse);
	mtpRequestId wasSent(mtpMsgId msgId) const;

	int32 handleOneReceived(const mtpPrime *from, const mtpPrime *end, uint64 msgId, int32 serverTime, uint64 serverSalt, bool badTime);
	mtpBuffer ungzip(const mtpPrime *from, const mtpPrime *end) const;
	void handleMsgsStates(const QVector<MTPlong> &ids, const string &states, QVector<MTPlong> &acked);

	void clearMessages();

	bool setState(int32 state, int32 ifState = MTProtoConnection::UpdateAlways);
	mutable QReadWriteLock stateMutex;
	int32 _state;

	uint32 dc;
	MTProtoConnection *_owner;
	MTPabstractConnection *conn;

	QTimer retryTimer; // exp retry timer
	uint32 retryTimeout;
	quint64 retryWillFinish;

	QTimer oldConnectionTimer;
	bool oldConnection;

	QTimer connCheckTimer;
	uint32 receiveDelay;
	int64 firstSentAt;

	MTPMsgsAck ackRequest;
	QVector<MTPlong> *ackRequestData;

	// if badTime received - search for ids in sessionData->haveSent and sessionData->wereAcked and sync time/salt, return true if found
	bool requestsFixTimeSalt(const QVector<MTPlong> &ids, int32 serverTime, uint64 serverSalt);
	
	// remove msgs with such ids from sessionData->haveSent, add to sessionData->wereAcked
	void requestsAcked(const QVector<MTPlong> &ids);

	mtpPingId pingId, toSendPingId;
	mtpMsgId pingMsgId;

	mtpRequestId resend(mtpMsgId msgId, uint64 msCanWait = 0, bool forceContainer = false, bool sendMsgStateInfo = false);

	template <typename TRequest>
	void sendRequestNotSecure(const TRequest &request);

	template <typename TResponse>
	bool readResponseNotSecure(TResponse &response);

	bool restarted;

	uint64 keyId;
	MTPSessionData *sessionData;
	bool myKeyLock;
	void lockKey();
	void unlockKey();

	// Auth key creation fields and methods
	struct AuthKeyCreateData {
		AuthKeyCreateData()
		: new_nonce(*(MTPint256*)((uchar*)new_nonce_buf))
		, auth_key_aux_hash(*(MTPlong*)((uchar*)new_nonce_buf + 33))
		, retries(0)
		, g(0)
		, req_num(0)
		, msgs_sent(0) {
			memset(new_nonce_buf, 0, sizeof(new_nonce_buf));
			memset(aesKey, 0, sizeof(aesKey));
			memset(aesIV, 0, sizeof(aesIV));
			memset(auth_key, 0, sizeof(auth_key));
		}
		MTPint128 nonce, server_nonce;
		uchar new_nonce_buf[41]; // 32 bytes new_nonce + 1 check byte + 8 bytes of auth_key_aux_hash
		MTPint256 &new_nonce;
		MTPlong &auth_key_aux_hash;

		uint32 retries;
		MTPlong retry_id;

		string dh_prime;
		int32 g;
		string g_a;

		uchar aesKey[32], aesIV[32];
		uint32 auth_key[64];
		MTPlong auth_key_hash;

		uint32 req_num; // sent not encrypted request number
		uint32 msgs_sent;
	};
	AuthKeyCreateData *authKeyData;
	void dhClientParamsSend();
	void authKeyCreated();
	void clearAuthKeyData();

	QTimer pinger;

};
