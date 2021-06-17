/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/type_utils.h"
#include "mtproto/mtp_instance.h"

namespace MTP {
namespace details {

[[nodiscard]] bool paused();
void pause();
void unpause();
[[nodiscard]] rpl::producer<> unpaused();

} // namespace details

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

// send(MTPupload_GetFile(), MTP::groupCallStreamDcId(dc)) - for gorup call stream
constexpr ShiftedDcId groupCallStreamDcId(DcId dcId) {
	return ShiftDcId(dcId, kGroupCallStreamDcShift);
}

constexpr auto kUploadSessionsCount = 2;

namespace details {

constexpr ShiftedDcId downloadDcId(DcId dcId, int index) {
	Expects(index < kMaxMediaDcCount);

	return ShiftDcId(dcId, kBaseDownloadDcShift + index);
};

} // namespace details

// send(req, callbacks, MTP::downloadDcId(dc, index)) - for download shifted dc id
inline ShiftedDcId downloadDcId(DcId dcId, int index) {
	return details::downloadDcId(dcId, index);
}

inline constexpr bool isDownloadDcId(ShiftedDcId shiftedDcId) {
	return (shiftedDcId >= details::downloadDcId(0, 0))
		&& (shiftedDcId < details::downloadDcId(0, kMaxMediaDcCount - 1) + kDcShift);
}

inline bool isCdnDc(MTPDdcOption::Flags flags) {
	return (flags & MTPDdcOption::Flag::f_cdn);
}

inline bool isTemporaryDcId(ShiftedDcId shiftedDcId) {
	auto dcId = BareDcId(shiftedDcId);
	return (dcId >= Instance::Fields::kTemporaryMainDc);
}

inline DcId getRealIdFromTemporaryDcId(ShiftedDcId shiftedDcId) {
	auto dcId = BareDcId(shiftedDcId);
	return (dcId >= Instance::Fields::kTemporaryMainDc) ? (dcId - Instance::Fields::kTemporaryMainDc) : 0;
}

inline DcId getTemporaryIdFromRealDcId(ShiftedDcId shiftedDcId) {
	auto dcId = BareDcId(shiftedDcId);
	return (dcId < Instance::Fields::kTemporaryMainDc) ? (dcId + Instance::Fields::kTemporaryMainDc) : 0;
}

namespace details {

constexpr ShiftedDcId uploadDcId(DcId dcId, int index) {
	static_assert(kUploadSessionsCount < kMaxMediaDcCount, "Too large MTPUploadSessionsCount!");
	return ShiftDcId(dcId, kBaseUploadDcShift + index);
};

} // namespace details

// send(req, callbacks, MTP::uploadDcId(index)) - for upload shifted dc id
// uploading always to the main dc so BareDcId(result) == 0
inline ShiftedDcId uploadDcId(int index) {
	Expects(index >= 0 && index < kUploadSessionsCount);

	return details::uploadDcId(0, index);
};

constexpr bool isUploadDcId(ShiftedDcId shiftedDcId) {
	return (shiftedDcId >= details::uploadDcId(0, 0))
		&& (shiftedDcId < details::uploadDcId(0, kUploadSessionsCount - 1) + kDcShift);
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

} // namespace MTP
