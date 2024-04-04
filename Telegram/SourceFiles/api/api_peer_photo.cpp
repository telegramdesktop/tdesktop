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
#include "storage/file_upload.h"
#include "storage/localimageloader.h"
#include "storage/storage_user_photos.h"

#include <QtCore/QBuffer>

namespace Api {
namespace {

constexpr auto kSharedMediaLimit = 100;

[[nodiscard]] SendMediaReady PreparePeerPhoto(
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
		jpeg);
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
		// You can't use _session->lifetime() in the constructor,
		// only queued, because it is not constructed yet.
		_session->uploader().photoReady(
		) | rpl::start_with_next([=](const Storage::UploadedMedia &data) {
			ready(data.fullId, data.info.file, std::nullopt);
		}, _session->lifetime());
	});
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
	const auto mtpMarkup = PrepareMtpMarkup(_session, photo);

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
	if (mtpMarkup) {
		ready(fakeId, std::nullopt, mtpMarkup);
	} else {
		const auto ready = PreparePeerPhoto(
			_api.instance().mainDcId(),
			peer->id,
			base::take(photo.image));
		_session->uploader().uploadMedia(fakeId, ready);
	}
}

void PeerPhoto::suggest(not_null<PeerData*> peer, UserPhoto &&photo) {
	upload(peer, std::move(photo), UploadType::Suggestion, nullptr);
}

void PeerPhoto::clear(not_null<PhotoData*> photo) {
	const auto self = _session->user();
	if (self->userpicPhotoId() == photo->id) {
		_api.request(MTPphotos_UpdateProfilePhoto(
			MTP_flags(0),
			MTPInputUser(), // bot
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
		user->inputUser,
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
		_api.request(MTPphotos_UpdateProfilePhoto(
			MTP_flags(0),
			MTPInputUser(), // bot
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

void PeerPhoto::ready(
		const FullMsgId &msgId,
		std::optional<MTPInputFile> file,
		std::optional<MTPVideoSize> videoSize) {
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
	const auto botUserInput = [&] {
		const auto user = peer->asUser();
		return (user && user->botInfo && user->botInfo->canEditInformation)
			? std::make_optional<MTPInputUser>(user->inputUser)
			: std::nullopt;
	}();
	if (peer->isSelf() || botUserInput) {
		using Flag = MTPphotos_UploadProfilePhoto::Flag;
		const auto none = MTPphotos_UploadProfilePhoto::Flags(0);
		_api.request(MTPphotos_UploadProfilePhoto(
			MTP_flags((file ? Flag::f_file : none)
				| (botUserInput ? Flag::f_bot : none)
				| (videoSize ? Flag::f_video_emoji_markup : none)
				| ((type == UploadType::Fallback) ? Flag::f_fallback : none)),
			botUserInput ? (*botUserInput) : MTPInputUser(), // bot
			file ? (*file) : MTPInputFile(),
			MTPInputFile(), // video
			MTPdouble(), // video_start_ts
			videoSize ? (*videoSize) : MTPVideoSize() // video_emoji_markup
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
		using Flag = MTPDinputChatUploadedPhoto::Flag;
		const auto none = MTPDinputChatUploadedPhoto::Flags(0);
		history->sendRequestId = _api.request(MTPmessages_EditChatPhoto(
			chat->inputChat,
			MTP_inputChatUploadedPhoto(
				MTP_flags((file ? Flag::f_file : none)
					| (videoSize ? Flag::f_video_emoji_markup : none)),
				file ? (*file) : MTPInputFile(),
				MTPInputFile(), // video
				MTPdouble(), // video_start_ts
				videoSize ? (*videoSize) : MTPVideoSize()) // video_emoji_markup
		)).done(applier).afterRequest(history->sendRequestId).send();
	} else if (const auto channel = peer->asChannel()) {
		using Flag = MTPDinputChatUploadedPhoto::Flag;
		const auto none = MTPDinputChatUploadedPhoto::Flags(0);
		const auto history = _session->data().history(channel);
		history->sendRequestId = _api.request(MTPchannels_EditPhoto(
			channel->inputChannel,
			MTP_inputChatUploadedPhoto(
				MTP_flags((file ? Flag::f_file : none)
					| (videoSize ? Flag::f_video_emoji_markup : none)),
				file ? (*file) : MTPInputFile(),
				MTPInputFile(), // video
				MTPdouble(), // video_start_ts
				videoSize ? (*videoSize) : MTPVideoSize()) // video_emoji_markup
		)).done(applier).afterRequest(history->sendRequestId).send();
	} else if (const auto user = peer->asUser()) {
		using Flag = MTPphotos_UploadContactProfilePhoto::Flag;
		const auto none = MTPphotos_UploadContactProfilePhoto::Flags(0);
		_api.request(MTPphotos_UploadContactProfilePhoto(
			MTP_flags((file ? Flag::f_file : none)
				| (videoSize ? Flag::f_video_emoji_markup : none)
				| ((type == UploadType::Suggestion)
					? Flag::f_suggest
					: Flag::f_save)),
			user->inputUser,
			file ? (*file) : MTPInputFile(),
			MTPInputFile(), // video
			MTPdouble(), // video_start_ts
			videoSize ? (*videoSize) : MTPVideoSize() // video_emoji_markup
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
	auto &list = emojiList(type);
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
