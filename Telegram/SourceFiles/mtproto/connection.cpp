/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/connection.h"

#include "mtproto/details/mtproto_bound_key_creator.h"
#include "mtproto/details/mtproto_dump_to_text.h"
#include "mtproto/session.h"
#include "mtproto/mtproto_rsa_public_key.h"
#include "mtproto/mtproto_rpc_sender.h"
#include "mtproto/dc_options.h"
#include "mtproto/connection_abstract.h"
#include "zlib.h"
#include "core/application.h"
#include "core/launcher.h"
#include "lang/lang_keys.h"
#include "base/openssl_help.h"
#include "base/qthelp_url.h"
#include "base/unixtime.h"

namespace MTP {
namespace internal {
namespace {

constexpr auto kIntSize = static_cast<int>(sizeof(mtpPrime));
constexpr auto kWaitForBetterTimeout = crl::time(2000);
constexpr auto kMinConnectedTimeout = crl::time(1000);
constexpr auto kMaxConnectedTimeout = crl::time(8000);
constexpr auto kMinReceiveTimeout = crl::time(4000);
constexpr auto kMaxReceiveTimeout = crl::time(64000);
constexpr auto kMarkConnectionOldTimeout = crl::time(192000);
constexpr auto kPingDelayDisconnect = 60;
constexpr auto kPingSendAfter = 30 * crl::time(1000);
constexpr auto kPingSendAfterForce = 45 * crl::time(1000);
constexpr auto kTemporaryExpiresIn = TimeId(10);
constexpr auto kBindKeyAdditionalExpiresTimeout = TimeId(30);
constexpr auto kTestModeDcIdShift = 10000;
constexpr auto kCheckSentRequestsEach = 1 * crl::time(1000);
constexpr auto kKeyOldEnoughForDestroy = 60 * crl::time(1000);

// If we can't connect for this time we will ask _instance to update config.
constexpr auto kRequestConfigTimeout = 8 * crl::time(1000);

// Don't try to handle messages larger than this size.
constexpr auto kMaxMessageLength = 16 * 1024 * 1024;

// How much time passed from send till we resend request or check its state.
constexpr auto kCheckSentRequestTimeout = 10 * crl::time(1000);

// How much time to wait for some more requests,
// when resending request or checking its state.
constexpr auto kSendStateRequestWaiting = crl::time(1000);

// Container lives 10 minutes in haveSent map.
constexpr auto kContainerLives = TimeId(600);

using namespace details;

[[nodiscard]] QString LogIdsVector(const QVector<MTPlong> &ids) {
	if (!ids.size()) return "[]";
	auto idsStr = QString("[%1").arg(ids.cbegin()->v);
	for (const auto &id : ids) {
		idsStr += QString(", %2").arg(id.v);
	}
	return idsStr + "]";
}

[[nodiscard]] QString LogIds(const QVector<uint64> &ids) {
	if (!ids.size()) return "[]";
	auto idsStr = QString("[%1").arg(*ids.cbegin());
	for (const auto id : ids) {
		idsStr += QString(", %2").arg(id);
	}
	return idsStr + "]";
}

void wrapInvokeAfter(SecureRequest &to, const SecureRequest &from, const RequestMap &haveSent, int32 skipBeforeRequest = 0) {
	const auto afterId = *(mtpMsgId*)(from->after->data() + 4);
	const auto i = afterId ? haveSent.constFind(afterId) : haveSent.cend();
	int32 size = to->size(), lenInInts = (tl::count_length(from) >> 2), headlen = 4, fulllen = headlen + lenInInts;
	if (i == haveSent.constEnd()) { // no invoke after or such msg was not sent or was completed recently
		to->resize(size + fulllen + skipBeforeRequest);
		if (skipBeforeRequest) {
			memcpy(to->data() + size, from->constData() + 4, headlen * sizeof(mtpPrime));
			memcpy(to->data() + size + headlen + skipBeforeRequest, from->constData() + 4 + headlen, lenInInts * sizeof(mtpPrime));
		} else {
			memcpy(to->data() + size, from->constData() + 4, fulllen * sizeof(mtpPrime));
		}
	} else {
		to->resize(size + fulllen + skipBeforeRequest + 3);
		memcpy(to->data() + size, from->constData() + 4, headlen * sizeof(mtpPrime));
		(*to)[size + 3] += 3 * sizeof(mtpPrime);
		*((mtpTypeId*)&((*to)[size + headlen + skipBeforeRequest])) = mtpc_invokeAfterMsg;
		memcpy(to->data() + size + headlen + skipBeforeRequest + 1, &afterId, 2 * sizeof(mtpPrime));
		memcpy(to->data() + size + headlen + skipBeforeRequest + 3, from->constData() + 4 + headlen, lenInInts * sizeof(mtpPrime));
		if (size + 3 != 7) (*to)[7] += 3 * sizeof(mtpPrime);
	}
}

} // namespace

Connection::Connection(not_null<Instance*> instance)
: _instance(instance) {
}

Connection::~Connection() {
	Expects(_private == nullptr);

	if (_thread) {
		waitTillFinish();
	}
}

void Connection::start(std::shared_ptr<SessionData> sessionData, ShiftedDcId shiftedDcId) {
	Expects(_thread == nullptr && _private == nullptr);

	_thread = std::make_unique<QThread>();
	auto newData = std::make_unique<ConnectionPrivate>(
		_instance,
		_thread.get(),
		this,
		std::move(sessionData),
		shiftedDcId);

	// will be deleted in the thread::finished signal
	_private = newData.release();
	_thread->start();
}

void Connection::kill() {
	Expects(_private != nullptr && _thread != nullptr);

	_private->stop();
	_private = nullptr;
	_thread->quit();
}

void Connection::waitTillFinish() {
	Expects(_private == nullptr && _thread != nullptr);

	DEBUG_LOG(("Waiting for connectionThread to finish"));
	_thread->wait();
	_thread.reset();
}

int32 Connection::state() const {
	Expects(_private != nullptr && _thread != nullptr);

	return _private->getState();
}

QString Connection::transport() const {
	Expects(_private != nullptr && _thread != nullptr);

	return _private->transport();
}

void ConnectionPrivate::appendTestConnection(
		DcOptions::Variants::Protocol protocol,
		const QString &ip,
		int port,
		const bytes::vector &protocolSecret) {
	QWriteLocker lock(&stateConnMutex);

	const auto priority = (qthelp::is_ipv6(ip) ? 0 : 1)
		+ (protocol == DcOptions::Variants::Tcp ? 1 : 0)
		+ (protocolSecret.empty() ? 0 : 1);
	_testConnections.push_back({
		AbstractConnection::Create(
			_instance,
			protocol,
			thread(),
			protocolSecret,
			_connectionOptions->proxy),
		priority
	});
	const auto weak = _testConnections.back().data.get();
	connect(weak, &AbstractConnection::error, [=](int errorCode) {
		onError(weak, errorCode);
	});
	connect(weak, &AbstractConnection::receivedSome, [=] {
		onReceivedSome();
	});
	_firstSentAt = 0;
	if (_oldConnection) {
		_oldConnection = false;
		DEBUG_LOG(("This connection marked as not old!"));
	}
	_oldConnectionTimer.callOnce(kMarkConnectionOldTimeout);
	connect(weak, &AbstractConnection::connected, [=] {
		onConnected(weak);
	});
	connect(weak, &AbstractConnection::disconnected, [=] {
		onDisconnected(weak);
	});
	connect(weak, &AbstractConnection::syncTimeRequest, [=] {
		InvokeQueued(_instance, [instance = _instance] {
			instance->syncHttpUnixtime();
		});
	});

	InvokeQueued(_testConnections.back().data, [=] {
		weak->connectToServer(ip, port, protocolSecret, getProtocolDcId());
	});
}

int16 ConnectionPrivate::getProtocolDcId() const {
	const auto dcId = BareDcId(_shiftedDcId);
	const auto simpleDcId = isTemporaryDcId(dcId)
		? getRealIdFromTemporaryDcId(dcId)
		: dcId;
	const auto testedDcId = cTestMode()
		? (kTestModeDcIdShift + simpleDcId)
		: simpleDcId;
	return (_dcType == DcType::MediaDownload)
		? -testedDcId
		: testedDcId;
}

void ConnectionPrivate::checkSentRequests() {
	QVector<mtpMsgId> removingIds; // remove very old (10 minutes) containers and resend requests
	auto requesting = false;
	{
		QReadLocker locker(_sessionData->haveSentMutex());
		auto &haveSent = _sessionData->haveSentMap();
		const auto haveSentCount = haveSent.size();
		auto ms = crl::now();
		for (auto i = haveSent.begin(), e = haveSent.end(); i != e; ++i) {
			auto &req = i.value();
			if (req->msDate > 0) {
				if (req->msDate + kCheckSentRequestTimeout < ms) {
					// Need to check state.
					req->msDate = ms;
					if (_stateRequestData.emplace(i.key()).second) {
						requesting = true;
					}
				}
			} else if (base::unixtime::now()
				> int32(i.key() >> 32) + kContainerLives) {
				removingIds.reserve(haveSentCount);
				removingIds.push_back(i.key());
			}
		}
	}
	if (requesting) {
		_sessionData->queueSendAnything(kSendStateRequestWaiting);
	}
	if (!removingIds.isEmpty()) {
		QWriteLocker locker(_sessionData->haveSentMutex());
		auto &haveSent = _sessionData->haveSentMap();
		for (uint32 i = 0, l = removingIds.size(); i < l; ++i) {
			auto j = haveSent.find(removingIds[i]);
			if (j != haveSent.cend()) {
				Assert(!j.value()->requestId);
				haveSent.erase(j);
			}
		}
	}
}

void ConnectionPrivate::destroyAllConnections() {
	clearUnboundKeyCreator();
	_waitForBetterTimer.cancel();
	_waitForReceivedTimer.cancel();
	_waitForConnectedTimer.cancel();
	_testConnections.clear();
	_connection = nullptr;
}

ConnectionPrivate::ConnectionPrivate(
	not_null<Instance*> instance,
	not_null<QThread*> thread,
	not_null<Connection*> owner,
	std::shared_ptr<SessionData> data,
	ShiftedDcId shiftedDcId)
: QObject(nullptr)
, _instance(instance)
, _state(DisconnectedState)
, _shiftedDcId(shiftedDcId)
, _owner(owner)
, _retryTimer(thread, [=] { retryByTimer(); })
, _oldConnectionTimer(thread, [=] { markConnectionOld(); })
, _waitForConnectedTimer(thread, [=] { waitConnectedFailed(); })
, _waitForReceivedTimer(thread, [=] { waitReceivedFailed(); })
, _waitForBetterTimer(thread, [=] { waitBetterFailed(); })
, _waitForReceived(kMinReceiveTimeout)
, _waitForConnected(kMinConnectedTimeout)
, _pingSender(thread, [=] { sendPingByTimer(); })
, _checkSentRequestsTimer(thread, [=] { checkSentRequests(); })
, _sessionData(std::move(data)) {
	Expects(_shiftedDcId != 0);

	moveToThread(thread);

	connect(thread, &QThread::started, this, [=] {
		_checkSentRequestsTimer.callEach(kCheckSentRequestsEach);
		connectToServer();
	});
	connect(thread, &QThread::finished, this, [=] { finishAndDestroy(); });

	connect(_sessionData->owner(), SIGNAL(authKeyChanged()), this, SLOT(updateAuthKey()), Qt::QueuedConnection);
	connect(_sessionData->owner(), SIGNAL(needToRestart()), this, SLOT(restartNow()), Qt::QueuedConnection);
	connect(_sessionData->owner(), SIGNAL(needToSend()), this, SLOT(tryToSend()), Qt::QueuedConnection);
	connect(_sessionData->owner(), SIGNAL(needToPing()), this, SLOT(onPingSendForce()), Qt::QueuedConnection);
}

ConnectionPrivate::~ConnectionPrivate() {
	releaseKeyCreationOnFail();

	Expects(_finished);
	Expects(!_connection);
	Expects(_testConnections.empty());
}

void ConnectionPrivate::onConfigLoaded() {
	connectToServer(true);
}

void ConnectionPrivate::onCDNConfigLoaded() {
	restart();
}

int32 ConnectionPrivate::getShiftedDcId() const {
	return _shiftedDcId;
}

int32 ConnectionPrivate::getState() const {
	QReadLocker lock(&stateConnMutex);
	int32 result = _state;
	if (_state < 0) {
		if (_retryTimer.isActive()) {
			result = int32(crl::now() - _retryWillFinish);
			if (result >= 0) {
				result = -1;
			}
		}
	}
	return result;
}

QString ConnectionPrivate::transport() const {
	QReadLocker lock(&stateConnMutex);
	if (!_connection || (_state < 0)) {
		return QString();
	}

	Assert(_connectionOptions != nullptr);
	return _connection->transport();
}

bool ConnectionPrivate::setState(int32 state, int32 ifState) {
	if (ifState != Connection::UpdateAlways) {
		QReadLocker lock(&stateConnMutex);
		if (_state != ifState) {
			return false;
		}
	}

	QWriteLocker lock(&stateConnMutex);
	if (_state == state) {
		return false;
	}
	_state = state;
	if (state < 0) {
		_retryTimeout = -state;
		_retryTimer.callOnce(_retryTimeout);
		_retryWillFinish = crl::now() + _retryTimeout;
	}
	lock.unlock();

	_sessionData->queueConnectionStateChange(state);
	return true;
}

void ConnectionPrivate::resetSession() {
	MTP_LOG(_shiftedDcId, ("Resetting session!"));
	_needSessionReset = false;

	DEBUG_LOG(("MTP Info: creating new session in resetSession."));
	changeSessionId();

	_sessionData->queueResetDone();
}

void ConnectionPrivate::changeSessionId() {
	auto sessionId = _sessionId;
	do {
		sessionId = openssl::RandomValue<uint64>();
	} while (_sessionId == sessionId);

	DEBUG_LOG(("MTP Info: setting server_session: %1").arg(sessionId));

	_sessionId = sessionId;
	_messagesCounter = 0;
	_sessionMarkedAsStarted = false;
	_ackRequestData.clear();
	_resendRequestData.clear();
	_stateRequestData.clear();
	_receivedMessageIds.clear();
}

uint32 ConnectionPrivate::nextRequestSeqNumber(bool needAck) {
	const auto result = _messagesCounter;
	_messagesCounter += (needAck ? 1 : 0);
	return result * 2 + (needAck ? 1 : 0);
}

bool ConnectionPrivate::markSessionAsStarted() {
	if (_sessionMarkedAsStarted) {
		return false;
	}
	_sessionMarkedAsStarted = true;
	return true;
}

mtpMsgId ConnectionPrivate::prepareToSend(
		SecureRequest &request,
		mtpMsgId currentLastId,
		bool forceNewMsgId) {
	Expects(request->size() > 8);

	if (const auto msgId = request.getMsgId()) {
		// resending this request
		QWriteLocker lock(_sessionData->toResendMutex());
		auto &toResend = _sessionData->toResendMap();
		const auto i = toResend.find(msgId);
		if (i != toResend.cend()) {
			toResend.erase(i);
		}
		lock.unlock();

		return (forceNewMsgId || msgId > currentLastId)
			? replaceMsgId(request, currentLastId)
			: msgId;
	}
	request.setMsgId(currentLastId);
	request.setSeqNo(nextRequestSeqNumber(request.needAck()));
	if (request->requestId) {
		MTP_LOG(_shiftedDcId, ("[r%1] msg_id 0 -> %2").arg(request->requestId).arg(currentLastId));
	}
	return currentLastId;
}

mtpMsgId ConnectionPrivate::replaceMsgId(SecureRequest &request, mtpMsgId newId) {
	Expects(request->size() > 8);

	const auto oldMsgId = request.getMsgId();
	if (oldMsgId == newId) {
		return newId;
	}
	QWriteLocker locker(_sessionData->toResendMutex());
	// haveSentMutex() and wereAckedMutex() were locked in tryToSend()

	auto &toResend = _sessionData->toResendMap();
	auto &wereAcked = _sessionData->wereAckedMap();
	auto &haveSent = _sessionData->haveSentMap();

	while (toResend.constFind(newId) != toResend.cend()
		|| wereAcked.constFind(newId) != wereAcked.cend()
		|| haveSent.constFind(newId) != haveSent.cend()) {
		newId = base::unixtime::mtproto_msg_id();
	}

	MTP_LOG(_shiftedDcId, ("[r%1] msg_id %2 -> %3").arg(request->requestId).arg(oldMsgId).arg(newId));

	const auto i = toResend.find(oldMsgId);
	if (i != toResend.cend()) {
		const auto req = i.value();
		toResend.erase(i);
		toResend.insert(newId, req);
	}

	const auto j = wereAcked.find(oldMsgId);
	if (j != wereAcked.cend()) {
		const auto req = j.value();
		wereAcked.erase(j);
		wereAcked.insert(newId, req);
	}

	const auto k = haveSent.find(oldMsgId);
	if (k != haveSent.cend()) {
		const auto req = k.value();
		haveSent.erase(k);
		haveSent.insert(newId, req);
	}

	for (auto l = haveSent.begin(); l != haveSent.cend(); ++l) {
		const auto req = l.value();
		if (req.isSentContainer()) {
			const auto ids = (mtpMsgId *)(req->data() + 8);
			for (uint32 i = 0, l = (req->size() - 8) >> 1; i < l; ++i) {
				if (ids[i] == oldMsgId) {
					ids[i] = newId;
				}
			}
		}
	}

	request.setMsgId(newId);
	request.setSeqNo(nextRequestSeqNumber(request.needAck()));
	return newId;
}

mtpMsgId ConnectionPrivate::placeToContainer(
		SecureRequest &toSendRequest,
		mtpMsgId &bigMsgId,
		bool forceNewMsgId,
		mtpMsgId *&haveSentArr,
		SecureRequest &req) {
	const auto msgId = prepareToSend(req, bigMsgId, forceNewMsgId);
	if (msgId >= bigMsgId) {
		bigMsgId = base::unixtime::mtproto_msg_id();
	}
	*(haveSentArr++) = msgId;

	uint32 from = toSendRequest->size(), len = req.messageSize();
	toSendRequest->resize(from + len);
	memcpy(toSendRequest->data() + from, req->constData() + 4, len * sizeof(mtpPrime));

	return msgId;
}

void ConnectionPrivate::tryToSend() {
	if (!_connection || !_keyId) {
		return;
	}

	const auto needsLayer = !_sessionData->connectionInited();
	const auto state = getState();
	const auto sendOnlyFirstPing = (state != ConnectedState);
	const auto sendAll = !sendOnlyFirstPing && !_keyCreator;
	const auto isMainSession = (GetDcIdShift(_shiftedDcId) == 0);
	if (sendOnlyFirstPing && !_pingIdToSend) {
		DEBUG_LOG(("MTP Info: dc %1 not sending, waiting for Connected state, state: %2").arg(_shiftedDcId).arg(state));
		return; // just do nothing, if is not connected yet
	} else if (isMainSession
		&& !sendOnlyFirstPing
		&& !_pingIdToSend
		&& !_pingId
		&& _pingSendAt <= crl::now()) {
		_pingIdToSend = openssl::RandomValue<mtpPingId>();
	}
	const auto forceNewMsgId = sendAll && markSessionAsStarted();
	if (forceNewMsgId && _keyCreator) {
		_keyCreator->restartBinder();
	}

	auto pingRequest = SecureRequest();
	auto ackRequest = SecureRequest();
	auto resendRequest = SecureRequest();
	auto stateRequest = SecureRequest();
	auto httpWaitRequest = SecureRequest();
	auto bindDcKeyRequest = SecureRequest();
	if (_pingIdToSend) {
		if (sendOnlyFirstPing || !isMainSession) {
			DEBUG_LOG(("MTP Info: sending ping, ping_id: %1"
				).arg(_pingIdToSend));
			pingRequest = SecureRequest::Serialize(MTPPing(
				MTP_long(_pingIdToSend)
			));
		} else {
			DEBUG_LOG(("MTP Info: sending ping_delay_disconnect, "
				"ping_id: %1").arg(_pingIdToSend));
			pingRequest = SecureRequest::Serialize(MTPPing_delay_disconnect(
				MTP_long(_pingIdToSend),
				MTP_int(kPingDelayDisconnect)));
			_pingSender.callOnce(kPingSendAfterForce);
		}
		_pingSendAt = pingRequest->msDate + kPingSendAfter;
		_pingId = base::take(_pingIdToSend);
	} else if (!sendAll) {
		DEBUG_LOG(("MTP Info: dc %1 sending only service or bind."
			).arg(_shiftedDcId));
	} else {
		DEBUG_LOG(("MTP Info: dc %1 trying to send after ping, state: %2"
			).arg(_shiftedDcId
			).arg(state));
	}

	if (!sendOnlyFirstPing) {
		if (!_ackRequestData.isEmpty()) {
			ackRequest = SecureRequest::Serialize(MTPMsgsAck(
				MTP_msgs_ack(MTP_vector<MTPlong>(
					base::take(_ackRequestData)))));
		}
		if (!_resendRequestData.isEmpty()) {
			resendRequest = SecureRequest::Serialize(MTPMsgResendReq(
				MTP_msg_resend_req(MTP_vector<MTPlong>(
					base::take(_resendRequestData)))));
		}
		if (!_stateRequestData.empty()) {
			auto ids = QVector<MTPlong>();
			ids.reserve(_stateRequestData.size());
			for (const auto id : base::take(_stateRequestData)) {
				ids.push_back(MTP_long(id));
			}
			stateRequest = SecureRequest::Serialize(MTPMsgsStateReq(
				MTP_msgs_state_req(MTP_vector<MTPlong>(ids))));
			// Add to haveSent / wereAcked maps, but don't add to requestMap.
			stateRequest->requestId = GetNextRequestId();
		}
		if (_connection->usingHttpWait()) {
			httpWaitRequest = SecureRequest::Serialize(MTPHttpWait(
				MTP_http_wait(MTP_int(100), MTP_int(30), MTP_int(25000))));
		}
		if (_keyCreator && _keyCreator->bindReadyToRequest()) {
			bindDcKeyRequest = _keyCreator->prepareBindRequest(
				_encryptionKey,
				_sessionId);

			// This is a special request with msgId used inside the message
			// body, so it is prepared already with a msgId and we place
			// seqNo for it manually here.
			bindDcKeyRequest.setSeqNo(
				nextRequestSeqNumber(bindDcKeyRequest.needAck()));
		//} else if (!_keyChecker) {
		//	if (const auto &keyForCheck = _sessionData->getKeyForCheck()) {
		//		_keyChecker = std::make_unique<details::DcKeyChecker>(
		//			_instance,
		//			_shiftedDcId,
		//			keyForCheck);
		//		bindDcKeyRequest = _keyChecker->prepareRequest(
		//			_encryptionKey,
		//			_sessionId);

		//		// This is a special request with msgId used inside the message
		//		// body, so it is prepared already with a msgId and we place
		//		// seqNo for it manually here.
		//		bindDcKeyRequest.setSeqNo(
		//			nextRequestSeqNumber(bindDcKeyRequest.needAck()));
		//	}
		}
	}

	MTPInitConnection<SecureRequest> initWrapper;
	int32 initSize = 0, initSizeInInts = 0;
	if (needsLayer) {
		Assert(_connectionOptions != nullptr);
		const auto systemLangCode = _connectionOptions->systemLangCode;
		const auto cloudLangCode = _connectionOptions->cloudLangCode;
		const auto langPackName = _connectionOptions->langPackName;
		const auto deviceModel = (_dcType == DcType::Cdn)
			? "n/a"
			: _instance->deviceModel();
		const auto systemVersion = (_dcType == DcType::Cdn)
			? "n/a"
			: _instance->systemVersion();
#if defined OS_MAC_STORE
		const auto appVersion = QString::fromLatin1(AppVersionStr)
			+ " mac store";
#elif defined OS_WIN_STORE // OS_MAC_STORE
		const auto appVersion = QString::fromLatin1(AppVersionStr)
			+ " win store";
#else // OS_MAC_STORE || OS_WIN_STORE
		const auto appVersion = QString::fromLatin1(AppVersionStr);
#endif // OS_MAC_STORE || OS_WIN_STORE
		const auto proxyType = _connectionOptions->proxy.type;
		const auto mtprotoProxy = (proxyType == ProxyData::Type::Mtproto);
		const auto clientProxyFields = mtprotoProxy
			? MTP_inputClientProxy(
				MTP_string(_connectionOptions->proxy.host),
				MTP_int(_connectionOptions->proxy.port))
			: MTPInputClientProxy();
		using Flag = MTPInitConnection<SecureRequest>::Flag;
		initWrapper = MTPInitConnection<SecureRequest>(
			MTP_flags(mtprotoProxy ? Flag::f_proxy : Flag(0)),
			MTP_int(ApiId),
			MTP_string(deviceModel),
			MTP_string(systemVersion),
			MTP_string(appVersion),
			MTP_string(systemLangCode),
			MTP_string(langPackName),
			MTP_string(cloudLangCode),
			clientProxyFields,
			SecureRequest());
		initSizeInInts = (tl::count_length(initWrapper) >> 2) + 2;
		initSize = initSizeInInts * sizeof(mtpPrime);
	}

	bool needAnyResponse = false;
	SecureRequest toSendRequest;
	{
		QWriteLocker locker1(_sessionData->toSendMutex());

		auto toSendDummy = PreRequestMap();
		auto &toSend = sendAll
			? _sessionData->toSendMap()
			: toSendDummy;
		if (!sendAll) {
			locker1.unlock();
		} else {
			int time = crl::now();
			int now = crl::now();
		}

		uint32 toSendCount = toSend.size();
		if (pingRequest) ++toSendCount;
		if (ackRequest) ++toSendCount;
		if (resendRequest) ++toSendCount;
		if (stateRequest) ++toSendCount;
		if (httpWaitRequest) ++toSendCount;
		if (bindDcKeyRequest) ++toSendCount;

		if (!toSendCount) {
			return; // nothing to send
		}

		const auto first = pingRequest
			? pingRequest
			: ackRequest
			? ackRequest
			: resendRequest
			? resendRequest
			: stateRequest
			? stateRequest
			: httpWaitRequest
			? httpWaitRequest
			: bindDcKeyRequest
			? bindDcKeyRequest
			: toSend.cbegin().value();
		if (toSendCount == 1 && first->msDate > 0) { // if can send without container
			toSendRequest = first;
			if (sendAll) {
				toSend.clear();
				locker1.unlock();
			}

			const auto msgId = prepareToSend(
				toSendRequest,
				base::unixtime::mtproto_msg_id(),
				forceNewMsgId);
			if (pingRequest) {
				_pingMsgId = msgId;
				needAnyResponse = true;
			} else if (resendRequest || stateRequest) {
				needAnyResponse = true;
			}

			if (toSendRequest->requestId) {
				if (toSendRequest.needAck()) {
					toSendRequest->msDate = toSendRequest.isStateRequest() ? 0 : crl::now();

					QWriteLocker locker2(_sessionData->haveSentMutex());
					auto &haveSent = _sessionData->haveSentMap();
					haveSent.insert(msgId, toSendRequest);

					const auto wrapLayer = needsLayer && toSendRequest->needsLayer;
					if (toSendRequest->after) {
						const auto toSendSize = tl::count_length(toSendRequest) >> 2;
						auto wrappedRequest = SecureRequest::Prepare(
							toSendSize,
							toSendSize + 3);
						wrappedRequest->resize(4);
						memcpy(wrappedRequest->data(), toSendRequest->constData(), 4 * sizeof(mtpPrime));
						wrapInvokeAfter(wrappedRequest, toSendRequest, haveSent);
						toSendRequest = std::move(wrappedRequest);
					}
					if (wrapLayer) {
						const auto noWrapSize = (tl::count_length(toSendRequest) >> 2);
						const auto toSendSize = noWrapSize + initSizeInInts;
						auto wrappedRequest = SecureRequest::Prepare(toSendSize);
						memcpy(wrappedRequest->data(), toSendRequest->constData(), 7 * sizeof(mtpPrime)); // all except length
						wrappedRequest->push_back(mtpc_invokeWithLayer);
						wrappedRequest->push_back(internal::CurrentLayer);
						initWrapper.write<mtpBuffer>(*wrappedRequest);
						wrappedRequest->resize(wrappedRequest->size() + noWrapSize);
						memcpy(wrappedRequest->data() + wrappedRequest->size() - noWrapSize, toSendRequest->constData() + 8, noWrapSize * sizeof(mtpPrime));
						toSendRequest = std::move(wrappedRequest);
					}

					needAnyResponse = true;
				} else {
					QWriteLocker locker3(_sessionData->wereAckedMutex());
					_sessionData->wereAckedMap().insert(msgId, toSendRequest->requestId);
				}
			}
		} else { // send in container
			bool willNeedInit = false;
			uint32 containerSize = 1 + 1, idsWrapSize = (toSendCount << 1); // cons + vector size, idsWrapSize - size of "request-like" wrap for msgId vector
			if (pingRequest) containerSize += pingRequest.messageSize();
			if (ackRequest) containerSize += ackRequest.messageSize();
			if (resendRequest) containerSize += resendRequest.messageSize();
			if (stateRequest) containerSize += stateRequest.messageSize();
			if (httpWaitRequest) containerSize += httpWaitRequest.messageSize();
			if (bindDcKeyRequest) containerSize += bindDcKeyRequest.messageSize();
			for (auto i = toSend.begin(), e = toSend.end(); i != e; ++i) {
				containerSize += i.value().messageSize();
				if (needsLayer && i.value()->needsLayer) {
					containerSize += initSizeInInts;
					willNeedInit = true;
				}
			}
			mtpBuffer initSerialized;
			if (willNeedInit) {
				initSerialized.reserve(initSizeInInts);
				initSerialized.push_back(mtpc_invokeWithLayer);
				initSerialized.push_back(internal::CurrentLayer);
				initWrapper.write<mtpBuffer>(initSerialized);
			}
			// prepare container + each in invoke after
			toSendRequest = SecureRequest::Prepare(
				containerSize,
				containerSize + 3 * toSend.size());
			toSendRequest->push_back(mtpc_msg_container);
			toSendRequest->push_back(toSendCount);

			// check for a valid container
			auto bigMsgId = base::unixtime::mtproto_msg_id();

			// the fact of this lock is used in replaceMsgId()
			QWriteLocker locker2(_sessionData->haveSentMutex());
			auto &haveSent = _sessionData->haveSentMap();

			// the fact of this lock is used in replaceMsgId()
			QWriteLocker locker3(_sessionData->wereAckedMutex());
			auto &wereAcked = _sessionData->wereAckedMap();

			// prepare "request-like" wrap for msgId vector
			auto haveSentIdsWrap = SecureRequest::Prepare(idsWrapSize);
			haveSentIdsWrap->msDate = 0; // Container: msDate = 0, seqNo = 0.
			haveSentIdsWrap->requestId = 0;
			haveSentIdsWrap->resize(haveSentIdsWrap->size() + idsWrapSize);
			auto haveSentArr = (mtpMsgId*)(haveSentIdsWrap->data() + 8);

			if (pingRequest) {
				_pingMsgId = placeToContainer(
					toSendRequest,
					bigMsgId,
					forceNewMsgId,
					haveSentArr,
					pingRequest);
				needAnyResponse = true;
			} else if (resendRequest || stateRequest || bindDcKeyRequest) {
				needAnyResponse = true;
			}
			for (auto i = toSend.begin(), e = toSend.end(); i != e; ++i) {
				auto &req = i.value();
				const auto msgId = prepareToSend(
					req,
					bigMsgId,
					forceNewMsgId);
				if (msgId >= bigMsgId) {
					bigMsgId = base::unixtime::mtproto_msg_id();
				}
				*(haveSentArr++) = msgId;
				bool added = false;
				if (req->requestId) {
					if (req.needAck()) {
						req->msDate = req.isStateRequest() ? 0 : crl::now();
						int32 reqNeedsLayer = (needsLayer && req->needsLayer) ? toSendRequest->size() : 0;
						if (req->after) {
							wrapInvokeAfter(toSendRequest, req, haveSent, reqNeedsLayer ? initSizeInInts : 0);
							if (reqNeedsLayer) {
								memcpy(toSendRequest->data() + reqNeedsLayer + 4, initSerialized.constData(), initSize);
								*(toSendRequest->data() + reqNeedsLayer + 3) += initSize;
							}
							added = true;
						} else if (reqNeedsLayer) {
							toSendRequest->resize(reqNeedsLayer + initSizeInInts + req.messageSize());
							memcpy(toSendRequest->data() + reqNeedsLayer, req->constData() + 4, 4 * sizeof(mtpPrime));
							memcpy(toSendRequest->data() + reqNeedsLayer + 4, initSerialized.constData(), initSize);
							memcpy(toSendRequest->data() + reqNeedsLayer + 4 + initSizeInInts, req->constData() + 8, tl::count_length(req));
							*(toSendRequest->data() + reqNeedsLayer + 3) += initSize;
							added = true;
						}
						Assert(!haveSent.contains(msgId));
						haveSent.insert(msgId, req);

						needAnyResponse = true;
					} else {
						wereAcked.insert(msgId, req->requestId);
					}
				}
				if (!added) {
					uint32 from = toSendRequest->size(), len = req.messageSize();
					toSendRequest->resize(from + len);
					memcpy(toSendRequest->data() + from, req->constData() + 4, len * sizeof(mtpPrime));
				}
			}
			if (stateRequest) {
				mtpMsgId msgId = placeToContainer(toSendRequest, bigMsgId, forceNewMsgId, haveSentArr, stateRequest);
				stateRequest->msDate = 0; // 0 for state request, do not request state of it
				Assert(!haveSent.contains(msgId));
				haveSent.insert(msgId, stateRequest);
			}
			if (resendRequest) placeToContainer(toSendRequest, bigMsgId, forceNewMsgId, haveSentArr, resendRequest);
			if (ackRequest) placeToContainer(toSendRequest, bigMsgId, forceNewMsgId, haveSentArr, ackRequest);
			if (httpWaitRequest) placeToContainer(toSendRequest, bigMsgId, forceNewMsgId, haveSentArr, httpWaitRequest);
			if (bindDcKeyRequest) placeToContainer(toSendRequest, bigMsgId, forceNewMsgId, haveSentArr, bindDcKeyRequest);

			const auto containerMsgId = prepareToSend(
				toSendRequest,
				bigMsgId,
				forceNewMsgId);
			*(mtpMsgId*)(haveSentIdsWrap->data() + 4) = containerMsgId;
			(*haveSentIdsWrap)[6] = 0; // for container, msDate = 0, seqNo = 0
			Assert(!haveSent.contains(containerMsgId));
			haveSent.insert(containerMsgId, haveSentIdsWrap);
			toSend.clear();
		}
	}
	sendSecureRequest(std::move(toSendRequest), needAnyResponse);
}

void ConnectionPrivate::retryByTimer() {
	if (_retryTimeout < 3) {
		++_retryTimeout;
	} else if (_retryTimeout == 3) {
		_retryTimeout = 1000;
	} else if (_retryTimeout < 64000) {
		_retryTimeout *= 2;
	}
	connectToServer();
}

void ConnectionPrivate::restartNow() {
	_retryTimeout = 1;
	_retryTimer.cancel();
	restart();
}

void ConnectionPrivate::connectToServer(bool afterConfig) {
	if (_finished) {
		DEBUG_LOG(("MTP Error: "
			"connectToServer() called for finished connection!"));
		return;
	}

	_connectionOptions = std::make_unique<ConnectionOptions>(
		_sessionData->connectionOptions());

	tryAcquireKeyCreation();

	const auto bareDc = BareDcId(_shiftedDcId);
	_dcType = _instance->dcOptions()->dcType(_shiftedDcId);

	// Use media_only addresses only if key for this dc is already created.
	if (_dcType == DcType::MediaDownload && _keyCreator) {
		_dcType = DcType::Regular;
	} else if (_dcType == DcType::Cdn && !_instance->isKeysDestroyer()) {
		if (!_instance->dcOptions()->hasCDNKeysForDc(bareDc)) {
			requestCDNConfig();
			return;
		}
	}

	if (afterConfig && (!_testConnections.empty() || _connection)) {
		return;
	}

	destroyAllConnections();
	if (_connectionOptions->proxy.type == ProxyData::Type::Mtproto) {
		// host, port, secret for mtproto proxy are taken from proxy.
		appendTestConnection(DcOptions::Variants::Tcp, {}, 0, {});
	} else {
		using Variants = DcOptions::Variants;
		const auto special = (_dcType == DcType::Temporary);
		const auto variants = _instance->dcOptions()->lookup(
			bareDc,
			_dcType,
			_connectionOptions->proxy.type != ProxyData::Type::None);
		const auto useIPv4 = special ? true : _connectionOptions->useIPv4;
		const auto useIPv6 = special ? false : _connectionOptions->useIPv6;
		const auto useTcp = special ? true : _connectionOptions->useTcp;
		const auto useHttp = special ? false : _connectionOptions->useHttp;
		const auto skipAddress = !useIPv4
			? Variants::IPv4
			: !useIPv6
			? Variants::IPv6
			: Variants::AddressTypeCount;
		const auto skipProtocol = !useTcp
			? Variants::Tcp
			: !useHttp
			? Variants::Http
			: Variants::ProtocolCount;
		for (auto address = 0; address != Variants::AddressTypeCount; ++address) {
			if (address == skipAddress) {
				continue;
			}
			for (auto protocol = 0; protocol != Variants::ProtocolCount; ++protocol) {
				if (protocol == skipProtocol) {
					continue;
				}
				for (const auto &endpoint : variants.data[address][protocol]) {
					appendTestConnection(
						static_cast<Variants::Protocol>(protocol),
						QString::fromStdString(endpoint.ip),
						endpoint.port,
						endpoint.secret);
				}
			}
		}
	}
	if (_testConnections.empty()) {
		if (_instance->isKeysDestroyer()) {
			LOG(("MTP Error: DC %1 options for not found for auth key destruction!").arg(_shiftedDcId));
			_instance->keyWasPossiblyDestroyed(_shiftedDcId);
			return;
		} else if (afterConfig) {
			LOG(("MTP Error: DC %1 options for not found right after config load!").arg(_shiftedDcId));
			return restart();
		}
		DEBUG_LOG(("MTP Info: DC %1 options not found, waiting for config").arg(_shiftedDcId));
		connect(_instance, SIGNAL(configLoaded()), this, SLOT(onConfigLoaded()), Qt::UniqueConnection);
		InvokeQueued(_instance, [instance = _instance] {
			instance->requestConfig();
		});
		return;
	}
	DEBUG_LOG(("Connection Info: Connecting to %1 with %2 test connections."
		).arg(_shiftedDcId
		).arg(_testConnections.size()));

	if (!_startedConnectingAt) {
		_startedConnectingAt = crl::now();
	} else if (crl::now() - _startedConnectingAt > kRequestConfigTimeout) {
		InvokeQueued(_instance, [instance = _instance] {
			instance->requestConfigIfOld();
		});
	}

	_retryTimer.cancel();
	_waitForConnectedTimer.cancel();

	setState(ConnectingState);

	_pingId = _pingMsgId = _pingIdToSend = _pingSendAt = 0;
	_pingSender.cancel();

	_waitForConnectedTimer.callOnce(_waitForConnected);
}

void ConnectionPrivate::restart() {
	DEBUG_LOG(("MTP Info: restarting Connection"));

	_waitForReceivedTimer.cancel();
	_waitForConnectedTimer.cancel();

	doDisconnect();

	if (_needSessionReset) {
		resetSession();
	}
	if (_retryTimer.isActive()) {
		return;
	}

	DEBUG_LOG(("MTP Info: restart timeout: %1ms").arg(_retryTimeout));

	setState(-_retryTimeout);
}

void ConnectionPrivate::onSentSome(uint64 size) {
	if (!_waitForReceivedTimer.isActive()) {
		auto remain = static_cast<uint64>(_waitForReceived);
		if (!_oldConnection) {
			// 8kb / sec, so 512 kb give 64 sec
			auto remainBySize = size * _waitForReceived / 8192;
			remain = snap(remainBySize, remain, uint64(kMaxReceiveTimeout));
			if (remain != _waitForReceived) {
				DEBUG_LOG(("Checking connect for request with size %1 bytes, delay will be %2").arg(size).arg(remain));
			}
		}
		if (isUploadDcId(_shiftedDcId)) {
			remain *= kUploadSessionsCount;
		} else if (isDownloadDcId(_shiftedDcId)) {
			remain *= kDownloadSessionsCount;
		}
		_waitForReceivedTimer.callOnce(remain);
	}
	if (!_firstSentAt) _firstSentAt = crl::now();
}

void ConnectionPrivate::onReceivedSome() {
	if (_oldConnection) {
		_oldConnection = false;
		DEBUG_LOG(("This connection marked as not old!"));
	}
	_oldConnectionTimer.callOnce(kMarkConnectionOldTimeout);
	_waitForReceivedTimer.cancel();
	if (_firstSentAt > 0) {
		const auto ms = crl::now() - _firstSentAt;
		DEBUG_LOG(("MTP Info: response in %1ms, _waitForReceived: %2ms").arg(ms).arg(_waitForReceived));

		if (ms > 0 && ms * 2 < _waitForReceived) {
			_waitForReceived = qMax(ms * 2, kMinReceiveTimeout);
		}
		_firstSentAt = -1;
	}
}

void ConnectionPrivate::markConnectionOld() {
	_oldConnection = true;
	_waitForReceived = kMinReceiveTimeout;
	DEBUG_LOG(("This connection marked as old! _waitForReceived now %1ms").arg(_waitForReceived));
}

void ConnectionPrivate::sendPingByTimer() {
	if (_pingId) {
		// _pingSendAt: when to send next ping (lastPingAt + kPingSendAfter)
		// could be equal to zero.
		const auto now = crl::now();
		const auto mustSendTill = _pingSendAt
			+ kPingSendAfterForce
			- kPingSendAfter;
		if (mustSendTill < now + 1000) {
			LOG(("Could not send ping for some seconds, restarting..."));
			return restart();
		} else {
			_pingSender.callOnce(mustSendTill - now);
		}
	} else {
		_sessionData->queueNeedToResumeAndSend();
	}
}

void ConnectionPrivate::onPingSendForce() {
	if (!_pingId) {
		_pingSendAt = 0;
		DEBUG_LOG(("Will send ping!"));
		tryToSend();
	}
}

void ConnectionPrivate::waitReceivedFailed() {
	Expects(_connectionOptions != nullptr);

	if (!_connectionOptions->useTcp) {
		return;
	}

	DEBUG_LOG(("MTP Info: bad connection, _waitForReceived: %1ms").arg(_waitForReceived));
	if (_waitForReceived < kMaxReceiveTimeout) {
		_waitForReceived *= 2;
	}
	doDisconnect();
	if (_retryTimer.isActive()) {
		return;
	}

	DEBUG_LOG(("MTP Info: immediate restart!"));
	InvokeQueued(this, [=] { connectToServer(); });
}

void ConnectionPrivate::waitConnectedFailed() {
	DEBUG_LOG(("MTP Info: can't connect in %1ms").arg(_waitForConnected));
	auto maxTimeout = kMaxConnectedTimeout;
	for (const auto &connection : _testConnections) {
		accumulate_max(maxTimeout, connection.data->fullConnectTimeout());
	}
	if (_waitForConnected < maxTimeout) {
		_waitForConnected = std::min(maxTimeout, 2 * _waitForConnected);
	}

	connectingTimedOut();

	DEBUG_LOG(("MTP Info: immediate restart!"));
	InvokeQueued(this, [=] { connectToServer(); });
}

void ConnectionPrivate::waitBetterFailed() {
	confirmBestConnection();
}

void ConnectionPrivate::connectingTimedOut() {
	for (const auto &connection : _testConnections) {
		connection.data->timedOut();
	}
	doDisconnect();
}

void ConnectionPrivate::doDisconnect() {
	destroyAllConnections();
	setState(DisconnectedState);
}

void ConnectionPrivate::finishAndDestroy() {
	doDisconnect();
	_finished = true;
	const auto connection = _owner;
	const auto instance = _instance;
	InvokeQueued(instance, [=] {
		instance->connectionFinished(connection);
	});
	deleteLater();
}

void ConnectionPrivate::requestCDNConfig() {
	connect(
		_instance,
		SIGNAL(cdnConfigLoaded()),
		this,
		SLOT(onCDNConfigLoaded()),
		Qt::UniqueConnection);
	InvokeQueued(_instance, [instance = _instance] {
		instance->requestCDNConfig();
	});
}

void ConnectionPrivate::handleReceived() {
	Expects(_encryptionKey != nullptr);

	onReceivedSome();

	while (!_connection->received().empty()) {
		auto intsBuffer = std::move(_connection->received().front());
		_connection->received().pop_front();

		constexpr auto kExternalHeaderIntsCount = 6U; // 2 auth_key_id, 4 msg_key
		constexpr auto kEncryptedHeaderIntsCount = 8U; // 2 salt, 2 session, 2 msg_id, 1 seq_no, 1 length
		constexpr auto kMinimalEncryptedIntsCount = kEncryptedHeaderIntsCount + 4U; // + 1 data + 3 padding
		constexpr auto kMinimalIntsCount = kExternalHeaderIntsCount + kMinimalEncryptedIntsCount;
		auto intsCount = uint32(intsBuffer.size());
		auto ints = intsBuffer.constData();
		if ((intsCount < kMinimalIntsCount) || (intsCount > kMaxMessageLength / kIntSize)) {
			LOG(("TCP Error: bad message received, len %1").arg(intsCount * kIntSize));
			TCP_LOG(("TCP Error: bad message %1").arg(Logs::mb(ints, intsCount * kIntSize).str()));

			return restart();
		}
		if (_keyId != *(uint64*)ints) {
			LOG(("TCP Error: bad auth_key_id %1 instead of %2 received").arg(_keyId).arg(*(uint64*)ints));
			TCP_LOG(("TCP Error: bad message %1").arg(Logs::mb(ints, intsCount * kIntSize).str()));

			return restart();
		}

		auto encryptedInts = ints + kExternalHeaderIntsCount;
		auto encryptedIntsCount = (intsCount - kExternalHeaderIntsCount) & ~0x03U;
		auto encryptedBytesCount = encryptedIntsCount * kIntSize;
		auto decryptedBuffer = QByteArray(encryptedBytesCount, Qt::Uninitialized);
		auto msgKey = *(MTPint128*)(ints + 2);

#ifdef TDESKTOP_MTPROTO_OLD
		aesIgeDecrypt_oldmtp(encryptedInts, decryptedBuffer.data(), encryptedBytesCount, _encryptionKey, msgKey);
#else // TDESKTOP_MTPROTO_OLD
		aesIgeDecrypt(encryptedInts, decryptedBuffer.data(), encryptedBytesCount, _encryptionKey, msgKey);
#endif // TDESKTOP_MTPROTO_OLD

		auto decryptedInts = reinterpret_cast<const mtpPrime*>(decryptedBuffer.constData());
		auto serverSalt = *(uint64*)&decryptedInts[0];
		auto session = *(uint64*)&decryptedInts[2];
		auto msgId = *(uint64*)&decryptedInts[4];
		auto seqNo = *(uint32*)&decryptedInts[6];
		auto needAck = ((seqNo & 0x01) != 0);

		auto messageLength = *(uint32*)&decryptedInts[7];
		if (messageLength > kMaxMessageLength) {
			LOG(("TCP Error: bad messageLength %1").arg(messageLength));
			TCP_LOG(("TCP Error: bad message %1").arg(Logs::mb(ints, intsCount * kIntSize).str()));

			return restart();

		}
		auto fullDataLength = kEncryptedHeaderIntsCount * kIntSize + messageLength; // Without padding.

		// Can underflow, but it is an unsigned type, so we just check the range later.
		auto paddingSize = static_cast<uint32>(encryptedBytesCount) - static_cast<uint32>(fullDataLength);

#ifdef TDESKTOP_MTPROTO_OLD
		constexpr auto kMinPaddingSize_oldmtp = 0U;
		constexpr auto kMaxPaddingSize_oldmtp = 15U;
		auto badMessageLength = (/*paddingSize < kMinPaddingSize_oldmtp || */paddingSize > kMaxPaddingSize_oldmtp);

		auto hashedDataLength = badMessageLength ? encryptedBytesCount : fullDataLength;
		auto sha1ForMsgKeyCheck = hashSha1(decryptedInts, hashedDataLength);

		constexpr auto kMsgKeyShift_oldmtp = 4U;
		if (memcmp(&msgKey, sha1ForMsgKeyCheck.data() + kMsgKeyShift_oldmtp, sizeof(msgKey)) != 0) {
			LOG(("TCP Error: bad SHA1 hash after aesDecrypt in message."));
			TCP_LOG(("TCP Error: bad message %1").arg(Logs::mb(encryptedInts, encryptedBytesCount).str()));

			return restart();
		}
#else // TDESKTOP_MTPROTO_OLD
		constexpr auto kMinPaddingSize = 12U;
		constexpr auto kMaxPaddingSize = 1024U;
		auto badMessageLength = (paddingSize < kMinPaddingSize || paddingSize > kMaxPaddingSize);

		std::array<uchar, 32> sha256Buffer = { { 0 } };

		SHA256_CTX msgKeyLargeContext;
		SHA256_Init(&msgKeyLargeContext);
		SHA256_Update(&msgKeyLargeContext, _encryptionKey->partForMsgKey(false), 32);
		SHA256_Update(&msgKeyLargeContext, decryptedInts, encryptedBytesCount);
		SHA256_Final(sha256Buffer.data(), &msgKeyLargeContext);

		constexpr auto kMsgKeyShift = 8U;
		if (memcmp(&msgKey, sha256Buffer.data() + kMsgKeyShift, sizeof(msgKey)) != 0) {
			LOG(("TCP Error: bad SHA256 hash after aesDecrypt in message"));
			TCP_LOG(("TCP Error: bad message %1").arg(Logs::mb(encryptedInts, encryptedBytesCount).str()));

			return restart();
		}
#endif // TDESKTOP_MTPROTO_OLD

		if (badMessageLength || (messageLength & 0x03)) {
			LOG(("TCP Error: bad msg_len received %1, data size: %2").arg(messageLength).arg(encryptedBytesCount));
			TCP_LOG(("TCP Error: bad message %1").arg(Logs::mb(encryptedInts, encryptedBytesCount).str()));

			return restart();
		}

		TCP_LOG(("TCP Info: decrypted message %1,%2,%3 is %4 len").arg(msgId).arg(seqNo).arg(Logs::b(needAck)).arg(fullDataLength));

		if (session != _sessionId) {
			LOG(("MTP Error: bad server session received"));
			TCP_LOG(("MTP Error: bad server session %1 instead of %2 in message received").arg(session).arg(_sessionId));

			return restart();
		}

		const auto serverTime = int32(msgId >> 32);
		const auto isReply = ((msgId & 0x03) == 1);
		if (!isReply && ((msgId & 0x03) != 3)) {
			LOG(("MTP Error: bad msg_id %1 in message received").arg(msgId));

			return restart();
		}

		const auto clientTime = base::unixtime::now();
		const auto badTime = (serverTime > clientTime + 60)
			|| (serverTime + 300 < clientTime);
		if (badTime) {
			DEBUG_LOG(("MTP Info: bad server time from msg_id: %1, my time: %2").arg(serverTime).arg(clientTime));
		}

		bool wasConnected = (getState() == ConnectedState);
		if (serverSalt != _sessionSalt) {
			if (!badTime) {
				DEBUG_LOG(("MTP Info: other salt received... received: %1, my salt: %2, updating...").arg(serverSalt).arg(_sessionSalt));
				_sessionSalt = serverSalt;

				if (setState(ConnectedState, ConnectingState)) {
					_sessionData->resendAll();
				}
			} else {
				DEBUG_LOG(("MTP Info: other salt received... received: %1, my salt: %2").arg(serverSalt).arg(_sessionSalt));
			}
		} else {
			serverSalt = 0; // dont pass to handle method, so not to lock in setSalt()
		}

		if (needAck) _ackRequestData.push_back(MTP_long(msgId));

		auto res = HandleResult::Success; // if no need to handle, then succeed
		auto from = decryptedInts + kEncryptedHeaderIntsCount;
		auto end = from + (messageLength / kIntSize);
		auto sfrom = decryptedInts + 4U; // msg_id + seq_no + length + message
		MTP_LOG(_shiftedDcId, ("Recv: ") + details::DumpToText(sfrom, end) + QString(" (keyId:%1)").arg(_encryptionKey->keyId()));

		if (_receivedMessageIds.registerMsgId(msgId, needAck)) {
			res = handleOneReceived(from, end, msgId, serverTime, serverSalt, badTime);
		}
		_receivedMessageIds.shrink();

		// send acks
		if (const auto toAckSize = _ackRequestData.size()) {
			DEBUG_LOG(("MTP Info: will send %1 acks, ids: %2").arg(toAckSize).arg(LogIdsVector(_ackRequestData)));
			_sessionData->queueSendAnything(kAckSendWaiting);
		}

		auto lock = QReadLocker(_sessionData->haveReceivedMutex());
		const auto tryToReceive = !_sessionData->haveReceivedResponses().isEmpty() || !_sessionData->haveReceivedUpdates().isEmpty();
		lock.unlock();

		if (tryToReceive) {
			DEBUG_LOG(("MTP Info: queueTryToReceive() - need to parse in another thread, %1 responses, %2 updates.").arg(_sessionData->haveReceivedResponses().size()).arg(_sessionData->haveReceivedUpdates().size()));
			_sessionData->queueTryToReceive();
		}

		if (res != HandleResult::Success && res != HandleResult::Ignored) {
			if (res == HandleResult::DestroyTemporaryKey) {
				destroyTemporaryKey();
			} else if (res == HandleResult::ResetSession) {
				_needSessionReset = true;
			}
			return restart();
		}
		_retryTimeout = 1; // reset restart() timer

		_startedConnectingAt = crl::time(0);

		if (!wasConnected) {
			if (getState() == ConnectedState) {
				_sessionData->queueNeedToResumeAndSend();
			}
		}
	}
	if (_connection->needHttpWait()) {
		_sessionData->queueSendAnything();
	}
}

ConnectionPrivate::HandleResult ConnectionPrivate::handleOneReceived(const mtpPrime *from, const mtpPrime *end, uint64 msgId, int32 serverTime, uint64 serverSalt, bool badTime) {
	const auto cons = mtpTypeId(*from);

	switch (cons) {

	case mtpc_gzip_packed: {
		DEBUG_LOG(("Message Info: gzip container"));
		mtpBuffer response = ungzip(++from, end);
		if (response.empty()) {
			return HandleResult::RestartConnection;
		}
		return handleOneReceived(response.data(), response.data() + response.size(), msgId, serverTime, serverSalt, badTime);
	}

	case mtpc_msg_container: {
		if (++from >= end) {
			return HandleResult::ParseError;
		}

		const mtpPrime *otherEnd;
		const auto msgsCount = (uint32)*(from++);
		DEBUG_LOG(("Message Info: container received, count: %1").arg(msgsCount));
		for (uint32 i = 0; i < msgsCount; ++i) {
			if (from + 4 >= end) {
				return HandleResult::ParseError;
			}
			otherEnd = from + 4;

			MTPlong inMsgId;
			if (!inMsgId.read(from, otherEnd)) {
				return HandleResult::ParseError;
			}
			bool isReply = ((inMsgId.v & 0x03) == 1);
			if (!isReply && ((inMsgId.v & 0x03) != 3)) {
				LOG(("Message Error: bad msg_id %1 in contained message received").arg(inMsgId.v));
				return HandleResult::RestartConnection;
			}

			MTPint inSeqNo;
			if (!inSeqNo.read(from, otherEnd)) {
				return HandleResult::ParseError;
			}
			MTPint bytes;
			if (!bytes.read(from, otherEnd)) {
				return HandleResult::ParseError;
			}
			if ((bytes.v & 0x03) || bytes.v < 4) {
				LOG(("Message Error: bad length %1 of contained message received").arg(bytes.v));
				return HandleResult::RestartConnection;
			}

			bool needAck = (inSeqNo.v & 0x01);
			if (needAck) _ackRequestData.push_back(inMsgId);

			DEBUG_LOG(("Message Info: message from container, msg_id: %1, needAck: %2").arg(inMsgId.v).arg(Logs::b(needAck)));

			otherEnd = from + (bytes.v >> 2);
			if (otherEnd > end) {
				return HandleResult::ParseError;
			}

			auto res = HandleResult::Success; // if no need to handle, then succeed
			if (_receivedMessageIds.registerMsgId(inMsgId.v, needAck)) {
				res = handleOneReceived(from, otherEnd, inMsgId.v, serverTime, serverSalt, badTime);
				badTime = false;
			}
			if (res != HandleResult::Success) {
				return res;
			}

			from = otherEnd;
		}
	} return HandleResult::Success;

	case mtpc_msgs_ack: {
		MTPMsgsAck msg;
		if (!msg.read(from, end)) {
			return HandleResult::ParseError;
		}
		auto &ids = msg.c_msgs_ack().vmsg_ids().v;
		uint32 idsCount = ids.size();

		DEBUG_LOG(("Message Info: acks received, ids: %1").arg(LogIdsVector(ids)));
		if (!idsCount) return (badTime ? HandleResult::Ignored : HandleResult::Success);

		if (badTime) {
			if (requestsFixTimeSalt(ids, serverTime, serverSalt)) {
				badTime = false;
			} else {
				return HandleResult::Ignored;
			}
		}
		requestsAcked(ids);
	} return HandleResult::Success;

	case mtpc_bad_msg_notification: {
		MTPBadMsgNotification msg;
		if (!msg.read(from, end)) {
			return HandleResult::ParseError;
		}
		const auto &data(msg.c_bad_msg_notification());
		LOG(("Message Info: bad message notification received (error_code %3) for msg_id = %1, seq_no = %2").arg(data.vbad_msg_id().v).arg(data.vbad_msg_seqno().v).arg(data.verror_code().v));

		mtpMsgId resendId = data.vbad_msg_id().v;
		if (resendId == _pingMsgId) {
			_pingId = 0;
		}
		int32 errorCode = data.verror_code().v;
		if (false
			|| errorCode == 16
			|| errorCode == 17
			|| errorCode == 32
			|| errorCode == 33
			|| errorCode == 64) { // can handle
			const auto needResend = false
				|| (errorCode == 16) // bad msg_id
				|| (errorCode == 17) // bad msg_id
				|| (errorCode == 64); // bad container
			if (errorCode == 64) { // bad container!
				if (Logs::DebugEnabled()) {
					SecureRequest request;
					{
						QWriteLocker locker(_sessionData->haveSentMutex());
						auto &haveSent = _sessionData->haveSentMap();

						const auto i = haveSent.constFind(resendId);
						if (i == haveSent.cend()) {
							LOG(("Message Error: Container not found!"));
						} else {
							request = i.value();
						}
					}
					if (request) {
						if (request.isSentContainer()) {
							QStringList lst;
							const auto ids = (const mtpMsgId*)(request->constData() + 8);
							for (uint32 i = 0, l = (request->size() - 8) >> 1; i < l; ++i) {
								lst.push_back(QString::number(ids[i]));
							}
							LOG(("Message Info: bad container received! messages: %1").arg(lst.join(',')));
						} else {
							LOG(("Message Error: bad container received, but request is not a container!"));
						}
					}
				}
			}

			if (!wasSent(resendId)) {
				DEBUG_LOG(("Message Error: "
					"such message was not sent recently %1").arg(resendId));
				return badTime
					? HandleResult::Ignored
					: HandleResult::Success;
			}

			if (needResend) { // bad msg_id or bad container
				if (serverSalt) {
					_sessionSalt = serverSalt;
				}
				base::unixtime::update(serverTime, true);

				DEBUG_LOG(("Message Info: unixtime updated, now %1, resending in container...").arg(serverTime));

				resend(resendId, 0, true);
			} else { // must create new session, because msg_id and msg_seqno are inconsistent
				if (badTime) {
					if (serverSalt) {
						_sessionSalt = serverSalt;
					}
					base::unixtime::update(serverTime, true);
					badTime = false;
				}
				LOG(("Message Info: bad message notification received, msgId %1, error_code %2").arg(data.vbad_msg_id().v).arg(errorCode));
				return HandleResult::ResetSession;
			}
		} else { // fatal (except 48, but it must not get here)
			const auto badMsgId = mtpMsgId(data.vbad_msg_id().v);
			const auto requestId = wasSent(resendId);
			if (requestId) {
				LOG(("Message Error: "
					"bad message notification received, "
					"msgId %1, error_code %2, fatal: clearing callbacks"
					).arg(badMsgId
					).arg(errorCode
					));
				_instance->clearCallbacksDelayed({ 1, RPCCallbackClear(
					requestId,
					-errorCode) });
			} else {
				DEBUG_LOG(("Message Error: "
					"such message was not sent recently %1").arg(badMsgId));
			}
			return badTime
				? HandleResult::Ignored
				: HandleResult::Success;
		}
	} return HandleResult::Success;

	case mtpc_bad_server_salt: {
		MTPBadMsgNotification msg;
		if (!msg.read(from, end)) {
			return HandleResult::ParseError;
		}
		const auto &data(msg.c_bad_server_salt());
		DEBUG_LOG(("Message Info: bad server salt received (error_code %4) for msg_id = %1, seq_no = %2, new salt: %3").arg(data.vbad_msg_id().v).arg(data.vbad_msg_seqno().v).arg(data.vnew_server_salt().v).arg(data.verror_code().v));

		mtpMsgId resendId = data.vbad_msg_id().v;
		if (resendId == _pingMsgId) {
			_pingId = 0;
		} else if (!wasSent(resendId)) {
			DEBUG_LOG(("Message Error: such message was not sent recently %1").arg(resendId));
			return (badTime ? HandleResult::Ignored : HandleResult::Success);
		}

		_sessionSalt = data.vnew_server_salt().v;
		base::unixtime::update(serverTime);

		if (setState(ConnectedState, ConnectingState)) {
			_sessionData->resendAll();
		}

		badTime = false;

		DEBUG_LOG(("Message Info: unixtime updated, now %1, server_salt updated, now %2, resending...").arg(serverTime).arg(serverSalt));
		resend(resendId);
	} return HandleResult::Success;

	case mtpc_msgs_state_req: {
		if (badTime) {
			DEBUG_LOG(("Message Info: skipping with bad time..."));
			return HandleResult::Ignored;
		}
		MTPMsgsStateReq msg;
		if (!msg.read(from, end)) {
			return HandleResult::ParseError;
		}
		auto &ids = msg.c_msgs_state_req().vmsg_ids().v;
		auto idsCount = ids.size();
		DEBUG_LOG(("Message Info: msgs_state_req received, ids: %1").arg(LogIdsVector(ids)));
		if (!idsCount) return HandleResult::Success;

		QByteArray info(idsCount, Qt::Uninitialized);
		{
			const auto minRecv = _receivedMessageIds.min();
			const auto maxRecv = _receivedMessageIds.max();

			QReadLocker locker(_sessionData->wereAckedMutex());
			const auto &wereAcked = _sessionData->wereAckedMap();
			const auto wereAckedEnd = wereAcked.cend();

			for (uint32 i = 0, l = idsCount; i < l; ++i) {
				char state = 0;
				uint64 reqMsgId = ids[i].v;
				if (reqMsgId < minRecv) {
					state |= 0x01;
				} else if (reqMsgId > maxRecv) {
					state |= 0x03;
				} else {
					auto msgIdState = _receivedMessageIds.lookup(reqMsgId);
					if (msgIdState == ReceivedIdsManager::State::NotFound) {
						state |= 0x02;
					} else {
						state |= 0x04;
						if (wereAcked.constFind(reqMsgId) != wereAckedEnd) {
							state |= 0x80; // we know, that server knows, that we received request
						}
						if (msgIdState == ReceivedIdsManager::State::NeedsAck) { // need ack, so we sent ack
							state |= 0x08;
						} else {
							state |= 0x10;
						}
					}
				}
				info[i] = state;
			}
		}
		_sessionData->queueSendMsgsStateInfo(msgId, info);
	} return HandleResult::Success;

	case mtpc_msgs_state_info: {
		MTPMsgsStateInfo msg;
		if (!msg.read(from, end)) {
			return HandleResult::ParseError;
		}
		auto &data = msg.c_msgs_state_info();

		auto reqMsgId = data.vreq_msg_id().v;
		auto &states = data.vinfo().v;

		DEBUG_LOG(("Message Info: msg state received, msgId %1, reqMsgId: %2, HEX states %3").arg(msgId).arg(reqMsgId).arg(Logs::mb(states.data(), states.length()).str()));
		SecureRequest requestBuffer;
		{ // find this request in session-shared sent requests map
			QReadLocker locker(_sessionData->haveSentMutex());
			const auto &haveSent = _sessionData->haveSentMap();
			const auto replyTo = haveSent.constFind(reqMsgId);
			if (replyTo == haveSent.cend()) { // do not look in toResend, because we do not resend msgs_state_req requests
				DEBUG_LOG(("Message Error: such message was not sent recently %1").arg(reqMsgId));
				return (badTime ? HandleResult::Ignored : HandleResult::Success);
			}
			if (badTime) {
				if (serverSalt) {
					_sessionSalt = serverSalt; // requestsFixTimeSalt with no lookup
				}
				base::unixtime::update(serverTime, true);

				DEBUG_LOG(("Message Info: unixtime updated from mtpc_msgs_state_info, now %1").arg(serverTime));

				badTime = false;
			}
			requestBuffer = replyTo.value();
		}
		QVector<MTPlong> toAckReq(1, MTP_long(reqMsgId)), toAck;
		requestsAcked(toAck, true);

		if (requestBuffer->size() < 9) {
			LOG(("Message Error: bad request %1 found in requestMap, size: %2").arg(reqMsgId).arg(requestBuffer->size()));
			return HandleResult::RestartConnection;
		}
		const mtpPrime *rFrom = requestBuffer->constData() + 8, *rEnd = requestBuffer->constData() + requestBuffer->size();
		if (mtpTypeId(*rFrom) == mtpc_msgs_state_req) {
			MTPMsgsStateReq request;
			if (!request.read(rFrom, rEnd)) {
				LOG(("Message Error: could not parse sent msgs_state_req"));
				return HandleResult::ParseError;
			}
			handleMsgsStates(request.c_msgs_state_req().vmsg_ids().v, states, toAck);
		} else {
			MTPMsgResendReq request;
			if (!request.read(rFrom, rEnd)) {
				LOG(("Message Error: could not parse sent msgs_state_req"));
				return HandleResult::ParseError;
			}
			handleMsgsStates(request.c_msg_resend_req().vmsg_ids().v, states, toAck);
		}

		requestsAcked(toAck);
	} return HandleResult::Success;

	case mtpc_msgs_all_info: {
		if (badTime) {
			DEBUG_LOG(("Message Info: skipping with bad time..."));
			return HandleResult::Ignored;
		}

		MTPMsgsAllInfo msg;
		if (!msg.read(from, end)) {
			return HandleResult::ParseError;
		}
		auto &data = msg.c_msgs_all_info();
		auto &ids = data.vmsg_ids().v;
		auto &states = data.vinfo().v;

		QVector<MTPlong> toAck;

		DEBUG_LOG(("Message Info: msgs all info received, msgId %1, reqMsgIds: %2, states %3").arg(msgId).arg(LogIdsVector(ids)).arg(Logs::mb(states.data(), states.length()).str()));
		handleMsgsStates(ids, states, toAck);

		requestsAcked(toAck);
	} return HandleResult::Success;

	case mtpc_msg_detailed_info: {
		MTPMsgDetailedInfo msg;
		if (!msg.read(from, end)) {
			return HandleResult::ParseError;
		}
		const auto &data(msg.c_msg_detailed_info());

		DEBUG_LOG(("Message Info: msg detailed info, sent msgId %1, answerId %2, status %3, bytes %4").arg(data.vmsg_id().v).arg(data.vanswer_msg_id().v).arg(data.vstatus().v).arg(data.vbytes().v));

		QVector<MTPlong> ids(1, data.vmsg_id());
		if (badTime) {
			if (requestsFixTimeSalt(ids, serverTime, serverSalt)) {
				badTime = false;
			} else {
				DEBUG_LOG(("Message Info: error, such message was not sent recently %1").arg(data.vmsg_id().v));
				return HandleResult::Ignored;
			}
		}
		requestsAcked(ids);

		const auto resMsgId = data.vanswer_msg_id();
		if (_receivedMessageIds.lookup(resMsgId.v) != ReceivedIdsManager::State::NotFound) {
			_ackRequestData.push_back(resMsgId);
		} else {
			DEBUG_LOG(("Message Info: answer message %1 was not received, requesting...").arg(resMsgId.v));
			_resendRequestData.push_back(resMsgId);
		}
	} return HandleResult::Success;

	case mtpc_msg_new_detailed_info: {
		if (badTime) {
			DEBUG_LOG(("Message Info: skipping msg_new_detailed_info with bad time..."));
			return HandleResult::Ignored;
		}
		MTPMsgDetailedInfo msg;
		if (!msg.read(from, end)) {
			return HandleResult::ParseError;
		}
		const auto &data(msg.c_msg_new_detailed_info());

		DEBUG_LOG(("Message Info: msg new detailed info, answerId %2, status %3, bytes %4").arg(data.vanswer_msg_id().v).arg(data.vstatus().v).arg(data.vbytes().v));

		const auto resMsgId = data.vanswer_msg_id();
		if (_receivedMessageIds.lookup(resMsgId.v) != ReceivedIdsManager::State::NotFound) {
			_ackRequestData.push_back(resMsgId);
		} else {
			DEBUG_LOG(("Message Info: answer message %1 was not received, requesting...").arg(resMsgId.v));
			_resendRequestData.push_back(resMsgId);
		}
	} return HandleResult::Success;

	case mtpc_rpc_result: {
		if (from + 3 > end) {
			return HandleResult::ParseError;
		}
		auto response = SerializedMessage();

		MTPlong reqMsgId;
		if (!reqMsgId.read(++from, end)) {
			return HandleResult::ParseError;
		}
		mtpTypeId typeId = from[0];

		DEBUG_LOG(("RPC Info: response received for %1, queueing...").arg(reqMsgId.v));

		QVector<MTPlong> ids(1, reqMsgId);
		if (badTime) {
			if (requestsFixTimeSalt(ids, serverTime, serverSalt)) {
				badTime = false;
			} else {
				DEBUG_LOG(("Message Info: error, such message was not sent recently %1").arg(reqMsgId.v));
				return HandleResult::Ignored;
			}
		}

		if (typeId == mtpc_gzip_packed) {
			DEBUG_LOG(("RPC Info: gzip container"));
			response = ungzip(++from, end);
			if (response.empty()) {
				return HandleResult::RestartConnection;
			}
			typeId = response[0];
		} else {
			response.resize(end - from);
			memcpy(response.data(), from, (end - from) * sizeof(mtpPrime));
		}
		if (typeId == mtpc_rpc_error) {
			if (IsDestroyedTemporaryKeyError(response)) {
				return HandleResult::DestroyTemporaryKey;
			}
			// An error could be some RPC_CALL_FAIL or other error inside
			// the initConnection, so we're not sure yet that it was inited.
			// Wait till a good response is received.
		} else {
			_sessionData->notifyConnectionInited(*_connectionOptions);
		}
		requestsAcked(ids, true);

		if (_keyCreator) {
			const auto result = _keyCreator->handleBindResponse(
				reqMsgId,
				response);
			switch (result) {
			case DcKeyBindState::Success:
				_sessionData->releaseKeyCreationOnDone(
					_encryptionKey,
					base::take(_keyCreator)->bindPersistentKey());
				_sessionData->queueNeedToResumeAndSend();
				return HandleResult::Success;
			case DcKeyBindState::DefinitelyDestroyed:
				if (destroyOldEnoughPersistentKey()) {
					return HandleResult::DestroyTemporaryKey;
				}
				[[fallthrough]];
			case DcKeyBindState::Failed:
				_sessionData->queueNeedToResumeAndSend();
				return HandleResult::Success;
			}
		}
		auto requestId = wasSent(reqMsgId.v);
		if (requestId && requestId != mtpRequestId(0xFFFFFFFF)) {
			// Save rpc_result for processing in the main thread.
			QWriteLocker locker(_sessionData->haveReceivedMutex());
			_sessionData->haveReceivedResponses().insert(requestId, response);
		} else {
			DEBUG_LOG(("RPC Info: requestId not found for msgId %1").arg(reqMsgId.v));
		}
	} return HandleResult::Success;

	case mtpc_new_session_created: {
		const mtpPrime *start = from;
		MTPNewSession msg;
		if (!msg.read(from, end)) {
			return HandleResult::ParseError;
		}
		const auto &data(msg.c_new_session_created());

		if (badTime) {
			if (requestsFixTimeSalt(QVector<MTPlong>(1, data.vfirst_msg_id()), serverTime, serverSalt)) {
				badTime = false;
			} else {
				DEBUG_LOG(("Message Info: error, such message was not sent recently %1").arg(data.vfirst_msg_id().v));
				return HandleResult::Ignored;
			}
		}

		DEBUG_LOG(("Message Info: new server session created, unique_id %1, first_msg_id %2, server_salt %3").arg(data.vunique_id().v).arg(data.vfirst_msg_id().v).arg(data.vserver_salt().v));
		_sessionSalt = data.vserver_salt().v;

		mtpMsgId firstMsgId = data.vfirst_msg_id().v;
		QVector<quint64> toResend;
		{
			QReadLocker locker(_sessionData->haveSentMutex());
			const auto &haveSent = _sessionData->haveSentMap();
			toResend.reserve(haveSent.size());
			for (auto i = haveSent.cbegin(), e = haveSent.cend(); i != e; ++i) {
				if (i.key() >= firstMsgId) {
					break;
				} else if (i.value()->requestId) {
					toResend.push_back(i.key());
				}
			}
		}
		for (const auto msgId : toResend) {
			_sessionData->resend(msgId, 10, true);
		}

		mtpBuffer update(from - start);
		if (from > start) memcpy(update.data(), start, (from - start) * sizeof(mtpPrime));

		// Notify main process about new session - need to get difference.
		QWriteLocker locker(_sessionData->haveReceivedMutex());
		_sessionData->haveReceivedUpdates().push_back(SerializedMessage(update));
	} return HandleResult::Success;

	case mtpc_pong: {
		MTPPong msg;
		if (!msg.read(from, end)) {
			return HandleResult::ParseError;
		}
		const auto &data(msg.c_pong());
		DEBUG_LOG(("Message Info: pong received, msg_id: %1, ping_id: %2").arg(data.vmsg_id().v).arg(data.vping_id().v));

		if (!wasSent(data.vmsg_id().v)) {
			DEBUG_LOG(("Message Error: such msg_id %1 ping_id %2 was not sent recently").arg(data.vmsg_id().v).arg(data.vping_id().v));
			return HandleResult::Ignored;
		}
		if (data.vping_id().v == _pingId) {
			_pingId = 0;
		} else {
			DEBUG_LOG(("Message Info: just pong..."));
		}

		QVector<MTPlong> ids(1, data.vmsg_id());
		if (badTime) {
			if (requestsFixTimeSalt(ids, serverTime, serverSalt)) {
				badTime = false;
			} else {
				return HandleResult::Ignored;
			}
		}
		requestsAcked(ids, true);
	} return HandleResult::Success;

	}

	if (badTime) {
		DEBUG_LOG(("Message Error: bad time in updates cons, must create new session"));
		return HandleResult::ResetSession;
	}

	if (_dcType == DcType::Regular) {
		mtpBuffer update(end - from);
		if (end > from) memcpy(update.data(), from, (end - from) * sizeof(mtpPrime));

		// Notify main process about the new updates.
		QWriteLocker locker(_sessionData->haveReceivedMutex());
		_sessionData->haveReceivedUpdates().push_back(SerializedMessage(update));

		if (cons != mtpc_updatesTooLong
			&& cons != mtpc_updateShortMessage
			&& cons != mtpc_updateShortChatMessage
			&& cons != mtpc_updateShortSentMessage
			&& cons != mtpc_updateShort
			&& cons != mtpc_updatesCombined
			&& cons != mtpc_updates) {
			// Maybe some new unknown update?
			LOG(("Message Error: unknown constructor 0x%1").arg(cons, 0, 16));
		}
	} else {
		LOG(("Message Error: unexpected updates in dcType: %1").arg(static_cast<int>(_dcType)));
	}

	return HandleResult::Success;
}

mtpBuffer ConnectionPrivate::ungzip(const mtpPrime *from, const mtpPrime *end) const {
	mtpBuffer result; // * 4 because of mtpPrime type
	result.resize(0);

	MTPstring packed;
	if (!packed.read(from, end)) { // read packed string as serialized mtp string type
		LOG(("RPC Error: could not read gziped bytes."));
		return result;
	}
	uint32 packedLen = packed.v.size(), unpackedChunk = packedLen, unpackedLen = 0;

	z_stream stream;
	stream.zalloc = 0;
	stream.zfree = 0;
	stream.opaque = 0;
	stream.avail_in = 0;
	stream.next_in = 0;
	int res = inflateInit2(&stream, 16 + MAX_WBITS);
	if (res != Z_OK) {
		LOG(("RPC Error: could not init zlib stream, code: %1").arg(res));
		return result;
	}
	stream.avail_in = packedLen;
	stream.next_in = reinterpret_cast<Bytef*>(packed.v.data());

	stream.avail_out = 0;
	while (!stream.avail_out) {
		result.resize(result.size() + unpackedChunk);
		stream.avail_out = unpackedChunk * sizeof(mtpPrime);
		stream.next_out = (Bytef*)&result[result.size() - unpackedChunk];
		int res = inflate(&stream, Z_NO_FLUSH);
		if (res != Z_OK && res != Z_STREAM_END) {
			inflateEnd(&stream);
			LOG(("RPC Error: could not unpack gziped data, code: %1").arg(res));
			DEBUG_LOG(("RPC Error: bad gzip: %1").arg(Logs::mb(packed.v.constData(), packedLen).str()));
			return mtpBuffer();
		}
	}
	if (stream.avail_out & 0x03) {
		uint32 badSize = result.size() * sizeof(mtpPrime) - stream.avail_out;
		LOG(("RPC Error: bad length of unpacked data %1").arg(badSize));
		DEBUG_LOG(("RPC Error: bad unpacked data %1").arg(Logs::mb(result.data(), badSize).str()));
		return mtpBuffer();
	}
	result.resize(result.size() - (stream.avail_out >> 2));
	inflateEnd(&stream);
	if (!result.size()) {
		LOG(("RPC Error: bad length of unpacked data 0"));
	}
	return result;
}

bool ConnectionPrivate::requestsFixTimeSalt(const QVector<MTPlong> &ids, int32 serverTime, uint64 serverSalt) {
	uint32 idsCount = ids.size();

	for (uint32 i = 0; i < idsCount; ++i) {
		if (wasSent(ids[i].v)) {// found such msg_id in recent acked requests or in recent sent requests
			if (serverSalt) {
				_sessionSalt = serverSalt;
			}
			base::unixtime::update(serverTime, true);
			return true;
		}
	}
	return false;
}

void ConnectionPrivate::requestsAcked(const QVector<MTPlong> &ids, bool byResponse) {
	uint32 idsCount = ids.size();

	DEBUG_LOG(("Message Info: requests acked, ids %1").arg(LogIdsVector(ids)));

	auto clearedBecauseTooOld = std::vector<RPCCallbackClear>();
	QVector<MTPlong> toAckMore;
	{
		QWriteLocker locker1(_sessionData->wereAckedMutex());
		auto &wereAcked = _sessionData->wereAckedMap();

		{
			QWriteLocker locker2(_sessionData->haveSentMutex());
			auto &haveSent = _sessionData->haveSentMap();

			for (uint32 i = 0; i < idsCount; ++i) {
				mtpMsgId msgId = ids[i].v;
				const auto req = haveSent.find(msgId);
				if (req != haveSent.cend()) {
					if (!req.value()->msDate) {
						DEBUG_LOG(("Message Info: container ack received, msgId %1").arg(ids[i].v));
						uint32 inContCount = ((*req)->size() - 8) / 2;
						const mtpMsgId *inContId = (const mtpMsgId *)(req.value()->constData() + 8);
						toAckMore.reserve(toAckMore.size() + inContCount);
						for (uint32 j = 0; j < inContCount; ++j) {
							toAckMore.push_back(MTP_long(*(inContId++)));
						}
						haveSent.erase(req);
					} else {
						mtpRequestId reqId = req.value()->requestId;
						bool moveToAcked = byResponse;
						if (!moveToAcked) { // ignore ACK, if we need a response (if we have a handler)
							moveToAcked = !_instance->hasCallbacks(reqId);
						}
						if (moveToAcked) {
							wereAcked.insert(msgId, reqId);
							haveSent.erase(req);
						} else {
							DEBUG_LOG(("Message Info: ignoring ACK for msgId %1 because request %2 requires a response").arg(msgId).arg(reqId));
						}
					}
				} else {
					DEBUG_LOG(("Message Info: msgId %1 was not found in recent sent, while acking requests, searching in resend...").arg(msgId));
					QWriteLocker locker3(_sessionData->toResendMutex());
					auto &toResend = _sessionData->toResendMap();
					const auto reqIt = toResend.find(msgId);
					if (reqIt != toResend.cend()) {
						const auto reqId = reqIt.value();
						bool moveToAcked = byResponse;
						if (!moveToAcked) { // ignore ACK, if we need a response (if we have a handler)
							moveToAcked = !_instance->hasCallbacks(reqId);
						}
						if (moveToAcked) {
							QWriteLocker locker4(_sessionData->toSendMutex());
							auto &toSend = _sessionData->toSendMap();
							const auto req = toSend.find(reqId);
							if (req != toSend.cend()) {
								wereAcked.insert(msgId, req.value()->requestId);
								if (req.value()->requestId != reqId) {
									DEBUG_LOG(("Message Error: for msgId %1 found resent request, requestId %2, contains requestId %3").arg(msgId).arg(reqId).arg(req.value()->requestId));
								} else {
									DEBUG_LOG(("Message Info: acked msgId %1 that was prepared to resend, requestId %2").arg(msgId).arg(reqId));
								}
								toSend.erase(req);
							} else {
								DEBUG_LOG(("Message Info: msgId %1 was found in recent resent, requestId %2 was not found in prepared to send").arg(msgId));
							}
							toResend.erase(reqIt);
						} else {
							DEBUG_LOG(("Message Info: ignoring ACK for msgId %1 because request %2 requires a response").arg(msgId).arg(reqId));
						}
					} else {
						DEBUG_LOG(("Message Info: msgId %1 was not found in recent resent either").arg(msgId));
					}
				}
			}
		}

		uint32 ackedCount = wereAcked.size();
		if (ackedCount > kIdsBufferSize) {
			DEBUG_LOG(("Message Info: removing some old acked sent msgIds %1").arg(ackedCount - kIdsBufferSize));
			clearedBecauseTooOld.reserve(ackedCount - kIdsBufferSize);
			while (ackedCount-- > kIdsBufferSize) {
				auto i = wereAcked.begin();
				clearedBecauseTooOld.push_back(RPCCallbackClear(
					i.value(),
					RPCError::TimeoutError));
				wereAcked.erase(i);
			}
		}
	}

	if (!clearedBecauseTooOld.empty()) {
		_instance->clearCallbacksDelayed(std::move(clearedBecauseTooOld));
	}

	if (toAckMore.size()) {
		requestsAcked(toAckMore);
	}
}

void ConnectionPrivate::handleMsgsStates(const QVector<MTPlong> &ids, const QByteArray &states, QVector<MTPlong> &acked) {
	uint32 idsCount = ids.size();
	if (!idsCount) {
		DEBUG_LOG(("Message Info: void ids vector in handleMsgsStates()"));
		return;
	}
	if (states.size() < idsCount) {
		LOG(("Message Error: got less states than required ids count."));
		return;
	}

	acked.reserve(acked.size() + idsCount);
	for (uint32 i = 0, count = idsCount; i < count; ++i) {
		char state = states[i];
		uint64 requestMsgId = ids[i].v;
		{
			QReadLocker locker(_sessionData->haveSentMutex());
			const auto &haveSent = _sessionData->haveSentMap();
			const auto haveSentEnd = haveSent.cend();
			if (haveSent.find(requestMsgId) == haveSentEnd) {
				DEBUG_LOG(("Message Info: state was received for msgId %1, but request is not found, looking in resent requests...").arg(requestMsgId));
				QWriteLocker locker2(_sessionData->toResendMutex());
				auto &toResend = _sessionData->toResendMap();
				const auto reqIt = toResend.find(requestMsgId);
				if (reqIt != toResend.cend()) {
					if ((state & 0x07) != 0x04) { // was received
						DEBUG_LOG(("Message Info: state was received for msgId %1, state %2, already resending in container").arg(requestMsgId).arg((int32)state));
					} else {
						DEBUG_LOG(("Message Info: state was received for msgId %1, state %2, ack, cancelling resend").arg(requestMsgId).arg((int32)state));
						acked.push_back(MTP_long(requestMsgId)); // will remove from resend in requestsAcked
					}
				} else {
					DEBUG_LOG(("Message Info: msgId %1 was not found in recent resent either").arg(requestMsgId));
				}
				continue;
			}
		}
		if ((state & 0x07) != 0x04) { // was received
			DEBUG_LOG(("Message Info: state was received for msgId %1, state %2, resending in container").arg(requestMsgId).arg((int32)state));
			resend(requestMsgId, 10, true);
		} else {
			DEBUG_LOG(("Message Info: state was received for msgId %1, state %2, ack").arg(requestMsgId).arg((int32)state));
			acked.push_back(MTP_long(requestMsgId));
		}
	}
}

void ConnectionPrivate::resend(
		mtpMsgId msgId,
		crl::time msCanWait,
		bool forceContainer) {
	if (msgId != _pingMsgId) {
		_sessionData->resend(msgId, msCanWait, forceContainer);
	}
}

void ConnectionPrivate::resendMany(
		QVector<mtpMsgId> msgIds,
		crl::time msCanWait,
		bool forceContainer) {
	for (const auto msgId : msgIds) {
		resend(msgId, msCanWait, forceContainer);
	}
}

void ConnectionPrivate::onConnected(
		not_null<AbstractConnection*> connection) {
	disconnect(connection, &AbstractConnection::connected, nullptr, nullptr);
	if (!connection->isConnected()) {
		LOG(("Connection Error: not connected in onConnected(), "
			"state: %1").arg(connection->debugState()));
		return restart();
	}

	_waitForConnected = kMinConnectedTimeout;
	_waitForConnectedTimer.cancel();

	const auto i = ranges::find(
		_testConnections,
		connection.get(),
		[](const TestConnection &test) { return test.data.get(); });
	Assert(i != end(_testConnections));
	const auto my = i->priority;
	const auto j = ranges::find_if(
		_testConnections,
		[&](const TestConnection &test) { return test.priority > my; });
	if (j != end(_testConnections)) {
		DEBUG_LOG(("MTP Info: connection %1 succeed, "
			"waiting for %2.").arg(i->data->tag()).arg(j->data->tag()));
		_waitForBetterTimer.callOnce(kWaitForBetterTimeout);
	} else {
		DEBUG_LOG(("MTP Info: connection through IPv4 succeed."));
		_waitForBetterTimer.cancel();
		_connection = std::move(i->data);
		_testConnections.clear();
		checkAuthKey();
	}
}

void ConnectionPrivate::onDisconnected(
		not_null<AbstractConnection*> connection) {
	removeTestConnection(connection);

	if (_testConnections.empty()) {
		destroyAllConnections();
		restart();
	} else {
		confirmBestConnection();
	}
}

void ConnectionPrivate::confirmBestConnection() {
	if (_waitForBetterTimer.isActive()) {
		return;
	}
	const auto i = ranges::max_element(
		_testConnections,
		std::less<>(),
		[](const TestConnection &test) {
			return test.data->isConnected() ? test.priority : -1;
		});
	Assert(i != end(_testConnections));
	if (!i->data->isConnected()) {
		return;
	}

	DEBUG_LOG(("MTP Info: can't connect through better, using %1."
		).arg(i->data->tag()));

	_connection = std::move(i->data);
	_testConnections.clear();

	checkAuthKey();
}

void ConnectionPrivate::removeTestConnection(
		not_null<AbstractConnection*> connection) {
	_testConnections.erase(
		ranges::remove(
			_testConnections,
			connection.get(),
			[](const TestConnection &test) { return test.data.get(); }),
		end(_testConnections));
}

void ConnectionPrivate::checkAuthKey() {
	if (_keyId) {
		authKeyChecked();
	} else if (_instance->isKeysDestroyer()) {
		applyAuthKey(_sessionData->getPersistentKey());
	} else {
		applyAuthKey(_sessionData->getTemporaryKey());
	}
}

void ConnectionPrivate::updateAuthKey() {
	if (_instance->isKeysDestroyer() || _keyCreator) {
		return;
	}

	DEBUG_LOG(("AuthKey Info: Connection updating key from Session, dc %1").arg(_shiftedDcId));
	applyAuthKey(_sessionData->getTemporaryKey());
}

void ConnectionPrivate::setCurrentKeyId(uint64 newKeyId) {
	if (_keyId == newKeyId) {
		return;
	}
	_keyId = newKeyId;

	DEBUG_LOG(("MTP Info: auth key id set to id %1").arg(newKeyId));
	changeSessionId();
}

void ConnectionPrivate::applyAuthKey(AuthKeyPtr &&encryptionKey) {
	_encryptionKey = std::move(encryptionKey);
	const auto newKeyId = _encryptionKey ? _encryptionKey->keyId() : 0;
	if (_keyId) {
		if (_keyId == newKeyId) {
			return;
		}
		setCurrentKeyId(0);
		DEBUG_LOG(("MTP Error: auth_key id for dc %1 changed, restarting..."
			).arg(_shiftedDcId));
		if (_connection) {
			restart();
		}
		return;
	}
	if (!_connection) {
		return;
	}
	setCurrentKeyId(newKeyId);
	Assert(!_connection->sentEncryptedWithKeyId());

	DEBUG_LOG(("AuthKey Info: Connection update key from Session, dc %1 result: %2").arg(_shiftedDcId).arg(Logs::mb(&_keyId, sizeof(_keyId)).str()));
	if (_keyId) {
		return authKeyChecked();
	}

	if (_instance->isKeysDestroyer()) {
		// We are here to destroy an old key, so we're done.
		LOG(("MTP Error: No key %1 in updateAuthKey() for destroying.").arg(_shiftedDcId));
		_instance->keyWasPossiblyDestroyed(_shiftedDcId);
	} else if (_keyCreator) {
		DEBUG_LOG(("AuthKey Info: No key in updateAuthKey(), creating."));
		_keyCreator->start(
			BareDcId(_shiftedDcId),
			getProtocolDcId(),
			_connection.get(),
			_instance->dcOptions());
	} else {
		DEBUG_LOG(("AuthKey Info: No key in updateAuthKey(), but someone is creating already."));
	}
}

bool ConnectionPrivate::destroyOldEnoughPersistentKey() {
	Expects(_keyCreator != nullptr);

	const auto key = _keyCreator->bindPersistentKey();
	Assert(key != nullptr);

	const auto created = key->creationTime();
	if (created > 0 && crl::now() - created < kKeyOldEnoughForDestroy) {
		return false;
	}
	const auto instance = _instance;
	const auto shiftedDcId = _shiftedDcId;
	const auto keyId = key->keyId();
	InvokeQueued(instance, [=] {
		instance->keyDestroyedOnServer(shiftedDcId, keyId);
	});
	return true;
}

void ConnectionPrivate::tryAcquireKeyCreation() {
	if (_instance->isKeysDestroyer()
		|| _keyCreator
		|| !_sessionData->acquireKeyCreation()) {
		return;
	}

	using Result = DcKeyResult;
	using Error = DcKeyError;
	auto delegate = BoundKeyCreator::Delegate();
	delegate.unboundReady = [=](base::expected<Result, Error> result) {
		if (!result) {
			releaseKeyCreationOnFail();
			if (result.error() == Error::UnknownPublicKey) {
				if (_dcType == DcType::Cdn) {
					LOG(("Warning: CDN public RSA key not found"));
					requestCDNConfig();
					return;
				}
				LOG(("AuthKey Error: could not choose public RSA key"));
			}
			restart();
			return;
		}
		DEBUG_LOG(("AuthKey Info: unbound key creation succeed, "
			"ids: (%1, %2) server salts: (%3, %4)"
			).arg(result->temporaryKey
				? result->temporaryKey->keyId()
				: 0
			).arg(result->persistentKey
				? result->persistentKey->keyId()
				: 0
			).arg(result->temporaryServerSalt
			).arg(result->persistentServerSalt));

		_sessionSalt = result->temporaryServerSalt;
		if (result->persistentKey) {
			_sessionData->clearForNewKey(_instance);
		}

		auto key = result->persistentKey
			? std::move(result->persistentKey)
			: _sessionData->getPersistentKey();
		if (!key) {
			releaseKeyCreationOnFail();
			restart();
			return;
		}
		result->temporaryKey->setExpiresAt(base::unixtime::now()
			+ kTemporaryExpiresIn
			+ kBindKeyAdditionalExpiresTimeout);
		_keyCreator->bind(std::move(key));
		applyAuthKey(std::move(result->temporaryKey));
	};
	delegate.sentSome = [=](uint64 size) {
		onSentSome(size);
	};
	delegate.receivedSome = [=] {
		onReceivedSome();
	};

	auto request = DcKeyRequest();
	request.persistentNeeded = !_sessionData->getPersistentKey();
	request.temporaryExpiresIn = kTemporaryExpiresIn;
	_keyCreator = std::make_unique<BoundKeyCreator>(
		request,
		std::move(delegate));
}

void ConnectionPrivate::authKeyChecked() {
	connect(_connection, &AbstractConnection::receivedData, [=] {
		handleReceived();
	});

	if (_sessionSalt && setState(ConnectedState)) {
		_sessionData->resendAll();
	} // else receive salt in bad_server_salt first, then try to send all the requests

	_pingIdToSend = rand_value<uint64>(); // get server_salt
	_sessionData->queueNeedToResumeAndSend();
}

void ConnectionPrivate::onError(
		not_null<AbstractConnection*> connection,
		qint32 errorCode) {
	if (errorCode == -429) {
		LOG(("Protocol Error: -429 flood code returned!"));
	} else if (errorCode == -444) {
		LOG(("Protocol Error: -444 bad dc_id code returned!"));
		InvokeQueued(_instance, [instance = _instance] {
			instance->badConfigurationError();
		});
	}
	removeTestConnection(connection);

	if (_testConnections.empty()) {
		handleError(errorCode);
	} else {
		confirmBestConnection();
	}
}

void ConnectionPrivate::handleError(int errorCode) {
	destroyAllConnections();
	_waitForConnectedTimer.cancel();

	if (errorCode == -404) {
		destroyTemporaryKey();
	} else {
		MTP_LOG(_shiftedDcId, ("Restarting after error in connection, error code: %1...").arg(errorCode));
		return restart();
	}
}

void ConnectionPrivate::destroyTemporaryKey() {
	if (_instance->isKeysDestroyer()) {
		LOG(("MTP Info: -404 error received in destroyer %1, assuming key was destroyed.").arg(_shiftedDcId));
		_instance->keyWasPossiblyDestroyed(_shiftedDcId);
		return;
	}
	LOG(("MTP Info: -404 error received in %1 with temporary key, assuming it was destroyed.").arg(_shiftedDcId));
	releaseKeyCreationOnFail();
	if (_encryptionKey) {
		_sessionData->destroyTemporaryKey(_encryptionKey->keyId());
	}
	applyAuthKey(nullptr);
	restart();
}

bool ConnectionPrivate::sendSecureRequest(
		SecureRequest &&request,
		bool needAnyResponse) {
#ifdef TDESKTOP_MTPROTO_OLD
	const auto oldPadding = true;
#else // TDESKTOP_MTPROTO_OLD
	const auto oldPadding = false;
#endif // TDESKTOP_MTPROTO_OLD
	request.addPadding(_connection->requiresExtendedPadding(), oldPadding);

	uint32 fullSize = request->size();
	if (fullSize < 9) {
		return false;
	}

	auto messageSize = request.messageSize();
	if (messageSize < 5 || fullSize < messageSize + 4) {
		return false;
	}

	memcpy(request->data() + 0, &_sessionSalt, 2 * sizeof(mtpPrime));
	memcpy(request->data() + 2, &_sessionId, 2 * sizeof(mtpPrime));

	auto from = request->constData() + 4;
	MTP_LOG(_shiftedDcId, ("Send: ") + details::DumpToText(from, from + messageSize) + QString(" (keyId:%1)").arg(_encryptionKey->keyId()));

#ifdef TDESKTOP_MTPROTO_OLD
	uint32 padding = fullSize - 4 - messageSize;

	uchar encryptedSHA[20];
	MTPint128 &msgKey(*(MTPint128*)(encryptedSHA + 4));
	hashSha1(
		request->constData(),
		(fullSize - padding) * sizeof(mtpPrime),
		encryptedSHA);

	auto packet = _connection->prepareSecurePacket(_keyId, msgKey, fullSize);
	const auto prefix = packet.size();
	packet.resize(prefix + fullSize);

	aesIgeEncrypt_oldmtp(
		request->constData(),
		&packet[prefix],
		fullSize * sizeof(mtpPrime),
		_encryptionKey,
		msgKey);
#else // TDESKTOP_MTPROTO_OLD
	uchar encryptedSHA256[32];
	MTPint128 &msgKey(*(MTPint128*)(encryptedSHA256 + 8));

	SHA256_CTX msgKeyLargeContext;
	SHA256_Init(&msgKeyLargeContext);
	SHA256_Update(&msgKeyLargeContext, _encryptionKey->partForMsgKey(true), 32);
	SHA256_Update(&msgKeyLargeContext, request->constData(), fullSize * sizeof(mtpPrime));
	SHA256_Final(encryptedSHA256, &msgKeyLargeContext);

	auto packet = _connection->prepareSecurePacket(_keyId, msgKey, fullSize);
	const auto prefix = packet.size();
	packet.resize(prefix + fullSize);

	aesIgeEncrypt(
		request->constData(),
		&packet[prefix],
		fullSize * sizeof(mtpPrime),
		_encryptionKey,
		msgKey);
#endif // TDESKTOP_MTPROTO_OLD

	DEBUG_LOG(("MTP Info: sending request, size: %1, num: %2, time: %3").arg(fullSize + 6).arg((*request)[4]).arg((*request)[5]));

	_connection->setSentEncryptedWithKeyId(_keyId);
	_connection->sendData(std::move(packet));

	if (needAnyResponse) {
		onSentSome((prefix + fullSize) * sizeof(mtpPrime));
	}

	return true;
}

mtpRequestId ConnectionPrivate::wasSent(mtpMsgId msgId) const {
	if (msgId == _pingMsgId) return mtpRequestId(0xFFFFFFFF);
	{
		QReadLocker locker(_sessionData->haveSentMutex());
		const auto &haveSent = _sessionData->haveSentMap();
		const auto i = haveSent.constFind(msgId);
		if (i != haveSent.cend()) {
			return i.value()->requestId
				? i.value()->requestId
				: mtpRequestId(0xFFFFFFFF);
		}
	}
	{
		QReadLocker locker(_sessionData->toResendMutex());
		const auto &toResend = _sessionData->toResendMap();
		const auto i = toResend.constFind(msgId);
		if (i != toResend.cend()) return i.value();
	}
	{
		QReadLocker locker(_sessionData->wereAckedMutex());
		const auto &wereAcked = _sessionData->wereAckedMap();
		const auto i = wereAcked.constFind(msgId);
		if (i != wereAcked.cend()) return i.value();
	}
	return 0;
}

void ConnectionPrivate::clearUnboundKeyCreator() {
	if (_keyCreator) {
		_keyCreator->stop();
	}
}

void ConnectionPrivate::releaseKeyCreationOnFail() {
	if (!_keyCreator) {
		return;
	}
	_keyCreator = nullptr;
	_sessionData->releaseKeyCreationOnFail();
}

void ConnectionPrivate::stop() {
}

} // namespace internal
} // namespace MTP
