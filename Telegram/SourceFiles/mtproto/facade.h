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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "mtproto/core_types.h"
#include "mtproto/session.h"
#include "core/single_timer.h"

namespace MTP {
namespace internal {

Session *getSession(ShiftedDcId shiftedDcId); // 0 - current set dc

bool paused();
void pause();
void unpause();

void registerRequest(mtpRequestId requestId, int32 dc);
void unregisterRequest(mtpRequestId requestId);

mtpRequestId storeRequest(mtpRequest &request, const RPCResponseHandler &parser);
mtpRequest getRequest(mtpRequestId req);
void wrapInvokeAfter(mtpRequest &to, const mtpRequest &from, const mtpRequestMap &haveSent, int32 skipBeforeRequest = 0);
void clearCallbacks(mtpRequestId requestId, int32 errorCode = RPCError::NoError); // 0 - do not toggle onError callback
void clearCallbacksDelayed(const RPCCallbackClears &requestIds);
void performDelayedClear();
void execCallback(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end);
bool hasCallbacks(mtpRequestId requestId);
void globalCallback(const mtpPrime *from, const mtpPrime *end);
void onStateChange(int32 dcWithShift, int32 state);
void onSessionReset(int32 dcWithShift);
bool rpcErrorOccured(mtpRequestId requestId, const RPCFailHandlerPtr &onFail, const RPCError &err); // return true if need to clean request data
inline bool rpcErrorOccured(mtpRequestId requestId, const RPCResponseHandler &handler, const RPCError &err) {
	return rpcErrorOccured(requestId, handler.onFail, err);
}

// used for:
// - resending requests by timer which were postponed by flood delay
// - destroying MTProtoConnections whose thread has finished
class GlobalSlotCarrier : public QObject {
	Q_OBJECT

public:
	GlobalSlotCarrier();

public slots:
	void checkDelayed();
	void connectionFinished(Connection *connection);

private:
	SingleTimer _timer;

};

GlobalSlotCarrier *globalSlotCarrier();
void queueQuittingConnection(Connection *connection);

} // namespace internal

constexpr ShiftedDcId DCShift = 10000;
constexpr DcId bareDcId(ShiftedDcId shiftedDcId) {
	return (shiftedDcId % DCShift);
}
constexpr ShiftedDcId shiftDcId(DcId dcId, int value) {
	return dcId + DCShift * value;
}
constexpr int getDcIdShift(ShiftedDcId shiftedDcId) {
	return (shiftedDcId - bareDcId(shiftedDcId)) / DCShift;
}

// send(MTPhelp_GetConfig(), MTP::cfgDcId(dc)) - for dc enumeration
constexpr ShiftedDcId cfgDcId(DcId dcId) {
	return shiftDcId(dcId, 0x01);
}

// send(MTPauth_LogOut(), MTP::lgtDcId(dc)) - for logout of guest dcs enumeration
constexpr ShiftedDcId lgtDcId(DcId dcId) {
	return shiftDcId(dcId, 0x02);
}

namespace internal {

constexpr ShiftedDcId downloadDcId(DcId dcId, int index) {
	static_assert(MTPDownloadSessionsCount < 0x10, "Too large MTPDownloadSessionsCount!");
	return shiftDcId(dcId, 0x10 + index);
};

} // namespace internal

// send(req, callbacks, MTP::dldDcId(dc, index)) - for download shifted dc id
inline ShiftedDcId dldDcId(DcId dcId, int index) {
	t_assert(index >= 0 && index < MTPDownloadSessionsCount);
	return internal::downloadDcId(dcId, index);
}

constexpr bool isDldDcId(ShiftedDcId shiftedDcId) {
	return (shiftedDcId >= internal::downloadDcId(0, 0)) && (shiftedDcId < internal::downloadDcId(0, MTPDownloadSessionsCount - 1) + DCShift);
}

namespace internal {

constexpr ShiftedDcId uploadDcId(DcId dcId, int index) {
	static_assert(MTPUploadSessionsCount < 0x10, "Too large MTPUploadSessionsCount!");
	return shiftDcId(dcId, 0x20 + index);
};

} // namespace internal

// send(req, callbacks, MTP::uplDcId(index)) - for upload shifted dc id
// uploading always to the main dc so bareDcId == 0
inline ShiftedDcId uplDcId(int index) {
	t_assert(index >= 0 && index < MTPUploadSessionsCount);
	return internal::uploadDcId(0, index);
};

constexpr bool isUplDcId(ShiftedDcId shiftedDcId) {
	return (shiftedDcId >= internal::uploadDcId(0, 0)) && (shiftedDcId < internal::uploadDcId(0, MTPUploadSessionsCount - 1) + DCShift);
}

void start();
bool started();
void restart();
void restart(int32 dcMask);

class PauseHolder {
public:
	PauseHolder() {
		restart();
	}
	void restart() {
		if (!base::take(_paused, true)) {
			internal::pause();
		}
	}
	void release() {
		if (base::take(_paused)) {
			internal::unpause();
		}
	}
	~PauseHolder() {
		release();
	}

private:
	bool _paused = false;

};

void configure(int32 dc, int32 user);

void setdc(int32 dc, bool fromZeroOnly = false);
int32 maindc();

enum {
	DisconnectedState = 0,
	ConnectingState = 1,
	ConnectedState = 2,
};
int32 dcstate(int32 dc = 0);
QString dctransport(int32 dc = 0);

template <typename TRequest>
inline mtpRequestId send(const TRequest &request, RPCResponseHandler callbacks = RPCResponseHandler(), int32 dc = 0, TimeMs msCanWait = 0, mtpRequestId after = 0) {
	if (internal::Session *session = internal::getSession(dc)) {
		return session->send(request, callbacks, msCanWait, true, !dc, after);
	}
	return 0;
}
template <typename TRequest>
inline mtpRequestId send(const TRequest &request, RPCDoneHandlerPtr onDone, RPCFailHandlerPtr onFail = RPCFailHandlerPtr(), int32 dc = 0, TimeMs msCanWait = 0, mtpRequestId after = 0) {
	return send(request, RPCResponseHandler(onDone, onFail), dc, msCanWait, after);
}
inline void sendAnything(int32 dc = 0, TimeMs msCanWait = 0) {
	if (auto session = internal::getSession(dc)) {
		return session->sendAnything(msCanWait);
	}
}
void ping();
void cancel(mtpRequestId req);
void killSession(int32 dc);
void stopSession(int32 dc);

enum {
	RequestSent = 0,
	RequestConnecting = 1,
	RequestSending = 2
};
int32 state(mtpRequestId req); // < 0 means waiting for such count of ms

void finish();

void setAuthedId(int32 uid);
int32 authedId();
void logoutKeys(RPCDoneHandlerPtr onDone, RPCFailHandlerPtr onFail);

void setGlobalDoneHandler(RPCDoneHandlerPtr handler);
void setGlobalFailHandler(RPCFailHandlerPtr handler);
void setStateChangedHandler(MTPStateChangedHandler handler);
void setSessionResetHandler(MTPSessionResetHandler handler);
void clearGlobalHandlers();

void updateDcOptions(const QVector<MTPDcOption> &options);

AuthKeysMap getKeys();
void setKey(int32 dc, AuthKeyPtr key);

QReadWriteLock *dcOptionsMutex();

struct DcOption {
	DcOption(int id, MTPDdcOption::Flags flags, const std::string &ip, int port) : id(id), flags(flags), ip(ip), port(port) {
	}

