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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "fileuploader.h"

FileUploader::FileUploader() : sentSize(0) {
	memset(sentSizes, 0, sizeof(sentSizes));
	nextTimer.setSingleShot(true);
	connect(&nextTimer, SIGNAL(timeout()), this, SLOT(sendNext()));
	killSessionsTimer.setSingleShot(true);
	connect(&killSessionsTimer, SIGNAL(timeout()), this, SLOT(killSessions()));
}

void FileUploader::uploadMedia(const FullMsgId &msgId, const ReadyLocalMedia &media) {
	if (media.type == PreparePhoto) {
		App::feedPhoto(media.photo, media.photoThumbs);
	} else if (media.type == PrepareDocument) {
		DocumentData *document;
		if (media.photoThumbs.isEmpty()) {
			document = App::feedDocument(media.document);
		} else {
			document = App::feedDocument(media.document, media.photoThumbs.begin().value());
		}
		document->status = FileUploading;
		if (!media.file.isEmpty()) {
			document->setLocation(FileLocation(StorageFilePartial, media.file));
		}
	} else if (media.type == PrepareAudio) {
		AudioData *audio = App::feedAudio(media.audio);
		audio->status = FileUploading;
		audio->setData(media.data);
	}
	queue.insert(msgId, File(media));
	sendNext();
}

void FileUploader::upload(const FullMsgId &msgId, const FileLoadResultPtr &file) {
	if (file->type == PreparePhoto) {
		PhotoData *photo = App::feedPhoto(file->photo, file->photoThumbs);
		photo->uploadingData = new PhotoData::UploadingData(file->partssize);
	} else if (file->type == PrepareDocument) {
		DocumentData *document;
		if (file->thumb.isNull()) {
			document = App::feedDocument(file->document);
		} else {
			document = App::feedDocument(file->document, file->thumb);
		}
		document->status = FileUploading;
		if (!file->filepath.isEmpty()) {
			document->setLocation(FileLocation(StorageFilePartial, file->filepath));
		}
	} else if (file->type == PrepareAudio) {
		AudioData *audio = App::feedAudio(file->audio);
		audio->status = FileUploading;
		audio->setData(file->content);
	}
	queue.insert(msgId, File(file));
	sendNext();
}

void FileUploader::currentFailed() {
	Queue::iterator j = queue.find(uploading);
	if (j != queue.end()) {
		if (j->type() == PreparePhoto) {
			emit photoFailed(j.key());
		} else if (j->type() == PrepareDocument) {
			DocumentData *doc = App::document(j->id());
			if (doc->status == FileUploading) {
				doc->status = FileUploadFailed;
			}
			emit documentFailed(j.key());
		} else if (j->type() == PrepareAudio) {
			AudioData *audio = App::audio(j->id());
			if (audio->status == FileUploading) {
				audio->status = FileUploadFailed;
			}
			emit audioFailed(j.key());
		}
		queue.erase(j);
	}

	requestsSent.clear();
	docRequestsSent.clear();
	dcMap.clear();
	uploading = FullMsgId();
	sentSize = 0;
	for (int i = 0; i < MTPUploadSessionsCount; ++i) {
		sentSizes[i] = 0;
	}

	sendNext();
}

void FileUploader::killSessions() {
	for (int i = 0; i < MTPUploadSessionsCount; ++i) {
		MTP::stopSession(MTP::upl[i]);
	}
}

