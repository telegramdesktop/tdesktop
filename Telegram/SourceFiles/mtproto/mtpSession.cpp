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
#include "stdafx.h"
#include <QtCore/QSharedPointer>

void MTPSessionData::clear() {
	RPCCallbackClears clearCallbacks;
	{
		QReadLocker locker1(haveSentMutex()), locker2(toResendMutex()), locker3(haveReceivedMutex()), locker4(wereAckedMutex());
		mtpResponseMap::const_iterator end = haveReceived.cend();
		clearCallbacks.reserve(haveSent.size() + wereAcked.size());
		for (mtpRequestMap::const_iterator i = haveSent.cbegin(), e = haveSent.cend(); i != e; ++i) {
			mtpRequestId requestId = i.value()->requestId;
			if (haveReceived.find(requestId) == end) {
				clearCallbacks.push_back(requestId);
			}
		}
		for (mtpRequestIdsMap::const_iterator i = toResend.cbegin(), e = toResend.cend(); i != e; ++i) {
			mtpRequestId requestId = i.value();
			if (haveReceived.find(requestId) == end) {
				clearCallbacks.push_back(requestId);
			}
		}
		for (mtpRequestIdsMap::const_iterator i = wereAcked.cbegin(), e = wereAcked.cend(); i != e; ++i) {
			mtpRequestId requestId = i.value();
			if (haveReceived.find(requestId) == end) {
				clearCallbacks.push_back(requestId);
			}
		}
	}
	{
		QWriteLocker locker(haveSentMutex());
		haveSent.clear();
	}
	{
		QWriteLocker locker(toResendMutex());
		toResend.clear();
	}
	{
		QWriteLocker locker(wereAckedMutex());
		wereAcked.clear();
	}
	{
		QWriteLocker locker(receivedIdsMutex());
		receivedIds.clear();
	}
	_mtp_internal::clearCallbacksDelayed(clearCallbacks);
}


MTProtoSession::MTProtoSession() : QObject()
, _killed(false)
, _needToReceive(false)
, data(this)
, dcWithShift(0)
, dc(0)
, msSendCall(0)
, msWait(0)
, _ping(false) {
}

void MTProtoSession::start(int32 dcenter) {
	if (_killed) {
		DEBUG_LOG(("Session Error: can't start a killed session"));
		return;
	}
	if (dcWithShift) {
		DEBUG_LOG(("Session Info: MTProtoSession::start called on already started session"));
		return;
	}

	msSendCall = msWait = 0;

	connect(&timeouter, SIGNAL(timeout()), this, SLOT(checkRequestsByTimer()));
	timeouter.start(1000);

	connect(&sender, SIGNAL(timeout()), this, SLOT(needToResumeAndSend()));

	MTProtoDCMap &dcs(mtpDCMap());

	connections.reserve(cConnectionsInSession());
	for (uint32 i = 0; i < cConnectionsInSession(); ++i) {
		connections.push_back(new MTProtoConnection());
		dcWithShift = connections.back()->start(&data, dcenter);
		if (!dcWithShift) {
			for (MTProtoConnections::const_iterator j = connections.cbegin(), e = connections.cend(); j != e; ++j) {
				delete *j;
			}
			connections.clear();
			DEBUG_LOG(("Session Info: could not start connection %1 to dc %2").arg(i).arg(dcenter));
			return;
		}
		if (!dc) {
			dcenter = dcWithShift;
			int32 dcId = dcWithShift % _mtp_internal::dcShift;
			MTProtoDCMap::const_iterator dcIndex = dcs.constFind(dcId);
			if (dcIndex == dcs.cend()) {
				dc = MTProtoDCPtr(new MTProtoDC(dcId, mtpAuthKeyPtr()));
				dcs.insert(dcWithShift % _mtp_internal::dcShift, dc);
			} else {
				dc = dcIndex.value();
			}

			ReadLockerAttempt lock(keyMutex());
			data.setKey(lock ? dc->getKey() : mtpAuthKeyPtr(0));
			if (lock && dc->connectionInited()) {
				data.setLayerWasInited(true);
			}
			connect(dc.data(), SIGNAL(authKeyCreated()), this, SLOT(authKeyCreatedForDC()), Qt::QueuedConnection);
			connect(dc.data(), SIGNAL(layerWasInited(bool)), this, SLOT(layerWasInitedForDC(bool)), Qt::QueuedConnection);
		}
	}
}

