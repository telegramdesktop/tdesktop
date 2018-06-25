/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/session.h"

#include "mtproto/connection.h"
#include "mtproto/dcenter.h"
#include "mtproto/auth_key.h"
#include "core/crash_reports.h"

namespace MTP {
namespace internal {
namespace {

QString LogIds(const QVector<uint64> &ids) {
	if (!ids.size()) return "[]";
	auto idsStr = QString("[%1").arg(*ids.cbegin());
	for (const auto id : ids) {
		idsStr += QString(", %2").arg(id);
	}
	return idsStr + "]";
}

} // namespace

ConnectionOptions::ConnectionOptions(
	const QString &systemLangCode,
	const QString &cloudLangCode,
	const ProxyData &proxy,
	bool useIPv4,
	bool useIPv6,
	bool useHttp,
	bool useTcp)
: systemLangCode(systemLangCode)
, cloudLangCode(cloudLangCode)
, proxy(proxy)
, useIPv4(useIPv4)
, useIPv6(useIPv6)
, useHttp(useHttp)
, useTcp(useTcp) {
}

void SessionData::setKey(const AuthKeyPtr &key) {
	if (_authKey != key) {
		uint64 session = rand_value<uint64>();
		_authKey = key;

		DEBUG_LOG(("MTP Info: new auth key set in SessionData, id %1, setting random server_session %2").arg(key ? key->keyId() : 0).arg(session));
		QWriteLocker locker(&_lock);
		if (_session != session) {
			_session = session;
			_messagesSent = 0;
		}
		_layerInited = false;
	}
}

void SessionData::notifyConnectionInited(const ConnectionOptions &options) {
	QWriteLocker locker(&_lock);
	if (options.cloudLangCode == _options.cloudLangCode
		&& options.systemLangCode == _options.systemLangCode
		&& options.proxy == _options.proxy
		&& !_options.inited) {
		_options.inited = true;

		locker.unlock();
		owner()->notifyDcConnectionInited();
	}
}

void SessionData::clear(Instance *instance) {
	auto clearCallbacks = std::vector<RPCCallbackClear>();
	{
		QReadLocker locker1(haveSentMutex()), locker2(toResendMutex()), locker3(haveReceivedMutex()), locker4(wereAckedMutex());
		auto receivedResponsesEnd = _receivedResponses.cend();
		clearCallbacks.reserve(_haveSent.size() + _wereAcked.size());
		for (auto i = _haveSent.cbegin(), e = _haveSent.cend(); i != e; ++i) {
			auto requestId = i.value()->requestId;
			if (!_receivedResponses.contains(requestId)) {
				clearCallbacks.push_back(requestId);
			}
		}
		for (auto i = _toResend.cbegin(), e = _toResend.cend(); i != e; ++i) {
			auto requestId = i.value();
			if (!_receivedResponses.contains(requestId)) {
				clearCallbacks.push_back(requestId);
			}
		}
		for (auto i = _wereAcked.cbegin(), e = _wereAcked.cend(); i != e; ++i) {
			auto requestId = i.value();
			if (!_receivedResponses.contains(requestId)) {
				clearCallbacks.push_back(requestId);
			}
		}
	}
	{
		QWriteLocker locker(haveSentMutex());
		_haveSent.clear();
	}
	{
		QWriteLocker locker(toResendMutex());
		_toResend.clear();
	}
	{
		QWriteLocker locker(wereAckedMutex());
		_wereAcked.clear();
	}
	{
		QWriteLocker locker(receivedIdsMutex());
		_receivedIds.clear();
	}
	instance->clearCallbacksDelayed(std::move(clearCallbacks));
}

Session::Session(not_null<Instance*> instance, ShiftedDcId shiftedDcId) : QObject()
, _instance(instance)
, data(this)
, dcWithShift(shiftedDcId) {
	connect(&timeouter, SIGNAL(timeout()), this, SLOT(checkRequestsByTimer()));
	timeouter.start(1000);

	refreshOptions();

	connect(&sender, SIGNAL(timeout()), this, SLOT(needToResumeAndSend()));
}

void Session::start() {
	createDcData();
	_connection = std::make_unique<Connection>(_instance);
	_connection->start(&data, dcWithShift);
	if (_instance->isKeysDestroyer()) {
		_instance->scheduleKeyDestroy(dcWithShift);
	}
}

void Session::createDcData() {
	if (dc) {
		return;
	}
	dc = _instance->getDcById(dcWithShift);

	if (auto lock = ReadLockerAttempt(keyMutex())) {
		data.setKey(dc->getKey());
		if (dc->connectionInited()) {
			data.setConnectionInited();
		}
	}
	connect(dc.get(), SIGNAL(authKeyCreated()), this, SLOT(authKeyCreatedForDC()), Qt::QueuedConnection);
	connect(dc.get(), SIGNAL(connectionWasInited()), this, SLOT(connectionWasInitedForDC()), Qt::QueuedConnection);
}

bool Session::rpcErrorOccured(mtpRequestId requestId, const RPCFailHandlerPtr &onFail, const RPCError &err) { // return true if need to clean request data
	return _instance->rpcErrorOccured(requestId, onFail, err);
}

void Session::restart() {
	if (_killed) {
		DEBUG_LOG(("Session Error: can't restart a killed session"));
		return;
	}
	refreshOptions();
	emit needToRestart();
}

void Session::refreshOptions() {
	const auto &proxy = Global::SelectedProxy();
	const auto proxyType = Global::UseProxy()
		? proxy.type
		: ProxyData::Type::None;
	const auto useTcp = (proxyType != ProxyData::Type::Http);
	const auto useHttp = (proxyType != ProxyData::Type::Mtproto);
	const auto useIPv4 = true;
	const auto useIPv6 = Global::TryIPv6();
	data.applyConnectionOptions(ConnectionOptions(
		_instance->systemLangCode(),
		_instance->cloudLangCode(),
		Global::UseProxy() ? proxy : ProxyData(),
		useIPv4,
		useIPv6,
		useHttp,
		useTcp));
}

void Session::reInitConnection() {
	dc->setConnectionInited(false);
	data.setConnectionInited(false);
	restart();
}

void Session::stop() {
	if (_killed) {
		DEBUG_LOG(("Session Error: can't kill a killed session"));
		return;
	}
	DEBUG_LOG(("Session Info: stopping session dcWithShift %1").arg(dcWithShift));
	if (_connection) {
		_connection->kill();
		_instance->queueQuittingConnection(std::move(_connection));
	}
}

void Session::kill() {
	stop();
	_killed = true;
	DEBUG_LOG(("Session Info: marked session dcWithShift %1 as killed").arg(dcWithShift));
}

void Session::unpaused() {
	if (_needToReceive) {
		_needToReceive = false;
		QTimer::singleShot(0, this, SLOT(tryToReceive()));
	}
}

void Session::sendAnything(qint64 msCanWait) {
	if (_killed) {
		DEBUG_LOG(("Session Error: can't send anything in a killed session"));
		return;
	}
	auto ms = getms(true);
	if (msSendCall) {
		if (ms > msSendCall + msWait) {
			msWait = 0;
		} else {
			msWait = (msSendCall + msWait) - ms;
			if (msWait > msCanWait) {
				msWait = msCanWait;
			}
		}
	} else {
		msWait = msCanWait;
	}
	if (msWait) {
		DEBUG_LOG(("MTP Info: dcWithShift %1 can wait for %2ms from current %3").arg(dcWithShift).arg(msWait).arg(msSendCall));
		msSendCall = ms;
		sender.start(msWait);
	} else {
		DEBUG_LOG(("MTP Info: dcWithShift %1 stopped send timer, can wait for %2ms from current %3").arg(dcWithShift).arg(msWait).arg(msSendCall));
		sender.stop();
		msSendCall = 0;
		needToResumeAndSend();
	}
}

void Session::needToResumeAndSend() {
	if (_killed) {
		DEBUG_LOG(("Session Info: can't resume a killed session"));
		return;
	}
	if (!_connection) {
		DEBUG_LOG(("Session Info: resuming session dcWithShift %1").arg(dcWithShift));
		createDcData();
		_connection = std::make_unique<Connection>(_instance);
		_connection->start(&data, dcWithShift);
	}
	if (_ping) {
		_ping = false;
		emit needToPing();
	} else {
		emit needToSend();
	}
}

void Session::sendPong(quint64 msgId, quint64 pingId) {
	_instance->sendProtocolMessage(
		dcWithShift,
		MTPPong(MTP_pong(MTP_long(msgId), MTP_long(pingId))));
}

void Session::sendMsgsStateInfo(quint64 msgId, QByteArray data) {
	auto info = bytes::vector();
	if (!data.isEmpty()) {
		info.resize(data.size());
		bytes::copy(info, bytes::make_span(data));
	}
	_instance->sendProtocolMessage(
		dcWithShift,
		MTPMsgsStateInfo(
			MTP_msgs_state_info(MTP_long(msgId), MTP_bytes(data))));
}

void Session::checkRequestsByTimer() {
	QVector<mtpMsgId> resendingIds;
	QVector<mtpMsgId> removingIds; // remove very old (10 minutes) containers and resend requests
	QVector<mtpMsgId> stateRequestIds;

	{
		QReadLocker locker(data.haveSentMutex());
		auto &haveSent = data.haveSentMap();
		const auto haveSentCount = haveSent.size();
		auto ms = getms(true);
		for (auto i = haveSent.begin(), e = haveSent.end(); i != e; ++i) {
			auto &req = i.value();
			if (req->msDate > 0) {
				if (req->msDate + MTPCheckResendTimeout < ms) { // need to resend or check state
					if (req.messageSize() < MTPResendThreshold) { // resend
						resendingIds.reserve(haveSentCount);
						resendingIds.push_back(i.key());
					} else {
						req->msDate = ms;
						stateRequestIds.reserve(haveSentCount);
						stateRequestIds.push_back(i.key());
					}
				}
			} else if (unixtime() > (int32)(i.key() >> 32) + MTPContainerLives) {
				removingIds.reserve(haveSentCount);
				removingIds.push_back(i.key());
			}
		}
	}

	if (stateRequestIds.size()) {
		DEBUG_LOG(("MTP Info: requesting state of msgs: %1").arg(LogIds(stateRequestIds)));
		{
			QWriteLocker locker(data.stateRequestMutex());
			for (uint32 i = 0, l = stateRequestIds.size(); i < l; ++i) {
				data.stateRequestMap().insert(stateRequestIds[i], true);
			}
		}
		sendAnything(MTPCheckResendWaiting);
	}
	if (!resendingIds.isEmpty()) {
		for (uint32 i = 0, l = resendingIds.size(); i < l; ++i) {
			DEBUG_LOG(("MTP Info: resending request %1").arg(resendingIds[i]));
			resend(resendingIds[i], MTPCheckResendWaiting);
		}
	}
	if (!removingIds.isEmpty()) {
		auto clearCallbacks = std::vector<RPCCallbackClear>();
		{
			QWriteLocker locker(data.haveSentMutex());
			auto &haveSent = data.haveSentMap();
			for (uint32 i = 0, l = removingIds.size(); i < l; ++i) {
				auto j = haveSent.find(removingIds[i]);
				if (j != haveSent.cend()) {
					if (j.value()->requestId) {
						clearCallbacks.push_back(j.value()->requestId);
					}
					haveSent.erase(j);
				}
			}
		}
		_instance->clearCallbacksDelayed(std::move(clearCallbacks));
	}
}

void Session::onConnectionStateChange(qint32 newState) {
	_instance->onStateChange(dcWithShift, newState);
}

void Session::onResetDone() {
	_instance->onSessionReset(dcWithShift);
}

void Session::cancel(mtpRequestId requestId, mtpMsgId msgId) {
	if (requestId) {
		QWriteLocker locker(data.toSendMutex());
		data.toSendMap().remove(requestId);
	}
	if (msgId) {
		QWriteLocker locker(data.haveSentMutex());
		data.haveSentMap().remove(msgId);
	}
}

void Session::ping() {
	_ping = true;
	sendAnything(0);
}

int32 Session::requestState(mtpRequestId requestId) const {
	int32 result = MTP::RequestSent;

	bool connected = false;
	if (_connection) {
		int32 s = _connection->state();
		if (s == ConnectedState) {
			connected = true;
		} else if (s == ConnectingState || s == DisconnectedState) {
			if (result < 0 || result == MTP::RequestSent) {
				result = MTP::RequestConnecting;
			}
		} else if (s < 0) {
			if ((result < 0 && s > result) || result == MTP::RequestSent) {
				result = s;
			}
		}
	}
	if (!connected) {
		return result;
	}
	if (!requestId) return MTP::RequestSent;

	QWriteLocker locker(data.toSendMutex());
	const auto &toSend = data.toSendMap();
	const auto i = toSend.constFind(requestId);
	if (i != toSend.cend()) {
		return MTP::RequestSending;
	} else {
		return MTP::RequestSent;
	}
}

int32 Session::getState() const {
	int32 result = -86400000;

	if (_connection) {
		int32 s = _connection->state();
		if (s == ConnectedState) {
			return s;
		} else if (s == ConnectingState || s == DisconnectedState) {
			if (result < 0) {
				return s;
			}
		} else if (s < 0) {
			if (result < 0 && s > result) {
				result = s;
			}
		}
	}
	if (result == -86400000) {
		result = DisconnectedState;
	}
	return result;
}

QString Session::transport() const {
	return _connection ? _connection->transport() : QString();
}

mtpRequestId Session::resend(quint64 msgId, qint64 msCanWait, bool forceContainer, bool sendMsgStateInfo) {
	SecureRequest request;
	{
		QWriteLocker locker(data.haveSentMutex());
		auto &haveSent = data.haveSentMap();

		auto i = haveSent.find(msgId);
		if (i == haveSent.end()) {
			if (sendMsgStateInfo) {
				char cantResend[2] = {1, 0};
				DEBUG_LOG(("Message Info: cant resend %1, request not found").arg(msgId));

				auto info = std::string(cantResend, cantResend + 1);
				return _instance->sendProtocolMessage(
					dcWithShift,
					MTPMsgsStateInfo(
						MTP_msgs_state_info(
							MTP_long(msgId),
							MTP_string(std::move(info)))));
			}
			return 0;
		}

		request = i.value();
		haveSent.erase(i);
	}
	if (request.isSentContainer()) { // for container just resend all messages we can
		DEBUG_LOG(("Message Info: resending container from haveSent, msgId %1").arg(msgId));
		const mtpMsgId *ids = (const mtpMsgId *)(request->constData() + 8);
		for (uint32 i = 0, l = (request->size() - 8) >> 1; i < l; ++i) {
			resend(ids[i], 10, true);
		}
		return 0xFFFFFFFF;
	} else if (!request.isStateRequest()) {
		request->msDate = forceContainer ? 0 : getms(true);
		sendPrepared(request, msCanWait, false);
		{
			QWriteLocker locker(data.toResendMutex());
			data.toResendMap().insert(msgId, request->requestId);
		}
		return request->requestId;
	} else {
		return 0;
	}
}

void Session::resendMany(QVector<quint64> msgIds, qint64 msCanWait, bool forceContainer, bool sendMsgStateInfo) {
	for (int32 i = 0, l = msgIds.size(); i < l; ++i) {
		resend(msgIds.at(i), msCanWait, forceContainer, sendMsgStateInfo);
	}
}

void Session::resendAll() {
	QVector<mtpMsgId> toResend;
	{
		QReadLocker locker(data.haveSentMutex());
		const auto &haveSent = data.haveSentMap();
		toResend.reserve(haveSent.size());
		for (auto i = haveSent.cbegin(), e = haveSent.cend(); i != e; ++i) {
			if (i.value()->requestId) {
				toResend.push_back(i.key());
			}
		}
	}
	for (uint32 i = 0, l = toResend.size(); i < l; ++i) {
		resend(toResend[i], 10, true);
	}
}

void Session::sendPrepared(
		const SecureRequest &request,
		TimeMs msCanWait,
		bool newRequest) {
	DEBUG_LOG(("MTP Info: adding request to toSendMap, msCanWait %1"
		).arg(msCanWait));
	{
		QWriteLocker locker(data.toSendMutex());
		data.toSendMap().insert(request->requestId, request);

		if (newRequest) {
			*(mtpMsgId*)(request->data() + 4) = 0;
			*(request->data() + 6) = 0;
		}
	}

	DEBUG_LOG(("MTP Info: added, requestId %1").arg(request->requestId));

	sendAnything(msCanWait);
}

QReadWriteLock *Session::keyMutex() const {
	return dc->keyMutex();
}

void Session::authKeyCreatedForDC() {
	DEBUG_LOG(("AuthKey Info: Session::authKeyCreatedForDC slot, emitting authKeyCreated(), dcWithShift %1").arg(dcWithShift));
	data.setKey(dc->getKey());
	emit authKeyCreated();
}

void Session::notifyKeyCreated(AuthKeyPtr &&key) {
	DEBUG_LOG(("AuthKey Info: Session::keyCreated(), setting, dcWithShift %1").arg(dcWithShift));
	dc->setKey(std::move(key));
}

void Session::connectionWasInitedForDC() {
	DEBUG_LOG(("MTP Info: Session::connectionWasInitedForDC slot, dcWithShift %1").arg(dcWithShift));
	data.setConnectionInited();
}

void Session::notifyDcConnectionInited() {
	DEBUG_LOG(("MTP Info: emitting MTProtoDC::connectionWasInited(), dcWithShift %1").arg(dcWithShift));
	dc->setConnectionInited();
	emit dc->connectionWasInited();
}

void Session::destroyKey() {
	if (!dc) return;

	if (data.getKey()) {
		DEBUG_LOG(("MTP Info: destroying auth_key for dcWithShift %1").arg(dcWithShift));
		if (data.getKey() == dc->getKey()) {
			dc->destroyKey();
		}
		data.setKey(AuthKeyPtr());
	}
}

int32 Session::getDcWithShift() const {
	return dcWithShift;
}

void Session::tryToReceive() {
	if (_killed) {
		DEBUG_LOG(("Session Error: can't receive in a killed session"));
		return;
	}
	if (paused()) {
		_needToReceive = true;
		return;
	}
	while (true) {
		auto requestId = mtpRequestId(0);
		auto isUpdate = false;
		auto message = SerializedMessage();
		{
			QWriteLocker locker(data.haveReceivedMutex());
			auto &responses = data.haveReceivedResponses();
			auto response = responses.begin();
			if (response == responses.cend()) {
				auto &updates = data.haveReceivedUpdates();
				auto update = updates.begin();
				if (update == updates.cend()) {
					return;
				} else {
					message = std::move(*update);
					isUpdate = true;
					updates.pop_front();
				}
			} else {
				requestId = response.key();
				message = std::move(response.value());
				responses.erase(response);
			}
		}
		if (isUpdate) {
			if (dcWithShift == BareDcId(dcWithShift)) { // call globalCallback only in main session
				_instance->globalCallback(message.constData(), message.constData() + message.size());
			}
		} else {
			_instance->execCallback(requestId, message.constData(), message.constData() + message.size());
		}
	}
}

Session::~Session() {
	Assert(_connection == nullptr);
}

MTPrpcError rpcClientError(const QString &type, const QString &description) {
	return MTP_rpc_error(MTP_int(0), MTP_string(("CLIENT_" + type + (description.length() ? (": " + description) : "")).toUtf8().constData()));
}

} // namespace internal
} // namespace MTP
