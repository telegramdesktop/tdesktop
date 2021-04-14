/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/session_private.h"

#include "mtproto/details/mtproto_bound_key_creator.h"
#include "mtproto/details/mtproto_dcenter.h"
#include "mtproto/details/mtproto_dump_to_text.h"
#include "mtproto/details/mtproto_rsa_public_key.h"
#include "mtproto/session.h"
#include "mtproto/mtproto_response.h"
#include "mtproto/mtproto_dc_options.h"
#include "mtproto/connection_abstract.h"
#include "platform/platform_specific.h"
#include "base/openssl_help.h"
#include "base/qthelp_url.h"
#include "base/unixtime.h"
#include "base/platform/base_platform_info.h"
#include "zlib.h"

namespace MTP {
namespace details {
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
constexpr auto kTemporaryExpiresIn = TimeId(86400);
constexpr auto kBindKeyAdditionalExpiresTimeout = TimeId(30);
constexpr auto kTestModeDcIdShift = 10000;
constexpr auto kKeyOldEnoughForDestroy = 60 * crl::time(1000);
constexpr auto kSentContainerLives = 600 * crl::time(1000);
constexpr auto kFastRequestDuration = crl::time(500);

// If we can't connect for this time we will ask _instance to update config.
constexpr auto kRequestConfigTimeout = 8 * crl::time(1000);

// Don't try to handle messages larger than this size.
constexpr auto kMaxMessageLength = 16 * 1024 * 1024;

// How much time passed from send till we resend request or check its state.
constexpr auto kCheckSentRequestTimeout = 10 * crl::time(1000);

// How much time to wait for some more requests,
// when resending request or checking its state.
constexpr auto kSendStateRequestWaiting = crl::time(1000);

// How much time to wait for some more requests, when sending msg acks.
constexpr auto kAckSendWaiting = 10 * crl::time(1000);

auto SyncTimeRequestDuration = kFastRequestDuration;

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

[[nodiscard]] QString ComputeAppVersion() {
	return QString::fromLatin1(AppVersionStr) + ([] {
#if defined OS_MAC_STORE
		return u" Mac App Store"_q;
#elif defined OS_WIN_STORE // OS_MAC_STORE
		return (Platform::IsWindows64Bit() ? u" x64"_q : QString())
			+ u" Microsoft Store"_q;
#elif defined Q_OS_UNIX && !defined Q_OS_MAC // OS_MAC_STORE || OS_WIN_STORE
		return Platform::InFlatpak()
			? u" Flatpak"_q
			: Platform::InSnap()
			? u" Snap"_q
			: QString();
#else // OS_MAC_STORE || OS_WIN_STORE || (defined Q_OS_UNIX && !defined Q_OS_MAC)
		return Platform::IsWindows64Bit() ? u" x64"_q : QString();
#endif // OS_MAC_STORE || OS_WIN_STORE || (defined Q_OS_UNIX && !defined Q_OS_MAC)
	})();
}

void WrapInvokeAfter(
		SerializedRequest &to,
		const SerializedRequest &from,
		const base::flat_map<mtpMsgId, SerializedRequest> &haveSent,
		int32 skipBeforeRequest = 0) {
	const auto afterId = *(mtpMsgId*)(from->after->data() + 4);
	const auto i = afterId ? haveSent.find(afterId) : haveSent.end();
	int32 size = to->size(), lenInInts = (tl::count_length(from) >> 2), headlen = 4, fulllen = headlen + lenInInts;
	if (i == haveSent.end()) { // no invoke after or such msg was not sent or was completed recently
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

[[nodiscard]] bool ConstTimeIsDifferent(
		const void *a,
		const void *b,
		size_t size) {
	auto ca = reinterpret_cast<const char*>(a);
	auto cb = reinterpret_cast<const char*>(b);
	volatile auto different = false;
	for (const auto ce = ca + size; ca != ce; ++ca, ++cb) {
		different = different | (*ca != *cb);
	}
	return different;
}

} // namespace

SessionPrivate::SessionPrivate(
	not_null<Instance*> instance,
	not_null<QThread*> thread,
	std::shared_ptr<SessionData> data,
	ShiftedDcId shiftedDcId)
: QObject(nullptr)
, _instance(instance)
, _shiftedDcId(shiftedDcId)
, _realDcType(_instance->dcOptions().dcType(_shiftedDcId))
, _currentDcType(_realDcType)
, _state(DisconnectedState)
, _retryTimer(thread, [=] { retryByTimer(); })
, _oldConnectionTimer(thread, [=] { markConnectionOld(); })
, _waitForConnectedTimer(thread, [=] { waitConnectedFailed(); })
, _waitForReceivedTimer(thread, [=] { waitReceivedFailed(); })
, _waitForBetterTimer(thread, [=] { waitBetterFailed(); })
, _waitForReceived(kMinReceiveTimeout)
, _waitForConnected(kMinConnectedTimeout)
, _pingSender(thread, [=] { sendPingByTimer(); })
, _checkSentRequestsTimer(thread, [=] { checkSentRequests(); })
, _clearOldContainersTimer(thread, [=] { clearOldContainers(); })
, _sessionData(std::move(data)) {
	Expects(_shiftedDcId != 0);

	moveToThread(thread);

	InvokeQueued(this, [=] {
		_clearOldContainersTimer.callEach(kSentContainerLives);
		connectToServer();
	});
}

SessionPrivate::~SessionPrivate() {
	releaseKeyCreationOnFail();
	doDisconnect();

	Expects(!_connection);
	Expects(_testConnections.empty());
}

void SessionPrivate::appendTestConnection(
		DcOptions::Variants::Protocol protocol,
		const QString &ip,
		int port,
		const bytes::vector &protocolSecret) {
	QWriteLocker lock(&_stateMutex);

	const auto priority = (qthelp::is_ipv6(ip) ? 0 : 1)
		+ (protocol == DcOptions::Variants::Tcp ? 1 : 0)
		+ (protocolSecret.empty() ? 0 : 1);
	_testConnections.push_back({
		AbstractConnection::Create(
			_instance,
			protocol,
			thread(),
			protocolSecret,
			_options->proxy),
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

	const auto protocolDcId = getProtocolDcId();
	InvokeQueued(_testConnections.back().data, [=] {
		weak->connectToServer(ip, port, protocolSecret, protocolDcId);
	});
}

int16 SessionPrivate::getProtocolDcId() const {
	const auto dcId = BareDcId(_shiftedDcId);
	const auto simpleDcId = isTemporaryDcId(dcId)
		? getRealIdFromTemporaryDcId(dcId)
		: dcId;
	const auto testedDcId = _instance->isTestMode()
		? (kTestModeDcIdShift + simpleDcId)
		: simpleDcId;
	return (_currentDcType == DcType::MediaCluster)
		? -testedDcId
		: testedDcId;
}

void SessionPrivate::checkSentRequests() {
	const auto now = crl::now();
	const auto checkTime = now - kCheckSentRequestTimeout;
	if (_bindMsgId && _bindMessageSent < checkTime) {
		DEBUG_LOG(("MTP Info: "
			"Request state while key is not bound, restarting."));
		restart();
		_checkSentRequestsTimer.callOnce(kCheckSentRequestTimeout);
		return;
	}
	auto requesting = false;
	auto nextTimeout = kCheckSentRequestTimeout;
	{
		QReadLocker locker(_sessionData->haveSentMutex());
		auto &haveSent = _sessionData->haveSentMap();
		for (const auto &[msgId, request] : haveSent) {
			if (request->lastSentTime <= checkTime) {
				// Need to check state.
				request->lastSentTime = now;
				if (_stateRequestData.emplace(msgId).second) {
					requesting = true;
				}
			} else {
				nextTimeout = std::min(request->lastSentTime - checkTime, nextTimeout);
			}
		}
	}
	if (requesting) {
		_sessionData->queueSendAnything(kSendStateRequestWaiting);
	}
	if (nextTimeout < kCheckSentRequestTimeout) {
		_checkSentRequestsTimer.callOnce(nextTimeout);
	}
}

void SessionPrivate::clearOldContainers() {
	auto resent = false;
	auto nextTimeout = kSentContainerLives;
	const auto now = crl::now();
	const auto checkTime = now - kSentContainerLives;
	for (auto i = _sentContainers.begin(); i != _sentContainers.end();) {
		if (i->second.sent <= checkTime) {
			DEBUG_LOG(("MTP Info: Removing old container with resending %1, "
				"sent: %2, now: %3, current unixtime: %4"
				).arg(i->first
				).arg(i->second.sent
				).arg(now
				).arg(base::unixtime::now()));

			const auto ids = std::move(i->second.messages);
			i = _sentContainers.erase(i);

			resent = resent || !ids.empty();
			for (const auto innerMsgId : ids) {
				resend(innerMsgId, -1, true);
			}
		} else {
			nextTimeout = std::min(i->second.sent - checkTime, nextTimeout);
			++i;
		}
	}
	if (resent) {
		_sessionData->queueNeedToResumeAndSend();
	}
	if (nextTimeout < kSentContainerLives) {
		_clearOldContainersTimer.callOnce(nextTimeout);
	} else if (!_clearOldContainersTimer.isActive()) {
		_clearOldContainersTimer.callEach(nextTimeout);
	}
}

void SessionPrivate::destroyAllConnections() {
	clearUnboundKeyCreator();
	_waitForBetterTimer.cancel();
	_waitForReceivedTimer.cancel();
	_waitForConnectedTimer.cancel();
	_testConnections.clear();
	_connection = nullptr;
}

void SessionPrivate::cdnConfigChanged() {
	connectToServer(true);
}

int32 SessionPrivate::getShiftedDcId() const {
	return _shiftedDcId;
}

void SessionPrivate::dcOptionsChanged() {
	_retryTimeout = 1;
	connectToServer(true);
}

int32 SessionPrivate::getState() const {
	QReadLocker lock(&_stateMutex);
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

QString SessionPrivate::transport() const {
	QReadLocker lock(&_stateMutex);
	if (!_connection || (_state < 0)) {
		return QString();
	}

	Assert(_options != nullptr);
	return _connection->transport();
}

bool SessionPrivate::setState(int state, int ifState) {
	if (ifState != kUpdateStateAlways) {
		QReadLocker lock(&_stateMutex);
		if (_state != ifState) {
			return false;
		}
	}

	QWriteLocker lock(&_stateMutex);
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

void SessionPrivate::resetSession() {
	MTP_LOG(_shiftedDcId, ("Resetting session!"));
	_needSessionReset = false;

	DEBUG_LOG(("MTP Info: creating new session in resetSession."));
	changeSessionId();

	_sessionData->queueResetDone();
}

void SessionPrivate::changeSessionId() {
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

uint32 SessionPrivate::nextRequestSeqNumber(bool needAck) {
	const auto result = _messagesCounter;
	_messagesCounter += (needAck ? 1 : 0);
	return result * 2 + (needAck ? 1 : 0);
}

bool SessionPrivate::realDcTypeChanged() {
	const auto now = _instance->dcOptions().dcType(_shiftedDcId);
	if (_realDcType == now) {
		return false;
	}
	_realDcType = now;
	return true;
}

bool SessionPrivate::markSessionAsStarted() {
	if (_sessionMarkedAsStarted) {
		return false;
	}
	_sessionMarkedAsStarted = true;
	return true;
}

mtpMsgId SessionPrivate::prepareToSend(
		SerializedRequest &request,
		mtpMsgId currentLastId,
		bool forceNewMsgId) {
	Expects(request->size() > 8);

	if (const auto msgId = request.getMsgId()) {
		// resending this request
		const auto i = _resendingIds.find(msgId);
		if (i != _resendingIds.cend()) {
			_resendingIds.erase(i);
		}

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

mtpMsgId SessionPrivate::replaceMsgId(SerializedRequest &request, mtpMsgId newId) {
	Expects(request->size() > 8);

	const auto oldMsgId = request.getMsgId();
	if (oldMsgId == newId) {
		return newId;
	}
	// haveSentMutex() was locked in tryToSend()
	auto &haveSent = _sessionData->haveSentMap();

	while (_resendingIds.contains(newId)
		|| _ackedIds.contains(newId)
		|| haveSent.contains(newId)) {
		newId = base::unixtime::mtproto_msg_id();
	}

	MTP_LOG(_shiftedDcId, ("[r%1] msg_id %2 -> %3"
		).arg(request->requestId
		).arg(oldMsgId
		).arg(newId));

	const auto i = _resendingIds.find(oldMsgId);
	if (i != _resendingIds.end()) {
		const auto requestId = i->second;
		_resendingIds.erase(i);
		_resendingIds.emplace(newId, requestId);
	}

	const auto j = _ackedIds.find(oldMsgId);
	if (j != _ackedIds.end()) {
		const auto requestId = j->second;
		_ackedIds.erase(j);
		_ackedIds.emplace(newId, requestId);
	}

	const auto k = haveSent.find(oldMsgId);
	if (k != haveSent.end()) {
		const auto request = k->second;
		haveSent.erase(k);
		haveSent.emplace(newId, request);
	}
	for (auto &[msgId, container] : _sentContainers) {
		for (auto &innerMsgId : container.messages) {
			if (innerMsgId == oldMsgId) {
				innerMsgId = newId;
			}
		}
	}
	request.setMsgId(newId);
	request.setSeqNo(nextRequestSeqNumber(request.needAck()));
	return newId;
}

mtpMsgId SessionPrivate::placeToContainer(
		SerializedRequest &toSendRequest,
		mtpMsgId &bigMsgId,
		bool forceNewMsgId,
		SerializedRequest &req) {
	const auto msgId = prepareToSend(req, bigMsgId, forceNewMsgId);
	if (msgId >= bigMsgId) {
		bigMsgId = base::unixtime::mtproto_msg_id();
	}

	uint32 from = toSendRequest->size(), len = req.messageSize();
	toSendRequest->resize(from + len);
	memcpy(toSendRequest->data() + from, req->constData() + 4, len * sizeof(mtpPrime));

	return msgId;
}

MTPVector<MTPJSONObjectValue> SessionPrivate::prepareInitParams() {
	const auto local = QDateTime::currentDateTime();
	const auto utc = QDateTime(local.date(), local.time(), Qt::UTC);
	const auto shift = base::unixtime::now() - (TimeId)::time(nullptr);
	const auto delta = int(utc.toTime_t()) - int(local.toTime_t()) - shift;
	auto sliced = delta;
	while (sliced < -12 * 3600) {
		sliced += 24 * 3600;
	}
	while (sliced > 14 * 3600) {
		sliced -= 24 * 3600;
	}
	const auto sign = (sliced < 0) ? -1 : 1;
	const auto rounded = std::round(std::abs(sliced) / 900.) * 900 * sign;
	return MTP_vector<MTPJSONObjectValue>(
		1,
		MTP_jsonObjectValue(
			MTP_string("tz_offset"),
			MTP_jsonNumber(MTP_double(rounded))));
}

void SessionPrivate::tryToSend() {
	DEBUG_LOG(("MTP Info: tryToSend for dc %1.").arg(_shiftedDcId));
	if (!_connection) {
		DEBUG_LOG(("MTP Info: not yet connected in dc %1.").arg(_shiftedDcId));
		return;
	} else if (!_keyId) {
		DEBUG_LOG(("MTP Info: not yet with auth key in dc %1.").arg(_shiftedDcId));
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

	auto pingRequest = SerializedRequest();
	auto ackRequest = SerializedRequest();
	auto resendRequest = SerializedRequest();
	auto stateRequest = SerializedRequest();
	auto httpWaitRequest = SerializedRequest();
	auto bindDcKeyRequest = SerializedRequest();
	if (_pingIdToSend) {
		if (sendOnlyFirstPing || !isMainSession) {
			DEBUG_LOG(("MTP Info: sending ping, ping_id: %1"
				).arg(_pingIdToSend));
			pingRequest = SerializedRequest::Serialize(MTPPing(
				MTP_long(_pingIdToSend)
			));
		} else {
			DEBUG_LOG(("MTP Info: sending ping_delay_disconnect, "
				"ping_id: %1").arg(_pingIdToSend));
			pingRequest = SerializedRequest::Serialize(MTPPing_delay_disconnect(
				MTP_long(_pingIdToSend),
				MTP_int(kPingDelayDisconnect)));
			_pingSender.callOnce(kPingSendAfterForce);
		}
		_pingSendAt = pingRequest->lastSentTime + kPingSendAfter;
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
			ackRequest = SerializedRequest::Serialize(MTPMsgsAck(
				MTP_msgs_ack(MTP_vector<MTPlong>(
					base::take(_ackRequestData)))));
		}
		if (!_resendRequestData.isEmpty()) {
			resendRequest = SerializedRequest::Serialize(MTPMsgResendReq(
				MTP_msg_resend_req(MTP_vector<MTPlong>(
					base::take(_resendRequestData)))));
		}
		if (!_stateRequestData.empty()) {
			auto ids = QVector<MTPlong>();
			ids.reserve(_stateRequestData.size());
			for (const auto id : base::take(_stateRequestData)) {
				ids.push_back(MTP_long(id));
			}
			stateRequest = SerializedRequest::Serialize(MTPMsgsStateReq(
				MTP_msgs_state_req(MTP_vector<MTPlong>(ids))));
		}
		if (_connection->usingHttpWait()) {
			httpWaitRequest = SerializedRequest::Serialize(MTPHttpWait(
				MTP_http_wait(MTP_int(100), MTP_int(30), MTP_int(25000))));
		}
		if (!_bindMsgId && _keyCreator && _keyCreator->readyToBind()) {
			bindDcKeyRequest = _keyCreator->prepareBindRequest(
				_encryptionKey,
				_sessionId);

			// This is a special request with msgId used inside the message
			// body, so it is prepared already with a msgId and we place
			// seqNo for it manually here.
			bindDcKeyRequest.setSeqNo(
				nextRequestSeqNumber(bindDcKeyRequest.needAck()));
		}
	}

	MTPInitConnection<SerializedRequest> initWrapper;
	int32 initSize = 0, initSizeInInts = 0;
	if (needsLayer) {
		Assert(_options != nullptr);
		const auto systemLangCode = _options->systemLangCode;
		const auto cloudLangCode = _options->cloudLangCode;
		const auto langPackName = _options->langPackName;
		const auto deviceModel = (_currentDcType == DcType::Cdn)
			? "n/a"
			: _instance->deviceModel();
		const auto systemVersion = (_currentDcType == DcType::Cdn)
			? "n/a"
			: _instance->systemVersion();
		const auto appVersion = ComputeAppVersion();
		const auto proxyType = _options->proxy.type;
		const auto mtprotoProxy = (proxyType == ProxyData::Type::Mtproto);
		const auto clientProxyFields = mtprotoProxy
			? MTP_inputClientProxy(
				MTP_string(_options->proxy.host),
				MTP_int(_options->proxy.port))
			: MTPInputClientProxy();
		using Flag = MTPInitConnection<SerializedRequest>::Flag;
		initWrapper = MTPInitConnection<SerializedRequest>(
			MTP_flags(Flag::f_params
				| (mtprotoProxy ? Flag::f_proxy : Flag(0))),
			MTP_int(ApiId),
			MTP_string(deviceModel),
			MTP_string(systemVersion),
			MTP_string(appVersion),
			MTP_string(systemLangCode),
			MTP_string(langPackName),
			MTP_string(cloudLangCode),
			clientProxyFields,
			MTP_jsonObject(prepareInitParams()),
			SerializedRequest());
		initSizeInInts = (tl::count_length(initWrapper) >> 2) + 2;
		initSize = initSizeInInts * sizeof(mtpPrime);
	}

	bool needAnyResponse = false;
	SerializedRequest toSendRequest;
	{
		QWriteLocker locker1(_sessionData->toSendMutex());

		auto scheduleCheckSentRequests = false;

		auto toSendDummy = base::flat_map<mtpRequestId, SerializedRequest>();
		auto &toSend = sendAll
			? _sessionData->toSendMap()
			: toSendDummy;
		if (!sendAll) {
			locker1.unlock();
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
			: toSend.begin()->second;
		if (toSendCount == 1 && !first->forceSendInContainer) {
			toSendRequest = first;
			if (sendAll) {
				toSend.clear();
				locker1.unlock();
			}

			const auto msgId = prepareToSend(
				toSendRequest,
				base::unixtime::mtproto_msg_id(),
				forceNewMsgId && !bindDcKeyRequest);
			if (bindDcKeyRequest) {
				_bindMsgId = msgId;
				_bindMessageSent = crl::now();
				needAnyResponse = true;
			} else if (pingRequest) {
				_pingMsgId = msgId;
				needAnyResponse = true;
			} else if (stateRequest || resendRequest) {
				_stateAndResendRequests.emplace(
					msgId,
					stateRequest ? stateRequest : resendRequest);
				needAnyResponse = true;
			}

			if (toSendRequest->requestId) {
				if (toSendRequest.needAck()) {
					toSendRequest->lastSentTime = crl::now();

					QWriteLocker locker2(_sessionData->haveSentMutex());
					auto &haveSent = _sessionData->haveSentMap();
					haveSent.emplace(msgId, toSendRequest);
					scheduleCheckSentRequests = true;

					const auto wrapLayer = needsLayer && toSendRequest->needsLayer;
					if (toSendRequest->after) {
						const auto toSendSize = tl::count_length(toSendRequest) >> 2;
						auto wrappedRequest = SerializedRequest::Prepare(
							toSendSize,
							toSendSize + 3);
						wrappedRequest->resize(4);
						memcpy(wrappedRequest->data(), toSendRequest->constData(), 4 * sizeof(mtpPrime));
						WrapInvokeAfter(wrappedRequest, toSendRequest, haveSent);
						toSendRequest = std::move(wrappedRequest);
					}
					if (wrapLayer) {
						const auto noWrapSize = (tl::count_length(toSendRequest) >> 2);
						const auto toSendSize = noWrapSize + initSizeInInts;
						auto wrappedRequest = SerializedRequest::Prepare(toSendSize);
						memcpy(wrappedRequest->data(), toSendRequest->constData(), 7 * sizeof(mtpPrime)); // all except length
						wrappedRequest->push_back(mtpc_invokeWithLayer);
						wrappedRequest->push_back(kCurrentLayer);
						initWrapper.write<mtpBuffer>(*wrappedRequest);
						wrappedRequest->resize(wrappedRequest->size() + noWrapSize);
						memcpy(wrappedRequest->data() + wrappedRequest->size() - noWrapSize, toSendRequest->constData() + 8, noWrapSize * sizeof(mtpPrime));
						toSendRequest = std::move(wrappedRequest);
					}

					needAnyResponse = true;
				} else {
					_ackedIds.emplace(msgId, toSendRequest->requestId);
				}
			}
		} else { // send in container
			bool willNeedInit = false;
			uint32 containerSize = 1 + 1; // cons + vector size
			if (pingRequest) containerSize += pingRequest.messageSize();
			if (ackRequest) containerSize += ackRequest.messageSize();
			if (resendRequest) containerSize += resendRequest.messageSize();
			if (stateRequest) containerSize += stateRequest.messageSize();
			if (httpWaitRequest) containerSize += httpWaitRequest.messageSize();
			if (bindDcKeyRequest) containerSize += bindDcKeyRequest.messageSize();
			for (const auto &[requestId, request] : toSend) {
				containerSize += request.messageSize();
				if (needsLayer && request->needsLayer) {
					containerSize += initSizeInInts;
					willNeedInit = true;
				}
			}
			mtpBuffer initSerialized;
			if (willNeedInit) {
				initSerialized.reserve(initSizeInInts);
				initSerialized.push_back(mtpc_invokeWithLayer);
				initSerialized.push_back(kCurrentLayer);
				initWrapper.write<mtpBuffer>(initSerialized);
			}
			// prepare container + each in invoke after
			toSendRequest = SerializedRequest::Prepare(
				containerSize,
				containerSize + 3 * toSend.size());
			toSendRequest->push_back(mtpc_msg_container);
			toSendRequest->push_back(toSendCount);

			// check for a valid container
			auto bigMsgId = base::unixtime::mtproto_msg_id();

			// the fact of this lock is used in replaceMsgId()
			QWriteLocker locker2(_sessionData->haveSentMutex());
			auto &haveSent = _sessionData->haveSentMap();

			// prepare sent container
			auto sentIdsWrap = SentContainer();
			sentIdsWrap.sent = crl::now();
			sentIdsWrap.messages.reserve(toSendCount);

			if (bindDcKeyRequest) {
				_bindMsgId = placeToContainer(
					toSendRequest,
					bigMsgId,
					false,
					bindDcKeyRequest);
				_bindMessageSent = crl::now();
				needAnyResponse = true;
			}
			if (pingRequest) {
				_pingMsgId = placeToContainer(
					toSendRequest,
					bigMsgId,
					forceNewMsgId,
					pingRequest);
				needAnyResponse = true;
			}

			for (auto &[requestId, request] : toSend) {
				const auto msgId = prepareToSend(
					request,
					bigMsgId,
					forceNewMsgId);
				if (msgId >= bigMsgId) {
					bigMsgId = base::unixtime::mtproto_msg_id();
				}
				bool added = false;
				if (request->requestId) {
					if (request.needAck()) {
						request->lastSentTime = crl::now();
						int32 reqNeedsLayer = (needsLayer && request->needsLayer) ? toSendRequest->size() : 0;
						if (request->after) {
							WrapInvokeAfter(toSendRequest, request, haveSent, reqNeedsLayer ? initSizeInInts : 0);
							if (reqNeedsLayer) {
								memcpy(toSendRequest->data() + reqNeedsLayer + 4, initSerialized.constData(), initSize);
								*(toSendRequest->data() + reqNeedsLayer + 3) += initSize;
							}
							added = true;
						} else if (reqNeedsLayer) {
							toSendRequest->resize(reqNeedsLayer + initSizeInInts + request.messageSize());
							memcpy(toSendRequest->data() + reqNeedsLayer, request->constData() + 4, 4 * sizeof(mtpPrime));
							memcpy(toSendRequest->data() + reqNeedsLayer + 4, initSerialized.constData(), initSize);
							memcpy(toSendRequest->data() + reqNeedsLayer + 4 + initSizeInInts, request->constData() + 8, tl::count_length(request));
							*(toSendRequest->data() + reqNeedsLayer + 3) += initSize;
							added = true;
						}

						// #TODO rewrite so that it will always hold.
						//Assert(!haveSent.contains(msgId));
						haveSent.emplace(msgId, request);
						sentIdsWrap.messages.push_back(msgId);
						scheduleCheckSentRequests = true;
						needAnyResponse = true;
					} else {
						_ackedIds.emplace(msgId, request->requestId);
					}
				}
				if (!added) {
					uint32 from = toSendRequest->size(), len = request.messageSize();
					toSendRequest->resize(from + len);
					memcpy(toSendRequest->data() + from, request->constData() + 4, len * sizeof(mtpPrime));
				}
			}
			toSend.clear();

			if (stateRequest) {
				const auto msgId = placeToContainer(
					toSendRequest,
					bigMsgId,
					forceNewMsgId,
					stateRequest);
				_stateAndResendRequests.emplace(msgId, stateRequest);
				needAnyResponse = true;
			}
			if (resendRequest) {
				const auto msgId = placeToContainer(
					toSendRequest,
					bigMsgId,
					forceNewMsgId,
					resendRequest);
				_stateAndResendRequests.emplace(msgId, resendRequest);
				needAnyResponse = true;
			}
			if (ackRequest) {
				placeToContainer(
					toSendRequest,
					bigMsgId,
					forceNewMsgId,
					ackRequest);
			}
			if (httpWaitRequest) {
				placeToContainer(
					toSendRequest,
					bigMsgId,
					forceNewMsgId,
					httpWaitRequest);
			}

			const auto containerMsgId = prepareToSend(
				toSendRequest,
				bigMsgId,
				forceNewMsgId);
			_sentContainers.emplace(containerMsgId, std::move(sentIdsWrap));

			if (scheduleCheckSentRequests && !_checkSentRequestsTimer.isActive()) {
				_checkSentRequestsTimer.callOnce(kCheckSentRequestTimeout);
			}
		}
	}
	sendSecureRequest(std::move(toSendRequest), needAnyResponse);
}

void SessionPrivate::retryByTimer() {
	if (_retryTimeout < 3) {
		++_retryTimeout;
	} else if (_retryTimeout == 3) {
		_retryTimeout = 1000;
	} else if (_retryTimeout < 64000) {
		_retryTimeout *= 2;
	}
	connectToServer();
}

void SessionPrivate::restartNow() {
	_retryTimeout = 1;
	_retryTimer.cancel();
	restart();
}

void SessionPrivate::connectToServer(bool afterConfig) {
	if (afterConfig && (!_testConnections.empty() || _connection)) {
		return;
	}

	destroyAllConnections();

	if (realDcTypeChanged() && _keyCreator) {
		destroyTemporaryKey();
		return;
	}

	_options = std::make_unique<SessionOptions>(_sessionData->options());

	const auto bareDc = BareDcId(_shiftedDcId);

	_currentDcType = tryAcquireKeyCreation();
	if (_currentDcType == DcType::Cdn && !_instance->isKeysDestroyer()) {
		if (!_instance->dcOptions().hasCDNKeysForDc(bareDc)) {
			requestCDNConfig();
			return;
		}
	}
	if (_options->proxy.type == ProxyData::Type::Mtproto) {
		// host, port, secret for mtproto proxy are taken from proxy.
		appendTestConnection(DcOptions::Variants::Tcp, {}, 0, {});
	} else {
		using Variants = DcOptions::Variants;
		const auto special = (_currentDcType == DcType::Temporary);
		const auto variants = _instance->dcOptions().lookup(
			bareDc,
			_currentDcType,
			_options->proxy.type != ProxyData::Type::None);
		const auto useIPv4 = special ? true : _options->useIPv4;
		const auto useIPv6 = special ? false : _options->useIPv6;
		const auto useTcp = special ? true : _options->useTcp;
		const auto useHttp = special ? false : _options->useHttp;
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

	_bindMsgId = 0;
	_pingId = _pingMsgId = _pingIdToSend = _pingSendAt = 0;
	_pingSender.cancel();

	_waitForConnectedTimer.callOnce(_waitForConnected);
}

void SessionPrivate::restart() {
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

void SessionPrivate::onSentSome(uint64 size) {
	if (!_waitForReceivedTimer.isActive()) {
		auto remain = static_cast<uint64>(_waitForReceived);
		if (!_oldConnection) {
			// 8kb / sec, so 512 kb give 64 sec
			auto remainBySize = size * _waitForReceived / 8192;
			remain = std::clamp(
				remainBySize,
				remain,
				uint64(kMaxReceiveTimeout));
			if (remain != _waitForReceived) {
				DEBUG_LOG(("Checking connect for request with size %1 bytes, delay will be %2").arg(size).arg(remain));
			}
		}
		if (isUploadDcId(_shiftedDcId)) {
			remain *= kUploadSessionsCount;
		}
		_waitForReceivedTimer.callOnce(remain);
	}
	if (!_firstSentAt) {
		_firstSentAt = crl::now();
	}
}

void SessionPrivate::onReceivedSome() {
	if (_oldConnection) {
		_oldConnection = false;
		DEBUG_LOG(("This connection marked as not old!"));
	}
	_oldConnectionTimer.callOnce(kMarkConnectionOldTimeout);
	_waitForReceivedTimer.cancel();
	if (_firstSentAt > 0) {
		const auto ms = crl::now() - _firstSentAt;
		DEBUG_LOG(("MTP Info: response in %1ms, _waitForReceived: %2ms"
			).arg(ms
			).arg(_waitForReceived));

		if (ms > 0 && ms * 2 < _waitForReceived) {
			_waitForReceived = qMax(ms * 2, kMinReceiveTimeout);
		}
		_firstSentAt = -1;
	}
}

void SessionPrivate::markConnectionOld() {
	_oldConnection = true;
	_waitForReceived = kMinReceiveTimeout;
	DEBUG_LOG(("This connection marked as old! _waitForReceived now %1ms"
		).arg(_waitForReceived));
}

void SessionPrivate::sendPingByTimer() {
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

void SessionPrivate::sendPingForce() {
	DEBUG_LOG(("MTP Info: send ping force for dcWithShift %1.").arg(_shiftedDcId));
	if (!_pingId) {
		_pingSendAt = 0;
		DEBUG_LOG(("Will send ping!"));
		tryToSend();
	}
}

void SessionPrivate::waitReceivedFailed() {
	Expects(_options != nullptr);

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

	const auto instance = _instance;
	const auto shiftedDcId = _shiftedDcId;
	InvokeQueued(instance, [=] {
		instance->restartedByTimeout(shiftedDcId);
	});
}

void SessionPrivate::waitConnectedFailed() {
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

void SessionPrivate::waitBetterFailed() {
	confirmBestConnection();
}

void SessionPrivate::connectingTimedOut() {
	for (const auto &connection : _testConnections) {
		connection.data->timedOut();
	}
	doDisconnect();
}

void SessionPrivate::doDisconnect() {
	destroyAllConnections();
	setState(DisconnectedState);
}

void SessionPrivate::requestCDNConfig() {
	InvokeQueued(_instance, [instance = _instance] {
		instance->requestCDNConfig();
	});
}

void SessionPrivate::handleReceived() {
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
		if (ConstTimeIsDifferent(&msgKey, sha1ForMsgKeyCheck.data() + kMsgKeyShift_oldmtp, sizeof(msgKey))) {
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
		if (ConstTimeIsDifferent(&msgKey, sha256Buffer.data() + kMsgKeyShift, sizeof(msgKey))) {
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
					resendAll();
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
		MTP_LOG(_shiftedDcId, ("Recv: ")
			+ DumpToText(sfrom, end)
			+ QString(" (protocolDcId:%1,key:%2)"
			).arg(getProtocolDcId()
			).arg(_encryptionKey->keyId()));

		if (_receivedMessageIds.registerMsgId(msgId, needAck)) {
			res = handleOneReceived(from, end, msgId, {
				.outerMsgId = msgId,
				.serverSalt = serverSalt,
				.serverTime = serverTime,
				.badTime = badTime,
			});
		}
		_receivedMessageIds.shrink();

		// send acks
		if (const auto toAckSize = _ackRequestData.size()) {
			DEBUG_LOG(("MTP Info: will send %1 acks, ids: %2").arg(toAckSize).arg(LogIdsVector(_ackRequestData)));
			_sessionData->queueSendAnything(kAckSendWaiting);
		}

		auto lock = QReadLocker(_sessionData->haveReceivedMutex());
		const auto tryToReceive = !_sessionData->haveReceivedMessages().empty();
		lock.unlock();

		if (tryToReceive) {
			DEBUG_LOG(("MTP Info: queueTryToReceive() - need to parse in another thread, %1 messages.").arg(_sessionData->haveReceivedMessages().size()));
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

SessionPrivate::HandleResult SessionPrivate::handleOneReceived(
		const mtpPrime *from,
		const mtpPrime *end,
		uint64 msgId,
		OuterInfo info) {
	Expects(from < end);

	switch (mtpTypeId(*from)) {

	case mtpc_gzip_packed: {
		DEBUG_LOG(("Message Info: gzip container"));
		mtpBuffer response = ungzip(++from, end);
		if (response.empty()) {
			return HandleResult::RestartConnection;
		}
		return handleOneReceived(response.data(), response.data() + response.size(), msgId, info);
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
				res = handleOneReceived(from, otherEnd, inMsgId.v, info);
				info.badTime = false;
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
		const auto &ids = msg.c_msgs_ack().vmsg_ids().v;
		DEBUG_LOG(("Message Info: acks received, ids: %1"
			).arg(LogIdsVector(ids)));
		if (ids.isEmpty()) {
			return info.badTime ? HandleResult::Ignored : HandleResult::Success;
		}

		if (info.badTime) {
			if (!requestsFixTimeSalt(ids, info)) {
				return HandleResult::Ignored;
			}
		} else {
			correctUnixtimeByFastRequest(ids, info.serverTime);
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

		const auto resendId = data.vbad_msg_id().v;
		const auto errorCode = data.verror_code().v;
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
					const auto i = _sentContainers.find(resendId);
					if (i == _sentContainers.end()) {
						LOG(("Message Error: Container not found!"));
					} else {
						auto idsList = QStringList();
						for (const auto innerMsgId : i->second.messages) {
							idsList.push_back(QString::number(innerMsgId));
						}
						LOG(("Message Info: bad container received! messages: %1").arg(idsList.join(',')));
					}
				}
			}

			if (!wasSent(resendId)) {
				DEBUG_LOG(("Message Error: "
					"such message was not sent recently %1").arg(resendId));
				return info.badTime
					? HandleResult::Ignored
					: HandleResult::Success;
			}

			if (needResend) { // bad msg_id or bad container
				if (info.serverSalt) {
					_sessionSalt = info.serverSalt;
				}

				correctUnixtimeWithBadLocal(info.serverTime);

				DEBUG_LOG(("Message Info: unixtime updated, now %1, resending in container...").arg(info.serverTime));

				resend(resendId, 0, true);
			} else { // must create new session, because msg_id and msg_seqno are inconsistent
				if (info.badTime) {
					if (info.serverSalt) {
						_sessionSalt = info.serverSalt;
					}
					correctUnixtimeWithBadLocal(info.serverTime);
					info.badTime = false;
				}
				LOG(("Message Info: bad message notification received, msgId %1, error_code %2").arg(data.vbad_msg_id().v).arg(errorCode));
				return HandleResult::ResetSession;
			}
		} else { // fatal (except 48, but it must not get here)
			const auto badMsgId = mtpMsgId(data.vbad_msg_id().v);
			const auto requestId = wasSent(resendId);
			if (requestId) {
				LOG(("Message Error: "
					"fatal bad message notification received, "
					"msgId %1, error_code %2, requestId: %3"
					).arg(badMsgId
					).arg(errorCode
					).arg(requestId));
				auto reply = mtpBuffer();
				MTPRpcError(MTP_rpc_error(
					MTP_int(500),
					MTP_string("PROTOCOL_ERROR")
				)).write(reply);

				// Save rpc_error for processing in the main thread.
				QWriteLocker locker(_sessionData->haveReceivedMutex());
				_sessionData->haveReceivedMessages().push_back({
					.reply = std::move(reply),
					.outerMsgId = info.outerMsgId,
					.requestId = requestId,
				});
			} else {
				DEBUG_LOG(("Message Error: "
					"such message was not sent recently %1").arg(badMsgId));
			}
			return info.badTime
				? HandleResult::Ignored
				: HandleResult::Success;
		}
	} return HandleResult::Success;

	case mtpc_bad_server_salt: {
		MTPBadMsgNotification msg;
		if (!msg.read(from, end)) {
			return HandleResult::ParseError;
		}
		const auto &data = msg.c_bad_server_salt();
		DEBUG_LOG(("Message Info: bad server salt received (error_code %4) for msg_id = %1, seq_no = %2, new salt: %3").arg(data.vbad_msg_id().v).arg(data.vbad_msg_seqno().v).arg(data.vnew_server_salt().v).arg(data.verror_code().v));

		const auto resendId = data.vbad_msg_id().v;
		if (!wasSent(resendId)) {
			DEBUG_LOG(("Message Error: such message was not sent recently %1").arg(resendId));
			return (info.badTime ? HandleResult::Ignored : HandleResult::Success);
		}

		_sessionSalt = data.vnew_server_salt().v;
		correctUnixtimeWithBadLocal(info.serverTime);

		if (setState(ConnectedState, ConnectingState)) {
			resendAll();
		}

		info.badTime = false;

		DEBUG_LOG(("Message Info: unixtime updated, now %1, server_salt updated, now %2, resending...").arg(info.serverTime).arg(info.serverSalt));
		resend(resendId);
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
		const auto i = _stateAndResendRequests.find(reqMsgId);
		if (i == _stateAndResendRequests.end()) {
			DEBUG_LOG(("Message Error: such message was not sent recently %1").arg(reqMsgId));
			return info.badTime
				? HandleResult::Ignored
				: HandleResult::Success;
		}
		if (info.badTime) {
			if (info.serverSalt) {
				_sessionSalt = info.serverSalt; // requestsFixTimeSalt with no lookup
			}
			correctUnixtimeWithBadLocal(info.serverTime);

			DEBUG_LOG(("Message Info: unixtime updated from mtpc_msgs_state_info, now %1").arg(info.serverTime));

			info.badTime = false;
		}
		const auto originalRequest = i->second;
		Assert(originalRequest->size() > 8);

		requestsAcked(QVector<MTPlong>(1, MTP_long(reqMsgId)), true);

		auto rFrom = originalRequest->constData() + 8;
		const auto rEnd = originalRequest->constData() + originalRequest->size();
		if (mtpTypeId(*rFrom) == mtpc_msgs_state_req) {
			MTPMsgsStateReq request;
			if (!request.read(rFrom, rEnd)) {
				LOG(("Message Error: could not parse sent msgs_state_req"));
				return HandleResult::ParseError;
			}
			handleMsgsStates(request.c_msgs_state_req().vmsg_ids().v, states);
		} else {
			MTPMsgResendReq request;
			if (!request.read(rFrom, rEnd)) {
				LOG(("Message Error: could not parse sent msgs_resend_req"));
				return HandleResult::ParseError;
			}
			handleMsgsStates(request.c_msg_resend_req().vmsg_ids().v, states);
		}
	} return HandleResult::Success;

	case mtpc_msgs_all_info: {
		if (info.badTime) {
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

		DEBUG_LOG(("Message Info: msgs all info received, msgId %1, reqMsgIds: %2, states %3").arg(
			QString::number(msgId),
			LogIdsVector(ids),
			Logs::mb(states.data(), states.length()).str()));

		handleMsgsStates(ids, states);
	} return HandleResult::Success;

	case mtpc_msg_detailed_info: {
		MTPMsgDetailedInfo msg;
		if (!msg.read(from, end)) {
			return HandleResult::ParseError;
		}
		const auto &data(msg.c_msg_detailed_info());

		DEBUG_LOG(("Message Info: msg detailed info, sent msgId %1, answerId %2, status %3, bytes %4").arg(data.vmsg_id().v).arg(data.vanswer_msg_id().v).arg(data.vstatus().v).arg(data.vbytes().v));

		QVector<MTPlong> ids(1, data.vmsg_id());
		if (info.badTime) {
			if (requestsFixTimeSalt(ids, info)) {
				info.badTime = false;
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
		if (info.badTime) {
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
		auto response = mtpBuffer();

		MTPlong reqMsgId;
		if (!reqMsgId.read(++from, end)) {
			return HandleResult::ParseError;
		}
		const auto requestMsgId = reqMsgId.v;

		DEBUG_LOG(("RPC Info: response received for %1, queueing...").arg(requestMsgId));

		QVector<MTPlong> ids(1, reqMsgId);
		if (info.badTime) {
			if (requestsFixTimeSalt(ids, info)) {
				info.badTime = false;
			} else {
				DEBUG_LOG(("Message Info: error, such message was not sent recently %1").arg(requestMsgId));
				return HandleResult::Ignored;
			}
		}

		mtpTypeId typeId = from[0];
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
			_sessionData->notifyConnectionInited(*_options);
		}
		requestsAcked(ids, true);

		const auto bindResult = handleBindResponse(requestMsgId, response);
		if (bindResult != HandleResult::Ignored) {
			return bindResult;
		}
		const auto requestId = wasSent(requestMsgId);
		if (requestId && requestId != mtpRequestId(0xFFFFFFFF)) {
			// Save rpc_result for processing in the main thread.
			QWriteLocker locker(_sessionData->haveReceivedMutex());
			_sessionData->haveReceivedMessages().push_back({
				.reply = std::move(response),
				.outerMsgId = info.outerMsgId,
				.requestId = requestId,
			});
		} else {
			DEBUG_LOG(("RPC Info: requestId not found for msgId %1").arg(requestMsgId));
		}
	} return HandleResult::Success;

	case mtpc_new_session_created: {
		const mtpPrime *start = from;
		MTPNewSession msg;
		if (!msg.read(from, end)) {
			return HandleResult::ParseError;
		}
		const auto &data(msg.c_new_session_created());

		if (info.badTime) {
			if (requestsFixTimeSalt(QVector<MTPlong>(1, data.vfirst_msg_id()), info)) {
				info.badTime = false;
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
			for (const auto &[msgId, request] : haveSent) {
				if (msgId >= firstMsgId) {
					break;
				} else if (request->requestId) {
					toResend.push_back(msgId);
				}
			}
		}
		for (const auto msgId : toResend) {
			resend(msgId, 10, true);
		}

		mtpBuffer update(from - start);
		if (from > start) memcpy(update.data(), start, (from - start) * sizeof(mtpPrime));

		// Notify main process about new session - need to get difference.
		QWriteLocker locker(_sessionData->haveReceivedMutex());
		_sessionData->haveReceivedMessages().push_back({
			.reply = update,
			.outerMsgId = info.outerMsgId,
		});
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
		if (info.badTime) {
			if (requestsFixTimeSalt(ids, info)) {
				info.badTime = false;
			} else {
				return HandleResult::Ignored;
			}
		}
		requestsAcked(ids, true);
	} return HandleResult::Success;

	}

	if (info.badTime) {
		DEBUG_LOG(("Message Error: bad time in updates cons, must create new session"));
		return HandleResult::ResetSession;
	}

	if (_currentDcType == DcType::Regular) {
		mtpBuffer update(end - from);
		if (end > from) {
			memcpy(update.data(), from, (end - from) * sizeof(mtpPrime));
		}

		// Notify main process about the new updates.
		QWriteLocker locker(_sessionData->haveReceivedMutex());
		_sessionData->haveReceivedMessages().push_back({
			.reply = update,
			.outerMsgId = info.outerMsgId,
		});
	} else {
		LOG(("Message Error: unexpected updates in dcType: %1"
			).arg(static_cast<int>(_currentDcType)));
	}

	return HandleResult::Success;
}

SessionPrivate::HandleResult SessionPrivate::handleBindResponse(
		mtpMsgId requestMsgId,
		const mtpBuffer &response) {
	if (!_keyCreator || !_bindMsgId || _bindMsgId != requestMsgId) {
		return HandleResult::Ignored;
	}
	_bindMsgId = 0;

	const auto result = _keyCreator->handleBindResponse(response);
	switch (result) {
	case DcKeyBindState::Success:
		if (!_sessionData->releaseKeyCreationOnDone(
			_encryptionKey,
			base::take(_keyCreator)->bindPersistentKey())) {
			return HandleResult::DestroyTemporaryKey;
		}
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
	Unexpected("Result of BoundKeyCreator::handleBindResponse.");
}

mtpBuffer SessionPrivate::ungzip(const mtpPrime *from, const mtpPrime *end) const {
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

bool SessionPrivate::requestsFixTimeSalt(const QVector<MTPlong> &ids, const OuterInfo &info) {
	for (const auto &id : ids) {
		if (wasSent(id.v)) {
			// Found such msg_id in recent acked or in recent sent requests.
			if (info.serverSalt) {
				_sessionSalt = info.serverSalt;
			}
			correctUnixtimeWithBadLocal(info.serverTime);
			return true;
		}
	}
	return false;
}

void SessionPrivate::correctUnixtimeByFastRequest(
		const QVector<MTPlong> &ids,
		TimeId serverTime) {
	const auto now = crl::now();

	QReadLocker locker(_sessionData->haveSentMutex());
	const auto &haveSent = _sessionData->haveSentMap();
	for (const auto &id : ids) {
		const auto i = haveSent.find(id.v);
		if (i == haveSent.end()) {
			continue;
		}
		const auto duration = (now - i->second->lastSentTime);
		if (duration < 0 || duration > SyncTimeRequestDuration) {
			continue;
		}
		locker.unlock();

		SyncTimeRequestDuration = duration;
		base::unixtime::update(serverTime, true);
		return;
	}
}

void SessionPrivate::correctUnixtimeWithBadLocal(TimeId serverTime) {
	SyncTimeRequestDuration = kFastRequestDuration;
	base::unixtime::update(serverTime, true);
}

void SessionPrivate::requestsAcked(const QVector<MTPlong> &ids, bool byResponse) {
	uint32 idsCount = ids.size();

	DEBUG_LOG(("Message Info: requests acked, ids %1").arg(LogIdsVector(ids)));

	QVector<MTPlong> toAckMore;
	{
		QWriteLocker locker2(_sessionData->haveSentMutex());
		auto &haveSent = _sessionData->haveSentMap();

		for (const auto &wrappedMsgId : ids) {
			const auto msgId = wrappedMsgId.v;
			if (const auto i = _sentContainers.find(msgId); i != end(_sentContainers)) {
				DEBUG_LOG(("Message Info: container ack received, msgId %1").arg(msgId));
				const auto &list = i->second.messages;
				toAckMore.reserve(toAckMore.size() + list.size());
				for (const auto msgId : list) {
					toAckMore.push_back(MTP_long(msgId));
				}
				_sentContainers.erase(i);
				continue;
			}
			if (const auto i = _stateAndResendRequests.find(msgId); i != end(_stateAndResendRequests)) {
				_stateAndResendRequests.erase(i);
				continue;
			}
			if (const auto i = haveSent.find(msgId); i != end(haveSent)) {
				const auto requestId = i->second->requestId;

				if (!byResponse && _instance->hasCallback(requestId)) {
					DEBUG_LOG(("Message Info: ignoring ACK for msgId %1 because request %2 requires a response").arg(msgId).arg(requestId));
					continue;
				}
				haveSent.erase(i);

				_ackedIds.emplace(msgId, requestId);
				continue;
			}
			DEBUG_LOG(("Message Info: msgId %1 was not found in recent sent, while acking requests, searching in resend...").arg(msgId));
			if (const auto i = _resendingIds.find(msgId); i != end(_resendingIds)) {
				const auto requestId = i->second;

				if (!byResponse && _instance->hasCallback(requestId)) {
					DEBUG_LOG(("Message Info: ignoring ACK for msgId %1 because request %2 requires a response").arg(msgId).arg(requestId));
					continue;
				}
				_resendingIds.erase(i);

				QWriteLocker locker4(_sessionData->toSendMutex());
				auto &toSend = _sessionData->toSendMap();
				const auto j = toSend.find(requestId);
				if (j == end(toSend)) {
					DEBUG_LOG(("Message Info: msgId %1 was found in recent resent, requestId %2 was not found in prepared to send").arg(msgId).arg(requestId));
					continue;
				}
				if (j->second->requestId != requestId) {
					DEBUG_LOG(("Message Error: for msgId %1 found resent request, requestId %2, contains requestId %3").arg(msgId).arg(requestId).arg(j->second->requestId));
				} else {
					DEBUG_LOG(("Message Info: acked msgId %1 that was prepared to resend, requestId %2").arg(msgId).arg(requestId));
				}

				_ackedIds.emplace(msgId, j->second->requestId);

				toSend.erase(j);
				continue;
			}
			DEBUG_LOG(("Message Info: msgId %1 was not found in recent resent either").arg(msgId));
		}
	}

	auto ackedCount = _ackedIds.size();
	if (ackedCount > kIdsBufferSize) {
		DEBUG_LOG(("Message Info: removing some old acked sent msgIds %1").arg(ackedCount - kIdsBufferSize));
		while (ackedCount-- > kIdsBufferSize) {
			_ackedIds.erase(_ackedIds.begin());
		}
	}

	if (toAckMore.size()) {
		requestsAcked(toAckMore);
	}
}

void SessionPrivate::handleMsgsStates(const QVector<MTPlong> &ids, const QByteArray &states) {
	const auto idsCount = ids.size();
	if (!idsCount) {
		DEBUG_LOG(("Message Info: void ids vector in handleMsgsStates()"));
		return;
	}
	if (states.size() != idsCount) {
		LOG(("Message Error: got less states than required ids count."));
		return;
	}

	auto acked = QVector<MTPlong>();
	acked.reserve(idsCount);
	for (auto i = 0; i != idsCount; ++i) {
		const auto state = states[i];
		const auto requestMsgId = ids[i].v;
		{
			QReadLocker locker(_sessionData->haveSentMutex());
			if (!_sessionData->haveSentMap().contains(requestMsgId)) {
				DEBUG_LOG(("Message Info: state was received for msgId %1, but request is not found, looking in resent requests...").arg(requestMsgId));
				const auto reqIt = _resendingIds.find(requestMsgId);
				if (reqIt != _resendingIds.cend()) {
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
	requestsAcked(acked);
}

void SessionPrivate::clearSpecialMsgId(mtpMsgId msgId) {
	if (msgId == _pingMsgId) {
		_pingMsgId = 0;
		_pingId = 0;
	} else if (msgId == _bindMsgId) {
		_bindMsgId = 0;
	}
}

void SessionPrivate::resend(
		mtpMsgId msgId,
		crl::time msCanWait,
		bool forceContainer) {
	const auto guard = gsl::finally([&] {
		clearSpecialMsgId(msgId);
		if (msCanWait >= 0) {
			_sessionData->queueSendAnything(msCanWait);
		}
	});

	if (const auto i = _sentContainers.find(msgId); i != end(_sentContainers)) {
		DEBUG_LOG(("Message Info: resending container, msgId %1").arg(msgId));
		const auto ids = std::move(i->second.messages);
		_sentContainers.erase(i);

		for (const auto innerMsgId : ids) {
			resend(innerMsgId, -1, true);
		}
		return;
	}
	auto lock = QWriteLocker(_sessionData->haveSentMutex());
	auto &haveSent = _sessionData->haveSentMap();
	auto i = haveSent.find(msgId);
	if (i == haveSent.end()) {
		return;
	}
	auto request = i->second;
	haveSent.erase(i);
	lock.unlock();

	request->lastSentTime = crl::now();
	request->forceSendInContainer = forceContainer;
	_resendingIds.emplace(msgId, request->requestId);
	{
		QWriteLocker locker(_sessionData->toSendMutex());
		_sessionData->toSendMap().emplace(request->requestId, request);
	}
}

void SessionPrivate::resendAll() {
	auto lock = QWriteLocker(_sessionData->haveSentMutex());
	auto haveSent = base::take(_sessionData->haveSentMap());
	lock.unlock();
	{
		auto lock = QWriteLocker(_sessionData->toSendMutex());
		auto &toSend = _sessionData->toSendMap();
		const auto now = crl::now();
		for (auto &[msgId, request] : haveSent) {
			const auto requestId = request->requestId;
			request->lastSentTime = now;
			request->forceSendInContainer = true;
			_resendingIds.emplace(msgId, requestId);
			toSend.emplace(requestId, std::move(request));
		}
	}

	_sessionData->queueSendAnything();
}

void SessionPrivate::onConnected(
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
		DEBUG_LOG(("MTP Info: connection %1 succeed, waiting for %2.").arg(
			i->data->tag(),
			j->data->tag()));
		_waitForBetterTimer.callOnce(kWaitForBetterTimeout);
	} else {
		DEBUG_LOG(("MTP Info: connection through IPv4 succeed."));
		_waitForBetterTimer.cancel();
		_connection = std::move(i->data);
		_testConnections.clear();
		checkAuthKey();
	}
}

void SessionPrivate::onDisconnected(
		not_null<AbstractConnection*> connection) {
	removeTestConnection(connection);

	if (_testConnections.empty()) {
		destroyAllConnections();
		restart();
	} else {
		confirmBestConnection();
	}
}

void SessionPrivate::confirmBestConnection() {
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

void SessionPrivate::removeTestConnection(
		not_null<AbstractConnection*> connection) {
	_testConnections.erase(
		ranges::remove(
			_testConnections,
			connection.get(),
			[](const TestConnection &test) { return test.data.get(); }),
		end(_testConnections));
}

void SessionPrivate::checkAuthKey() {
	if (_keyId) {
		authKeyChecked();
	} else if (_instance->isKeysDestroyer()) {
		applyAuthKey(_sessionData->getPersistentKey());
	} else {
		applyAuthKey(_sessionData->getTemporaryKey(
			TemporaryKeyTypeByDcType(_currentDcType)));
	}
}

void SessionPrivate::updateAuthKey() {
	if (_instance->isKeysDestroyer() || _keyCreator || !_connection) {
		return;
	}

	DEBUG_LOG(("AuthKey Info: Connection updating key from Session, dc %1"
		).arg(_shiftedDcId));
	applyAuthKey(_sessionData->getTemporaryKey(
		TemporaryKeyTypeByDcType(_currentDcType)));
}

void SessionPrivate::setCurrentKeyId(uint64 newKeyId) {
	if (_keyId == newKeyId) {
		return;
	}
	_keyId = newKeyId;

	DEBUG_LOG(("MTP Info: auth key id set to id %1").arg(newKeyId));
	changeSessionId();
}

void SessionPrivate::applyAuthKey(AuthKeyPtr &&encryptionKey) {
	_encryptionKey = std::move(encryptionKey);
	const auto newKeyId = _encryptionKey ? _encryptionKey->keyId() : 0;
	if (_keyId) {
		if (_keyId == newKeyId) {
			return;
		}
		setCurrentKeyId(0);
		DEBUG_LOG(("MTP Info: auth_key id for dc %1 changed, restarting..."
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

	DEBUG_LOG(("AuthKey Info: Connection update key from Session, "
		"dc %1 result: %2"
		).arg(_shiftedDcId
		).arg(Logs::mb(&_keyId, sizeof(_keyId)).str()));
	if (_keyId) {
		return authKeyChecked();
	}

	if (_instance->isKeysDestroyer()) {
		// We are here to destroy an old key, so we're done.
		LOG(("MTP Error: No key %1 in updateAuthKey() for destroying."
			).arg(_shiftedDcId));
		_instance->keyWasPossiblyDestroyed(_shiftedDcId);
	} else if (noMediaKeyWithExistingRegularKey()) {
		DEBUG_LOG(("AuthKey Info: No key in updateAuthKey() for media, "
			"but someone has created regular, trying to acquire."));
		const auto dcType = tryAcquireKeyCreation();
		if (_keyCreator && dcType != _currentDcType) {
			DEBUG_LOG(("AuthKey Info: "
				"Dc type changed for creation, restarting."));
			restart();
			return;
		}
	}
	if (_keyCreator) {
		DEBUG_LOG(("AuthKey Info: No key in updateAuthKey(), creating."));
		_keyCreator->start(
			BareDcId(_shiftedDcId),
			getProtocolDcId(),
			_connection.get(),
			&_instance->dcOptions());
	} else {
		DEBUG_LOG(("AuthKey Info: No key in updateAuthKey(), "
			"but someone is creating already, waiting."));
	}
}

bool SessionPrivate::noMediaKeyWithExistingRegularKey() const {
	return (TemporaryKeyTypeByDcType(_currentDcType)
			== TemporaryKeyType::MediaCluster)
		&& _sessionData->getTemporaryKey(TemporaryKeyType::Regular);
}

bool SessionPrivate::destroyOldEnoughPersistentKey() {
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

DcType SessionPrivate::tryAcquireKeyCreation() {
	if (_keyCreator) {
		return _currentDcType;
	} else if (_instance->isKeysDestroyer()) {
		return _realDcType;
	}

	const auto acquired = _sessionData->acquireKeyCreation(_realDcType);
	if (acquired == CreatingKeyType::None) {
		return _realDcType;
	}

	using Result = DcKeyResult;
	using Error = DcKeyError;
	auto delegate = BoundKeyCreator::Delegate();
	delegate.unboundReady = [=](base::expected<Result, Error> result) {
		if (!result) {
			releaseKeyCreationOnFail();
			if (result.error() == Error::UnknownPublicKey) {
				if (_realDcType == DcType::Cdn) {
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
		result->temporaryKey->setExpiresAt(base::unixtime::now()
			+ kTemporaryExpiresIn
			+ kBindKeyAdditionalExpiresTimeout);
		if (_realDcType != DcType::Cdn) {
			auto key = result->persistentKey
				? std::move(result->persistentKey)
				: _sessionData->getPersistentKey();
			if (!key) {
				releaseKeyCreationOnFail();
				restart();
				return;
			}
			_keyCreator->bind(std::move(key));
		}
		applyAuthKey(std::move(result->temporaryKey));
		if (_realDcType == DcType::Cdn) {
			_keyCreator = nullptr;
			if (!_sessionData->releaseCdnKeyCreationOnDone(_encryptionKey)) {
				restart();
			} else {
				_sessionData->queueNeedToResumeAndSend();
			}
		}
	};
	delegate.sentSome = [=](uint64 size) {
		onSentSome(size);
	};
	delegate.receivedSome = [=] {
		onReceivedSome();
	};

	auto request = DcKeyRequest();
	request.persistentNeeded = (acquired == CreatingKeyType::Persistent);
	request.temporaryExpiresIn = kTemporaryExpiresIn;
	_keyCreator = std::make_unique<BoundKeyCreator>(
		request,
		std::move(delegate));
	const auto forceUseRegular = (_realDcType == DcType::MediaCluster)
		&& (acquired != CreatingKeyType::TemporaryMediaCluster);
	return forceUseRegular ? DcType::Regular : _realDcType;
}

void SessionPrivate::authKeyChecked() {
	connect(_connection, &AbstractConnection::receivedData, [=] {
		handleReceived();
	});

	if (_sessionSalt && setState(ConnectedState)) {
		resendAll();
	} // else receive salt in bad_server_salt first, then try to send all the requests

	_pingIdToSend = openssl::RandomValue<uint64>(); // get server_salt
	_sessionData->queueNeedToResumeAndSend();
}

void SessionPrivate::onError(
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

void SessionPrivate::handleError(int errorCode) {
	destroyAllConnections();
	_waitForConnectedTimer.cancel();

	if (errorCode == -404) {
		destroyTemporaryKey();
	} else {
		MTP_LOG(_shiftedDcId, ("Restarting after error in connection, error code: %1...").arg(errorCode));
		return restart();
	}
}

void SessionPrivate::destroyTemporaryKey() {
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

bool SessionPrivate::sendSecureRequest(
		SerializedRequest &&request,
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
	MTP_LOG(_shiftedDcId, ("Send: ")
		+ DumpToText(from, from + messageSize)
		+ QString(" (protocolDcId:%1,key:%2)"
		).arg(getProtocolDcId()
		).arg(_encryptionKey->keyId()));

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

mtpRequestId SessionPrivate::wasSent(mtpMsgId msgId) const {
	if (msgId == _pingMsgId || msgId == _bindMsgId) {
		return mtpRequestId(0xFFFFFFFF);
	}
	if (const auto i = _resendingIds.find(msgId); i != end(_resendingIds)) {
		return i->second;
	}
	if (const auto i = _ackedIds.find(msgId); i != end(_ackedIds)) {
		return i->second;
	}
	if (const auto i = _sentContainers.find(msgId); i != end(_sentContainers)) {
		return mtpRequestId(0xFFFFFFFF);
	}

	{
		QReadLocker locker(_sessionData->haveSentMutex());
		const auto &haveSent = _sessionData->haveSentMap();
		const auto i = haveSent.find(msgId);
		if (i != haveSent.end()) {
			return i->second->requestId
				? i->second->requestId
				: mtpRequestId(0xFFFFFFFF);
		}
	}
	return 0;
}

void SessionPrivate::clearUnboundKeyCreator() {
	if (_keyCreator) {
		_keyCreator->stop();
	}
}

void SessionPrivate::releaseKeyCreationOnFail() {
	if (!_keyCreator) {
		return;
	}
	_keyCreator = nullptr;
	_sessionData->releaseKeyCreationOnFail();
}

} // namespace details
} // namespace MTP