	int id;
	MTPDdcOption::Flags flags;
	std::string ip;
	int port;
};
typedef QMap<int, DcOption> DcOptions;

namespace internal {

	template <typename TRequest>
	mtpRequestId Session::send(const TRequest &request, RPCResponseHandler callbacks, TimeMs msCanWait, bool needsLayer, bool toMainDC, mtpRequestId after) {
		mtpRequestId requestId = 0;
		try {
			uint32 requestSize = request.innerLength() >> 2;
			mtpRequest reqSerialized(mtpRequestData::prepare(requestSize));
			request.write(*reqSerialized);

			DEBUG_LOG(("MTP Info: adding request to toSendMap, msCanWait %1").arg(msCanWait));

			reqSerialized->msDate = getms(true); // > 0 - can send without container
			reqSerialized->needsLayer = needsLayer;
			if (after) reqSerialized->after = MTP::internal::getRequest(after);
			requestId = MTP::internal::storeRequest(reqSerialized, callbacks);

			sendPrepared(reqSerialized, msCanWait);
		} catch (Exception &e) {
			requestId = 0;
			MTP::internal::rpcErrorOccured(requestId, callbacks, rpcClientError("NO_REQUEST_ID", QString("send() failed to queue request, exception: %1").arg(e.what())));
		}
		if (requestId) MTP::internal::registerRequest(requestId, toMainDC ? -getDcWithShift() : getDcWithShift());
		return requestId;
	}

} // namespace internal

} // namespace MTP