void FileUploader::sendNext() {
	if (sentSize >= MaxUploadFileParallelSize || _paused.msg) return;

	bool killing = killSessionsTimer.isActive();
	if (queue.isEmpty()) {
		if (!killing) {
			killSessionsTimer.start(MTPAckSendWaiting + MTPKillFileSessionTimeout);
		}
		return;
	}

	if (killing) {
		killSessionsTimer.stop();
	}
	Queue::iterator i = uploading.msg ? queue.find(uploading) : queue.begin();
	if (!uploading.msg) {
		uploading = i.key();
	} else if (i == queue.end()) {
		i = queue.begin(); 
		uploading = i.key();
	}
	int todc = 0;
	for (int dc = 1; dc < MTPUploadSessionsCount; ++dc) {
		if (sentSizes[dc] < sentSizes[todc]) {
			todc = dc;
		}
	}

	UploadFileParts &parts(i->file ? (i->type() == PreparePhoto ? i->file->fileparts : i->file->thumbparts) : i->media.parts);
	uint64 partsOfId(i->file ? (i->type() == PreparePhoto ? i->file->id : i->file->thumbId) : i->media.thumbId);
	if (parts.isEmpty()) {
		if (i->docSentParts >= i->docPartsCount) {
			if (requestsSent.isEmpty() && docRequestsSent.isEmpty()) {
				if (i->type() == PreparePhoto) {
					emit photoReady(uploading, MTP_inputFile(MTP_long(i->id()), MTP_int(i->partsCount), MTP_string(i->filename()), MTP_string(i->file ? i->file->filemd5 : i->media.jpeg_md5)));
				} else if (i->type() == PrepareDocument) {
					QByteArray docMd5(32, Qt::Uninitialized);
					hashMd5Hex(i->md5Hash.result(), docMd5.data());

					MTPInputFile doc = (i->docSize > UseBigFilesFrom) ? MTP_inputFileBig(MTP_long(i->id()), MTP_int(i->docPartsCount), MTP_string(i->filename())) : MTP_inputFile(MTP_long(i->id()), MTP_int(i->docPartsCount), MTP_string(i->filename()), MTP_string(docMd5));
					if (i->partsCount) {
						emit thumbDocumentReady(uploading, doc, MTP_inputFile(MTP_long(i->thumbId()), MTP_int(i->partsCount), MTP_string(i->file ? i->file->thumbname : (qsl("thumb.") + i->media.thumbExt)), MTP_string(i->file ? i->file->thumbmd5 : i->media.jpeg_md5)));
					} else {
						emit documentReady(uploading, doc);
					}
				} else if (i->type() == PrepareAudio) {
					QByteArray audioMd5(32, Qt::Uninitialized);
					hashMd5Hex(i->md5Hash.result(), audioMd5.data());

					MTPInputFile audio = (i->docSize > UseBigFilesFrom) ? MTP_inputFileBig(MTP_long(i->id()), MTP_int(i->docPartsCount), MTP_string(i->filename())) : MTP_inputFile(MTP_long(i->id()), MTP_int(i->docPartsCount), MTP_string(i->filename()), MTP_string(audioMd5));
					emit audioReady(uploading, audio);
				}
				queue.remove(uploading);
				uploading = FullMsgId();
				sendNext();
			}
			return;
		}

		QByteArray &content(i->file ? i->file->content : i->media.data);
		QByteArray toSend;
		if (content.isEmpty()) {
			if (!i->docFile) {
				i->docFile.reset(new QFile(i->file ? i->file->filepath : i->media.file));
				if (!i->docFile->open(QIODevice::ReadOnly)) {
					currentFailed();
					return;
				}
			}
			toSend = i->docFile->read(i->docPartSize);
			if (i->docSize <= UseBigFilesFrom) {
				i->md5Hash.feed(toSend.constData(), toSend.size());
			}
		} else {
			toSend = content.mid(i->docSentParts * i->docPartSize, i->docPartSize);
			if ((i->type() == PrepareDocument || i->type() == PrepareAudio) && i->docSentParts <= UseBigFilesFrom) {
				i->md5Hash.feed(toSend.constData(), toSend.size());
			}
		}
		if (toSend.size() > i->docPartSize || (toSend.size() < i->docPartSize && i->docSentParts + 1 != i->docPartsCount)) {
			currentFailed();
			return;
		}
		mtpRequestId requestId;
		if (i->docSize > UseBigFilesFrom) {
			requestId = MTP::send(MTPupload_SaveBigFilePart(MTP_long(i->id()), MTP_int(i->docSentParts), MTP_int(i->docPartsCount), MTP_string(toSend)), rpcDone(&FileUploader::partLoaded), rpcFail(&FileUploader::partFailed), MTP::upl[todc]);
		} else {
			requestId = MTP::send(MTPupload_SaveFilePart(MTP_long(i->id()), MTP_int(i->docSentParts), MTP_string(toSend)), rpcDone(&FileUploader::partLoaded), rpcFail(&FileUploader::partFailed), MTP::upl[todc]);
		}
		docRequestsSent.insert(requestId, i->docSentParts);
		dcMap.insert(requestId, todc);
		sentSize += i->docPartSize;
		sentSizes[todc] += i->docPartSize;

		i->docSentParts++;
	} else {
		UploadFileParts::iterator part = parts.begin();
	
		mtpRequestId requestId = MTP::send(MTPupload_SaveFilePart(MTP_long(partsOfId), MTP_int(part.key()), MTP_string(part.value())), rpcDone(&FileUploader::partLoaded), rpcFail(&FileUploader::partFailed), MTP::upl[todc]);
		requestsSent.insert(requestId, part.value());
		dcMap.insert(requestId, todc);
		sentSize += part.value().size();
		sentSizes[todc] += part.value().size();

		parts.erase(part);
	}
	nextTimer.start(UploadRequestInterval);
}

