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

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "localimageloader.h"

class FileUploader : public QObject, public RPCSender {
	Q_OBJECT

public:

	FileUploader();
	void uploadMedia(MsgId msgId, const ReadyLocalMedia &image);

	int32 currentOffset(MsgId msgId) const; // -1 means file not found
	int32 fullSize(MsgId msgId) const;

	void cancel(MsgId msgId);
	void confirm(MsgId msgId);

	void clear();

public slots:

	void sendNext();
	void killSessions();

signals:

	void photoReady(MsgId msgId, const MTPInputFile &file);
	void documentReady(MsgId msgId, const MTPInputFile &file);
	void thumbDocumentReady(MsgId msgId, const MTPInputFile &file, const MTPInputFile &thumb);

	void photoProgress(MsgId msgId);
	void documentProgress(MsgId msgId);

	void photoFailed(MsgId msgId);
	void documentFailed(MsgId msgId);

private:

	struct File {
		File(const ReadyLocalMedia &media) : media(media), docSentParts(0) {
			partsCount = media.parts.size();
			if (media.type == ToPrepareDocument) {
				docSize = media.file.isEmpty() ? media.data.size() : media.filesize;
				if (docSize >= 1024 * 1024 || !setPartSize(DocumentUploadPartSize0)) {
					if (docSize > 32 * 1024 * 1024 || !setPartSize(DocumentUploadPartSize1)) {
						if (!setPartSize(DocumentUploadPartSize2)) {
							if (!setPartSize(DocumentUploadPartSize3)) {
								if (!setPartSize(DocumentUploadPartSize4)) {
									LOG(("Upload Error: bad doc size: %1").arg(docSize));
								}
							}
						}
					}
				}
			} else {
				docSize = docPartSize = docPartsCount = 0;
			}
		}
		bool setPartSize(uint32 partSize) {
			docPartSize = partSize;
			docPartsCount = (docSize / docPartSize) + ((docSize % docPartSize) ? 1 : 0);
			return (docPartsCount <= DocumentMaxPartsCount);
		}

		ReadyLocalMedia media;
		int32 partsCount;

		QSharedPointer<QFile> docFile;
		int32 docSentParts;
		int32 docSize;
		int32 docPartSize;
		int32 docPartsCount;
		HashMd5 docHash;
	};
	typedef QMap<MsgId, File> Queue;

	void partLoaded(const MTPBool &result, mtpRequestId requestId);
	bool partFailed(const RPCError &err, mtpRequestId requestId);

	void currentFailed();

	QMap<mtpRequestId, QByteArray> requestsSent;
	QMap<mtpRequestId, int32> docRequestsSent;
	QMap<mtpRequestId, int32> dcMap;
	uint32 sentSize;
	uint32 sentSizes[MTPUploadSessionsCount];
	
	MsgId uploading;
	Queue queue;
	Queue uploaded;
	QTimer nextTimer, killSessionsTimer;

};
