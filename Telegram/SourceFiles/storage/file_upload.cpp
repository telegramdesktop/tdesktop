/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/file_upload.h"

#include "api/api_editing.h"
#include "api/api_send_progress.h"
#include "storage/localimageloader.h"
#include "storage/file_download.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "ui/image/image_location_factory.h"
#include "history/history_item.h"
#include "history/history.h"
#include "core/file_location.h"
#include "core/mime_type.h"
#include "main/main_session.h"
#include "apiwrap.h"

namespace Storage {
namespace {

// max 512kb uploaded at the same time in each session
constexpr auto kMaxUploadFileParallelSize = MTP::kUploadSessionsCount * 512 * 1024;

constexpr auto kDocumentMaxPartsCountDefault = 4000;

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
constexpr auto kKillSessionTimeout = 15 * crl::time(1000);

[[nodiscard]] const char *ThumbnailFormat(const QString &mime) {
	return Core::IsMimeSticker(mime) ? "WEBP" : "JPG";
}

} // namespace

struct Uploader::File {
	explicit File(const std::shared_ptr<FilePrepareResult> &file);

	void setDocSize(int64 size);
	bool setPartSize(uint32 partSize);

	std::shared_ptr<FilePrepareResult> file;
	const std::vector<QByteArray> &parts;
	const uint64 partsOfId = 0;
	int partsSent = 0;

	mutable int64 fileSentSize = 0;

	HashMd5 md5Hash;

