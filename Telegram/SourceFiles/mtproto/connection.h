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
#include "mtproto/facade.h"
#include "base/openssl_help.h"
#include "base/timer.h"

namespace MTP {
namespace details {
class DcKeyCreator;
class DcKeyChecker;
} // namespace details

// How much time to wait for some more requests, when sending msg acks.
constexpr auto kAckSendWaiting = crl::time(10000);

class Instance;

namespace internal {

class AbstractConnection;
class ConnectionPrivate;
class SessionData;
class RSAPublicKey;
struct ConnectionOptions;

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

	void createDcKey();
	void resetSession();
	void lockKey();
	void unlockKey();
	void authKeyCreated();

	not_null<Instance*> _instance;
	DcType _dcType = DcType::Regular;

	mutable QReadWriteLock stateConnMutex;
	int32 _state = DisconnectedState;

	bool _needSessionReset = false;

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
	crl::time _firstSentAt = -1;

	QVector<MTPlong> _ackRequestData;
	QVector<MTPlong> _resendRequestData;

	mtpPingId _pingId = 0;
	mtpPingId _pingIdToSend = 0;
	crl::time _pingSendAt = 0;
	mtpMsgId _pingMsgId = 0;
	base::Timer _pingSender;

	bool _restarted = false;
	bool _finished = false;

	uint64 _keyId = 0;
	QReadWriteLock _sessionDataMutex;
	SessionData *_sessionData = nullptr;
	std::unique_ptr<ConnectionOptions> _connectionOptions;

	bool _myKeyLock = false;

	std::unique_ptr<details::DcKeyCreator> _keyCreator;
	std::unique_ptr<details::DcKeyChecker> _keyChecker;

};

} // namespace internal
} // namespace MTP
