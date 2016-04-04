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

namespace {

using StringToTypeMap = QMap<QString, Result::Type>;
NeverFreedPointer<StringToTypeMap> stringToTypeMap;

} // namespace

Result::Result(const Creator &creator) : queryId(creator.queryId), type(creator.type) {
}

UniquePointer<Result> Result::create(uint64 queryId, const MTPBotInlineResult &mtpData) {
	stringToTypeMap.createIfNull([]() -> StringToTypeMap* {
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
	});
	auto getInlineResultType = [](const MTPBotInlineResult &inlineResult) -> Type {
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
		result->id = qs(r.vid);
		if (r.has_title()) result->title = qs(r.vtitle);
		if (r.has_description()) result->description = qs(r.vdescription);
		if (r.has_url()) result->url = qs(r.vurl);
		if (r.has_thumb_url()) result->thumb_url = qs(r.vthumb_url);
		if (r.has_content_type()) result->content_type = qs(r.vcontent_type);
		if (r.has_content_url()) result->content_url = qs(r.vcontent_url);
		if (r.has_w()) result->width = r.vw.v;
		if (r.has_h()) result->height = r.vh.v;
		if (r.has_duration()) result->duration = r.vduration.v;
		if (!result->thumb_url.isEmpty() && (result->thumb_url.startsWith(qstr("http://"), Qt::CaseInsensitive) || result->thumb_url.startsWith(qstr("https://"), Qt::CaseInsensitive))) {
			result->thumb = ImagePtr(result->thumb_url);
		}
		message = &r.vsend_message;
	} break;
	case mtpc_botInlineMediaResult: {
		const MTPDbotInlineMediaResult &r(mtpData.c_botInlineMediaResult());
		result->id = qs(r.vid);
		if (r.has_title()) result->title = qs(r.vtitle);
		if (r.has_description()) result->description = qs(r.vdescription);
		if (r.has_photo()) result->photo = App::feedPhoto(r.vphoto);
		if (r.has_document()) result->document = App::feedDocument(r.vdocument);
		message = &r.vsend_message;
	} break;
	}
	bool badAttachment = (result->photo && !result->photo->access) || (result->document && !result->document->access);

	if (!message) {
		return UniquePointer<Result>();
	}

	switch (message->type()) {
	case mtpc_botInlineMessageMediaAuto: {
		const MTPDbotInlineMessageMediaAuto &r(message->c_botInlineMessageMediaAuto());
		if (result->type == Type::Photo) {
			result->sendData.reset(new internal::SendPhoto(result->photo, result->content_url, qs(r.vcaption)));
		} else {
			result->sendData.reset(new internal::SendFile(result->document, result->content_url, qs(r.vcaption)));
		}
	} break;

	case mtpc_botInlineMessageText: {
		const MTPDbotInlineMessageText &r(message->c_botInlineMessageText());
		EntitiesInText entities = r.has_entities() ? entitiesFromMTP(r.ventities.c_vector().v) : EntitiesInText();
		result->sendData.reset(new internal::SendText(qs(r.vmessage), entities, r.is_no_webpage()));
	} break;

	case mtpc_botInlineMessageMediaGeo: {
		const MTPDbotInlineMessageMediaGeo &r(message->c_botInlineMessageMediaGeo());
		if (r.vgeo.type() == mtpc_geoPoint) {
			result->sendData.reset(new internal::SendGeo(r.vgeo.c_geoPoint()));
		} else {
			badAttachment = true;
		}
	} break;

	case mtpc_botInlineMessageMediaVenue: {
		const MTPDbotInlineMessageMediaVenue &r(message->c_botInlineMessageMediaVenue());
		if (r.vgeo.type() == mtpc_geoPoint) {
			result->sendData.reset(new internal::SendVenue(r.vgeo.c_geoPoint(), qs(r.vvenue_id), qs(r.vprovider), qs(r.vtitle), qs(r.vaddress)));
		} else {
			badAttachment = true;
		}
	} break;

	case mtpc_botInlineMessageMediaContact: {
		const MTPDbotInlineMessageMediaContact &r(message->c_botInlineMessageMediaContact());
		result->sendData.reset(new internal::SendContact(qs(r.vfirst_name), qs(r.vlast_name), qs(r.vphone_number)));
	} break;

	default: {
		badAttachment = true;
	} break;
	}

	if (badAttachment || !result->sendData || !result->sendData->isValid()) {
		return UniquePointer<Result>();
	}
	return result;
}

