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
#include "mtproto/mtp_instance.h"

namespace MTP {
namespace internal {

bool paused();
void pause();
void unpause();

} // namespace internal

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

enum {
	DisconnectedState = 0,
	ConnectingState = 1,
	ConnectedState = 2,
};

enum {
	RequestSent = 0,
	RequestConnecting = 1,
	RequestSending = 2
};

Instance *MainInstance();

inline void restart() {
	return MainInstance()->restart();
}

inline void restart(ShiftedDcId shiftedDcId) {
	return MainInstance()->restart(shiftedDcId);
}

inline DcId maindc() {
	return MainInstance()->mainDcId();
}

inline int32 dcstate(ShiftedDcId shiftedDcId = 0) {
	if (auto instance = MainInstance()) {
		return instance->dcstate(shiftedDcId);
	}
	return DisconnectedState;
}

inline QString dctransport(ShiftedDcId shiftedDcId = 0) {
	if (auto instance = MainInstance()) {
		return instance->dctransport(shiftedDcId);
	}
	return QString();
}

template <typename TRequest>
inline mtpRequestId send(const TRequest &request, RPCResponseHandler callbacks = RPCResponseHandler(), ShiftedDcId dcId = 0, TimeMs msCanWait = 0, mtpRequestId after = 0) {
	return MainInstance()->send(request, std::move(callbacks), dcId, msCanWait, after);
}

template <typename TRequest>
inline mtpRequestId send(const TRequest &request, RPCDoneHandlerPtr onDone, RPCFailHandlerPtr onFail = RPCFailHandlerPtr(), ShiftedDcId dcId = 0, TimeMs msCanWait = 0, mtpRequestId after = 0) {
	return MainInstance()->send(request, std::move(onDone), std::move(onFail), dcId, msCanWait, after);
}

inline void sendAnything(ShiftedDcId shiftedDcId = 0, TimeMs msCanWait = 0) {
	return MainInstance()->sendAnything(shiftedDcId, msCanWait);
}

inline void cancel(mtpRequestId requestId) {
	return MainInstance()->cancel(requestId);
}

inline void ping() {
	return MainInstance()->ping();
}

inline void killSession(ShiftedDcId shiftedDcId) {
	return MainInstance()->killSession(shiftedDcId);
}

inline void stopSession(ShiftedDcId shiftedDcId) {
	return MainInstance()->stopSession(shiftedDcId);
}

inline int32 state(mtpRequestId requestId) { // < 0 means waiting for such count of ms
	return MainInstance()->state(requestId);
}

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
		if (after) reqSerialized->after = getRequest(after);
		requestId = storeRequest(reqSerialized, callbacks);

		sendPrepared(reqSerialized, msCanWait);
	} catch (Exception &e) {
		requestId = 0;
		rpcErrorOccured(requestId, callbacks.onFail, rpcClientError("NO_REQUEST_ID", QString("send() failed to queue request, exception: %1").arg(e.what())));
	}
	if (requestId) registerRequest(requestId, toMainDC ? -getDcWithShift() : getDcWithShift());
	return requestId;
}

} // namespace internal
} // namespace MTP
