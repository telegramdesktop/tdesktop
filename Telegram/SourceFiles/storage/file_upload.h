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

	~Uploader();

public slots:
	void unpause();
	void sendNext();
	void stopSessions();

signals:
	void photoReady(const FullMsgId &msgId, bool silent, const MTPInputFile &file);
	void documentReady(const FullMsgId &msgId, bool silent, const MTPInputFile &file);
	void thumbDocumentReady(const FullMsgId &msgId, bool silent, const MTPInputFile &file, const MTPInputFile &thumb);

	void photoProgress(const FullMsgId &msgId);
	void documentProgress(const FullMsgId &msgId);

	void photoFailed(const FullMsgId &msgId);
	void documentFailed(const FullMsgId &msgId);

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

};

} // namespace Storage
