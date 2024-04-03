/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "api/api_common.h"
#include "base/timer.h"
#include "base/weak_ptr.h"
#include "mtproto/facade.h"

class ApiWrap;
struct FilePrepareResult;

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

class Uploader final : public base::has_weak_ptr {
public:
	explicit Uploader(not_null<ApiWrap*> api);
	~Uploader();

	[[nodiscard]] Main::Session &session() const;
	[[nodiscard]] FullMsgId currentUploadId() const;

	void upload(
		FullMsgId itemId,
		const std::shared_ptr<FilePrepareResult> &file);

	void pause(FullMsgId itemId);
	void cancel(FullMsgId itemId);
	void cancelAll();

	[[nodiscard]] rpl::producer<UploadedMedia> photoReady() const {
		return _photoReady.events();
	}
	[[nodiscard]] rpl::producer<UploadedMedia> documentReady() const {
		return _documentReady.events();
	}
	[[nodiscard]] rpl::producer<UploadSecureDone> secureReady() const {
		return _secureReady.events();
	}
	[[nodiscard]] rpl::producer<FullMsgId> photoProgress() const {
		return _photoProgress.events();
	}
	[[nodiscard]] rpl::producer<FullMsgId> documentProgress() const {
		return _documentProgress.events();
	}
	[[nodiscard]] auto secureProgress() const
	-> rpl::producer<UploadSecureProgress> {
		return _secureProgress.events();
	}
	[[nodiscard]] rpl::producer<FullMsgId> photoFailed() const {
		return _photoFailed.events();
	}
	[[nodiscard]] rpl::producer<FullMsgId> documentFailed() const {
		return _documentFailed.events();
	}
	[[nodiscard]] rpl::producer<FullMsgId> secureFailed() const {
		return _secureFailed.events();
	}

	[[nodiscard]] rpl::producer<FullMsgId> nonPremiumDelays() const {
		return _nonPremiumDelays.events();
	}

	void unpause();
	void stopSessions();

private:
	struct Entry;
	struct Request;

	enum class SendResult : uchar {
		Success,
		Failed,
		DcIndexFull,
	};

	void maybeSend();
	[[nodiscard]] bool canAddDcIndex() const;
	[[nodiscard]] std::optional<uchar> chooseDcIndexForNextRequest(
		const base::flat_set<uchar> &used);
	[[nodiscard]] Entry *chooseEntryForNextRequest();
	[[nodiscard]] SendResult sendPart(not_null<Entry*> entry, uchar dcIndex);
	[[nodiscard]] auto sendPendingPart(not_null<Entry*> entry, uchar dcIndex)
		-> SendResult;
	[[nodiscard]] auto sendDocPart(not_null<Entry*> entry, uchar dcIndex)
		-> SendResult;
	[[nodiscard]] auto sendSlicedPart(not_null<Entry*> entry, uchar dcIndex)
		-> SendResult;
	[[nodiscard]] QByteArray readDocPart(not_null<Entry*> entry);
	void removeDcIndex();

	template <typename Prepared>
	void sendPreparedRequest(Prepared &&prepared, Request &&request);

	void maybeFinishFront();
	void finishFront();

	void partLoaded(const MTPBool &result, mtpRequestId requestId);
	void partFailed(const MTP::Error &error, mtpRequestId requestId);
	Request finishRequest(mtpRequestId requestId);

	void processPhotoProgress(FullMsgId itemId);
	void processPhotoFailed(FullMsgId itemId);
	void processDocumentProgress(FullMsgId itemId);
	void processDocumentFailed(FullMsgId itemId);

	void notifyFailed(const Entry &entry);
	void failed(FullMsgId itemId);
	void cancelRequests(FullMsgId itemId);
	void cancelAllRequests();
	void clear();

	void sendProgressUpdate(
		not_null<HistoryItem*> item,
		Api::SendProgressType type,
		int progress = 0);

	const not_null<ApiWrap*> _api;

	std::vector<Entry> _queue;

	base::flat_map<mtpRequestId, Request> _requests;
	std::vector<int> _sentPerDcIndex;

	// Fast requests since the latest dc index addition.
	base::flat_set<uchar> _dcIndicesWithFastRequests;
	crl::time _latestDcIndexAdded = 0;
	crl::time _latestDcIndexRemoved = 0;
	std::vector<Request> _pendingFromRemovedDcIndices;

	FullMsgId _pausedId;
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
