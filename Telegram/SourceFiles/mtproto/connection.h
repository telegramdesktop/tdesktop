/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/auth_key.h"
#include "mtproto/dc_options.h"
#include "mtproto/connection_abstract.h"
#include "base/openssl_help.h"
#include "base/timer.h"

namespace MTP {

// How much time to wait for some more requests, when sending msg acks.
constexpr auto kAckSendWaiting = crl::time(10000);

class Instance;

[[nodiscard]] bool IsPrimeAndGood(bytes::const_span primeBytes, int g);
struct ModExpFirst {
	static constexpr auto kRandomPowerSize = 256;

	bytes::vector modexp;
	bytes::vector randomPower;
};
[[nodiscard]] bool IsGoodModExpFirst(
	const openssl::BigNum &modexp,
	const openssl::BigNum &prime);
[[nodiscard]] ModExpFirst CreateModExp(
	int g,
	bytes::const_span primeBytes,
	bytes::const_span randomSeed);
[[nodiscard]] bytes::vector CreateAuthKey(
	bytes::const_span firstBytes,
	bytes::const_span randomBytes,
	bytes::const_span primeBytes);

namespace internal {

class AbstractConnection;
class ConnectionPrivate;
class SessionData;
class RSAPublicKey;
struct ConnectionOptions;

class Thread : public QThread {
	// The Q_OBJECT meta info is used for qobject_cast!
	Q_OBJECT

public:
	Thread() {
		static int ThreadCounter = 0;
		_threadIndex = ++ThreadCounter;
	}
	int getThreadIndex() const {
		return _threadIndex;
	}

private:
	int _threadIndex = 0;

};

class Connection {
public:
	enum ConnectionType {
		TcpConnection,
		HttpConnection
	};

	Connection(not_null<Instance*> instance);

	void start(SessionData *data, ShiftedDcId shiftedDcId);

	void kill();
	void waitTillFinish();
	~Connection();

	static const int UpdateAlways = 666;

	int32 state() const;
	QString transport() const;

private:
	not_null<Instance*> _instance;
	std::unique_ptr<QThread> _thread;
	ConnectionPrivate *_private = nullptr;

};

class ConnectionPrivate : public QObject {
	Q_OBJECT

public:
	ConnectionPrivate(
		not_null<Instance*> instance,
		not_null<QThread*> thread,
		not_null<Connection*> owner,
		not_null<SessionData*> data,
		ShiftedDcId shiftedDcId);
	~ConnectionPrivate();

	void stop();

	int32 getShiftedDcId() const;

	int32 getState() const;
	QString transport() const;

signals:
	void needToReceive();
	void needToRestart();
	void stateChanged(qint32 newState);
	void sessionResetDone();

	void needToSendAsync();
	void sendAnythingAsync(qint64 msWait);
	void sendHttpWaitAsync();
	void sendPongAsync(quint64 msgId, quint64 pingId);
	void sendMsgsStateInfoAsync(quint64 msgId, QByteArray data);
	void resendAsync(quint64 msgId, qint64 msCanWait, bool forceContainer, bool sendMsgStateInfo);
	void resendManyAsync(QVector<quint64> msgIds, qint64 msCanWait, bool forceContainer, bool sendMsgStateInfo);
	void resendAllAsync();

	void finished(internal::Connection *connection);

public slots:
	void restartNow();

	void onPingSendForce();

	void onSentSome(uint64 size);
	void onReceivedSome();

	void onReadyData();

	// Auth key creation packet receive slots
	void pqAnswered();
	void dhParamsAnswered();
	void dhClientParamsAnswered();

	// General packet receive slot, connected to conn->receivedData signal
	void handleReceived();

	// Sessions signals, when we need to send something
	void tryToSend();

	void updateAuthKey();

	void onConfigLoaded();
	void onCDNConfigLoaded();

private:
	struct TestConnection {
		ConnectionPointer data;
		int priority = 0;
	};
	void connectToServer(bool afterConfig = false);
	void connectingTimedOut();
	void doDisconnect();
	void restart();
	void finishAndDestroy();
	void requestCDNConfig();
	void handleError(int errorCode);
	void onError(
		not_null<AbstractConnection*> connection,
		qint32 errorCode);
	void onConnected(not_null<AbstractConnection*> connection);
	void onDisconnected(not_null<AbstractConnection*> connection);

	void retryByTimer();
	void waitConnectedFailed();
	void waitReceivedFailed();
	void waitBetterFailed();
	void markConnectionOld();
	void sendPingByTimer();

	void destroyAllConnections();
	void confirmBestConnection();
	void removeTestConnection(not_null<AbstractConnection*> connection);
	int16 getProtocolDcId() const;

	mtpMsgId placeToContainer(
		SecureRequest &toSendRequest,
		mtpMsgId &bigMsgId,
		mtpMsgId *&haveSentArr,
		SecureRequest &req);
	mtpMsgId prepareToSend(SecureRequest &request, mtpMsgId currentLastId);
	mtpMsgId replaceMsgId(SecureRequest &request, mtpMsgId newId);

