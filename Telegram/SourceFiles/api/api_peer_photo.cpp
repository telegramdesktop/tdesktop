/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_peer_photo.h"

#include "api/api_updates.h"
#include "apiwrap.h"
#include "base/random.h"
#include "base/unixtime.h"
#include "data/stickers/data_stickers.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_user_photos.h"
#include "history/history.h"
#include "main/main_session.h"
#include "media/media_video_encode.h"
#include "storage/file_upload.h"
#include "storage/localimageloader.h"
#include "storage/storage_user_photos.h"

#include <QtCore/QBuffer>
#include <QtCore/QFile>

namespace Api {
namespace {

constexpr auto kSharedMediaLimit = 100;

constexpr auto kTranscodeShare = 0.7;

[[nodiscard]] std::shared_ptr<FilePrepareResult> PreparePeerVideo(
		const QByteArray &content) {
	auto result = MakePreparedFile({
		.id = base::RandomValue<uint64>(),
		.type = SendMediaType::SecondaryFile,
	});
	result->filename = u"animation.mp4"_q;
	result->filemime = u"video/mp4"_q;
	result->filesize = int64(content.size());
	result->setFileData(content);
	return result;
}

[[nodiscard]] std::shared_ptr<FilePrepareResult> PreparePeerPhoto(
		MTP::DcId dcId,
		PeerId peerId,
		QImage &&image) {
	PreparedPhotoThumbs photoThumbs;
	QVector<MTPPhotoSize> photoSizes;

	QByteArray jpeg;
	QBuffer jpegBuffer(&jpeg);
	image.save(&jpegBuffer, "JPG", 87);

	const auto scaled = [&](int size) {
		return image.scaled(
			size,
			size,
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation);
	};
	const auto push = [&](
			const char *type,
			QImage &&image,
			QByteArray bytes = QByteArray()) {
		photoSizes.push_back(MTP_photoSize(
			MTP_string(type),
			MTP_int(image.width()),
			MTP_int(image.height()), MTP_int(0)));
		photoThumbs.emplace(type[0], PreparedPhotoThumb{
			.image = std::move(image),
			.bytes = std::move(bytes)
		});
	};
	push("a", scaled(160));
	push("b", scaled(320));
	push("c", std::move(image), jpeg);

	const auto id = base::RandomValue<PhotoId>();
	const auto photo = MTP_photo(
		MTP_flags(0),
		MTP_long(id),
		MTP_long(0),
		MTP_bytes(),
		MTP_int(base::unixtime::now()),
		MTP_vector<MTPPhotoSize>(photoSizes),
		MTPVector<MTPVideoSize>(),
		MTP_int(dcId));

	auto result = MakePreparedFile({
		.id = id,
		.type = SendMediaType::Photo,
	});
	result->type = SendMediaType::Photo;
	result->setFileData(jpeg);
	result->thumbId = id;
	result->thumbname = "thumb.jpg";
	result->photo = photo;
	result->photoThumbs = photoThumbs;
	return result;
}

[[nodiscard]] std::optional<MTPVideoSize> PrepareMtpMarkup(
		not_null<Main::Session*> session,
		const PeerPhoto::UserPhoto &d) {
	const auto &documentId = d.markupDocumentId;
	const auto &colors = d.markupColors;
	if (!documentId || colors.empty()) {
		return std::nullopt;
	}
	const auto document = session->data().document(documentId);
	if (const auto sticker = document->sticker()) {
		if (sticker->isStatic()) {
			return std::nullopt;
		}
		const auto serializeColor = [](const QColor &color) {
			return (quint32(std::clamp(color.red(), 0, 255)) << 16)
				| (quint32(std::clamp(color.green(), 0, 255)) << 8)
				| quint32(std::clamp(color.blue(), 0, 255));
		};

		auto mtpColors = QVector<MTPint>();
		mtpColors.reserve(colors.size());
		ranges::transform(
			colors,
			ranges::back_inserter(mtpColors),
			[&](const QColor &c) { return MTP_int(serializeColor(c)); });
		if (sticker->setType == Data::StickersType::Emoji) {
			return MTP_videoSizeEmojiMarkup(
				MTP_long(document->id),
				MTP_vector(mtpColors));
		} else if (sticker->set.id && sticker->set.accessHash) {
			return MTP_videoSizeStickerMarkup(
				MTP_inputStickerSetID(
					MTP_long(sticker->set.id),
					MTP_long(sticker->set.accessHash)),
				MTP_long(document->id),
				MTP_vector(mtpColors));
		} else if (!sticker->set.shortName.isEmpty()) {
			return MTP_videoSizeStickerMarkup(
				MTP_inputStickerSetShortName(
					MTP_string(sticker->set.shortName)),
				MTP_long(document->id),
				MTP_vector(mtpColors));
		} else {
			return MTP_videoSizeEmojiMarkup(
				MTP_long(document->id),
				MTP_vector(mtpColors));
		}
	}
	return std::nullopt;
}

} // namespace

PeerPhoto::PeerPhoto(not_null<ApiWrap*> api)
: _session(&api->session())
, _api(&api->instance()) {
	crl::on_main(_session, [=] {
		const auto &uploader = _session->uploader();

		// You can't use _session->lifetime() in the constructor,
		// only queued, because it is not constructed yet.
		uploader.photoReady(
		) | rpl::on_next([=](const Storage::UploadedMedia &data) {
			const auto i = _uploads.find(data.fullId);
			if (i != end(_uploads) && i->second.videoId) {
				i->second.photoFile = data.info.file;
				i->second.waitingPhoto = false;
				checkVideoUploadDone(data.fullId);
				return;
			}
			ready(data.fullId, { .file = data.info.file });
		}, _session->lifetime());

		uploader.photoProgress(
		) | rpl::on_next([=](const FullMsgId &id) {
			const auto i = _uploads.find(id);
			if (i == end(_uploads)
				|| !i->second.photoId
				|| i->second.videoId) {
				return;
			}
			const auto peer = i->second.peer;
			const auto photo = _session->data().photo(
				i->second.photoId);
			_uploadProgress.fire({ peer, photo->progress() });
		}, _session->lifetime());

		uploader.photoFailed(
		) | rpl::on_next([=](const FullMsgId &id) {
			const auto i = _uploads.find(id);
			if (i == end(_uploads)) {
				return;
			}
			const auto peer = i->second.peer;
			clearUpload(id);
			_uploadFailed.fire_copy(peer);
		}, _session->lifetime());

		uploader.secondaryFileReady(
		) | rpl::on_next([=](const Storage::UploadedMedia &data) {
			const auto photoMsgId = _videoToPhotoId.take(data.fullId);
			if (!photoMsgId) {
				return;
			}
			const auto i = _uploads.find(*photoMsgId);
			if (i == end(_uploads)) {
				return;
			}
			i->second.videoFile = data.info.file;
			i->second.waitingVideo = false;
			checkVideoUploadDone(*photoMsgId);
		}, _session->lifetime());

		uploader.secondaryFileProgress(
		) | rpl::on_next([=](const Storage::UploadFileProgress &data) {
			const auto photoMsgId = _videoToPhotoId.find(data.fullId);
			if (photoMsgId == end(_videoToPhotoId)) {
				return;
			}
			const auto i = _uploads.find(photoMsgId->second);
			if (i == end(_uploads)) {
				return;
			}
			const auto sent = (data.size > 0)
				? std::clamp(data.offset / float64(data.size), 0., 1.)
				: 0.;
			_uploadProgress.fire({
				i->second.peer,
				kTranscodeShare + (1. - kTranscodeShare) * sent,
			});
		}, _session->lifetime());

		uploader.secondaryFileFailed(
		) | rpl::on_next([=](const FullMsgId &id) {
			const auto photoMsgId = _videoToPhotoId.find(id);
			if (photoMsgId == end(_videoToPhotoId)) {
				return;
			}
			const auto i = _uploads.find(photoMsgId->second);
			if (i == end(_uploads)) {
				return;
			}
			const auto peer = i->second.peer;
			clearUpload(photoMsgId->second);
			_uploadFailed.fire_copy(peer);
		}, _session->lifetime());
	});
}

PeerPhoto::~PeerPhoto() {
	for (const auto &[msgId, value] : _uploads) {
		if (value.cancelTranscode) {
			value.cancelTranscode->store(true);
		}
	}
}

void PeerPhoto::clearUpload(FullMsgId msgId) {
	const auto i = _uploads.find(msgId);
	if (i == end(_uploads)) {
		return;
	}
	if (const auto cancel = i->second.cancelTranscode) {
		cancel->store(true);
	}
	const auto videoId = i->second.videoId;
	_uploads.erase(i);
	if (videoId) {
		_videoToPhotoId.remove(videoId);
		_session->uploader().cancel(videoId);
	}
	_session->uploader().cancel(msgId);
}

void PeerPhoto::upload(
		not_null<PeerData*> peer,
		UserPhoto &&photo,
		Fn<void()> done) {
	upload(peer, std::move(photo), UploadType::Default, std::move(done));
}

void PeerPhoto::uploadFallback(not_null<PeerData*> peer, UserPhoto &&photo) {
	upload(peer, std::move(photo), UploadType::Fallback, nullptr);
}

void PeerPhoto::updateSelf(
		not_null<PhotoData*> photo,
		Data::FileOrigin origin,
		Fn<void()> done) {
	const auto send = [=](auto resend) -> void {
		const auto usedFileReference = photo->fileReference();
		_api.request(MTPphotos_UpdateProfilePhoto(
			MTP_flags(0),
			MTPInputUser(), // bot
			photo->mtpInput()
		)).done([=](const MTPphotos_Photo &result) {
			result.match([&](const MTPDphotos_photo &data) {
				_session->data().processPhoto(data.vphoto());
				_session->data().processUsers(data.vusers());
			});
			if (done) {
				done();
			}
		}).fail([=](const MTP::Error &error) {
			if (error.code() == 400
				&& error.type().startsWith(u"FILE_REFERENCE_"_q)) {
				photo->session().api().refreshFileReference(origin, [=](
						const auto &) {
					if (photo->fileReference() != usedFileReference) {
						resend(resend);
					}
				});
			}
		}).send();
	};
	send(send);
}

void PeerPhoto::upload(
		not_null<PeerData*> peer,
		UserPhoto &&photo,
		UploadType type,
		Fn<void()> done) {
	peer = peer->migrateToOrMe();
	if (photo.video) {
		uploadWithVideo(peer, std::move(photo), type, std::move(done));
		return;
	}
	const auto mtpMarkup = PrepareMtpMarkup(_session, photo);

	const auto fakeId = FullMsgId(
		peer->id,
		_session->data().nextLocalMessageId());
	const auto already = ranges::find(
		_uploads,
		peer,
		[](const auto &pair) { return pair.second.peer; });
	if (already != end(_uploads)) {
		clearUpload(already->first);
	}
	const auto &[it, ok] = _uploads.emplace(
		fakeId,
		UploadValue{ peer, type, std::move(done), PhotoId(0) });
	if (mtpMarkup) {
		ready(fakeId, { .videoSize = mtpMarkup });
	} else {
		const auto prepared = PreparePeerPhoto(
			_api.instance().mainDcId(),
			peer->id,
			base::take(photo.image));
		it->second.photoId = prepared->thumbId;
		_session->uploader().upload(fakeId, prepared);
	}
}

void PeerPhoto::uploadWithVideo(
		not_null<PeerData*> peer,
		UserPhoto &&photo,
		UploadType type,
		Fn<void()> done) {
	const auto fakeId = FullMsgId(
		peer->id,
		_session->data().nextLocalMessageId());
	const auto already = ranges::find(
		_uploads,
		peer,
		[](const auto &pair) { return pair.second.peer; });
	if (already != end(_uploads)) {
		clearUpload(already->first);
	}
	const auto cancel = std::make_shared<std::atomic<bool>>(false);
	_uploads.emplace(fakeId, UploadValue{
		.peer = peer,
		.type = type,
		.done = std::move(done),
		.cancelTranscode = cancel,
		.transcoding = true,
	});
	_uploadProgress.fire({ peer, 0. });

	const auto source = photo.video;
	crl::async([=, weak = base::make_weak(_session.get())] {
		auto lastReported = -1.;
		auto result = Media::Encode::TranscodeVideo(*source, [&](
				float64 value) {
			if (value - lastReported >= 0.01 || value >= 1.) {
				lastReported = value;
				crl::on_main(weak, [=] {
					const auto i = _uploads.find(fakeId);
					if (i != end(_uploads) && i->second.transcoding) {
						_uploadProgress.fire({
							i->second.peer,
							value * kTranscodeShare,
						});
					}
				});
			}
			return !cancel->load();
		});
		crl::on_main([=, result = std::move(result)]() mutable {
			if (!weak.get()) {
				if (!result.path.isEmpty()) {
					QFile::remove(result.path);
				}
				return;
			}
			videoTranscoded(fakeId, std::move(result));
		});
	});
}

void PeerPhoto::videoTranscoded(
		FullMsgId msgId,
		Media::Encode::TranscodeResult &&result) {
	const auto guard = gsl::finally([&] {
		if (!result.path.isEmpty()) {
			QFile::remove(result.path);
		}
	});
	const auto i = _uploads.find(msgId);
	if (i == end(_uploads) || !i->second.transcoding) {
		return;
	}
	auto &value = i->second;
	value.transcoding = false;
	value.cancelTranscode = nullptr;

	const auto fail = [&] {
		const auto peer = value.peer;
		clearUpload(msgId);
		_uploadFailed.fire_copy(peer);
	};
	if (result.empty() || result.cover.isNull()) {
		fail();
		return;
	}
	auto content = QByteArray();
	auto file = QFile(result.path);
	if (file.open(QIODevice::ReadOnly)) {
		content = file.readAll();
	}
	file.close();
	if (content.isEmpty()) {
		fail();
		return;
	}

	const auto peer = value.peer;
	const auto videoId = FullMsgId(
		peer->id,
		_session->data().nextLocalMessageId());
	value.videoId = videoId;
	value.videoStartTs = result.coverOffset / 1000.;
	value.waitingPhoto = true;
	value.waitingVideo = true;
	_videoToPhotoId.emplace(videoId, msgId);

	const auto prepared = PreparePeerPhoto(
		_api.instance().mainDcId(),
		peer->id,
		std::move(result.cover));
	value.photoId = prepared->thumbId;
	_session->uploader().upload(msgId, prepared);
	_session->uploader().upload(videoId, PreparePeerVideo(content));
}

void PeerPhoto::checkVideoUploadDone(FullMsgId msgId) {
	const auto i = _uploads.find(msgId);
	if (i == end(_uploads)) {
		return;
	}
	const auto &value = i->second;
	if (value.transcoding || value.waitingPhoto || value.waitingVideo) {
		return;
	}
	ready(msgId, {
		.file = value.photoFile,
		.video = value.videoFile,
		.videoStartTs = value.videoStartTs,
	});
}

void PeerPhoto::suggest(not_null<PeerData*> peer, UserPhoto &&photo) {
	upload(peer, std::move(photo), UploadType::Suggestion, nullptr);
}

void PeerPhoto::subscribeToUpload(
		not_null<PeerData*> peer,
		rpl::lifetime &lifetime,
		UploadCallbacks callbacks) {
	uploadProgress(
	) | rpl::filter([=](const UploadProgress &data) {
		return (data.peer == peer);
	}) | rpl::on_next([cb = callbacks.progress](const UploadProgress &data) {
		if (cb) {
			cb(data.progress);
		}
	}, lifetime);

	uploadDone(
	) | rpl::filter([=](not_null<PeerData*> p) {
		return (p == peer);
	}) | rpl::on_next([cb = callbacks.done](not_null<PeerData*>) {
		if (cb) {
			cb();
		}
	}, lifetime);

	uploadFailed(
	) | rpl::filter([=](not_null<PeerData*> p) {
		return (p == peer);
	}) | rpl::on_next([cb = callbacks.failed](not_null<PeerData*>) {
		if (cb) {
			cb();
		}
	}, lifetime);
}

auto PeerPhoto::uploadProgress() const
-> rpl::producer<UploadProgress> {
	return _uploadProgress.events();
}

auto PeerPhoto::uploadDone() const
-> rpl::producer<not_null<PeerData*>> {
	return _uploadDone.events();
}

auto PeerPhoto::uploadFailed() const
-> rpl::producer<not_null<PeerData*>> {
	return _uploadFailed.events();
}

void PeerPhoto::cancelUpload(not_null<PeerData*> peer) {
	peer = peer->migrateToOrMe();
	const auto i = ranges::find(
		_uploads,
		peer,
		[](const auto &pair) { return pair.second.peer; });
	if (i == end(_uploads)) {
		return;
	}
	clearUpload(i->first);
	_uploadFailed.fire_copy(peer);
}

void PeerPhoto::clear(not_null<PhotoData*> photo) {
	const auto self = _session->user();
	if (self->userpicPhotoId() == photo->id) {
		const auto photoId = photo->id;
		const auto peerId = self->id;
		_api.request(MTPphotos_UpdateProfilePhoto(
			MTP_flags(0),
			MTPInputUser(), // bot
			MTP_inputPhotoEmpty()
		)).done([=](const MTPphotos_Photo &result) {
			self->setPhoto(MTP_userProfilePhotoEmpty());
			_session->storage().remove(
				Storage::UserPhotosRemoveOne(peerToUser(peerId), photoId));
		}).send();
	} else if (photo->peer && photo->peer->userpicPhotoId() == photo->id) {
		const auto applier = [=](const MTPUpdates &result) {
			_session->updates().applyUpdates(result);
		};
		if (const auto chat = photo->peer->asChat()) {
			_api.request(MTPmessages_EditChatPhoto(
				chat->inputChat(),
				MTP_inputChatPhotoEmpty()
			)).done(applier).send();
		} else if (const auto channel = photo->peer->asChannel()) {
			_api.request(MTPchannels_EditPhoto(
				channel->inputChannel(),
				MTP_inputChatPhotoEmpty()
			)).done(applier).send();
		}
	} else {
		const auto fallbackPhotoId = SyncUserFallbackPhotoViewer(self);
		if (fallbackPhotoId && (*fallbackPhotoId) == photo->id) {
			_api.request(MTPphotos_UpdateProfilePhoto(
				MTP_flags(MTPphotos_UpdateProfilePhoto::Flag::f_fallback),
				MTPInputUser(), // bot
				MTP_inputPhotoEmpty()
			)).send();
			_session->storage().add(Storage::UserPhotosSetBack(
				peerToUser(self->id),
				PhotoId()));
		} else {
			_api.request(MTPphotos_DeletePhotos(
				MTP_vector<MTPInputPhoto>(1, photo->mtpInput())
			)).send();
			_session->storage().remove(Storage::UserPhotosRemoveOne(
				peerToUser(self->id),
				photo->id));
		}
	}
}

void PeerPhoto::clearPersonal(not_null<UserData*> user) {
	_api.request(MTPphotos_UploadContactProfilePhoto(
		MTP_flags(MTPphotos_UploadContactProfilePhoto::Flag::f_save),
		user->inputUser(),
		MTPInputFile(),
		MTPInputFile(), // video
		MTPdouble(), // video_start_ts
		MTPVideoSize() // video_emoji_markup
	)).done([=](const MTPphotos_Photo &result) {
		result.match([&](const MTPDphotos_photo &data) {
			_session->data().processPhoto(data.vphoto());
			_session->data().processUsers(data.vusers());
		});
	}).send();

	if (!user->userpicPhotoUnknown() && user->hasPersonalPhoto()) {
		_session->storage().remove(Storage::UserPhotosRemoveOne(
			peerToUser(user->id),
			user->userpicPhotoId()));
	}
}

void PeerPhoto::set(not_null<PeerData*> peer, not_null<PhotoData*> photo) {
	if (peer->userpicPhotoId() == photo->id) {
		return;
	}
	if (peer == _session->user()) {
		const auto photoId = photo->id;
		const auto peerId = peer->id;
		_api.request(MTPphotos_UpdateProfilePhoto(
			MTP_flags(0),
			MTPInputUser(), // bot
			photo->mtpInput()
		)).done([=](const MTPphotos_Photo &result) {
			const auto newPhoto = _session->data().processPhoto(
				result.data().vphoto());
			_session->data().processUsers(result.data().vusers());
			_session->storage().replace(Storage::UserPhotosReplace(
				peerToUser(peerId),
				photoId,
				newPhoto->id));
		}).send();
	} else {
		const auto applier = [=](const MTPUpdates &result) {
			_session->updates().applyUpdates(result);
		};
		if (const auto chat = peer->asChat()) {
			_api.request(MTPmessages_EditChatPhoto(
				chat->inputChat(),
				MTP_inputChatPhoto(photo->mtpInput())
			)).done(applier).send();
		} else if (const auto channel = peer->asChannel()) {
			_api.request(MTPchannels_EditPhoto(
				channel->inputChannel(),
				MTP_inputChatPhoto(photo->mtpInput())
			)).done(applier).send();
		}
	}
}

void PeerPhoto::ready(FullMsgId msgId, ReadyFiles &&files) {
	const auto maybeUploadValue = _uploads.take(msgId);
	if (!maybeUploadValue) {
		return;
	}
	if (const auto videoId = maybeUploadValue->videoId) {
		_videoToPhotoId.remove(videoId);
	}
	const auto file = files.file;
	const auto video = files.video;
	const auto videoSize = files.videoSize;
	const auto videoStartTs = files.videoStartTs;
	const auto peer = maybeUploadValue->peer;
	const auto type = maybeUploadValue->type;
	const auto done = maybeUploadValue->done;
	const auto finish = [=] {
		_uploadDone.fire_copy(peer);
		if (done) {
			done();
		}
	};
	const auto fail = [=](const MTP::Error &error) {
		_uploadFailed.fire_copy(peer);
	};
	const auto applier = [=](const MTPUpdates &result) {
		_session->updates().applyUpdates(result);
		finish();
	};
	const auto botUserInput = [&] {
		const auto user = peer->asUser();
		return (user && user->botInfo && user->botInfo->canEditInformation)
			? std::make_optional<MTPInputUser>(user->inputUser())
			: std::nullopt;
	}();
	if (peer->isSelf() || botUserInput) {
		using Flag = MTPphotos_UploadProfilePhoto::Flag;
		const auto none = MTPphotos_UploadProfilePhoto::Flags(0);
		_api.request(MTPphotos_UploadProfilePhoto(
			MTP_flags((file ? Flag::f_file : none)
				| (botUserInput ? Flag::f_bot : none)
				| (video ? Flag::f_video : none)
				| (video ? Flag::f_video_start_ts : none)
				| (videoSize ? Flag::f_video_emoji_markup : none)
				| ((type == UploadType::Fallback) ? Flag::f_fallback : none)),
			botUserInput ? (*botUserInput) : MTPInputUser(), // bot
			file ? (*file) : MTPInputFile(),
			video ? (*video) : MTPInputFile(),
			MTP_double(videoStartTs),
			videoSize ? (*videoSize) : MTPVideoSize() // video_emoji_markup
		)).done([=](const MTPphotos_Photo &result) {
			const auto photoId = _session->data().processPhoto(
				result.data().vphoto())->id;
			_session->data().processUsers(result.data().vusers());
			if (type == UploadType::Fallback) {
				_session->storage().add(Storage::UserPhotosSetBack(
					peerToUser(peer->id),
					photoId));
			} else {
				_session->storage().add(Storage::UserPhotosAddNew(
					peerToUser(peer->id),
					photoId));
			}
			finish();
		}).fail(fail).send();
	} else if (const auto chat = peer->asChat()) {
		const auto history = _session->data().history(chat);
		using Flag = MTPDinputChatUploadedPhoto::Flag;
		const auto none = MTPDinputChatUploadedPhoto::Flags(0);
		history->sendRequestId = _api.request(MTPmessages_EditChatPhoto(
			chat->inputChat(),
			MTP_inputChatUploadedPhoto(
				MTP_flags((file ? Flag::f_file : none)
					| (video ? Flag::f_video : none)
					| (video ? Flag::f_video_start_ts : none)
					| (videoSize ? Flag::f_video_emoji_markup : none)),
				file ? (*file) : MTPInputFile(),
				video ? (*video) : MTPInputFile(),
				MTP_double(videoStartTs),
				videoSize ? (*videoSize) : MTPVideoSize()) // video_emoji_markup
		)).done(applier).fail(fail).afterRequest(history->sendRequestId).send();
	} else if (const auto channel = peer->asChannel()) {
		using Flag = MTPDinputChatUploadedPhoto::Flag;
		const auto none = MTPDinputChatUploadedPhoto::Flags(0);
		const auto history = _session->data().history(channel);
		history->sendRequestId = _api.request(MTPchannels_EditPhoto(
			channel->inputChannel(),
			MTP_inputChatUploadedPhoto(
				MTP_flags((file ? Flag::f_file : none)
					| (video ? Flag::f_video : none)
					| (video ? Flag::f_video_start_ts : none)
					| (videoSize ? Flag::f_video_emoji_markup : none)),
				file ? (*file) : MTPInputFile(),
				video ? (*video) : MTPInputFile(),
				MTP_double(videoStartTs),
				videoSize ? (*videoSize) : MTPVideoSize()) // video_emoji_markup
		)).done(applier).fail(fail).afterRequest(history->sendRequestId).send();
	} else if (const auto user = peer->asUser()) {
		using Flag = MTPphotos_UploadContactProfilePhoto::Flag;
		const auto none = MTPphotos_UploadContactProfilePhoto::Flags(0);
		_api.request(MTPphotos_UploadContactProfilePhoto(
			MTP_flags((file ? Flag::f_file : none)
				| (video ? Flag::f_video : none)
				| (video ? Flag::f_video_start_ts : none)
				| (videoSize ? Flag::f_video_emoji_markup : none)
				| ((type == UploadType::Suggestion)
					? Flag::f_suggest
					: Flag::f_save)),
			user->inputUser(),
			file ? (*file) : MTPInputFile(),
			video ? (*video) : MTPInputFile(),
			MTP_double(videoStartTs),
			videoSize ? (*videoSize) : MTPVideoSize() // video_emoji_markup
		)).done([=](const MTPphotos_Photo &result) {
			result.match([&](const MTPDphotos_photo &data) {
				_session->data().processPhoto(data.vphoto());
				_session->data().processUsers(data.vusers());
			});
			if (type != UploadType::Suggestion) {
				user->updateFullForced();
			}
			finish();
		}).fail(fail).send();
	}
}

void PeerPhoto::requestUserPhotos(
		not_null<UserData*> user,
		UserPhotoId afterId) {
	if (_userPhotosRequests.contains(user)) {
		return;
	}

	const auto requestId = _api.request(MTPphotos_GetUserPhotos(
		user->inputUser(),
		MTP_int(0),
		MTP_long(afterId),
		MTP_int(kSharedMediaLimit)
	)).done([this, user](const MTPphotos_Photos &result) {
		_userPhotosRequests.remove(user);

		auto fullCount = result.match([](const MTPDphotos_photos &d) {
			return int(d.vphotos().v.size());
		}, [](const MTPDphotos_photosSlice &d) {
			return d.vcount().v;
		});

		auto &owner = _session->data();
		auto photoIds = result.match([&](const auto &data) {
			owner.processUsers(data.vusers());

			auto photoIds = std::vector<PhotoId>();
			photoIds.reserve(data.vphotos().v.size());

			for (const auto &single : data.vphotos().v) {
				const auto photo = owner.processPhoto(single);
				if (!photo->isNull()) {
					photoIds.push_back(photo->id);
				}
			}
			return photoIds;
		});
		if (!user->userpicPhotoUnknown() && user->hasPersonalPhoto()) {
			const auto photo = owner.photo(user->userpicPhotoId());
			if (!photo->isNull()) {
				++fullCount;
				photoIds.insert(begin(photoIds), photo->id);
			}
		}

		_session->storage().add(Storage::UserPhotosAddSlice(
			peerToUser(user->id),
			std::move(photoIds),
			fullCount
		));
	}).fail([this, user] {
		_userPhotosRequests.remove(user);
	}).send();
	_userPhotosRequests.emplace(user, requestId);
}

auto PeerPhoto::emojiList(EmojiListType type) -> EmojiListData & {
	switch (type) {
	case EmojiListType::Profile: return _profileEmojiList;
	case EmojiListType::Group: return _groupEmojiList;
	case EmojiListType::Background: return _backgroundEmojiList;
	case EmojiListType::NoChannelStatus: return _noChannelStatusEmojiList;
	}
	Unexpected("Type in PeerPhoto::emojiList.");
}

auto PeerPhoto::emojiList(EmojiListType type) const
-> const EmojiListData & {
	return const_cast<PeerPhoto*>(this)->emojiList(type);
}

void PeerPhoto::requestEmojiList(EmojiListType type) {
	auto &list = emojiList(type);
	if (list.requestId) {
		return;
	}
	const auto send = [&](auto &&request) {
		return _api.request(
			std::move(request)
		).done([=](const MTPEmojiList &result) {
			auto &list = emojiList(type);
			list.requestId = 0;
			result.match([](const MTPDemojiListNotModified &data) {
			}, [&](const MTPDemojiList &data) {
				list.list = ranges::views::all(
					data.vdocument_id().v
				) | ranges::views::transform(
					&MTPlong::v
				) | ranges::to_vector;
			});
		}).fail([=] {
			emojiList(type).requestId = 0;
		}).send();
	};
	list.requestId = (type == EmojiListType::Profile)
		? send(MTPaccount_GetDefaultProfilePhotoEmojis())
		: (type == EmojiListType::Group)
		? send(MTPaccount_GetDefaultGroupPhotoEmojis())
		: (type == EmojiListType::NoChannelStatus)
		? send(MTPaccount_GetChannelRestrictedStatusEmojis())
		: send(MTPaccount_GetDefaultBackgroundEmojis());
}

rpl::producer<PeerPhoto::EmojiList> PeerPhoto::emojiListValue(
		EmojiListType type) {
	const auto &list = emojiList(type);
	if (list.list.current().empty() && !list.requestId) {
		requestEmojiList(type);
	}
	return list.list.value();
}

// Non-personal photo in case a personal photo is set.
void PeerPhoto::registerNonPersonalPhoto(
		not_null<UserData*> user,
		not_null<PhotoData*> photo) {
	_nonPersonalPhotos.emplace_or_assign(user, photo);
}

void PeerPhoto::unregisterNonPersonalPhoto(not_null<UserData*> user) {
	_nonPersonalPhotos.erase(user);
}

PhotoData *PeerPhoto::nonPersonalPhoto(
		not_null<UserData*> user) const {
	const auto i = _nonPersonalPhotos.find(user);
	return (i != end(_nonPersonalPhotos)) ? i->second.get() : nullptr;
}

} // namespace Api
