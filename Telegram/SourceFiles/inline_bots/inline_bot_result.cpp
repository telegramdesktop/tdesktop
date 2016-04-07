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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "inline_bots/inline_bot_result.h"

#include "inline_bots/inline_bot_layout_item.h"
#include "inline_bots/inline_bot_send_data.h"
#include "mtproto/file_download.h"
#include "mainwidget.h"

namespace InlineBots {

namespace {

using ResultsByLoaderMap = QMap<FileLoader*, Result*>;
NeverFreedPointer<ResultsByLoaderMap> ResultsByLoader;

void regLoader(FileLoader *loader, Result *result) {
	ResultsByLoader.createIfNull([]() -> ResultsByLoaderMap* {
		return new ResultsByLoaderMap();
	});
	ResultsByLoader->insert(loader, result);
}

void unregLoader(FileLoader *loader) {
	if (!ResultsByLoader) {
		return;
	}
	ResultsByLoader->remove(loader);
}

} // namespace

Result *getResultFromLoader(FileLoader *loader) {
	if (!ResultsByLoader) {
		return nullptr;
	}
	return ResultsByLoader->value(loader, nullptr);
}

Result::Result(const Creator &creator) : _queryId(creator.queryId), _type(creator.type) {
}

UniquePointer<Result> Result::create(uint64 queryId, const MTPBotInlineResult &mtpData) {
	using StringToTypeMap = QMap<QString, Result::Type>;
	StaticNeverFreedPointer<StringToTypeMap> stringToTypeMap{ ([]() -> StringToTypeMap* {
		auto result = MakeUnique<StringToTypeMap>();
		result->insert(qsl("photo"), Result::Type::Photo);
		result->insert(qsl("video"), Result::Type::Video);
		result->insert(qsl("audio"), Result::Type::Audio);
		result->insert(qsl("sticker"), Result::Type::Sticker);
		result->insert(qsl("file"), Result::Type::File);
		result->insert(qsl("gif"), Result::Type::Gif);
		result->insert(qsl("article"), Result::Type::Article);
		result->insert(qsl("contact"), Result::Type::Contact);
		result->insert(qsl("venue"), Result::Type::Venue);
		return result.release();
	})() };

	auto getInlineResultType = [&stringToTypeMap](const MTPBotInlineResult &inlineResult) -> Type {
		QString type;
		switch (inlineResult.type()) {
		case mtpc_botInlineResult: type = qs(inlineResult.c_botInlineResult().vtype); break;
		case mtpc_botInlineMediaResult: type = qs(inlineResult.c_botInlineMediaResult().vtype); break;
		}
		return stringToTypeMap->value(type, Type::Unknown);
	};
	Type type = getInlineResultType(mtpData);
	if (type == Type::Unknown) {
		return UniquePointer<Result>();
	}

	auto result = MakeUnique<Result>(Creator{ queryId, type });
	const MTPBotInlineMessage *message = nullptr;
	switch (mtpData.type()) {
	case mtpc_botInlineResult: {
		const MTPDbotInlineResult &r(mtpData.c_botInlineResult());
		result->_id = qs(r.vid);
		if (r.has_title()) result->_title = qs(r.vtitle);
		if (r.has_description()) result->_description = qs(r.vdescription);
		if (r.has_url()) result->_url = qs(r.vurl);
		if (r.has_thumb_url()) result->_thumb_url = qs(r.vthumb_url);
		if (r.has_content_type()) result->_content_type = qs(r.vcontent_type);
		if (r.has_content_url()) result->_content_url = qs(r.vcontent_url);
		if (r.has_w()) result->_width = r.vw.v;
		if (r.has_h()) result->_height = r.vh.v;
		if (r.has_duration()) result->_duration = r.vduration.v;
		if (!result->_thumb_url.isEmpty() && (result->_thumb_url.startsWith(qstr("http://"), Qt::CaseInsensitive) || result->_thumb_url.startsWith(qstr("https://"), Qt::CaseInsensitive))) {
			result->_thumb = ImagePtr(result->_thumb_url);
		}
		message = &r.vsend_message;
	} break;
	case mtpc_botInlineMediaResult: {
		const MTPDbotInlineMediaResult &r(mtpData.c_botInlineMediaResult());
		result->_id = qs(r.vid);
		if (r.has_title()) result->_title = qs(r.vtitle);
		if (r.has_description()) result->_description = qs(r.vdescription);
		if (r.has_photo()) {
			result->_mtpPhoto = r.vphoto;
			result->_photo = App::feedPhoto(r.vphoto);
		}
		if (r.has_document()) {
			result->_mtpDocument = r.vdocument;
			result->_document = App::feedDocument(r.vdocument);
		}
		message = &r.vsend_message;
	} break;
	}
	bool badAttachment = (result->_photo && !result->_photo->access) || (result->_document && !result->_document->access);

	if (!message) {
		return UniquePointer<Result>();
	}

	switch (message->type()) {
	case mtpc_botInlineMessageMediaAuto: {
		const MTPDbotInlineMessageMediaAuto &r(message->c_botInlineMessageMediaAuto());
		if (result->_type == Type::Photo) {
			result->sendData.reset(new internal::SendPhoto(result->_photo, result->_content_url, qs(r.vcaption)));
		} else {
			result->sendData.reset(new internal::SendFile(result->_document, result->_content_url, qs(r.vcaption)));
		}
		if (r.has_reply_markup()) {
			result->_mtpKeyboard = MakeUnique<MTPReplyMarkup>(r.vreply_markup);
		}
	} break;

	case mtpc_botInlineMessageText: {
		const MTPDbotInlineMessageText &r(message->c_botInlineMessageText());
		EntitiesInText entities = r.has_entities() ? entitiesFromMTP(r.ventities.c_vector().v) : EntitiesInText();
		result->sendData.reset(new internal::SendText(qs(r.vmessage), entities, r.is_no_webpage()));
		if (r.has_reply_markup()) {
			result->_mtpKeyboard = MakeUnique<MTPReplyMarkup>(r.vreply_markup);
		}
	} break;

	case mtpc_botInlineMessageMediaGeo: {
		const MTPDbotInlineMessageMediaGeo &r(message->c_botInlineMessageMediaGeo());
		if (r.vgeo.type() == mtpc_geoPoint) {
			result->sendData.reset(new internal::SendGeo(r.vgeo.c_geoPoint()));
		} else {
			badAttachment = true;
		}
		if (r.has_reply_markup()) {
			result->_mtpKeyboard = MakeUnique<MTPReplyMarkup>(r.vreply_markup);
		}
	} break;

	case mtpc_botInlineMessageMediaVenue: {
		const MTPDbotInlineMessageMediaVenue &r(message->c_botInlineMessageMediaVenue());
		if (r.vgeo.type() == mtpc_geoPoint) {
			result->sendData.reset(new internal::SendVenue(r.vgeo.c_geoPoint(), qs(r.vvenue_id), qs(r.vprovider), qs(r.vtitle), qs(r.vaddress)));
		} else {
			badAttachment = true;
		}
		if (r.has_reply_markup()) {
			result->_mtpKeyboard = MakeUnique<MTPReplyMarkup>(r.vreply_markup);
		}
	} break;

	case mtpc_botInlineMessageMediaContact: {
		const MTPDbotInlineMessageMediaContact &r(message->c_botInlineMessageMediaContact());
		result->sendData.reset(new internal::SendContact(qs(r.vfirst_name), qs(r.vlast_name), qs(r.vphone_number)));
		if (r.has_reply_markup()) {
			result->_mtpKeyboard = MakeUnique<MTPReplyMarkup>(r.vreply_markup);
		}
	} break;

	default: {
		badAttachment = true;
	} break;
	}

	if (badAttachment || !result->sendData || !result->sendData->isValid()) {
		return UniquePointer<Result>();
	}

	LocationCoords location;
	if (result->getLocationCoords(&location)) {
		int32 w = st::inlineThumbSize, h = st::inlineThumbSize;
		int32 zoom = 13, scale = 1;
		if (cScale() == dbisTwo || cRetina()) {
			scale = 2;
			w /= 2;
			h /= 2;
		}
		QString coords = qsl("%1,%2").arg(location.lat).arg(location.lon);
		QString url = qsl("https://maps.googleapis.com/maps/api/staticmap?center=") + coords + qsl("&zoom=%1&size=%2x%3&maptype=roadmap&scale=%4&markers=color:red|size:big|").arg(zoom).arg(w).arg(h).arg(scale) + coords + qsl("&sensor=false");
		result->_locationThumb = ImagePtr(url);
	}

	return result;
}

bool Result::onChoose(Layout::ItemBase *layout) {
	if (_photo && _type == Type::Photo) {
		if (_photo->medium->loaded() || _photo->thumb->loaded()) {
			return true;
		} else if (!_photo->medium->loading()) {
			_photo->thumb->loadEvenCancelled();
			_photo->medium->loadEvenCancelled();
		}
	}
	if (_document && (
		_type == Type::Video ||
		_type == Type::Audio ||
		_type == Type::Sticker ||
		_type == Type::File ||
		_type == Type::Gif)) {
		if (_type == Type::Gif) {
			if (_document->loaded()) {
				return true;
			} else if (_document->loading()) {
				_document->cancel();
			} else {
				DocumentOpenClickHandler::doOpen(_document, ActionOnLoadNone);
			}
		} else {
			return true;
		}
	}
	if (_type == Type::Photo) {
		if (_thumb->loaded()) {
			return true;
		} else if (!_thumb->loading()) {
			_thumb->loadEvenCancelled();
			Ui::repaintInlineItem(layout);
		}
	} else if (_type == Type::Gif) {
		if (loaded()) {
			return true;
		} else if (loading()) {
			cancelFile();
			Ui::repaintInlineItem(layout);
		} else {
			saveFile(QString(), LoadFromCloudOrLocal, false);
			Ui::repaintInlineItem(layout);
		}
	} else {
		return true;
	}
	return false;
}

void Result::automaticLoadGif() {
	if (loaded() || _type != Type::Gif) {
		return;
	}
	if (_content_type != qstr("video/mp4") && _content_type != qstr("image/gif")) {
		return;
	}

	if (_loader != CancelledWebFileLoader) {
		// if load at least anywhere
		bool loadFromCloud = !(cAutoDownloadGif() & dbiadNoPrivate) || !(cAutoDownloadGif() & dbiadNoGroups);
		saveFile(QString(), loadFromCloud ? LoadFromCloudOrLocal : LoadFromLocalOnly, true);
	}
}

void Result::automaticLoadSettingsChangedGif() {
	if (loaded() || _loader != CancelledWebFileLoader) return;
	_loader = nullptr;
}

void Result::saveFile(const QString &toFile, LoadFromCloudSetting fromCloud, bool autoLoading) {
	if (loaded()) {
		return;
	}

	if (_loader == CancelledWebFileLoader) _loader = nullptr;
	if (_loader) {
		if (!_loader->setFileName(toFile)) {
			cancelFile();
			_loader = nullptr;
		}
	}

	if (_loader) {
		if (fromCloud == LoadFromCloudOrLocal) _loader->permitLoadFromCloud();
	} else {
		_loader = new webFileLoader(_content_url, toFile, fromCloud, autoLoading);
		regLoader(_loader, this);

		_loader->connect(_loader, SIGNAL(progress(FileLoader*)), App::main(), SLOT(inlineResultLoadProgress(FileLoader*)));
		_loader->connect(_loader, SIGNAL(failed(FileLoader*, bool)), App::main(), SLOT(inlineResultLoadFailed(FileLoader*, bool)));
		_loader->start();
	}
}

void Result::openFile() {
	//if (loaded()) {
	//	bool playVoice = data->voice() && audioPlayer() && item;
	//	bool playMusic = data->song() && audioPlayer() && item;
	//	bool playAnimation = data->isAnimation() && item && item->getMedia();
	//	const FileLocation &location(data->location(true));
	//	if (!location.isEmpty() || (!data->data().isEmpty() && (playVoice || playMusic || playAnimation))) {
	//		if (playVoice) {
	//			AudioMsgId playing;
	//			AudioPlayerState playingState = AudioPlayerStopped;
	//			audioPlayer()->currentState(&playing, &playingState);
	//			if (playing.msgId == item->fullId() && !(playingState & AudioPlayerStoppedMask) && playingState != AudioPlayerFinishing) {
	//				audioPlayer()->pauseresume(OverviewVoiceFiles);
	//			} else {
	//				AudioMsgId audio(data, item->fullId());
	//				audioPlayer()->play(audio);
	//				if (App::main()) {
	//					App::main()->audioPlayProgress(audio);
	//					App::main()->mediaMarkRead(data);
	//				}
	//			}
	//		} else if (playMusic) {
	//			SongMsgId playing;
	//			AudioPlayerState playingState = AudioPlayerStopped;
	//			audioPlayer()->currentState(&playing, &playingState);
	//			if (playing.msgId == item->fullId() && !(playingState & AudioPlayerStoppedMask) && playingState != AudioPlayerFinishing) {
	//				audioPlayer()->pauseresume(OverviewFiles);
	//			} else {
	//				SongMsgId song(data, item->fullId());
	//				audioPlayer()->play(song);
	//				if (App::main()) App::main()->documentPlayProgress(song);
	//			}
	//		} else if (data->voice() || data->isVideo()) {
	//			psOpenFile(location.name());
	//			if (App::main()) App::main()->mediaMarkRead(data);
	//		} else if (data->size < MediaViewImageSizeLimit) {
	//			if (!data->data().isEmpty() && playAnimation) {
	//				if (action == ActionOnLoadPlayInline && item->getMedia()) {
	//					item->getMedia()->playInline(item);
	//				} else {
	//					App::wnd()->showDocument(data, item);
	//				}
	//			} else if (location.accessEnable()) {
	//				if (item && (data->isAnimation() || QImageReader(location.name()).canRead())) {
	//					if (action == ActionOnLoadPlayInline && item->getMedia()) {
	//						item->getMedia()->playInline(item);
	//					} else {
	//						App::wnd()->showDocument(data, item);
	//					}
	//				} else {
	//					psOpenFile(location.name());
	//				}
	//				location.accessDisable();
	//			} else {
	//				psOpenFile(location.name());
	//			}
	//		} else {
	//			psOpenFile(location.name());
	//		}
	//		return;
	//	}
	//}

	//QString filename = documentSaveFilename(data);
	//if (filename.isEmpty()) return;
	//if (!data->saveToCache()) {
	//	filename = documentSaveFilename(data);
	//	if (filename.isEmpty()) return;
	//}

	//saveFile()
	//data->save(filename, action, item ? item->fullId() : FullMsgId());
}

void Result::cancelFile() {
	if (!loading()) return;

	unregLoader(_loader);

	webFileLoader *l = _loader;
	_loader = CancelledWebFileLoader;
	if (l) {
		l->cancel();
		l->deleteLater();
		l->stop();
	}
}

QByteArray Result::data() const {
	return _data;
}

bool Result::loading() const {
	return _loader && _loader != CancelledWebFileLoader;
}

bool Result::loaded() const {
	if (loading() && _loader->done()) {
		unregLoader(_loader);
		if (_loader->fileType() == mtpc_storage_fileUnknown) {
			_loader->deleteLater();
			_loader->stop();
			_loader = CancelledWebFileLoader;
		} else {
			Result *that = const_cast<Result*>(this);
			that->_data = _loader->bytes();

			_loader->deleteLater();
			_loader->stop();
			_loader = nullptr;
		}
	}
	return !_data.isEmpty();
}

bool Result::displayLoading() const {
	return loading() ? (!_loader->loadingLocal() || !_loader->autoLoading()) : false;
}

void Result::forget() {
	_thumb->forget();
	_data.clear();
	if (_document) {
		_document->forget();
	}
	if (_photo) {
		_photo->forget();
	}
}

float64 Result::progress() const {
	return loading() ? _loader->currentProgress() : (loaded() ? 1 : 0);	return false;
}

bool Result::hasThumbDisplay() const {
	if (!_thumb->isNull()) {
		return true;
	}
	if (_type == Type::Contact) {
		return true;
	}
	if (sendData->hasLocationCoords()) {
		return true;
	}
	return false;
};

void Result::addToHistory(History *history, MTPDmessage::Flags flags, MsgId msgId, UserId fromId, MTPint mtpDate, UserId viaBotId, MsgId replyToId) const {
	flags |= MTPDmessage_ClientFlag::f_from_inline_bot;

	MTPReplyMarkup markup = MTPnullMarkup;
	if (_mtpKeyboard) {
		flags |= MTPDmessage::Flag::f_reply_markup;
		markup = *_mtpKeyboard;
	}
	if (DocumentData *document = sendData->getSentDocument()) {
		history->addNewDocument(msgId, flags, viaBotId, replyToId, date(mtpDate), fromId, document, sendData->getSentCaption(), markup);
	} else if (PhotoData *photo = sendData->getSentPhoto()) {
		history->addNewPhoto(msgId, flags, viaBotId, replyToId, date(mtpDate), fromId, photo, sendData->getSentCaption(), markup);
	} else {
		internal::SendData::SentMTPMessageFields fields = sendData->getSentMessageFields(this);
		if (!fields.entities.c_vector().v.isEmpty()) {
			flags |= MTPDmessage::Flag::f_entities;
		}
		history->addNewMessage(MTP_message(MTP_flags(flags), MTP_int(msgId), MTP_int(fromId), peerToMTP(history->peer->id), MTPnullFwdHeader, MTP_int(viaBotId), MTP_int(replyToId), mtpDate, fields.text, fields.media, markup, fields.entities, MTP_int(1), MTPint()), NewMessageUnread);
	}

}

bool Result::getLocationCoords(LocationCoords *outLocation) const {
	return sendData->getLocationCoords(outLocation);
}

QString Result::getLayoutTitle() const {
	return sendData->getLayoutTitle(this);
}

QString Result::getLayoutDescription() const {
	return sendData->getLayoutDescription(this);
}

Result::~Result() {
	cancelFile();
}

} // namespace InlineBots
