/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_common.h"
#include "base/timer.h"
#include "mtproto/facade.h"

class ApiWrap;
struct FileLoadResult;
struct SendMediaReady;

namespace Api {
enum class SendProgressType;
} // namespace Api

namespace Main {
class Session;
} // namespace Main

namespace Storage {

// MTP big files methods used for files greater than 10mb.
constexpr auto kUseBigFilesFrom = 10 * 1024 * 1024;

struct UploadedMedia {
	FullMsgId fullId;
	Api::RemoteFileInfo info;
	Api::SendOptions options;
	bool edit = false;
};

struct UploadSecureProgress {
	FullMsgId fullId;
	int64 offset = 0;
	int64 size = 0;
};

struct UploadSecureDone {
	FullMsgId fullId;
	uint64 fileId = 0;
	int partsCount = 0;
};

class Uploader final : public QObject {
public:
	explicit Uploader(not_null<ApiWrap*> api);
	~Uploader();

	[[nodiscard]] Main::Session &session() const;

	[[nodiscard]] FullMsgId currentUploadId() const {
		return uploadingId;
	}

	void uploadMedia(const FullMsgId &msgId, const SendMediaReady &image);
	void upload(
		const FullMsgId &msgId,
		const std::shared_ptr<FileLoadResult> &file);

	void cancel(const FullMsgId &msgId);
	void pause(const FullMsgId &msgId);
	void confirm(const FullMsgId &msgId);

	void cancelAll();
	void clear();

	rpl::producer<UploadedMedia> photoReady() const {
		return _photoReady.events();
	}
	rpl::producer<UploadedMedia> documentReady() const {
		return _documentReady.events();
	}
	rpl::producer<UploadSecureDone> secureReady() const {
		return _secureReady.events();
	}
	rpl::producer<FullMsgId> photoProgress() const {
		return _photoProgress.events();
	}
	rpl::producer<FullMsgId> documentProgress() const {
		return _documentProgress.events();
	}
	rpl::producer<UploadSecureProgress> secureProgress() const {
		return _secureProgress.events();
	}
	rpl::producer<FullMsgId> photoFailed() const {
		return _photoFailed.events();
	}
	rpl::producer<FullMsgId> documentFailed() const {
		return _documentFailed.events();
	}
	rpl::producer<FullMsgId> secureFailed() const {
		return _secureFailed.events();
	}

	[[nodiscard]] rpl::producer<FullMsgId> nonPremiumDelays() const {
		return _nonPremiumDelays.events();
	}

	void unpause();
	void sendNext();
	void stopSessions();

private:
	struct File;

	void partLoaded(const MTPBool &result, mtpRequestId requestId);
	void partFailed(const MTP::Error &error, mtpRequestId requestId);

	void processPhotoProgress(const FullMsgId &msgId);
	void processPhotoFailed(const FullMsgId &msgId);
	void processDocumentProgress(const FullMsgId &msgId);
	void processDocumentFailed(const FullMsgId &msgId);

	void notifyFailed(FullMsgId id, const File &file);
	void currentFailed();
	void cancelRequests();

	void sendProgressUpdate(
		not_null<HistoryItem*> item,
		Api::SendProgressType type,
		int progress = 0);

	const not_null<ApiWrap*> _api;
	base::flat_map<mtpRequestId, QByteArray> requestsSent;
	base::flat_map<mtpRequestId, int32> docRequestsSent;
	base::flat_map<mtpRequestId, int32> dcMap;
	base::flat_set<mtpRequestId> _nonPremiumDelayed;
	uint32 sentSize = 0; // FileSize: Right now any file size fits 32 bit.
	uint32 sentSizes[MTP::kUploadSessionsCount] = { 0 };

	FullMsgId uploadingId;
	FullMsgId _pausedId;
	std::map<FullMsgId, File> queue;
	base::Timer _nextTimer, _stopSessionsTimer;

	rpl::event_stream<UploadedMedia> _photoReady;
	rpl::event_stream<UploadedMedia> _documentReady;
	rpl::event_stream<UploadSecureDone> _secureReady;
	rpl::event_stream<FullMsgId> _photoProgress;
	rpl::event_stream<FullMsgId> _documentProgress;
	rpl::event_stream<UploadSecureProgress> _secureProgress;
	rpl::event_stream<FullMsgId> _photoFailed;
	rpl::event_stream<FullMsgId> _documentFailed;
	rpl::event_stream<FullMsgId> _secureFailed;
	rpl::event_stream<FullMsgId> _nonPremiumDelays;

	rpl::lifetime _lifetime;

};

} // namespace Storage