void Result::automaticLoadGif() {
	if (loaded() || type != Type::Gif || (content_type != qstr("video/mp4") && content_type != "image/gif")) return;

	if (_loader != CancelledWebFileLoader) {
		// if load at least anywhere
		bool loadFromCloud = !(cAutoDownloadGif() & dbiadNoPrivate) || !(cAutoDownloadGif() & dbiadNoGroups);
		saveFile(QString(), loadFromCloud ? LoadFromCloudOrLocal : LoadFromLocalOnly, true);
	}
}

void Result::automaticLoadSettingsChangedGif() {
	if (loaded() || _loader != CancelledWebFileLoader) return;
	_loader = 0;
}

void Result::saveFile(const QString &toFile, LoadFromCloudSetting fromCloud, bool autoLoading) {
	if (loaded()) {
		return;
	}

	if (_loader == CancelledWebFileLoader) _loader = 0;
	if (_loader) {
		if (!_loader->setFileName(toFile)) {
			cancelFile();
			_loader = 0;
		}
	}

	if (_loader) {
		if (fromCloud == LoadFromCloudOrLocal) _loader->permitLoadFromCloud();
	} else {
		_loader = new webFileLoader(content_url, toFile, fromCloud, autoLoading);
		regLoader(_loader, this);

		_loader->connect(_loader, SIGNAL(progress(FileLoader*)), App::main(), SLOT(inlineResultLoadProgress(FileLoader*)));
		_loader->connect(_loader, SIGNAL(failed(FileLoader*, bool)), App::main(), SLOT(inlineResultLoadFailed(FileLoader*, bool)));
		_loader->start();
	}
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
			_loader = 0;
		}
	}
	return !_data.isEmpty();
}

bool Result::displayLoading() const {
	return loading() ? (!_loader->loadingLocal() || !_loader->autoLoading()) : false;
}

void Result::forget() {
	thumb->forget();
	_data.clear();
}

float64 Result::progress() const {
	return loading() ? _loader->currentProgress() : (loaded() ? 1 : 0);	return false;
}

bool Result::hasThumbDisplay() const {
	if (!thumb->isNull()) {
		return true;
	}
	if (type == Type::Contact) {
		return true;
	}
	if (sendData->hasLocationCoords()) {
		return true;
	}
	return false;
};

void Result::addToHistory(History *history, MTPDmessage::Flags flags, MsgId msgId, UserId fromId, MTPint mtpDate, UserId viaBotId, MsgId replyToId) const {
	if (DocumentData *document = sendData->getSentDocument()) {
		history->addNewDocument(msgId, flags, viaBotId, replyToId, date(mtpDate), fromId, document, sendData->getSentCaption());
	} else if (PhotoData *photo = sendData->getSentPhoto()) {
		history->addNewPhoto(msgId, flags, viaBotId, replyToId, date(mtpDate), fromId, photo, sendData->getSentCaption());
	} else {
		internal::SendData::SentMTPMessageFields fields = sendData->getSentMessageFields(this);
		if (!fields.entities.c_vector().v.isEmpty()) {
			flags |= MTPDmessage::Flag::f_entities;
		}
		history->addNewMessage(MTP_message(MTP_flags(flags), MTP_int(msgId), MTP_int(fromId), peerToMTP(history->peer->id), MTPnullFwdHeader, MTP_int(viaBotId), MTP_int(replyToId), mtpDate, fields.text, fields.media, MTPnullMarkup, fields.entities, MTP_int(1), MTPint()), NewMessageUnread);
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
