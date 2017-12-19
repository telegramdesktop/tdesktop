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
	void killSessions();

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
	QTimer nextTimer, killSessionsTimer;

};

} // namespace Storage