	std::unique_ptr<QFile> docFile;
	int64 docSize = 0;
	int64 docPartSize = 0;
	int docSentParts = 0;
	int docPartsCount = 0;

};

Uploader::File::File(const std::shared_ptr<FilePrepareResult> &file)
: file(file)
, parts((file->type == SendMediaType::Photo
	|| file->type == SendMediaType::Secure)
		? file->fileparts
		: file->thumbparts)
, partsOfId((file->type == SendMediaType::Photo
	|| file->type == SendMediaType::Secure)
		? file->id
		: file->thumbId) {
	if (file->type == SendMediaType::File
		|| file->type == SendMediaType::ThemeFile
		|| file->type == SendMediaType::Audio) {
		setDocSize(file->filesize);
	} else {
		docSize = docPartSize = docPartsCount = 0;
	}
}

void Uploader::File::setDocSize(int64 size) {
	docSize = size;
	constexpr auto limit0 = 1024 * 1024;
	constexpr auto limit1 = 32 * limit0;
	if (docSize >= limit0 || !setPartSize(kDocumentUploadPartSize0)) {
		if (docSize > limit1 || !setPartSize(kDocumentUploadPartSize1)) {
			if (!setPartSize(kDocumentUploadPartSize2)) {
				if (!setPartSize(kDocumentUploadPartSize3)) {
					setPartSize(kDocumentUploadPartSize4);
				}
			}
		}
	}
}

bool Uploader::File::setPartSize(uint32 partSize) {
	docPartSize = partSize;
	docPartsCount = (docSize / docPartSize)
		+ ((docSize % docPartSize) ? 1 : 0);
	return (docPartsCount <= kDocumentMaxPartsCountDefault);
}

Uploader::Uploader(not_null<ApiWrap*> api)
: _api(api)
, _nextTimer([=] { sendNext(); })
, _stopSessionsTimer([=] { stopSessions(); }) {
	const auto session = &_api->session();
	photoReady(
	) | rpl::start_with_next([=](UploadedMedia &&data) {
		if (data.edit) {
			const auto item = session->data().message(data.fullId);
			Api::EditMessageWithUploadedPhoto(
				item,
				std::move(data.info),
				data.options);
		} else {
			_api->sendUploadedPhoto(
				data.fullId,
				std::move(data.info),
				data.options);
		}
	}, _lifetime);

	documentReady(
	) | rpl::start_with_next([=](UploadedMedia &&data) {
		if (data.edit) {
			const auto item = session->data().message(data.fullId);
			Api::EditMessageWithUploadedDocument(
				item,
				std::move(data.info),
				data.options);
		} else {
			_api->sendUploadedDocument(
				data.fullId,
				std::move(data.info),
				data.options);
		}
	}, _lifetime);

	photoProgress(
	) | rpl::start_with_next([=](const FullMsgId &fullId) {
		processPhotoProgress(fullId);
	}, _lifetime);

	photoFailed(
	) | rpl::start_with_next([=](const FullMsgId &fullId) {
		processPhotoFailed(fullId);
	}, _lifetime);

	documentProgress(
	) | rpl::start_with_next([=](const FullMsgId &fullId) {
		processDocumentProgress(fullId);
	}, _lifetime);

	documentFailed(
	) | rpl::start_with_next([=](const FullMsgId &fullId) {
		processDocumentFailed(fullId);
	}, _lifetime);

	_api->instance().nonPremiumDelayedRequests(
	) | rpl::start_with_next([=](mtpRequestId id) {
		if (_dcIndices.contains(id)) {
			_nonPremiumDelayed.emplace(id);
		}
	}, _lifetime);
}

void Uploader::processPhotoProgress(const FullMsgId &newId) {
	const auto session = &_api->session();
	if (const auto item = session->data().message(newId)) {
		sendProgressUpdate(item, Api::SendProgressType::UploadPhoto);
	}
}

void Uploader::processDocumentProgress(const FullMsgId &newId) {
	const auto session = &_api->session();
	if (const auto item = session->data().message(newId)) {
		const auto media = item->media();
		const auto document = media ? media->document() : nullptr;
		const auto sendAction = (document && document->isVoiceMessage())
			? Api::SendProgressType::UploadVoice
			: Api::SendProgressType::UploadFile;
		const auto progress = (document && document->uploading())
			? ((document->uploadingData->offset * 100)
				/ document->uploadingData->size)
			: 0;
		sendProgressUpdate(item, sendAction, progress);
	}
}

void Uploader::processPhotoFailed(const FullMsgId &newId) {
	const auto session = &_api->session();
	if (const auto item = session->data().message(newId)) {
		sendProgressUpdate(item, Api::SendProgressType::UploadPhoto, -1);
	}
}

void Uploader::processDocumentFailed(const FullMsgId &newId) {
	const auto session = &_api->session();
	if (const auto item = session->data().message(newId)) {
		const auto media = item->media();
		const auto document = media ? media->document() : nullptr;
		const auto sendAction = (document && document->isVoiceMessage())
			? Api::SendProgressType::UploadVoice
			: Api::SendProgressType::UploadFile;
		sendProgressUpdate(item, sendAction, -1);
	}
}

void Uploader::sendProgressUpdate(
		not_null<HistoryItem*> item,
		Api::SendProgressType type,
		int progress) {
	const auto history = item->history();
	auto &manager = _api->session().sendProgressManager();
	manager.update(history, type, progress);
	if (const auto replyTo = item->replyToTop()) {
		if (history->peer->isMegagroup()) {
			manager.update(history, replyTo, type, progress);
		}
	} else if (history->isForum()) {
		manager.update(history, item->topicRootId(), type, progress);
	}
	_api->session().data().requestItemRepaint(item);
}

Uploader::~Uploader() {
	clear();
}

Main::Session &Uploader::session() const {
	return _api->session();
}

void Uploader::upload(
		const FullMsgId &msgId,
		const std::shared_ptr<FilePrepareResult> &file) {
	if (file->type == SendMediaType::Photo) {
		const auto photo = session().data().processPhoto(
			file->photo,
			file->photoThumbs);
		photo->uploadingData = std::make_unique<Data::UploadState>(
			file->partssize);
	} else if (file->type == SendMediaType::File
		|| file->type == SendMediaType::ThemeFile
		|| file->type == SendMediaType::Audio) {
		const auto document = file->thumb.isNull()
			? session().data().processDocument(file->document)
			: session().data().processDocument(
				file->document,
				Images::FromImageInMemory(
					file->thumb,
					ThumbnailFormat(file->filemime),
					file->thumbbytes));
		document->uploadingData = std::make_unique<Data::UploadState>(
			document->size);
		if (const auto active = document->activeMediaView()) {
			if (!file->goodThumbnail.isNull()) {
				active->setGoodThumbnail(std::move(file->goodThumbnail));
			}
			if (!file->thumb.isNull()) {
				active->setThumbnail(file->thumb);
			}
		}
		if (!file->goodThumbnailBytes.isEmpty()) {
			document->owner().cache().putIfEmpty(
				document->goodThumbnailCacheKey(),
				Storage::Cache::Database::TaggedValue(
					std::move(file->goodThumbnailBytes),
					Data::kImageCacheTag));
		}
		if (!file->content.isEmpty()) {
			document->setDataAndCache(file->content);
		}
		if (!file->filepath.isEmpty()) {
			document->setLocation(Core::FileLocation(file->filepath));
		}
		if (file->type == SendMediaType::ThemeFile) {
			document->checkWallPaperProperties();
		}
	}
	queue.emplace(msgId, File(file));
	sendNext();
}

void Uploader::currentFailed() {
	auto j = queue.find(_uploadingId);
	if (j != queue.end()) {
		const auto [msgId, file] = std::move(*j);
		queue.erase(j);
		notifyFailed(msgId, file);
	}

	cancelRequests();
	_dcIndices.clear();
	_uploadingId = FullMsgId();
	_sentTotal = 0;
	for (int i = 0; i < MTP::kUploadSessionsCount; ++i) {
		_sentPerDc[i] = 0;
	}

	sendNext();
}

void Uploader::notifyFailed(FullMsgId id, const File &file) {
	const auto type = file.file->type;
	if (type == SendMediaType::Photo) {
		_photoFailed.fire_copy(id);
	} else if (type == SendMediaType::File
		|| type == SendMediaType::ThemeFile
		|| type == SendMediaType::Audio) {
		const auto document = session().data().document(file.file->id);
		if (document->uploading()) {
			document->status = FileUploadFailed;
		}
		_documentFailed.fire_copy(id);
	} else if (type == SendMediaType::Secure) {
		_secureFailed.fire_copy(id);
	} else {
		Unexpected("Type in Uploader::currentFailed.");
	}
}

void Uploader::stopSessions() {
	for (int i = 0; i < MTP::kUploadSessionsCount; ++i) {
		_api->instance().stopSession(MTP::uploadDcId(i));
	}
}

void Uploader::sendNext() {
	if (_sentTotal >= kMaxUploadFileParallelSize || _pausedId.msg) {
		return;
	}

	const auto stopping = _stopSessionsTimer.isActive();
	if (queue.empty()) {
		if (!stopping) {
			_stopSessionsTimer.callOnce(kKillSessionTimeout);
		}
		return;
	}

	if (stopping) {
		_stopSessionsTimer.cancel();
	}
	auto i = _uploadingId.msg ? queue.find(_uploadingId) : queue.begin();
	if (!_uploadingId.msg) {
		_uploadingId = i->first;
	} else if (i == queue.end()) {
		i = queue.begin();
		_uploadingId = i->first;
	}
	auto &uploadingData = i->second;

	auto todc = 0;
	for (auto dc = 1; dc != MTP::kUploadSessionsCount; ++dc) {
		if (_sentPerDc[dc] < _sentPerDc[todc]) {
			todc = dc;
		}
	}

	if (uploadingData.partsSent >= uploadingData.parts.size()) {
		if (uploadingData.docSentParts >= uploadingData.docPartsCount) {
			if (_sentSizes.empty()) {
				const auto options = uploadingData.file
					? uploadingData.file->to.options
					: Api::SendOptions();
				const auto edit = uploadingData.file &&
					uploadingData.file->to.replaceMediaOf;
				const auto attachedStickers = uploadingData.file
					? uploadingData.file->attachedStickers
					: std::vector<MTPInputDocument>();
				if (uploadingData.file->type == SendMediaType::Photo) {
					auto photoFilename = uploadingData.file->filename;
					if (!photoFilename.endsWith(u".jpg"_q, Qt::CaseInsensitive)) {
						// Server has some extensions checking for inputMediaUploadedPhoto,
						// so force the extension to be .jpg anyway. It doesn't matter,
						// because the filename from inputFile is not used anywhere.
						photoFilename += u".jpg"_q;
					}
					const auto md5 = uploadingData.file->filemd5;
					const auto file = MTP_inputFile(
						MTP_long(uploadingData.file->id),
						MTP_int(uploadingData.parts.size()),
						MTP_string(photoFilename),
						MTP_bytes(md5));
					_photoReady.fire({
						.fullId = _uploadingId,
						.info = {
							.file = file,
							.attachedStickers = attachedStickers,
						},
						.options = options,
						.edit = edit,
					});
				} else if (uploadingData.file->type == SendMediaType::File
					|| uploadingData.file->type == SendMediaType::ThemeFile
					|| uploadingData.file->type == SendMediaType::Audio) {
					QByteArray docMd5(32, Qt::Uninitialized);
					hashMd5Hex(uploadingData.md5Hash.result(), docMd5.data());

					const auto file = (uploadingData.docSize > kUseBigFilesFrom)
						? MTP_inputFileBig(
							MTP_long(uploadingData.file->id),
							MTP_int(uploadingData.docPartsCount),
							MTP_string(uploadingData.file->filename))
						: MTP_inputFile(
							MTP_long(uploadingData.file->id),
							MTP_int(uploadingData.docPartsCount),
							MTP_string(uploadingData.file->filename),
							MTP_bytes(docMd5));
					const auto thumb = [&]() -> std::optional<MTPInputFile> {
						if (uploadingData.parts.empty()) {
							return std::nullopt;
						}
						const auto thumbFilename = uploadingData.file->thumbname;
						const auto thumbMd5 = uploadingData.file->thumbmd5;
						return MTP_inputFile(
							MTP_long(uploadingData.file->thumbId),
							MTP_int(uploadingData.parts.size()),
							MTP_string(thumbFilename),
							MTP_bytes(thumbMd5));
					}();
					_documentReady.fire({
						.fullId = _uploadingId,
						.info = {
							.file = file,
							.thumb = thumb,
							.attachedStickers = attachedStickers,
						},
						.options = options,
						.edit = edit,
					});
				} else if (uploadingData.file->type == SendMediaType::Secure) {
					_secureReady.fire({
						_uploadingId,
						uploadingData.file->id,
						int(uploadingData.parts.size()),
					});
				}
				queue.erase(_uploadingId);
				_uploadingId = FullMsgId();
				sendNext();
			}
			return;
		}

		auto &content = uploadingData.file->content;
		QByteArray toSend;
		if (content.isEmpty()) {
			if (!uploadingData.docFile) {
				const auto filepath = uploadingData.file->filepath;
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
			if ((uploadingData.file->type == SendMediaType::File
				|| uploadingData.file->type == SendMediaType::ThemeFile
				|| uploadingData.file->type == SendMediaType::Audio)
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
			requestId = _api->request(MTPupload_SaveBigFilePart(
				MTP_long(uploadingData.file->id),
				MTP_int(uploadingData.docSentParts),
				MTP_int(uploadingData.docPartsCount),
				MTP_bytes(toSend)
			)).done([=](const MTPBool &result, mtpRequestId requestId) {
				partLoaded(result, requestId);
			}).fail([=](const MTP::Error &error, mtpRequestId requestId) {
				partFailed(error, requestId);
			}).toDC(MTP::uploadDcId(todc)).send();
		} else {
			requestId = _api->request(MTPupload_SaveFilePart(
				MTP_long(uploadingData.file->id),
				MTP_int(uploadingData.docSentParts),
				MTP_bytes(toSend)
			)).done([=](const MTPBool &result, mtpRequestId requestId) {
				partLoaded(result, requestId);
			}).fail([=](const MTP::Error &error, mtpRequestId requestId) {
				partFailed(error, requestId);
			}).toDC(MTP::uploadDcId(todc)).send();
		}
		_sentSizes.emplace(requestId, uploadingData.docPartSize);
		_docSentRequests.emplace(requestId);
		_dcIndices.emplace(requestId, todc);
		_sentTotal += uploadingData.docPartSize;
		_sentPerDc[todc] += uploadingData.docPartSize;

		uploadingData.docSentParts++;
	} else {
		const auto index = uploadingData.partsSent++;
		const auto partBytes = uploadingData.parts[index];
		const auto partSize = int(partBytes.size());

		const auto requestId = _api->request(MTPupload_SaveFilePart(
			MTP_long(uploadingData.partsOfId),
			MTP_int(index),
			MTP_bytes(partBytes)
		)).done([=](const MTPBool &result, mtpRequestId requestId) {
			partLoaded(result, requestId);
		}).fail([=](const MTP::Error &error, mtpRequestId requestId) {
			partFailed(error, requestId);
		}).toDC(MTP::uploadDcId(todc)).send();
		_sentSizes.emplace(requestId, partSize);
		_dcIndices.emplace(requestId, todc);
		_sentTotal += partSize;
		_sentPerDc[todc] += partSize;
	}
	_nextTimer.callOnce(kUploadRequestInterval);
}

void Uploader::cancel(const FullMsgId &msgId) {
	if (_uploadingId == msgId) {
		currentFailed();
	} else {
		queue.erase(msgId);
	}
}

void Uploader::cancelAll() {
	const auto single = queue.empty() ? _uploadingId : queue.begin()->first;
	if (!single) {
		return;
	}
	_pausedId = single;
	if (_uploadingId) {
		currentFailed();
	}
	while (!queue.empty()) {
		const auto [msgId, file] = std::move(*queue.begin());
		queue.erase(queue.begin());
		notifyFailed(msgId, file);
	}
	clear();
	unpause();
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

void Uploader::cancelRequests() {
	_docSentRequests.clear();
	for (const auto &requestData : _sentSizes) {
		_api->request(requestData.first).cancel();
	}
	_sentSizes.clear();
}

void Uploader::clear() {
	queue.clear();
	cancelRequests();
	_dcIndices.clear();
	_sentTotal = 0;
	for (int i = 0; i < MTP::kUploadSessionsCount; ++i) {
		_api->instance().stopSession(MTP::uploadDcId(i));
		_sentPerDc[i] = 0;
	}
	_stopSessionsTimer.cancel();
}

void Uploader::partLoaded(const MTPBool &result, mtpRequestId requestId) {
	_docSentRequests.remove(requestId);
	auto i = _sentSizes.find(requestId);
	const auto wasNonPremiumDelayed = _nonPremiumDelayed.remove(requestId);
	if (i != _sentSizes.cend()) {
		if (mtpIsFalse(result)) { // failed to upload current file
			currentFailed();
			return;
		} else {
			auto dcIt = _dcIndices.find(requestId);
			if (dcIt == _dcIndices.cend()) { // must not happen
				currentFailed();
				return;
			}
			auto dc = dcIt->second;
			_dcIndices.erase(dcIt);

			int64 sentPartSize = i->second;
			auto k = queue.find(_uploadingId);
			Assert(k != queue.cend());
			auto &[fullId, file] = *k;
			_sentSizes.erase(i);
			_sentTotal -= sentPartSize;
			_sentPerDc[dc] -= sentPartSize;
			if (file.file->type == SendMediaType::Photo) {
				file.fileSentSize += sentPartSize;
				const auto photo = session().data().photo(file.file->id);
				if (photo->uploading() && file.file) {
					photo->uploadingData->size = file.file->partssize;
					photo->uploadingData->offset = file.fileSentSize;
				}
				_photoProgress.fire_copy(fullId);
			} else if (file.file->type == SendMediaType::File
				|| file.file->type == SendMediaType::ThemeFile
				|| file.file->type == SendMediaType::Audio) {
				const auto document = session().data().document(file.file->id);
				if (document->uploading()) {
					const auto doneParts = file.docSentParts
						- int(_docSentRequests.size());
					document->uploadingData->offset = std::min(
						document->uploadingData->size,
						doneParts * file.docPartSize);
				}
				_documentProgress.fire_copy(fullId);
			} else if (file.file->type == SendMediaType::Secure) {
				file.fileSentSize += sentPartSize;
				_secureProgress.fire_copy({
					fullId,
					file.fileSentSize,
					file.file->partssize });
			}
			if (wasNonPremiumDelayed) {
				_nonPremiumDelays.fire_copy(fullId);
			}
		}
	}

	sendNext();
}

void Uploader::partFailed(const MTP::Error &error, mtpRequestId requestId) {
	// failed to upload current file
	_nonPremiumDelayed.remove(requestId);
	if (_sentSizes.find(requestId) != _sentSizes.cend()) {
		currentFailed();
	}
	sendNext();
}

} // namespace Storage