	bool sendSecureRequest(
		SecureRequest &&request,
		bool needAnyResponse,
		QReadLocker &lockFinished);
	mtpRequestId wasSent(mtpMsgId msgId) const;

	enum class HandleResult {
		Success,
		Ignored,
		RestartConnection,
		ResetSession,
		ParseError,
	};
	[[nodiscard]] HandleResult handleOneReceived(const mtpPrime *from, const mtpPrime *end, uint64 msgId, int32 serverTime, uint64 serverSalt, bool badTime);
	mtpBuffer ungzip(const mtpPrime *from, const mtpPrime *end) const;
	void handleMsgsStates(const QVector<MTPlong> &ids, const QByteArray &states, QVector<MTPlong> &acked);

	void clearMessages();

	bool setState(int32 state, int32 ifState = Connection::UpdateAlways);

	bytes::vector encryptPQInnerRSA(const MTPP_Q_inner_data &data, const internal::RSAPublicKey &key);
	std::string encryptClientDHInner(const MTPClient_DH_Inner_Data &data);
	void appendTestConnection(
		DcOptions::Variants::Protocol protocol,
		const QString &ip,
		int port,
		const bytes::vector &protocolSecret);

	// if badTime received - search for ids in sessionData->haveSent and sessionData->wereAcked and sync time/salt, return true if found
	bool requestsFixTimeSalt(const QVector<MTPlong> &ids, int32 serverTime, uint64 serverSalt);

	// remove msgs with such ids from sessionData->haveSent, add to sessionData->wereAcked
	void requestsAcked(const QVector<MTPlong> &ids, bool byResponse = false);

	void resend(quint64 msgId, qint64 msCanWait = 0, bool forceContainer = false, bool sendMsgStateInfo = false);
	void resendMany(QVector<quint64> msgIds, qint64 msCanWait = 0, bool forceContainer = false, bool sendMsgStateInfo = false);

	template <typename Request>
	void sendNotSecureRequest(const Request &request);

	template <typename Response>
	[[nodiscard]] bool readNotSecureResponse(Response &response);

	not_null<Instance*> _instance;
	DcType _dcType = DcType::Regular;

	mutable QReadWriteLock stateConnMutex;
	int32 _state = DisconnectedState;

	bool _needSessionReset = false;
	void resetSession();

	ShiftedDcId _shiftedDcId = 0;
	not_null<Connection*> _owner;
	ConnectionPointer _connection;
	std::vector<TestConnection> _testConnections;
	crl::time _startedConnectingAt = 0;

	base::Timer _retryTimer; // exp retry timer
	int _retryTimeout = 1;
	qint64 _retryWillFinish = 0;

	base::Timer _oldConnectionTimer;
	bool _oldConnection = true;

	base::Timer _waitForConnectedTimer;
	base::Timer _waitForReceivedTimer;
	base::Timer _waitForBetterTimer;
	crl::time _waitForReceived = 0;
	crl::time _waitForConnected = 0;
	crl::time firstSentAt = -1;

	QVector<MTPlong> ackRequestData, resendRequestData;

	mtpPingId _pingId = 0;
	mtpPingId _pingIdToSend = 0;
	crl::time _pingSendAt = 0;
	mtpMsgId _pingMsgId = 0;
	base::Timer _pingSender;

	bool restarted = false;
	bool _finished = false;

	uint64 keyId = 0;
	QReadWriteLock sessionDataMutex;
	SessionData *sessionData = nullptr;
	std::unique_ptr<ConnectionOptions> _connectionOptions;

	bool myKeyLock = false;
	void lockKey();
	void unlockKey();

	// Auth key creation fields and methods
	struct AuthKeyCreateData {
		AuthKeyCreateData()
		: new_nonce(*(MTPint256*)((uchar*)new_nonce_buf))
		, auth_key_aux_hash(*(MTPlong*)((uchar*)new_nonce_buf + 33)) {
		}
		MTPint128 nonce, server_nonce;
		uchar new_nonce_buf[41] = { 0 }; // 32 bytes new_nonce + 1 check byte + 8 bytes of auth_key_aux_hash
		MTPint256 &new_nonce;
		MTPlong &auth_key_aux_hash;

		uint32 retries = 0;
		MTPlong retry_id;

		int32 g = 0;

		uchar aesKey[32] = { 0 };
		uchar aesIV[32] = { 0 };
		MTPlong auth_key_hash;
	};
	struct AuthKeyCreateStrings {
		bytes::vector dh_prime;
		bytes::vector g_a;
		AuthKey::Data auth_key = { { gsl::byte{} } };
	};
	std::unique_ptr<AuthKeyCreateData> _authKeyData;
	std::unique_ptr<AuthKeyCreateStrings> _authKeyStrings;

	void dhClientParamsSend();
	void authKeyCreated();
	void clearAuthKeyData();

};

} // namespace internal
} // namespace MTP