void FileUploader::cancel(const FullMsgId &msgId) {
	uploaded.remove(msgId);
	if (uploading == msgId) {
		currentFailed();
	} else {
		queue.remove(msgId);
	}
}

void FileUploader::pause(const FullMsgId &msgId) {
	_paused = msgId;
}

void FileUploader::unpause() {
	_paused = FullMsgId();
	sendNext();
}

void FileUploader::confirm(const FullMsgId &msgId) {
}

void FileUploader::clear() {
	uploaded.clear();
	queue.clear();
	for (QMap<mtpRequestId, QByteArray>::const_iterator i = requestsSent.cbegin(), e = requestsSent.cend(); i != e; ++i) {
		MTP::cancel(i.key());
	}
	requestsSent.clear();
	for (QMap<mtpRequestId, int32>::const_iterator i = docRequestsSent.cbegin(), e = docRequestsSent.cend(); i != e; ++i) {
		MTP::cancel(i.key());
	}
	docRequestsSent.clear();
	dcMap.clear();
	sentSize = 0;
	for (int32 i = 0; i < MTPUploadSessionsCount; ++i) {
		MTP::stopSession(MTP::upl[i]);
		sentSizes[i] = 0;
	}
	killSessionsTimer.stop();
}

void FileUploader::partLoaded(const MTPBool &result, mtpRequestId requestId) {
	QMap<mtpRequestId, int32>::iterator j = docRequestsSent.end();
	QMap<mtpRequestId, QByteArray>::iterator i = requestsSent.find(requestId);
	if (i == requestsSent.cend()) {
		j = docRequestsSent.find(requestId);
	}
	if (i != requestsSent.cend() || j != docRequestsSent.cend()) {
		if (mtpIsFalse(result)) { // failed to upload current file
			currentFailed();
			return;
		} else {
			QMap<mtpRequestId, int32>::iterator dcIt = dcMap.find(requestId);
			if (dcIt == dcMap.cend()) { // must not happen
				currentFailed();
				return;
			}
			int32 dc = dcIt.value();
			dcMap.erase(dcIt);

			int32 sentPartSize = 0;
			Queue::const_iterator k = queue.constFind(uploading);
			if (i != requestsSent.cend()) {
				sentPartSize = i.value().size();
				requestsSent.erase(i);
			} else {
				sentPartSize = k->docPartSize;
				docRequestsSent.erase(j);
			}
			sentSize -= sentPartSize;
			sentSizes[dc] -= sentPartSize;
			if (k->type() == PreparePhoto) {
				k->fileSentSize += sentPartSize;
				PhotoData *photo = App::photo(k->id());
				if (photo->uploading() && k->file) {
					photo->uploadingData->size = k->file->partssize;
					photo->uploadingData->offset = k->fileSentSize;
				}
				emit photoProgress(k.key());
			} else if (k->type() == PrepareDocument) {
				DocumentData *doc = App::document(k->id());
				if (doc->uploading()) {
					doc->uploadOffset = (k->docSentParts - docRequestsSent.size()) * k->docPartSize;
					if (doc->uploadOffset > doc->size) {
						doc->uploadOffset = doc->size;
					}
				}
				emit documentProgress(k.key());
			} else if (k->type() == PrepareAudio) {
				AudioData *audio = App::audio(k->id());
				if (audio->uploading()) {
					audio->uploadOffset = (k->docSentParts - docRequestsSent.size()) * k->docPartSize;
					if (audio->uploadOffset > audio->size) {
						audio->uploadOffset = audio->size;
					}
				}
				emit audioProgress(k.key());
			}
		}
	}

	sendNext();
}

bool FileUploader::partFailed(const RPCError &error, mtpRequestId requestId) {
	if (mtpIsFlood(error)) return false;

	if (requestsSent.constFind(requestId) != requestsSent.cend() || docRequestsSent.constFind(requestId) != docRequestsSent.cend()) { // failed to upload current file
		currentFailed();
	}
	sendNext();
	return true;
}
