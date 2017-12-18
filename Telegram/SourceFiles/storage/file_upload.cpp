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
#include "storage/file_upload.h"

#include "data/data_document.h"
#include "data/data_photo.h"

namespace Storage {
namespace {

constexpr auto kMaxUploadFileParallelSize = MTP::kUploadSessionsCount * 512 * 1024; // max 512kb uploaded at the same time in each session

} // namespace

Uploader::Uploader() {
	nextTimer.setSingleShot(true);
	connect(&nextTimer, SIGNAL(timeout()), this, SLOT(sendNext()));
	killSessionsTimer.setSingleShot(true);
	connect(&killSessionsTimer, SIGNAL(timeout()), this, SLOT(killSessions()));
}

void Uploader::uploadMedia(const FullMsgId &msgId, const SendMediaReady &media) {
	if (media.type == SendMediaType::Photo) {
		App::feedPhoto(media.photo, media.photoThumbs);
	} else if (media.type == SendMediaType::File || media.type == SendMediaType::Audio) {
		DocumentData *document;
		if (media.photoThumbs.isEmpty()) {
			document = App::feedDocument(media.document);
		} else {
			document = App::feedDocument(media.document, media.photoThumbs.begin().value());
		}
		document->status = FileUploading;
		if (!media.data.isEmpty()) {
			document->setData(media.data);
		}
		if (!media.file.isEmpty()) {
			document->setLocation(FileLocation(media.file));
		}
	}
	queue.emplace(msgId, File(media));
	sendNext();
}

void Uploader::upload(const FullMsgId &msgId, const FileLoadResultPtr &file) {
	if (file->type == SendMediaType::Photo) {
		auto photo = App::feedPhoto(file->photo, file->photoThumbs);
		photo->uploadingData = std::make_unique<PhotoData::UploadingData>(file->partssize);
	} else if (file->type == SendMediaType::File || file->type == SendMediaType::Audio) {
		auto document = file->thumb.isNull() ? App::feedDocument(file->document) : App::feedDocument(file->document, file->thumb);
		document->status = FileUploading;
		if (!file->content.isEmpty()) {
			document->setData(file->content);
		}
		if (!file->filepath.isEmpty()) {
			document->setLocation(FileLocation(file->filepath));
		}
	}
	queue.emplace(msgId, File(file));
	sendNext();
}

void Uploader::currentFailed() {
	auto j = queue.find(uploadingId);
	if (j != queue.end()) {
		if (j->second.type() == SendMediaType::Photo) {
			emit photoFailed(j->first);
		} else if (j->second.type() == SendMediaType::File) {
			const auto document = App::document(j->second.id());
			if (document->status == FileUploading) {
				document->status = FileUploadFailed;
			}
			emit documentFailed(j->first);
		}
		queue.erase(j);
	}

	requestsSent.clear();
	docRequestsSent.clear();
	dcMap.clear();
	uploadingId = FullMsgId();
	sentSize = 0;
	for (int i = 0; i < MTP::kUploadSessionsCount; ++i) {
		sentSizes[i] = 0;
	}

	sendNext();
}

void Uploader::killSessions() {
	for (int i = 0; i < MTP::kUploadSessionsCount; ++i) {
		MTP::stopSession(MTP::uploadDcId(i));
	}
}

void Uploader::sendNext() {
	if (sentSize >= kMaxUploadFileParallelSize || _pausedId.msg) return;

	bool killing = killSessionsTimer.isActive();
	if (queue.empty()) {
		if (!killing) {
			killSessionsTimer.start(MTPAckSendWaiting + MTPKillFileSessionTimeout);
		}
		return;
	}

	if (killing) {
		killSessionsTimer.stop();
	}
	auto i = uploadingId.msg ? queue.find(uploadingId) : queue.begin();
	if (!uploadingId.msg) {
		uploadingId = i->first;
	} else if (i == queue.end()) {
		i = queue.begin();
		uploadingId = i->first;
	}
	auto &uploadingData = i->second;

	auto todc = 0;
	for (auto dc = 1; dc != MTP::kUploadSessionsCount; ++dc) {
		if (sentSizes[dc] < sentSizes[todc]) {
			todc = dc;
		}
	}

	auto &parts = uploadingData.file
		? (uploadingData.type() == SendMediaType::Photo
			? uploadingData.file->fileparts
			: uploadingData.file->thumbparts)
		: uploadingData.media.parts;
	const auto partsOfId = uploadingData.file
		? (uploadingData.type() == SendMediaType::Photo
			? uploadingData.file->id
			: uploadingData.file->thumbId)
		: uploadingData.media.thumbId;
	if (parts.isEmpty()) {
		if (uploadingData.docSentParts >= uploadingData.docPartsCount) {
			if (requestsSent.empty() && docRequestsSent.empty()) {
				const auto silent = uploadingData.file
					&& uploadingData.file->to.silent;
				if (uploadingData.type() == SendMediaType::Photo) {
					auto photoFilename = uploadingData.filename();
					if (!photoFilename.endsWith(qstr(".jpg"), Qt::CaseInsensitive)) {
						// Server has some extensions checking for inputMediaUploadedPhoto,
						// so force the extension to be .jpg anyway. It doesn't matter,
						// because the filename from inputFile is not used anywhere.
						photoFilename += qstr(".jpg");
					}
					const auto md5 = uploadingData.file
						? uploadingData.file->filemd5
						: uploadingData.media.jpeg_md5;
					const auto file = MTP_inputFile(
						MTP_long(uploadingData.id()),
						MTP_int(uploadingData.partsCount),
						MTP_string(photoFilename),
						MTP_bytes(md5));
					emit photoReady(uploadingId, silent, file);
				} else if (uploadingData.type() == SendMediaType::File
					|| uploadingData.type() == SendMediaType::Audio) {
					QByteArray docMd5(32, Qt::Uninitialized);
					hashMd5Hex(uploadingData.md5Hash.result(), docMd5.data());

					const auto file = (uploadingData.docSize > UseBigFilesFrom)
						? MTP_inputFileBig(
							MTP_long(uploadingData.id()),
							MTP_int(uploadingData.docPartsCount),
							MTP_string(uploadingData.filename()))
						: MTP_inputFile(
							MTP_long(uploadingData.id()),
							MTP_int(uploadingData.docPartsCount),
							MTP_string(uploadingData.filename()),
							MTP_bytes(docMd5));
					if (uploadingData.partsCount) {
						const auto thumbFilename = uploadingData.file
							? uploadingData.file->thumbname
							: (qsl("thumb.") + uploadingData.media.thumbExt);
						const auto thumbMd5 = uploadingData.file
							? uploadingData.file->thumbmd5
							: uploadingData.media.jpeg_md5;
						const auto thumb = MTP_inputFile(
							MTP_long(uploadingData.thumbId()),
							MTP_int(uploadingData.partsCount),
							MTP_string(thumbFilename),
							MTP_bytes(thumbMd5));
						emit thumbDocumentReady(
							uploadingId,
							silent,
							file,
							thumb);
					} else {
						emit documentReady(uploadingId, silent, file);
					}
				}
				queue.erase(uploadingId);
				uploadingId = FullMsgId();
				sendNext();
			}
			return;
		}

		auto &content = uploadingData.file
			? uploadingData.file->content
			: uploadingData.media.data;
		QByteArray toSend;
		if (content.isEmpty()) {
			if (!uploadingData.docFile) {
				const auto filepath = uploadingData.file
					? uploadingData.file->filepath
					: uploadingData.media.file;
				uploadingData.docFile = std::make_unique<QFile>(filepath);
				if (!uploadingData.docFile->open(QIODevice::ReadOnly)) {
					currentFailed();
					return;
				}
			}
			toSend = uploadingData.docFile->read(uploadingData.docPartSize);
			if (uploadingData.docSize <= UseBigFilesFrom) {
				uploadingData.md5Hash.feed(toSend.constData(), toSend.size());
			}
		} else {
			const auto offset = uploadingData.docSentParts
				* uploadingData.docPartSize;
			toSend = content.mid(offset, uploadingData.docPartSize);
			if ((uploadingData.type() == SendMediaType::File
				|| uploadingData.type() == SendMediaType::Audio)
				&& uploadingData.docSentParts <= UseBigFilesFrom) {
				uploadingData.md5Hash.feed(toSend.constData(), toSend.size());
			}
		}
		if ((toSend.size() > uploadingData.docPartSize)
			|| ((toSend.size() < uploadingData.docPartSize
				&& uploadingData.docSentParts + 1 != uploadingData.docPartsCount))) {
			currentFailed();
			return;
		}
		mtpRequestId requestId;
		if (uploadingData.docSize > UseBigFilesFrom) {
			requestId = MTP::send(
				MTPupload_SaveBigFilePart(
					MTP_long(uploadingData.id()),
					MTP_int(uploadingData.docSentParts),
					MTP_int(uploadingData.docPartsCount),
					MTP_bytes(toSend)),
				rpcDone(&Uploader::partLoaded),
				rpcFail(&Uploader::partFailed),
				MTP::uploadDcId(todc));
		} else {
			requestId = MTP::send(
				MTPupload_SaveFilePart(
					MTP_long(uploadingData.id()),
					MTP_int(uploadingData.docSentParts),
					MTP_bytes(toSend)),
				rpcDone(&Uploader::partLoaded),
				rpcFail(&Uploader::partFailed),
				MTP::uploadDcId(todc));
		}
		docRequestsSent.emplace(requestId, uploadingData.docSentParts);
		dcMap.emplace(requestId, todc);
		sentSize += uploadingData.docPartSize;
		sentSizes[todc] += uploadingData.docPartSize;

		uploadingData.docSentParts++;
	} else {
		auto part = parts.begin();

		const auto requestId = MTP::send(
			MTPupload_SaveFilePart(
				MTP_long(partsOfId),
				MTP_int(part.key()),
				MTP_bytes(part.value())),
			rpcDone(&Uploader::partLoaded),
			rpcFail(&Uploader::partFailed),
			MTP::uploadDcId(todc));
		requestsSent.emplace(requestId, part.value());
		dcMap.emplace(requestId, todc);
		sentSize += part.value().size();
		sentSizes[todc] += part.value().size();

		parts.erase(part);
	}
	nextTimer.start(UploadRequestInterval);
}

void Uploader::cancel(const FullMsgId &msgId) {
	uploaded.erase(msgId);
	if (uploadingId == msgId) {
		currentFailed();
	} else {
		queue.erase(msgId);
	}
}

void Uploader::pause(const FullMsgId &msgId) {
	_pausedId = msgId;
}

void Uploader::unpause() {
	_pausedId = FullMsgId();
	sendNext();
}

void Uploader::confirm(const FullMsgId &msgId) {
}

void Uploader::clear() {
	uploaded.clear();
	queue.clear();
	for (const auto &[requestId, requestData] : requestsSent) {
		MTP::cancel(requestId);
	}
	requestsSent.clear();
	for (const auto &[requestId, requestIndex] : docRequestsSent) {
		MTP::cancel(requestId);
	}
	docRequestsSent.clear();
	dcMap.clear();
	sentSize = 0;
	for (int i = 0; i < MTP::kUploadSessionsCount; ++i) {
		MTP::stopSession(MTP::uploadDcId(i));
		sentSizes[i] = 0;
	}
	killSessionsTimer.stop();
}

void Uploader::partLoaded(const MTPBool &result, mtpRequestId requestId) {
	auto j = docRequestsSent.end();
	auto i = requestsSent.find(requestId);
	if (i == requestsSent.cend()) {
		j = docRequestsSent.find(requestId);
	}
	if (i != requestsSent.cend() || j != docRequestsSent.cend()) {
		if (mtpIsFalse(result)) { // failed to upload current file
			currentFailed();
			return;
		} else {
			auto dcIt = dcMap.find(requestId);
			if (dcIt == dcMap.cend()) { // must not happen
				currentFailed();
				return;
			}
			auto dc = dcIt->second;
			dcMap.erase(dcIt);

			int32 sentPartSize = 0;
			auto k = queue.find(uploadingId);
			Assert(k != queue.cend());
			auto &[fullId, file] = *k;
			if (i != requestsSent.cend()) {
				sentPartSize = i->second.size();
				requestsSent.erase(i);
			} else {
				sentPartSize = file.docPartSize;
				docRequestsSent.erase(j);
			}
			sentSize -= sentPartSize;
			sentSizes[dc] -= sentPartSize;
			if (file.type() == SendMediaType::Photo) {
				file.fileSentSize += sentPartSize;
				const auto photo = App::photo(file.id());
				if (photo->uploading() && file.file) {
					photo->uploadingData->size = file.file->partssize;
					photo->uploadingData->offset = file.fileSentSize;
				}
				emit photoProgress(fullId);
			} else if (file.type() == SendMediaType::File
				|| file.type() == SendMediaType::Audio) {
				const auto document = App::document(file.id());
				if (document->uploading()) {
					const auto doneParts = file.docSentParts
						- int(docRequestsSent.size());
					document->uploadOffset = doneParts * file.docPartSize;
					if (document->uploadOffset > document->size) {
						document->uploadOffset = document->size;
					}
				}
				emit documentProgress(fullId);
			}
		}
	}

	sendNext();
}

bool Uploader::partFailed(const RPCError &error, mtpRequestId requestId) {
	if (MTP::isDefaultHandledError(error)) return false;

	// failed to upload current file
	if ((requestsSent.find(requestId) != requestsSent.cend())
		|| (docRequestsSent.find(requestId) != docRequestsSent.cend())) {
		currentFailed();
	}
	sendNext();
	return true;
}

Uploader::~Uploader() {
	clear();
}

} // namespace Storage
