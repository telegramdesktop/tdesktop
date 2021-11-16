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
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "main/main_session.h"
#include "storage/file_upload.h"
#include "storage/localimageloader.h"
#include "storage/storage_user_photos.h"

#include <QtCore/QBuffer>

namespace Api {
namespace {

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
	int32 filesize = 0;
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

void PeerPhoto::upload(not_null<PeerData*> peer, QImage &&image) {
	peer = peer->migrateToOrMe();
	const auto ready = PreparePeerPhoto(
		_api.instance().mainDcId(),
		peer->id,
		std::move(image));

	const auto fakeId = FullMsgId(
		peerToChannel(peer->id),
		_session->data().nextLocalMessageId());
	const auto already = ranges::find(
		_uploads,
		peer,
		[](const auto &pair) { return pair.second; });
	if (already != end(_uploads)) {
		_session->uploader().cancel(already->first);
		_uploads.erase(already);
	}
	_uploads.emplace(fakeId, peer);
	_session->uploader().uploadMedia(fakeId, ready);
}

void PeerPhoto::clear(not_null<PhotoData*> photo) {
	const auto self = _session->user();
	if (self->userpicPhotoId() == photo->id) {
		_api.request(MTPphotos_UpdateProfilePhoto(
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
		_api.request(MTPphotos_DeletePhotos(
			MTP_vector<MTPInputPhoto>(1, photo->mtpInput())
		)).send();
		_session->storage().remove(Storage::UserPhotosRemoveOne(
			peerToUser(self->id),
			photo->id));
	}
}

void PeerPhoto::ready(const FullMsgId &msgId, const MTPInputFile &file) {
	const auto maybePeer = _uploads.take(msgId);
	if (!maybePeer) {
		return;
	}
	const auto peer = *maybePeer;
	const auto applier = [=](const MTPUpdates &result) {
		_session->updates().applyUpdates(result);
	};
	if (peer->isSelf()) {
		_api.request(MTPphotos_UploadProfilePhoto(
			MTP_flags(MTPphotos_UploadProfilePhoto::Flag::f_file),
			file,
			MTPInputFile(), // video
			MTPdouble() // video_start_ts
		)).done([=](const MTPphotos_Photo &result) {
			result.match([&](const MTPDphotos_photo &data) {
				_session->data().processPhoto(data.vphoto());
				_session->data().processUsers(data.vusers());
			});
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
	}
}

} // namespace Api