void MTProtoSession::restart() {
	if (_killed) {
		DEBUG_LOG(("Session Error: can't restart a killed session"));
		return;
	}
	emit needToRestart();
}

void MTProtoSession::stop() {
	DEBUG_LOG(("Session Info: stopping session dcWithShift %1").arg(dcWithShift));
	while (!connections.isEmpty()) {
		connections.back()->stop();
		connections.pop_back();
	}
}

void MTProtoSession::kill() {
	stop();
	_killed = true;
	DEBUG_LOG(("Session Info: marked session dcWithShift %1 as killed").arg(dcWithShift));
}

void MTProtoSession::unpaused() {
	if (_needToReceive) {
		_needToReceive = false;
		QTimer::singleShot(0, this, SLOT(tryToReceive()));
	}
}

void MTProtoSession::sendAnything(quint64 msCanWait) {
	if (_killed) {
		DEBUG_LOG(("Session Error: can't send anything in a killed session"));
		return;
	}
	uint64 ms = getms(true);
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

void MTProtoSession::needToResumeAndSend() {
	if (_killed) {
		DEBUG_LOG(("Session Info: can't resume a killed session"));
		return;
	}
	if (connections.isEmpty()) {
		DEBUG_LOG(("Session Info: resuming session dcWithShift %1").arg(dcWithShift));
		MTProtoDCMap &dcs(mtpDCMap());

		connections.reserve(cConnectionsInSession());
		for (uint32 i = 0; i < cConnectionsInSession(); ++i) {
			connections.push_back(new MTProtoConnection());
			if (!connections.back()->start(&data, dcWithShift)) {
				for (MTProtoConnections::const_iterator j = connections.cbegin(), e = connections.cend(); j != e; ++j) {
					delete *j;
				}
				connections.clear();
				DEBUG_LOG(("Session Info: could not start connection %1 to dcWithShift %2").arg(i).arg(dcWithShift));
				dcWithShift = 0;
				return;
			}
		}
	}
	if (_ping) {
		_ping = false;
		emit needToPing();
	} else {
		emit needToSend();
	}
}

void MTProtoSession::sendPong(quint64 msgId, quint64 pingId) {
	send(MTP_pong(MTP_long(msgId), MTP_long(pingId)));
}

void MTProtoSession::sendMsgsStateInfo(quint64 msgId, QByteArray data) {
	MTPMsgsStateInfo req(MTP_msgs_state_info(MTP_long(msgId), MTPstring()));
	string &info(req._msgs_state_info().vinfo._string().v);
	info.resize(data.size());
	if (!data.isEmpty()) {
		memcpy(&info[0], data.constData(), data.size());
	}
	send(req);
}

void MTProtoSession::checkRequestsByTimer() {
	QVector<mtpMsgId> resendingIds;
	QVector<mtpMsgId> removingIds; // remove very old (10 minutes) containers and resend requests
	QVector<mtpMsgId> stateRequestIds;

	{
		QReadLocker locker(data.haveSentMutex());
		mtpRequestMap &haveSent(data.haveSentMap());
		uint32 haveSentCount(haveSent.size());
		uint64 ms = getms(true);
		for (mtpRequestMap::iterator i = haveSent.begin(), e = haveSent.end(); i != e; ++i) {
			mtpRequest &req(i.value());
			if (req->msDate > 0) {
				if (req->msDate + MTPCheckResendTimeout < ms) { // need to resend or check state
					if (mtpRequestData::messageSize(req) < MTPResendThreshold) { // resend
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
		DEBUG_LOG(("MTP Info: requesting state of msgs: %1").arg(Logs::vector(stateRequestIds)));
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
		RPCCallbackClears clearCallbacks;
		{
			QWriteLocker locker(data.haveSentMutex());
			mtpRequestMap &haveSent(data.haveSentMap());
			for (uint32 i = 0, l = removingIds.size(); i < l; ++i) {
				mtpRequestMap::iterator j = haveSent.find(removingIds[i]);
				if (j != haveSent.cend()) {
					if (j.value()->requestId) {
						clearCallbacks.push_back(j.value()->requestId);
					}
					haveSent.erase(j);
				}
			}
		}
		_mtp_internal::clearCallbacksDelayed(clearCallbacks);
	}
}

void MTProtoSession::onConnectionStateChange(qint32 newState) {
	_mtp_internal::onStateChange(dcWithShift, newState);
}

void MTProtoSession::onResetDone() {
	_mtp_internal::onSessionReset(dcWithShift);
}

void MTProtoSession::cancel(mtpRequestId requestId, mtpMsgId msgId) {
	if (requestId) {
		QWriteLocker locker(data.toSendMutex());
		data.toSendMap().remove(requestId);
	}
	if (msgId) {
		QWriteLocker locker(data.haveSentMutex());
		data.haveSentMap().remove(msgId);
	}
}

void MTProtoSession::ping() {
	_ping = true;
	sendAnything(0);
}

int32 MTProtoSession::requestState(mtpRequestId requestId) const {
	MTProtoConnections::const_iterator j = connections.cbegin(), e = connections.cend();
	int32 result = MTP::RequestSent;
	for (; j != e; ++j) {
		int32 s = (*j)->state();
		if (s == MTProtoConnection::Connected) {
			break;
		} else if (s == MTProtoConnection::Connecting || s == MTProtoConnection::Disconnected) {
			if (result < 0 || result == MTP::RequestSent) {
				result = MTP::RequestConnecting;
			}
		} else if (s < 0) {
			if ((result < 0 && s > result) || result == MTP::RequestSent) {
				result = s;
			}
		}
	}
	if (j == e) { // no one is connected
		return result;
	}
	if (!requestId) return MTP::RequestSent;

	QWriteLocker locker(data.toSendMutex());
	const mtpPreRequestMap &toSend(data.toSendMap());
	mtpPreRequestMap::const_iterator i = toSend.constFind(requestId);
	if (i != toSend.cend()) {
		return MTP::RequestSending;
	} else {
		return MTP::RequestSent;
	}
}

int32 MTProtoSession::getState() const {
	MTProtoConnections::const_iterator j = connections.cbegin(), e = connections.cend();
	int32 result = -86400000;
	for (; j != e; ++j) {
		int32 s = (*j)->state();
		if (s == MTProtoConnection::Connected) {
			return s;
		} else if (s == MTProtoConnection::Connecting || s == MTProtoConnection::Disconnected) {
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
		result = MTProtoConnection::Disconnected;
	}
	return result;
}

QString MTProtoSession::transport() const {
	MTProtoConnections::const_iterator j = connections.cbegin(), e = connections.cend();
	for (; j != e; ++j) {
		QString s = (*j)->transport();
		if (!s.isEmpty()) return s;
	}
	return QString();
}

mtpRequestId MTProtoSession::resend(quint64 msgId, quint64 msCanWait, bool forceContainer, bool sendMsgStateInfo) {
	mtpRequest request;
	{
		QWriteLocker locker(data.haveSentMutex());
		mtpRequestMap &haveSent(data.haveSentMap());

		mtpRequestMap::iterator i = haveSent.find(msgId);
		if (i == haveSent.end()) {
			if (sendMsgStateInfo) {
				char cantResend[2] = {1, 0};
				DEBUG_LOG(("Message Info: cant resend %1, request not found").arg(msgId));

				return send(MTP_msgs_state_info(MTP_long(msgId), MTP_string(string(cantResend, cantResend + 1))));
			}
			return 0;
		}

		request = i.value();
		haveSent.erase(i);
	}
	if (mtpRequestData::isSentContainer(request)) { // for container just resend all messages we can
		DEBUG_LOG(("Message Info: resending container from haveSent, msgId %1").arg(msgId));
		const mtpMsgId *ids = (const mtpMsgId *)(request->constData() + 8);
		for (uint32 i = 0, l = (request->size() - 8) >> 1; i < l; ++i) {
			resend(ids[i], 10, true);
		}
		return 0xFFFFFFFF;
	} else if (!mtpRequestData::isStateRequest(request)) {
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

void MTProtoSession::resendMany(QVector<quint64> msgIds, quint64 msCanWait, bool forceContainer, bool sendMsgStateInfo) {
	for (int32 i = 0, l = msgIds.size(); i < l; ++i) {
		resend(msgIds.at(i), msCanWait, forceContainer, sendMsgStateInfo);
	}
}

void MTProtoSession::resendAll() {
	QVector<mtpMsgId> toResend;
	{
		QReadLocker locker(data.haveSentMutex());
		const mtpRequestMap &haveSent(data.haveSentMap());
		toResend.reserve(haveSent.size());
		for (mtpRequestMap::const_iterator i = haveSent.cbegin(), e = haveSent.cend(); i != e; ++i) {
			if (i.value()->requestId) toResend.push_back(i.key());
		}
	}
	for (uint32 i = 0, l = toResend.size(); i < l; ++i) {
		resend(toResend[i], 10, true);
	}
}

void MTProtoSession::sendPrepared(const mtpRequest &request, uint64 msCanWait, bool newRequest) { // returns true, if emit of needToSend() is needed
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

QReadWriteLock *MTProtoSession::keyMutex() const {
	return dc->keyMutex();
}

void MTProtoSession::authKeyCreatedForDC() {
	DEBUG_LOG(("AuthKey Info: MTProtoSession::authKeyCreatedForDC slot, emitting authKeyCreated(), dcWithShift %1").arg(dcWithShift));
	data.setKey(dc->getKey());
	emit authKeyCreated();
}

void MTProtoSession::notifyKeyCreated(const mtpAuthKeyPtr &key) {
	DEBUG_LOG(("AuthKey Info: MTProtoSession::keyCreated(), setting, dcWithShift %1").arg(dcWithShift));
	dc->setKey(key);
}

void MTProtoSession::layerWasInitedForDC(bool wasInited) {
	DEBUG_LOG(("MTP Info: MTProtoSession::layerWasInitedForDC slot, dcWithShift %1").arg(dcWithShift));
	data.setLayerWasInited(wasInited);
}

void MTProtoSession::notifyLayerInited(bool wasInited) {
	DEBUG_LOG(("MTP Info: emitting MTProtoDC::layerWasInited(%1), dcWithShift %2").arg(Logs::b(wasInited)).arg(dcWithShift));
	dc->setConnectionInited(wasInited);
	emit dc->layerWasInited(wasInited);
}

void MTProtoSession::destroyKey() {
	if (!dc) return;

	if (data.getKey()) {
		DEBUG_LOG(("MTP Info: destroying auth_key for dcWithShift %1").arg(dcWithShift));
		if (data.getKey() == dc->getKey()) {
			dc->destroyKey();
		}
		data.setKey(mtpAuthKeyPtr(0));
	}
}

int32 MTProtoSession::getDcWithShift() const {
	return dcWithShift;
}

void MTProtoSession::tryToReceive() {
	if (_mtp_internal::paused()) {
		_needToReceive = true;
		return;
	}
	int32 cnt = 0;
	while (true) {
		mtpRequestId requestId;
		mtpResponse response;
		{
			QWriteLocker locker(data.haveReceivedMutex());
			mtpResponseMap &responses(data.haveReceivedMap());
			mtpResponseMap::iterator i = responses.begin();
			if (i == responses.end()) return;

			requestId = i.key();
			response = i.value();
			responses.erase(i);
		}
		if (requestId <= 0) {
			if (dcWithShift < int(_mtp_internal::dcShift)) { // call globalCallback only in main session
				_mtp_internal::globalCallback(response.constData(), response.constData() + response.size());
			}
		} else {
			_mtp_internal::execCallback(requestId, response.constData(), response.constData() + response.size());
		}
		++cnt;
	}
}

MTProtoSession::~MTProtoSession() {
	for (MTProtoConnections::const_iterator i = connections.cbegin(), e = connections.cend(); i != e; ++i) {
		delete *i;
	}
}

MTPrpcError rpcClientError(const QString &type, const QString &description) {
	return MTP_rpc_error(MTP_int(0), MTP_string(("CLIENT_" + type + (description.length() ? (": " + description) : "")).toUtf8().constData()));
}
