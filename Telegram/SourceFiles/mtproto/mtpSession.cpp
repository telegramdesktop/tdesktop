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


MTProtoSession::MTProtoSession() : data(this), dcId(0), dc(0), msSendCall(0), msWait(0) {
}

void MTProtoSession::start(int32 dcenter, uint32 connects) {
	if (dcId) {
		DEBUG_LOG(("Session Info: MTProtoSession::start called on already started session"));
		return;
	}
	if (connects < 1) {
		connects = cConnectionsInSession();
	} else if (connects > 4) {
		connects = 4;
	}
	
	msSendCall = msWait = 0;

	connect(&timeouter, SIGNAL(timeout()), this, SLOT(checkRequestsByTimer()));
	timeouter.start(1000);

	connect(&sender, SIGNAL(timeout()), this, SIGNAL(needToSend()));
	connect(this, SIGNAL(startSendTimer(int)), &sender, SLOT(start(int)));
	connect(this, SIGNAL(stopSendTimer()), &sender, SLOT(stop()));
	connect(this, SIGNAL(needToSendAsync()), this, SIGNAL(needToSend()));
	sender.setSingleShot(true);

	MTProtoDCMap &dcs(mtpDCMap());

	connections.reserve(connects);
	for (uint32 i = 0; i < connects; ++i) {	
		connections.push_back(new MTProtoConnection());
		dcId = connections.back()->start(&data, dcenter);
		if (!dcId) {
			for (MTProtoConnections::const_iterator j = connections.cbegin(), e = connections.cend(); j != e; ++j) {
				delete *j;
			}
			connections.clear();
			DEBUG_LOG(("Session Info: could not start connection %1 to dc %2").arg(i).arg(dcenter));
			return;
		}
		if (!dc) {
			dcenter = dcId;
			MTProtoDCMap::const_iterator dcIndex = dcs.constFind(dcId % _mtp_internal::dcShift);
			if (dcIndex == dcs.cend()) {
				dc = MTProtoDCPtr(new MTProtoDC(dcId % _mtp_internal::dcShift, mtpAuthKeyPtr()));
				dcs.insert(dcId % _mtp_internal::dcShift, dc);
			} else {
				dc = dcIndex.value();
			}

			ReadLockerAttempt lock(keyMutex());
			data.setKey(lock ? dc->getKey() : mtpAuthKeyPtr(0));

			connect(dc.data(), SIGNAL(authKeyCreated()), this, SLOT(authKeyCreatedForDC()));
		}
	}
}

void MTProtoSession::restart() {
	for (MTProtoConnections::const_iterator i = connections.cbegin(), e = connections.cend(); i != e; ++i) {
		(*i)->restart();
	}
}

void MTProtoSession::stop() {
	while (connections.size()) {
		connections.back()->stop();
		connections.pop_back();
	}
}

