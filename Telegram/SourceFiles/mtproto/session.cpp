/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/session.h"

#include "mtproto/connection.h"
#include "mtproto/dcenter.h"
#include "mtproto/mtproto_auth_key.h"
#include "base/unixtime.h"
#include "base/openssl_help.h"
#include "core/crash_reports.h"
#include "facades.h"

namespace MTP {
namespace internal {

ConnectionOptions::ConnectionOptions(
	const QString &systemLangCode,
	const QString &cloudLangCode,
	const QString &langPackName,
	const ProxyData &proxy,
	bool useIPv4,
	bool useIPv6,
	bool useHttp,
	bool useTcp)
: systemLangCode(systemLangCode)
, cloudLangCode(cloudLangCode)
, langPackName(langPackName)
, proxy(proxy)
, useIPv4(useIPv4)
, useIPv6(useIPv6)
, useHttp(useHttp)
, useTcp(useTcp) {
}

template <typename Callback>
void SessionData::withSession(Callback &&callback) {
	QMutexLocker lock(&_ownerMutex);
	if (const auto session = _owner) {
		InvokeQueued(session, [
			session,
			callback = std::forward<Callback>(callback)
		] {
			callback(session);
		});
	}
}

void SessionData::setKeyForCheck(const AuthKeyPtr &key) {
	_dcKeyForCheck = key;
}

void SessionData::notifyConnectionInited(const ConnectionOptions &options) {
	// #TODO race
	const auto current = connectionOptions();
	if (current.cloudLangCode == _options.cloudLangCode
		&& current.systemLangCode == _options.systemLangCode
		&& current.langPackName == _options.langPackName
		&& current.proxy == _options.proxy) {
		owner()->notifyDcConnectionInited();
	}
}

void SessionData::clearForNewKey(not_null<Instance*> instance) {
	auto clearCallbacks = std::vector<RPCCallbackClear>();
	{
		QReadLocker locker1(haveSentMutex());
		QReadLocker locker2(toResendMutex());
		QReadLocker locker3(haveReceivedMutex());
		QReadLocker locker4(wereAckedMutex());
		clearCallbacks.reserve(_haveSent.size() + _toResend.size() + _wereAcked.size());
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
	instance->clearCallbacksDelayed(std::move(clearCallbacks));
}

void SessionData::queueTryToReceive() {
	withSession([](not_null<Session*> session) {
		session->tryToReceive();
	});
}

void SessionData::queueNeedToResumeAndSend() {
	withSession([](not_null<Session*> session) {
		session->needToResumeAndSend();
	});
}

void SessionData::queueConnectionStateChange(int newState) {
	withSession([=](not_null<Session*> session) {
		session->connectionStateChange(newState);
	});
}

void SessionData::queueResetDone() {
	withSession([](not_null<Session*> session) {
		session->resetDone();
	});
}

void SessionData::queueSendAnything(crl::time msCanWait) {
	withSession([=](not_null<Session*> session) {
		session->sendAnything(msCanWait);
	});
}

void SessionData::queueSendMsgsStateInfo(quint64 msgId, QByteArray data) {
	withSession([=](not_null<Session*> session) {
		session->sendMsgsStateInfo(msgId, data);
	});
}

void SessionData::resend(
		mtpMsgId msgId,
		crl::time msCanWait,
		bool forceContainer) {
	QMutexLocker lock(&_ownerMutex);
	if (_owner) {
		_owner->resend(msgId, msCanWait, forceContainer);
	}
}

void SessionData::resendAll() {
	QMutexLocker lock(&_ownerMutex);
	if (_owner) {
		_owner->resendAll();
	}
}

bool SessionData::connectionInited() const {
	QMutexLocker lock(&_ownerMutex);
	return _owner ? _owner->connectionInited() : false;
}

AuthKeyPtr SessionData::getTemporaryKey() const {
	QMutexLocker lock(&_ownerMutex);
	return _owner ? _owner->getTemporaryKey() : nullptr;
}

AuthKeyPtr SessionData::getPersistentKey() const {
	QMutexLocker lock(&_ownerMutex);
	return _owner ? _owner->getPersistentKey() : nullptr;
}

bool SessionData::acquireKeyCreation() {
	QMutexLocker lock(&_ownerMutex);
	return _owner ? _owner->acquireKeyCreation() : false;
}

void SessionData::releaseKeyCreationOnDone(
		const AuthKeyPtr &temporaryKey,
		const AuthKeyPtr &persistentKey) {
	QMutexLocker lock(&_ownerMutex);
	if (_owner) {
		_owner->releaseKeyCreationOnDone(temporaryKey, persistentKey);
	}
}

void SessionData::releaseKeyCreationOnFail() {
	QMutexLocker lock(&_ownerMutex);
	if (_owner) {
		_owner->releaseKeyCreationOnFail();
	}
}

void SessionData::destroyTemporaryKey(uint64 keyId) {
	QMutexLocker lock(&_ownerMutex);
	if (_owner) {
		_owner->destroyTemporaryKey(keyId);
	}
}

void SessionData::detach() {
	QMutexLocker lock(&_ownerMutex);
	_owner = nullptr;
}

Session::Session(
	not_null<Instance*> instance,
	ShiftedDcId shiftedDcId,
	Dcenter *dc)
: QObject()
, _instance(instance)
, _shiftedDcId(shiftedDcId)
, _ownedDc(dc ? nullptr : std::make_unique<Dcenter>(shiftedDcId, nullptr))
, _dc(dc ? dc : _ownedDc.get())
, _data(std::make_shared<SessionData>(this))
, _sender([=] { needToResumeAndSend(); }) {
	_timeouter.callEach(1000);
	refreshOptions();
	if (sharedDc()) {
		watchDcKeyChanges();
	}
}

void Session::watchDcKeyChanges() {
	_instance->dcTemporaryKeyChanged(
	) | rpl::filter([=](DcId dcId) {
		return (dcId == _shiftedDcId) || (dcId == BareDcId(_shiftedDcId));
	}) | rpl::start_with_next([=] {
		DEBUG_LOG(("AuthKey Info: Session::authKeyCreatedForDC slot, "
			"emitting authKeyChanged(), dcWithShift %1").arg(_shiftedDcId));
		emit authKeyChanged();
	}, _lifetime);
}

void Session::start() {
	_connection = std::make_unique<Connection>(_instance);
	_connection->start(_data, _shiftedDcId);
	if (_instance->isKeysDestroyer()) {
		_instance->scheduleKeyDestroy(_shiftedDcId);
	}
}

bool Session::rpcErrorOccured(
		mtpRequestId requestId,
		const RPCFailHandlerPtr &onFail,
		const RPCError &error) { // return true if need to clean request data
	return _instance->rpcErrorOccured(requestId, onFail, error);
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
	const auto proxyType =
		(Global::ProxySettings() == ProxyData::Settings::Enabled
			? proxy.type
			: ProxyData::Type::None);
	const auto useTcp = (proxyType != ProxyData::Type::Http);
	const auto useHttp = (proxyType != ProxyData::Type::Mtproto);
	const auto useIPv4 = true;
	const auto useIPv6 = Global::TryIPv6();
	_data->setConnectionOptions(ConnectionOptions(
		_instance->systemLangCode(),
		_instance->cloudLangCode(),
		_instance->langPackName(),
		(Global::ProxySettings() == ProxyData::Settings::Enabled
			? proxy
			: ProxyData()),
		useIPv4,
		useIPv6,
		useHttp,
		useTcp));
}

void Session::reInitConnection() {
	_dc->setConnectionInited(false);
	restart();
}

void Session::stop() {
	if (_killed) {
		DEBUG_LOG(("Session Error: can't kill a killed session"));
		return;
	}
	DEBUG_LOG(("Session Info: stopping session dcWithShift %1").arg(_shiftedDcId));
	if (_connection) {
		_connection->kill();
		_instance->queueQuittingConnection(std::move(_connection));
	}
}

void Session::kill() {
	stop();
	_killed = true;
	_data->detach();
	DEBUG_LOG(("Session Info: marked session dcWithShift %1 as killed").arg(_shiftedDcId));
}

void Session::unpaused() {
	if (_needToReceive) {
		_needToReceive = false;
		InvokeQueued(this, [=] {
			tryToReceive();
		});
	}
}

void Session::sendDcKeyCheck(const AuthKeyPtr &key) {
	_data->setKeyForCheck(key);
	needToResumeAndSend();
}

void Session::sendAnything(crl::time msCanWait) {
	if (_killed) {
		DEBUG_LOG(("Session Error: can't send anything in a killed session"));
		return;
	}
	const auto ms = crl::now();
	if (_msSendCall) {
		if (ms > _msSendCall + _msWait) {
			_msWait = 0;
		} else {
			_msWait = (_msSendCall + _msWait) - ms;
			if (_msWait > msCanWait) {
				_msWait = msCanWait;
			}
		}
	} else {
		_msWait = msCanWait;
	}
	if (_msWait) {
		DEBUG_LOG(("MTP Info: dcWithShift %1 can wait for %2ms from current %3").arg(_shiftedDcId).arg(_msWait).arg(_msSendCall));
		_msSendCall = ms;
		_sender.callOnce(_msWait);
	} else {
		DEBUG_LOG(("MTP Info: dcWithShift %1 stopped send timer, can wait for %2ms from current %3").arg(_shiftedDcId).arg(_msWait).arg(_msSendCall));
		_sender.cancel();
		_msSendCall = 0;
		needToResumeAndSend();
	}
}

void Session::needToResumeAndSend() {
	if (_killed) {
		DEBUG_LOG(("Session Info: can't resume a killed session"));
		return;
	}
	if (!_connection) {
		DEBUG_LOG(("Session Info: resuming session dcWithShift %1").arg(_shiftedDcId));
		start();
	}
	if (_ping) {
		_ping = false;
		emit needToPing();
	} else {
		emit needToSend();
	}
}

void Session::sendMsgsStateInfo(quint64 msgId, QByteArray data) {
	auto info = bytes::vector();
	if (!data.isEmpty()) {
		info.resize(data.size());
		bytes::copy(info, bytes::make_span(data));
	}
	_instance->sendProtocolMessage(
		_shiftedDcId,
		MTPMsgsStateInfo(
			MTP_msgs_state_info(MTP_long(msgId), MTP_bytes(data))));
}

bool Session::sharedDc() const {
	return (_ownedDc == nullptr);
}

void Session::connectionStateChange(int newState) {
	_instance->onStateChange(_shiftedDcId, newState);
}

void Session::resetDone() {
	_instance->onSessionReset(_shiftedDcId);
}

void Session::cancel(mtpRequestId requestId, mtpMsgId msgId) {
	if (requestId) {
		QWriteLocker locker(_data->toSendMutex());
		_data->toSendMap().remove(requestId);
	}
	if (msgId) {
		QWriteLocker locker(_data->haveSentMutex());
		_data->haveSentMap().remove(msgId);
	}
}

void Session::ping() {
	_ping = true;
	sendAnything();
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

	QWriteLocker locker(_data->toSendMutex());
	const auto &toSend = _data->toSendMap();
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

void Session::resend(
		mtpMsgId msgId,
		crl::time msCanWait,
		bool forceContainer) {
	auto lock = QWriteLocker(_data->haveSentMutex());
	auto &haveSent = _data->haveSentMap();

	auto i = haveSent.find(msgId);
	if (i == haveSent.end()) {
		return;
	}
	auto request = i.value();
	haveSent.erase(i);
	lock.unlock();

	// For container just resend all messages we can.
	if (request.isSentContainer()) {
		DEBUG_LOG(("Message Info: resending container from haveSent, msgId %1").arg(msgId));
		const mtpMsgId *ids = (const mtpMsgId *)(request->constData() + 8);
		for (uint32 i = 0, l = (request->size() - 8) >> 1; i < l; ++i) {
			resend(ids[i], 10, true);
		}
	} else if (!request.isStateRequest()) {
		request->msDate = forceContainer ? 0 : crl::now();
		{
			QWriteLocker locker(_data->toResendMutex());
			_data->toResendMap().insert(msgId, request->requestId);
		}
		sendPrepared(request, msCanWait, false);
	}
}

void Session::resendAll() {
	QVector<mtpMsgId> toResend;
	{
		QReadLocker locker(_data->haveSentMutex());
		const auto &haveSent = _data->haveSentMap();
		toResend.reserve(haveSent.size());
		for (auto i = haveSent.cbegin(), e = haveSent.cend(); i != e; ++i) {
			if (i.value()->requestId) {
				toResend.push_back(i.key());
			}
		}
	}
	for (uint32 i = 0, l = toResend.size(); i < l; ++i) {
		resend(toResend[i], -1, true);
	}
	InvokeQueued(this, [=] {
		sendAnything();
	});
}

void Session::sendPrepared(
		const SecureRequest &request,
		crl::time msCanWait,
		bool newRequest) {
	DEBUG_LOG(("MTP Info: adding request to toSendMap, msCanWait %1"
		).arg(msCanWait));
	{
		QWriteLocker locker(_data->toSendMutex());
		_data->toSendMap().insert(request->requestId, request);

		if (newRequest) {
			*(mtpMsgId*)(request->data() + 4) = 0;
			*(request->data() + 6) = 0;
		}
	}

	DEBUG_LOG(("MTP Info: added, requestId %1").arg(request->requestId));
	if (msCanWait >= 0) {
		InvokeQueued(this, [=] {
			sendAnything(msCanWait);
		});
	}
}

bool Session::acquireKeyCreation() {
	Expects(!_myKeyCreation);

	if (!_dc->acquireKeyCreation()) {
		return false;
	}
	_myKeyCreation = true;
	return true;
}

void Session::releaseKeyCreationOnDone(
		const AuthKeyPtr &temporaryKey,
		const AuthKeyPtr &persistentKey) {
	Expects(_myKeyCreation);

	DEBUG_LOG(("AuthKey Info: Session key bound, setting, dcWithShift %1"
		).arg(_shiftedDcId));
	_dc->releaseKeyCreationOnDone(temporaryKey, persistentKey);
	_myKeyCreation = false;

	if (sharedDc()) {
		const auto dcId = _dc->id();
		const auto instance = _instance;
		InvokeQueued(instance, [=] {
			if (persistentKey) {
				instance->dcPersistentKeyChanged(dcId, persistentKey);
			} else {
				instance->dcTemporaryKeyChanged(dcId);
			}
		});
	}
}

void Session::releaseKeyCreationOnFail() {
	Expects(_myKeyCreation);

	_dc->releaseKeyCreationOnFail();
	_myKeyCreation = false;
}

void Session::notifyDcConnectionInited() {
	DEBUG_LOG(("MTP Info: emitting MTProtoDC::connectionWasInited(), dcWithShift %1").arg(_shiftedDcId));
	_dc->setConnectionInited();
}

void Session::destroyTemporaryKey(uint64 keyId) {
	if (!_dc->destroyTemporaryKey(keyId)) {
		return;
	}
	if (sharedDc()) {
		const auto dcId = _dc->id();
		const auto instance = _instance;
		InvokeQueued(instance, [=] {
			instance->dcTemporaryKeyChanged(dcId);
		});
	}
}

int32 Session::getDcWithShift() const {
	return _shiftedDcId;
}

AuthKeyPtr Session::getTemporaryKey() const {
	return _dc->getTemporaryKey();
}

AuthKeyPtr Session::getPersistentKey() const {
	return _dc->getPersistentKey();
}

bool Session::connectionInited() const {
	return _dc->connectionInited();
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
			QWriteLocker locker(_data->haveReceivedMutex());
			auto &responses = _data->haveReceivedResponses();
			auto response = responses.begin();
			if (response == responses.cend()) {
				auto &updates = _data->haveReceivedUpdates();
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
			if (_shiftedDcId == BareDcId(_shiftedDcId)) { // call globalCallback only in main session
				_instance->globalCallback(message.constData(), message.constData() + message.size());
			}
		} else {
			_instance->execCallback(requestId, message.constData(), message.constData() + message.size());
		}
	}
}

Session::~Session() {
	if (_myKeyCreation) {
		releaseKeyCreationOnFail();
	}
	Assert(_connection == nullptr);
}

} // namespace internal
} // namespace MTP
