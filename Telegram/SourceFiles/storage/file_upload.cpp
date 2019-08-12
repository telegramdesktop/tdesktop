/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/file_upload.h"

#include "storage/localimageloader.h"
#include "storage/file_download.h"
#include "mtproto/connection.h" // for MTP::kAckSendWaiting
#include "data/data_document.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "main/main_session.h"

namespace Storage {
namespace {

// max 512kb uploaded at the same time in each session
constexpr auto kMaxUploadFileParallelSize = MTP::kUploadSessionsCount * 512 * 1024;

constexpr auto kDocumentMaxPartsCount = 3000;

// 32kb for tiny document ( < 1mb )
constexpr auto kDocumentUploadPartSize0 = 32 * 1024;

// 64kb for little document ( <= 32mb )
constexpr auto kDocumentUploadPartSize1 = 64 * 1024;

// 128kb for small document ( <= 375mb )
constexpr auto kDocumentUploadPartSize2 = 128 * 1024;

// 256kb for medium document ( <= 750mb )
constexpr auto kDocumentUploadPartSize3 = 256 * 1024;

// 512kb for large document ( <= 1500mb )
constexpr auto kDocumentUploadPartSize4 = 512 * 1024;

// One part each half second, if not uploaded faster.
constexpr auto kUploadRequestInterval = crl::time(500);

// How much time without upload causes additional session kill.
constexpr auto kKillSessionTimeout = crl::time(5000);

} // namespace

struct Uploader::File {
	File(const SendMediaReady &media);
	File(const std::shared_ptr<FileLoadResult> &file);

	void setDocSize(int32 size);
	bool setPartSize(uint32 partSize);

	std::shared_ptr<FileLoadResult> file;
	SendMediaReady media;
	int32 partsCount = 0;
	mutable int32 fileSentSize = 0;

	uint64 id() const;
	SendMediaType type() const;
	uint64 thumbId() const;
	const QString &filename() const;

	HashMd5 md5Hash;