void MTProtoSession::checkRequestsByTimer() {
	MTPMsgsStateReq stateRequest(MTP_msgs_state_req(MTP_vector<MTPlong>(0)));
	QVector<MTPlong> &stateRequestIds(stateRequest._msgs_state_req().vmsg_ids._vector().v);

	QVector<mtpMsgId> resendingIds;
	QVector<mtpMsgId> removingIds; // remove very old (10 minutes) containers and resend requests

	{
		QReadLocker locker(data.haveSentMutex());
		mtpRequestMap &haveSent(data.haveSentMap());
		uint32 haveSentCount(haveSent.size());
		uint64 ms = getms();
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
						stateRequestIds.push_back(MTP_long(i.key()));
					}
				}
			} else if (unixtime() > (int32)(i.key() >> 32) + MTPContainerLives) {
				removingIds.reserve(haveSentCount);
				removingIds.push_back(i.key());
			}
		}
	}

	if (stateRequestIds.size()) {
		DEBUG_LOG(("MTP Info: requesting state of msgs: %1").arg(logVectorLong(stateRequestIds)));
		send(stateRequest, RPCResponseHandler(), MTPCheckResendWaiting);
	}
	for (uint32 i = 0, l = resendingIds.size(); i < l; ++i) {
		DEBUG_LOG(("MTP Info: resending request %1").arg(resendingIds[i]));
		resend(resendingIds[i], MTPCheckResendWaiting);
	}
	uint32 removingIdsCount = removingIds.size();
	if (removingIdsCount) {
		RPCCallbackClears clearCallbacks;
		{
			QWriteLocker locker(data.haveSentMutex());
			mtpRequestMap &haveSent(data.haveSentMap());
			for (uint32 i = 0; i < removingIdsCount; ++i) {
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
	_mtp_internal::onStateChange(dcId, newState);
}

void MTProtoSession::cancel(mtpRequestId requestId) {
	QWriteLocker locker(data.toSendMutex());
	mtpPreRequestMap &toSend(data.toSendMap());
	mtpPreRequestMap::iterator i = toSend.find(requestId);
	if (i != toSend.end()) toSend.erase(i);
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

mtpRequestId MTProtoSession::resend(mtpMsgId msgId, uint64 msCanWait, bool forceContainer, bool sendMsgStateInfo) {
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
		request->msDate = forceContainer ? 0 : getms();
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

	uint64 ms = getms();
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
		msSendCall = ms;
		emit startSendTimer(msWait);
		DEBUG_LOG(("MTP Info: can wait for %1ms from current %2").arg(msWait).arg(msSendCall));
	} else {
		emit stopSendTimer();
		msSendCall = 0;
		emit needToSendAsync();
	}
}

void MTProtoSession::sendPreparedWithInit(const mtpRequest &request, uint64 msCanWait) { // returns true, if emit of needToSend() is needed
	if (request->size() > 8 && request->at(8) == mtpc_initConnection) {
		sendPrepared(request, msCanWait, false);
		return;
	}
	MTPInitConnection<mtpRequest> requestWrap(MTPinitConnection<mtpRequest>(MTP_int(ApiId), MTP_string(cApiDeviceModel()), MTP_string(cApiSystemVersion()), MTP_string(cApiAppVersion()), MTP_string(ApiLang), request));
	uint32 requestSize = requestWrap.size() >> 2;
	mtpRequest reqSerialized(mtpRequestData::prepare(requestSize));
	requestWrap.write(*reqSerialized);

	reqSerialized->msDate = getms(); // > 0 - can send without container
	_mtp_internal::replaceRequest(reqSerialized, request);
	sendPrepared(reqSerialized, msCanWait);
}

QReadWriteLock *MTProtoSession::keyMutex() const {
	return dc->keyMutex();
}

void MTProtoSession::authKeyCreatedForDC() {
	DEBUG_LOG(("AuthKey Info: MTProtoSession::authKeyCreatedForDC slot, emitting authKeyCreated(), dc %1").arg(dcId));
	data.setKey(dc->getKey());
	emit authKeyCreated();
}

void MTProtoSession::keyCreated(const mtpAuthKeyPtr &key) {
	DEBUG_LOG(("AuthKey Info: MTProtoSession::keyCreated(), setting, dc %1").arg(dcId));
	dc->setKey(key);
}

void MTProtoSession::destroyKey() {
	if (!dc) return;

	if (data.getKey()) {
		DEBUG_LOG(("MTP Info: destroying auth_key for dc %1").arg(dcId));
		if (data.getKey() == dc->getKey()) {
			dc->destroyKey();
		}
		data.setKey(mtpAuthKeyPtr(0));
	}
}

int32 MTProtoSession::getDC() const {
	return dcId;
}

void MTProtoSession::tryToReceive() {
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
			_mtp_internal::globalCallback(response.constData(), response.constData() + response.size());
		} else {
			_mtp_internal::execCallback(requestId, response.constData(), response.constData() + response.size());
		}
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
