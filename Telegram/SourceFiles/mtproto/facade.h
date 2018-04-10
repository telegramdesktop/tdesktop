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

constexpr auto kDcShift = ShiftedDcId(10000);
constexpr auto kConfigDcShift = 0x01;
constexpr auto kLogoutDcShift = 0x02;
constexpr auto kMaxMediaDcCount = 0x10;
constexpr auto kBaseDownloadDcShift = 0x10;
constexpr auto kBaseUploadDcShift = 0x20;
constexpr auto kDestroyKeyStartDcShift = 0x100;

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

constexpr DcId bareDcId(ShiftedDcId shiftedDcId) {
	return (shiftedDcId % internal::kDcShift);
}
constexpr ShiftedDcId shiftDcId(DcId dcId, int value) {
	return dcId + internal::kDcShift * value;
}
constexpr int getDcIdShift(ShiftedDcId shiftedDcId) {
	return shiftedDcId / internal::kDcShift;
}

// send(MTPhelp_GetConfig(), MTP::configDcId(dc)) - for dc enumeration
constexpr ShiftedDcId configDcId(DcId dcId) {
	return shiftDcId(dcId, internal::kConfigDcShift);
}

// send(MTPauth_LogOut(), MTP::logoutDcId(dc)) - for logout of guest dcs enumeration
constexpr ShiftedDcId logoutDcId(DcId dcId) {
	return shiftDcId(dcId, internal::kLogoutDcShift);
}

constexpr auto kDownloadSessionsCount = 2;
constexpr auto kUploadSessionsCount = 2;

namespace internal {

constexpr ShiftedDcId downloadDcId(DcId dcId, int index) {
	static_assert(kDownloadSessionsCount < internal::kMaxMediaDcCount, "Too large MTPDownloadSessionsCount!");
	return shiftDcId(dcId, internal::kBaseDownloadDcShift + index);
};

} // namespace internal

// send(req, callbacks, MTP::downloadDcId(dc, index)) - for download shifted dc id
inline ShiftedDcId downloadDcId(DcId dcId, int index) {
	Expects(index >= 0 && index < kDownloadSessionsCount);
	return internal::downloadDcId(dcId, index);
}

inline constexpr bool isDownloadDcId(ShiftedDcId shiftedDcId) {
	return (shiftedDcId >= internal::downloadDcId(0, 0)) && (shiftedDcId < internal::downloadDcId(0, kDownloadSessionsCount - 1) + internal::kDcShift);
}

inline bool isCdnDc(MTPDdcOption::Flags flags) {
	return (flags & MTPDdcOption::Flag::f_cdn);
}

inline bool isTemporaryDcId(ShiftedDcId shiftedDcId) {
	auto dcId = bareDcId(shiftedDcId);
	return (dcId >= Instance::Config::kTemporaryMainDc);
}

inline DcId getRealIdFromTemporaryDcId(ShiftedDcId shiftedDcId) {
	auto dcId = bareDcId(shiftedDcId);
	return (dcId >= Instance::Config::kTemporaryMainDc) ? (dcId - Instance::Config::kTemporaryMainDc) : 0;
}

inline DcId getTemporaryIdFromRealDcId(ShiftedDcId shiftedDcId) {
	auto dcId = bareDcId(shiftedDcId);
	return (dcId < Instance::Config::kTemporaryMainDc) ? (dcId + Instance::Config::kTemporaryMainDc) : 0;
}

namespace internal {

constexpr ShiftedDcId uploadDcId(DcId dcId, int index) {
	static_assert(kUploadSessionsCount < internal::kMaxMediaDcCount, "Too large MTPUploadSessionsCount!");
	return shiftDcId(dcId, internal::kBaseUploadDcShift + index);
};

} // namespace internal

// send(req, callbacks, MTP::uploadDcId(index)) - for upload shifted dc id
// uploading always to the main dc so bareDcId == 0
inline ShiftedDcId uploadDcId(int index) {
	Expects(index >= 0 && index < kUploadSessionsCount);
	return internal::uploadDcId(0, index);
};

constexpr bool isUploadDcId(ShiftedDcId shiftedDcId) {
	return (shiftedDcId >= internal::uploadDcId(0, 0)) && (shiftedDcId < internal::uploadDcId(0, kUploadSessionsCount - 1) + internal::kDcShift);
}

inline ShiftedDcId destroyKeyNextDcId(ShiftedDcId shiftedDcId) {
	auto shift = getDcIdShift(shiftedDcId);
	return shiftDcId(bareDcId(shiftedDcId), shift ? (shift + 1) : internal::kDestroyKeyStartDcShift);
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
