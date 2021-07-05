/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/details/mtproto_received_ids_manager.h"
#include "mtproto/details/mtproto_serialized_request.h"
#include "mtproto/mtproto_auth_key.h"
#include "mtproto/mtproto_dc_options.h"
#include "mtproto/connection_abstract.h"
#include "mtproto/facade.h"
#include "base/openssl_help.h"
#include "base/timer.h"

namespace MTP {
namespace details {
class BoundKeyCreator;
} // namespace details

class Instance;

namespace details {

class AbstractConnection;
class SessionData;
class RSAPublicKey;
struct SessionOptions;

class SessionPrivate final : public QObject {
public:
	SessionPrivate(
		not_null<Instance*> instance,
		not_null<QThread*> thread,
		std::shared_ptr<SessionData> data,
		ShiftedDcId shiftedDcId);
	~SessionPrivate();

	[[nodiscard]] int32 getShiftedDcId() const;
	void dcOptionsChanged();
	void cdnConfigChanged();

	[[nodiscard]] int32 getState() const;
	[[nodiscard]] QString transport() const;

	void updateAuthKey();
	void restartNow();
	void sendPingForce();
	void tryToSend();

private:
	static constexpr auto kUpdateStateAlways = 666;

	struct TestConnection {
		ConnectionPointer data;
		int priority = 0;
	};
	struct SentContainer {
		crl::time sent = 0;
		std::vector<mtpMsgId> messages;
	};
	enum class HandleResult {
		Success,
		Ignored,
		RestartConnection,
		ResetSession,
		DestroyTemporaryKey,
		ParseError,
	};

	void connectToServer(bool afterConfig = false);
	void connectingTimedOut();
	void doDisconnect();
	void restart();
	void requestCDNConfig();
	void handleError(int errorCode);
	void onError(
		not_null<AbstractConnection*> connection,
		qint32 errorCode);
	void onConnected(not_null<AbstractConnection*> connection);
	void onDisconnected(not_null<AbstractConnection*> connection);
	void onSentSome(uint64 size);
	void onReceivedSome();

	void handleReceived();

	void retryByTimer();
	void waitConnectedFailed();
	void waitReceivedFailed();
	void waitBetterFailed();
	void markConnectionOld();
	void sendPingByTimer();
	void destroyAllConnections();

	void confirmBestConnection();
	void removeTestConnection(not_null<AbstractConnection*> connection);
	[[nodiscard]] int16 getProtocolDcId() const;

	void checkSentRequests();
	void clearOldContainers();

	mtpMsgId placeToContainer(
		SerializedRequest &toSendRequest,
		mtpMsgId &bigMsgId,
		bool forceNewMsgId,
		SerializedRequest &req);
	mtpMsgId prepareToSend(
		SerializedRequest &request,
		mtpMsgId currentLastId,
		bool forceNewMsgId);
	mtpMsgId replaceMsgId(
		SerializedRequest &request,
		mtpMsgId newId);

	bool sendSecureRequest(
		SerializedRequest &&request,
		bool needAnyResponse);
	mtpRequestId wasSent(mtpMsgId msgId) const;

	struct OuterInfo {
		mtpMsgId outerMsgId = 0;
		uint64 serverSalt = 0;
		int32 serverTime = 0;
		bool badTime = false;
	};
	[[nodiscard]] HandleResult handleOneReceived(
		const mtpPrime *from,
		const mtpPrime *end,
		uint64 msgId,
		OuterInfo info);
	[[nodiscard]] HandleResult handleBindResponse(
		mtpMsgId requestMsgId,
		const mtpBuffer &response);
	mtpBuffer ungzip(const mtpPrime *from, const mtpPrime *end) const;
	void handleMsgsStates(const QVector<MTPlong> &ids, const QByteArray &states);

	// _sessionDataMutex must be locked for read.
	bool setState(int state, int ifState = kUpdateStateAlways);

	void appendTestConnection(
		DcOptions::Variants::Protocol protocol,
		const QString &ip,
		int port,
		const bytes::vector &protocolSecret);

	// if badTime received - search for ids in sessionData->haveSent and sessionData->wereAcked and sync time/salt, return true if found
	bool requestsFixTimeSalt(const QVector<MTPlong> &ids, const OuterInfo &info);

	// if we had a confirmed fast request use its unixtime as a correct one.
	void correctUnixtimeByFastRequest(
		const QVector<MTPlong> &ids,
		TimeId serverTime);
	void correctUnixtimeWithBadLocal(TimeId serverTime);

	// remove msgs with such ids from sessionData->haveSent, add to sessionData->wereAcked
	void requestsAcked(const QVector<MTPlong> &ids, bool byResponse = false);

	void resend(mtpMsgId msgId, crl::time msCanWait = 0);
	void resendAll();
	void clearSpecialMsgId(mtpMsgId msgId);

	[[nodiscard]] DcType tryAcquireKeyCreation();
	void resetSession();
	void checkAuthKey();
	void authKeyChecked();
	void destroyTemporaryKey();
	void clearUnboundKeyCreator();
	void releaseKeyCreationOnFail();
	void applyAuthKey(AuthKeyPtr &&encryptionKey);
	[[nodiscard]] bool noMediaKeyWithExistingRegularKey() const;
	bool destroyOldEnoughPersistentKey();

	void setCurrentKeyId(uint64 newKeyId);
	void changeSessionId();
	[[nodiscard]] bool markSessionAsStarted();
	[[nodiscard]] uint32 nextRequestSeqNumber(bool needAck);

	[[nodiscard]] bool realDcTypeChanged();
	[[nodiscard]] MTPVector<MTPJSONObjectValue> prepareInitParams();

	const not_null<Instance*> _instance;
	const ShiftedDcId _shiftedDcId = 0;
	DcType _realDcType = DcType();
	DcType _currentDcType = DcType();

	mutable QReadWriteLock _stateMutex;
	int _state = DisconnectedState;

	bool _needSessionReset = false;

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

	mtpPingId _pingId = 0;
	mtpPingId _pingIdToSend = 0;
	crl::time _pingSendAt = 0;
	mtpMsgId _pingMsgId = 0;
	base::Timer _pingSender;
	base::Timer _checkSentRequestsTimer;
	base::Timer _clearOldContainersTimer;

	std::shared_ptr<SessionData> _sessionData;
	std::unique_ptr<SessionOptions> _options;
	AuthKeyPtr _encryptionKey;
	uint64 _keyId = 0;
	uint64 _sessionId = 0;
	uint64 _sessionSalt = 0;
	uint32 _messagesCounter = 0;
	bool _sessionMarkedAsStarted = false;

	QVector<MTPlong> _ackRequestData;
	QVector<MTPlong> _resendRequestData;
	base::flat_set<mtpMsgId> _stateRequestData;
	ReceivedIdsManager _receivedMessageIds;
	base::flat_map<mtpMsgId, mtpRequestId> _resendingIds;
	base::flat_map<mtpMsgId, mtpRequestId> _ackedIds;
	base::flat_map<mtpMsgId, SerializedRequest> _stateAndResendRequests;
	base::flat_map<mtpMsgId, SentContainer> _sentContainers;

	std::unique_ptr<BoundKeyCreator> _keyCreator;
	mtpMsgId _bindMsgId = 0;
	crl::time _bindMessageSent = 0;

};

} // namespace details
} // namespace MTP