	std::unique_ptr<QFile> docFile;
	int32 docSentParts = 0;
	int32 docSize = 0;
	int32 docPartSize = 0;
	int32 docPartsCount = 0;

};

Uploader::File::File(const SendMediaReady &media) : media(media) {
	partsCount = media.parts.size();
	if (type() == SendMediaType::File
		|| type() == SendMediaType::WallPaper
		|| type() == SendMediaType::Audio) {
		setDocSize(media.file.isEmpty()
			? media.data.size()
			: media.filesize);
	} else {
		docSize = docPartSize = docPartsCount = 0;
	}
}
Uploader::File::File(const std::shared_ptr<FileLoadResult> &file)
: file(file) {
	partsCount = (type() == SendMediaType::Photo
		|| type() == SendMediaType::Secure)
		? file->fileparts.size()
		: file->thumbparts.size();
	if (type() == SendMediaType::File
		|| type() == SendMediaType::WallPaper
		|| type() == SendMediaType::Audio) {
		setDocSize(file->filesize);
	} else {
		docSize = docPartSize = docPartsCount = 0;
	}
}

void Uploader::File::setDocSize(int32 size) {
	docSize = size;
	constexpr auto limit0 = 1024 * 1024;
	constexpr auto limit1 = 32 * limit0;
	if (docSize >= limit0 || !setPartSize(kDocumentUploadPartSize0)) {
		if (docSize > limit1 || !setPartSize(kDocumentUploadPartSize1)) {
			if (!setPartSize(kDocumentUploadPartSize2)) {
				if (!setPartSize(kDocumentUploadPartSize3)) {
					if (!setPartSize(kDocumentUploadPartSize4)) {
						LOG(("Upload Error: bad doc size: %1").arg(docSize));
					}
				}
			}
		}
	}
}

bool Uploader::File::setPartSize(uint32 partSize) {
	docPartSize = partSize;
	docPartsCount = (docSize / docPartSize)
		+ ((docSize % docPartSize) ? 1 : 0);
	return (docPartsCount <= kDocumentMaxPartsCount);
}

uint64 Uploader::File::id() const {
	return file ? file->id : media.id;
}

SendMediaType Uploader::File::type() const {
	return file ? file->type : media.type;
}

uint64 Uploader::File::thumbId() const {
	return file ? file->thumbId : media.thumbId;
}

const QString &Uploader::File::filename() const {
	return file ? file->filename : media.filename;
}

Uploader::Uploader(not_null<ApiWrap*> api)
: _api(api) {
	nextTimer.setSingleShot(true);
	connect(&nextTimer, SIGNAL(timeout()), this, SLOT(sendNext()));
	stopSessionsTimer.setSingleShot(true);
	connect(&stopSessionsTimer, SIGNAL(timeout()), this, SLOT(stopSessions()));
}

void Uploader::uploadMedia(
		const FullMsgId &msgId,
		const SendMediaReady &media) {
	if (media.type == SendMediaType::Photo) {
		Auth().data().processPhoto(media.photo, media.photoThumbs);
	} else if (media.type == SendMediaType::File
		|| media.type == SendMediaType::WallPaper
		|| media.type == SendMediaType::Audio) {
		const auto document = media.photoThumbs.empty()
			? Auth().data().processDocument(media.document)
			: Auth().data().processDocument(
				media.document,
				base::duplicate(media.photoThumbs.front().second));
		if (!media.data.isEmpty()) {
			document->setData(media.data);
			if (media.type == SendMediaType::WallPaper) {
				document->checkWallPaperProperties();
			}
			if (document->saveToCache()
				&& media.data.size() <= Storage::kMaxFileInMemory) {
				Auth().data().cache().put(
					document->cacheKey(),
					Storage::Cache::Database::TaggedValue(
						base::duplicate(media.data),
						document->cacheTag()));
			}
		}
		if (!media.file.isEmpty()) {
			document->setLocation(FileLocation(media.file));
		}
	}
	queue.emplace(msgId, File(media));
	sendNext();
}

void Uploader::upload(
		const FullMsgId &msgId,
		const std::shared_ptr<FileLoadResult> &file) {
	if (file->type == SendMediaType::Photo) {
		const auto photo = Auth().data().processPhoto(
			file->photo,
			file->photoThumbs);
		photo->uploadingData = std::make_unique<Data::UploadState>(
			file->partssize);
	} else if (file->type == SendMediaType::File
		|| file->type == SendMediaType::WallPaper
		|| file->type == SendMediaType::Audio) {
		const auto document = file->thumb.isNull()
			? Auth().data().processDocument(file->document)
			: Auth().data().processDocument(
				file->document,
				std::move(file->thumb));
		document->uploadingData = std::make_unique<Data::UploadState>(
			document->size);
		document->setGoodThumbnailOnUpload(
			std::move(file->goodThumbnail),
			std::move(file->goodThumbnailBytes));
		if (!file->content.isEmpty()) {
			document->setData(file->content);
			if (file->type == SendMediaType::WallPaper) {
				document->checkWallPaperProperties();
			}
			if (document->saveToCache()
				&& file->content.size() <= Storage::kMaxFileInMemory) {
				Auth().data().cache().put(
					document->cacheKey(),
					Storage::Cache::Database::TaggedValue(
						base::duplicate(file->content),
						document->cacheTag()));
			}
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
			_photoFailed.fire_copy(j->first);
		} else if (j->second.type() == SendMediaType::File
			|| j->second.type() == SendMediaType::WallPaper
			|| j->second.type() == SendMediaType::Audio) {
			const auto document = Auth().data().document(j->second.id());
			if (document->uploading()) {
				document->status = FileUploadFailed;
			}
			_documentFailed.fire_copy(j->first);
		} else if (j->second.type() == SendMediaType::Secure) {
			_secureFailed.fire_copy(j->first);
		} else {
			Unexpected("Type in Uploader::currentFailed.");
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

void Uploader::stopSessions() {
	for (int i = 0; i < MTP::kUploadSessionsCount; ++i) {
		MTP::stopSession(MTP::uploadDcId(i));
	}
}

void Uploader::sendNext() {
	if (sentSize >= kMaxUploadFileParallelSize || _pausedId.msg) return;

	bool stopping = stopSessionsTimer.isActive();
	if (queue.empty()) {
		if (!stopping) {
			stopSessionsTimer.start(
				MTP::kAckSendWaiting + kKillSessionTimeout);
		}
		return;
	}

	if (stopping) {
		stopSessionsTimer.stop();
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
		? ((uploadingData.type() == SendMediaType::Photo
			|| uploadingData.type() == SendMediaType::Secure)
			? uploadingData.file->fileparts
			: uploadingData.file->thumbparts)
		: uploadingData.media.parts;
	const auto partsOfId = uploadingData.file
		? ((uploadingData.type() == SendMediaType::Photo
			|| uploadingData.type() == SendMediaType::Secure)
			? uploadingData.file->id
			: uploadingData.file->thumbId)
		: uploadingData.media.thumbId;
	if (parts.isEmpty()) {
		if (uploadingData.docSentParts >= uploadingData.docPartsCount) {
			if (requestsSent.empty() && docRequestsSent.empty()) {
				const auto options = uploadingData.file
					? uploadingData.file->to.options
					: Api::SendOptions();
				const auto edit = uploadingData.file &&
					uploadingData.file->edit;
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
					_photoReady.fire({ uploadingId, options, file, edit });
				} else if (uploadingData.type() == SendMediaType::File
					|| uploadingData.type() == SendMediaType::WallPaper
					|| uploadingData.type() == SendMediaType::Audio) {
					QByteArray docMd5(32, Qt::Uninitialized);
					hashMd5Hex(uploadingData.md5Hash.result(), docMd5.data());

					const auto file = (uploadingData.docSize > kUseBigFilesFrom)
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
						_thumbDocumentReady.fire({
							uploadingId,
							options,
							file,
							thumb,
							edit });
					} else {
						_documentReady.fire({
							uploadingId,
							options,
							file,
							edit });
					}
				} else if (uploadingData.type() == SendMediaType::Secure) {
					_secureReady.fire({
						uploadingId,
						uploadingData.id(),
						uploadingData.partsCount });
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
			if (uploadingData.docSize <= kUseBigFilesFrom) {
				uploadingData.md5Hash.feed(toSend.constData(), toSend.size());
			}
		} else {
			const auto offset = uploadingData.docSentParts
				* uploadingData.docPartSize;
			toSend = content.mid(offset, uploadingData.docPartSize);
			if ((uploadingData.type() == SendMediaType::File
				|| uploadingData.type() == SendMediaType::WallPaper
				|| uploadingData.type() == SendMediaType::Audio)
				&& uploadingData.docSentParts <= kUseBigFilesFrom) {
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
		if (uploadingData.docSize > kUseBigFilesFrom) {
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
	nextTimer.start(kUploadRequestInterval);
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
	for (const auto &requestData : requestsSent) {
		MTP::cancel(requestData.first);
	}
	requestsSent.clear();
	for (const auto &requestData : docRequestsSent) {
		MTP::cancel(requestData.first);
	}
	docRequestsSent.clear();
	dcMap.clear();
	sentSize = 0;
	for (int i = 0; i < MTP::kUploadSessionsCount; ++i) {
		MTP::stopSession(MTP::uploadDcId(i));
		sentSizes[i] = 0;
	}
	stopSessionsTimer.stop();
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
				const auto photo = Auth().data().photo(file.id());
				if (photo->uploading() && file.file) {
					photo->uploadingData->size = file.file->partssize;
					photo->uploadingData->offset = file.fileSentSize;
				}
				_photoProgress.fire_copy(fullId);
			} else if (file.type() == SendMediaType::File
				|| file.type() == SendMediaType::WallPaper
				|| file.type() == SendMediaType::Audio) {
				const auto document = Auth().data().document(file.id());
				if (document->uploading()) {
					const auto doneParts = file.docSentParts
						- int(docRequestsSent.size());
					document->uploadingData->offset = std::min(
						document->uploadingData->size,
						doneParts * file.docPartSize);
				}
				_documentProgress.fire_copy(fullId);
			} else if (file.type() == SendMediaType::Secure) {
				file.fileSentSize += sentPartSize;
				_secureProgress.fire_copy({
					fullId,
					file.fileSentSize,
					file.file->partssize });
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
