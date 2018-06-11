/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/single_timer.h"
#include "mtproto/type_utils.h"
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
		if (!std::exchange(_paused, true)) {
			internal::pause();
		}
	}
	void release() {
		if (std::exchange(_paused, false)) {
			internal::unpause();
		}
	}
	~PauseHolder() {
		release();
	}

private:
	bool _paused = false;

};

// send(MTPhelp_GetConfig(), MTP::configDcId(dc)) - for dc enumeration
constexpr ShiftedDcId configDcId(DcId dcId) {
	return ShiftDcId(dcId, kConfigDcShift);
}

// send(MTPauth_LogOut(), MTP::logoutDcId(dc)) - for logout of guest dcs enumeration
constexpr ShiftedDcId logoutDcId(DcId dcId) {
	return ShiftDcId(dcId, kLogoutDcShift);
}

// send(MTPupload_GetFile(), MTP::updaterDcId(dc)) - for autoupdater
constexpr ShiftedDcId updaterDcId(DcId dcId) {
	return ShiftDcId(dcId, kUpdaterDcShift);
}

constexpr auto kDownloadSessionsCount = 2;
constexpr auto kUploadSessionsCount = 2;

namespace internal {

constexpr ShiftedDcId downloadDcId(DcId dcId, int index) {
	static_assert(kDownloadSessionsCount < kMaxMediaDcCount, "Too large MTPDownloadSessionsCount!");
	return ShiftDcId(dcId, kBaseDownloadDcShift + index);
};

} // namespace internal

// send(req, callbacks, MTP::downloadDcId(dc, index)) - for download shifted dc id
inline ShiftedDcId downloadDcId(DcId dcId, int index) {
	Expects(index >= 0 && index < kDownloadSessionsCount);
	return internal::downloadDcId(dcId, index);
}

inline constexpr bool isDownloadDcId(ShiftedDcId shiftedDcId) {
	return (shiftedDcId >= internal::downloadDcId(0, 0)) && (shiftedDcId < internal::downloadDcId(0, kDownloadSessionsCount - 1) + kDcShift);
}

inline bool isCdnDc(MTPDdcOption::Flags flags) {
	return (flags & MTPDdcOption::Flag::f_cdn);
}

inline bool isTemporaryDcId(ShiftedDcId shiftedDcId) {
	auto dcId = BareDcId(shiftedDcId);
	return (dcId >= Instance::Config::kTemporaryMainDc);
}

inline DcId getRealIdFromTemporaryDcId(ShiftedDcId shiftedDcId) {
	auto dcId = BareDcId(shiftedDcId);
	return (dcId >= Instance::Config::kTemporaryMainDc) ? (dcId - Instance::Config::kTemporaryMainDc) : 0;
}

inline DcId getTemporaryIdFromRealDcId(ShiftedDcId shiftedDcId) {
	auto dcId = BareDcId(shiftedDcId);
	return (dcId < Instance::Config::kTemporaryMainDc) ? (dcId + Instance::Config::kTemporaryMainDc) : 0;
}

namespace internal {

constexpr ShiftedDcId uploadDcId(DcId dcId, int index) {
	static_assert(kUploadSessionsCount < kMaxMediaDcCount, "Too large MTPUploadSessionsCount!");
	return ShiftDcId(dcId, kBaseUploadDcShift + index);
};

} // namespace internal

// send(req, callbacks, MTP::uploadDcId(index)) - for upload shifted dc id
// uploading always to the main dc so BareDcId(result) == 0
inline ShiftedDcId uploadDcId(int index) {
	Expects(index >= 0 && index < kUploadSessionsCount);

	return internal::uploadDcId(0, index);
};

constexpr bool isUploadDcId(ShiftedDcId shiftedDcId) {
	return (shiftedDcId >= internal::uploadDcId(0, 0)) && (shiftedDcId < internal::uploadDcId(0, kUploadSessionsCount - 1) + kDcShift);
}

inline ShiftedDcId destroyKeyNextDcId(ShiftedDcId shiftedDcId) {
	const auto shift = GetDcIdShift(shiftedDcId);
	return ShiftDcId(BareDcId(shiftedDcId), shift ? (shift + 1) : kDestroyKeyStartDcShift);
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
inline mtpRequestId send(
		const TRequest &request,
		RPCResponseHandler &&callbacks = {},
		ShiftedDcId dcId = 0,
		TimeMs msCanWait = 0,
		mtpRequestId after = 0) {
	return MainInstance()->send(request, std::move(callbacks), dcId, msCanWait, after);
}

template <typename TRequest>
inline mtpRequestId send(
		const TRequest &request,
		RPCDoneHandlerPtr &&onDone,
		RPCFailHandlerPtr &&onFail = nullptr,
		ShiftedDcId dcId = 0,
		TimeMs msCanWait = 0,
		mtpRequestId after = 0) {
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

} // namespace MTP
