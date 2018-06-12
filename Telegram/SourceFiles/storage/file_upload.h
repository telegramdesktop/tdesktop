/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

struct FileLoadResult;
struct SendMediaReady;

namespace Storage {

struct UploadedPhoto {
	FullMsgId fullId;
	bool silent = false;
	MTPInputFile file;
};

struct UploadedDocument {
	FullMsgId fullId;
	bool silent = false;
	MTPInputFile file;
};

struct UploadedThumbDocument {
	FullMsgId fullId;
	bool silent = false;
	MTPInputFile file;
	MTPInputFile thumb;
};

struct UploadSecureProgress {
	FullMsgId fullId;
	int offset = 0;
	int size = 0;
};

struct UploadSecureDone {
	FullMsgId fullId;
	uint64 fileId = 0;
	int partsCount = 0;
};

class Uploader : public QObject, public RPCSender {
	Q_OBJECT

public:
	Uploader();
	void uploadMedia(const FullMsgId &msgId, const SendMediaReady &image);
	void upload(
		const FullMsgId &msgId,
		const std::shared_ptr<FileLoadResult> &file);

	int32 currentOffset(const FullMsgId &msgId) const; // -1 means file not found
	int32 fullSize(const FullMsgId &msgId) const;

	void cancel(const FullMsgId &msgId);
	void pause(const FullMsgId &msgId);
	void confirm(const FullMsgId &msgId);

	void clear();

	rpl::producer<UploadedPhoto> photoReady() const {
		return _photoReady.events();
	}
	rpl::producer<UploadedDocument> documentReady() const {
		return _documentReady.events();
	}
	rpl::producer<UploadedThumbDocument> thumbDocumentReady() const {
		return _thumbDocumentReady.events();
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

	~Uploader();

public slots:
	void unpause();
	void sendNext();
	void stopSessions();

private:
	struct File;

	void partLoaded(const MTPBool &result, mtpRequestId requestId);
	bool partFailed(const RPCError &err, mtpRequestId requestId);

	void currentFailed();

	base::flat_map<mtpRequestId, QByteArray> requestsSent;
	base::flat_map<mtpRequestId, int32> docRequestsSent;
	base::flat_map<mtpRequestId, int32> dcMap;
	uint32 sentSize = 0;
	uint32 sentSizes[MTP::kUploadSessionsCount] = { 0 };

	FullMsgId uploadingId;
	FullMsgId _pausedId;
	std::map<FullMsgId, File> queue;
	std::map<FullMsgId, File> uploaded;
	QTimer nextTimer, stopSessionsTimer;

	rpl::event_stream<UploadedPhoto> _photoReady;
	rpl::event_stream<UploadedDocument> _documentReady;
	rpl::event_stream<UploadedThumbDocument> _thumbDocumentReady;
	rpl::event_stream<UploadSecureDone> _secureReady;
	rpl::event_stream<FullMsgId> _photoProgress;
	rpl::event_stream<FullMsgId> _documentProgress;
	rpl::event_stream<UploadSecureProgress> _secureProgress;
	rpl::event_stream<FullMsgId> _photoFailed;
	rpl::event_stream<FullMsgId> _documentFailed;
	rpl::event_stream<FullMsgId> _secureFailed;

};

} // namespace Storage
