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
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_file_origin.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_user_photos.h"
#include "history/history.h"
#include "main/main_session.h"
#include "storage/file_upload.h"
#include "storage/localimageloader.h"
#include "storage/storage_user_photos.h"

#include <QtCore/QBuffer>

namespace Api {
namespace {

constexpr auto kSharedMediaLimit = 100;

SendMediaReady PreparePeerPhoto(
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

	QString file, filename;
	int64 filesize = 0;
	QByteArray data;

	return SendMediaReady(
		SendMediaType::Photo,
		file,
		filename,
		filesize,
		data,
		id,
		id,
		u"jpg"_q,
		peerId,
		photo,
		photoThumbs,
		MTP_documentEmpty(MTP_long(0)),
		jpeg,
		0);
}

} // namespace

PeerPhoto::PeerPhoto(not_null<ApiWrap*> api)
: _session(&api->session())
, _api(&api->instance()) {
	crl::on_main(_session, [=] {
		// You can't use _session->lifetime() in the constructor,
		// only queued, because it is not constructed yet.
		_session->uploader().photoReady(
		) | rpl::start_with_next([=](const Storage::UploadedMedia &data) {
			ready(data.fullId, data.info.file);
		}, _session->lifetime());
	});
}

void PeerPhoto::upload(
		not_null<PeerData*> peer,
		QImage &&image,
		Fn<void()> done) {
	upload(peer, std::move(image), UploadType::Default, std::move(done));
}

void PeerPhoto::uploadFallback(not_null<PeerData*> peer, QImage &&image) {
	upload(peer, std::move(image), UploadType::Fallback, nullptr);
}

void PeerPhoto::updateSelf(
		not_null<PhotoData*> photo,
		Data::FileOrigin origin,
		Fn<void()> done) {
	const auto send = [=](auto resend) -> void {
		const auto usedFileReference = photo->fileReference();
		_api.request(MTPphotos_UpdateProfilePhoto(
			MTP_flags(0),
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
		QImage &&image,
		UploadType type,
		Fn<void()> done) {
	peer = peer->migrateToOrMe();
	const auto ready = PreparePeerPhoto(
		_api.instance().mainDcId(),
		peer->id,
		std::move(image));

	const auto fakeId = FullMsgId(
		peer->id,
		_session->data().nextLocalMessageId());
	const auto already = ranges::find(
		_uploads,
		peer,
		[](const auto &pair) { return pair.second.peer; });
	if (already != end(_uploads)) {
		_session->uploader().cancel(already->first);
		_uploads.erase(already);
	}
	_uploads.emplace(
		fakeId,
		UploadValue{ peer, type, std::move(done) });
	_session->uploader().uploadMedia(fakeId, ready);
}

void PeerPhoto::suggest(not_null<PeerData*> peer, QImage &&image) {
	upload(peer, std::move(image), UploadType::Suggestion, nullptr);
}

void PeerPhoto::clear(not_null<PhotoData*> photo) {
	const auto self = _session->user();
	if (self->userpicPhotoId() == photo->id) {
		_api.request(MTPphotos_UpdateProfilePhoto(
			MTP_flags(0),
			MTP_inputPhotoEmpty()
		)).done([=](const MTPphotos_Photo &result) {
			self->setPhoto(MTP_userProfilePhotoEmpty());
		}).send();
	} else if (photo->peer && photo->peer->userpicPhotoId() == photo->id) {
		const auto applier = [=](const MTPUpdates &result) {
			_session->updates().applyUpdates(result);
		};
		if (const auto chat = photo->peer->asChat()) {
			_api.request(MTPmessages_EditChatPhoto(
				chat->inputChat,
				MTP_inputChatPhotoEmpty()
			)).done(applier).send();
		} else if (const auto channel = photo->peer->asChannel()) {
			_api.request(MTPchannels_EditPhoto(
				channel->inputChannel,
				MTP_inputChatPhotoEmpty()
			)).done(applier).send();
		}
	} else {
		const auto fallbackPhotoId = SyncUserFallbackPhotoViewer(self);
		if (fallbackPhotoId && (*fallbackPhotoId) == photo->id) {
			_api.request(MTPphotos_UpdateProfilePhoto(
				MTP_flags(MTPphotos_UpdateProfilePhoto::Flag::f_fallback),
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
		user->inputUser,
		MTPInputFile(),
		MTPInputFile(), // video
		MTPdouble() // video_start_ts
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
		_api.request(MTPphotos_UpdateProfilePhoto(
			MTP_flags(0),
			photo->mtpInput()
		)).done([=](const MTPphotos_Photo &result) {
			result.match([&](const MTPDphotos_photo &data) {
				_session->data().processPhoto(data.vphoto());
				_session->data().processUsers(data.vusers());
			});
		}).send();
	} else {
		const auto applier = [=](const MTPUpdates &result) {
			_session->updates().applyUpdates(result);
		};
		if (const auto chat = peer->asChat()) {
			_api.request(MTPmessages_EditChatPhoto(
				chat->inputChat,
				MTP_inputChatPhoto(photo->mtpInput())
			)).done(applier).send();
		} else if (const auto channel = peer->asChannel()) {
			_api.request(MTPchannels_EditPhoto(
				channel->inputChannel,
				MTP_inputChatPhoto(photo->mtpInput())
			)).done(applier).send();
		}
	}
}

void PeerPhoto::ready(const FullMsgId &msgId, const MTPInputFile &file) {
	const auto maybeUploadValue = _uploads.take(msgId);
	if (!maybeUploadValue) {
		return;
	}
	const auto peer = maybeUploadValue->peer;
	const auto type = maybeUploadValue->type;
	const auto done = maybeUploadValue->done;
	const auto applier = [=](const MTPUpdates &result) {
		_session->updates().applyUpdates(result);
		if (done) {
			done();
		}
	};
	if (peer->isSelf()) {
		_api.request(MTPphotos_UploadProfilePhoto(
			MTP_flags(MTPphotos_UploadProfilePhoto::Flag::f_file
				| ((type == UploadType::Fallback)
					? MTPphotos_UploadProfilePhoto::Flag::f_fallback
					: MTPphotos_UploadProfilePhoto::Flags(0))),
			file,
			MTPInputFile(), // video
			MTPdouble() // video_start_ts
		)).done([=](const MTPphotos_Photo &result) {
			const auto photoId = _session->data().processPhoto(
				result.data().vphoto())->id;
			_session->data().processUsers(result.data().vusers());
			if (type == UploadType::Fallback) {
				_session->storage().add(Storage::UserPhotosSetBack(
					peerToUser(peer->id),
					photoId));
			}
			if (done) {
				done();
			}
		}).send();
	} else if (const auto chat = peer->asChat()) {
		const auto history = _session->data().history(chat);
		history->sendRequestId = _api.request(MTPmessages_EditChatPhoto(
			chat->inputChat,
			MTP_inputChatUploadedPhoto(
				MTP_flags(MTPDinputChatUploadedPhoto::Flag::f_file),
				file,
				MTPInputFile(), // video
				MTPdouble()) // video_start_ts
		)).done(applier).afterRequest(history->sendRequestId).send();
	} else if (const auto channel = peer->asChannel()) {
		const auto history = _session->data().history(channel);
		history->sendRequestId = _api.request(MTPchannels_EditPhoto(
			channel->inputChannel,
			MTP_inputChatUploadedPhoto(
				MTP_flags(MTPDinputChatUploadedPhoto::Flag::f_file),
				file,
				MTPInputFile(), // video
				MTPdouble()) // video_start_ts
		)).done(applier).afterRequest(history->sendRequestId).send();
	} else if (const auto user = peer->asUser()) {
		using Flag = MTPphotos_UploadContactProfilePhoto::Flag;
		_api.request(MTPphotos_UploadContactProfilePhoto(
			MTP_flags(Flag::f_file
				| ((type == UploadType::Suggestion)
					? Flag::f_suggest
					: Flag::f_save)),
			user->inputUser,
			file,
			MTPInputFile(), // video
			MTPdouble() // video_start_ts
		)).done([=](const MTPphotos_Photo &result) {
			result.match([&](const MTPDphotos_photo &data) {
				_session->data().processPhoto(data.vphoto());
				_session->data().processUsers(data.vusers());
			});
			if (type != UploadType::Suggestion) {
				user->updateFullForced();
			}
			if (done) {
				done();
			}
		}).send();
	}
}

void PeerPhoto::requestUserPhotos(
		not_null<UserData*> user,
		UserPhotoId afterId) {
	if (_userPhotosRequests.contains(user)) {
		return;
	}

	const auto requestId = _api.request(MTPphotos_GetUserPhotos(
		user->inputUser,
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
