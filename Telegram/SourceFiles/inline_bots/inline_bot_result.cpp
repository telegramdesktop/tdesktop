/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "inline_bots/inline_bot_result.h"

#include "data/data_photo.h"
#include "data/data_document.h"
#include "inline_bots/inline_bot_layout_item.h"
#include "inline_bots/inline_bot_send_data.h"
#include "storage/file_download.h"
#include "core/file_utilities.h"
#include "mainwidget.h"

namespace InlineBots {

Result::Result(const Creator &creator) : _queryId(creator.queryId), _type(creator.type) {
}

std::unique_ptr<Result> Result::create(uint64 queryId, const MTPBotInlineResult &mtpData) {
	using StringToTypeMap = QMap<QString, Result::Type>;
	static StaticNeverFreedPointer<StringToTypeMap> stringToTypeMap{ ([]() -> StringToTypeMap* {
		auto result = std::make_unique<StringToTypeMap>();
		result->insert(qsl("photo"), Result::Type::Photo);
		result->insert(qsl("video"), Result::Type::Video);
		result->insert(qsl("audio"), Result::Type::Audio);
		result->insert(qsl("voice"), Result::Type::Audio);
		result->insert(qsl("sticker"), Result::Type::Sticker);
		result->insert(qsl("file"), Result::Type::File);
		result->insert(qsl("gif"), Result::Type::Gif);
		result->insert(qsl("article"), Result::Type::Article);
		result->insert(qsl("contact"), Result::Type::Contact);
		result->insert(qsl("venue"), Result::Type::Venue);
		result->insert(qsl("geo"), Result::Type::Geo);
		result->insert(qsl("game"), Result::Type::Game);
		return result.release();
	})() };

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
		return nullptr;
	}

	auto result = std::make_unique<Result>(Creator{ queryId, type });
	const MTPBotInlineMessage *message = nullptr;
	switch (mtpData.type()) {
	case mtpc_botInlineResult: {
		const auto &r(mtpData.c_botInlineResult());
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
		if (!result->_thumb_url.startsWith(qstr("http://"), Qt::CaseInsensitive) && !result->_thumb_url.startsWith(qstr("https://"), Qt::CaseInsensitive)) {
			result->_thumb_url = QString();
		}
		message = &r.vsend_message;
	} break;
	case mtpc_botInlineMediaResult: {
		const auto &r(mtpData.c_botInlineMediaResult());
		result->_id = qs(r.vid);
		if (r.has_title()) result->_title = qs(r.vtitle);
		if (r.has_description()) result->_description = qs(r.vdescription);
		if (r.has_photo()) {
			result->_photo = App::feedPhoto(r.vphoto);
		}
		if (r.has_document()) {
			result->_document = App::feedDocument(r.vdocument);
		}
		message = &r.vsend_message;
	} break;
	}
	bool badAttachment = (result->_photo && !result->_photo->access) || (result->_document && !result->_document->isValid());

	if (!message) {
		return nullptr;
	}

	// Ensure required media fields for layouts.
	if (result->_type == Type::Photo) {
		if (!result->_photo && result->_content_url.isEmpty()) {
			return nullptr;
		}
		result->createPhoto();
	} else if (result->_type == Type::File || result->_type == Type::Gif || result->_type == Type::Sticker) {
		if (!result->_document && result->_content_url.isEmpty()) {
			return nullptr;
		}
		result->createDocument();
	}

	switch (message->type()) {
	case mtpc_botInlineMessageMediaAuto: {
		auto &r = message->c_botInlineMessageMediaAuto();
		if (result->_type == Type::Photo) {
			result->createPhoto();
			result->sendData = std::make_unique<internal::SendPhoto>(result->_photo, qs(r.vcaption));
		} else if (result->_type == Type::Game) {
			result->createGame();
			result->sendData = std::make_unique<internal::SendGame>(result->_game);
		} else {
			result->createDocument();
			result->sendData = std::make_unique<internal::SendFile>(result->_document, qs(r.vcaption));
		}
		if (r.has_reply_markup()) {
			result->_mtpKeyboard = std::make_unique<MTPReplyMarkup>(r.vreply_markup);
		}
	} break;

	case mtpc_botInlineMessageText: {
		auto &r = message->c_botInlineMessageText();
		auto entities = r.has_entities() ? TextUtilities::EntitiesFromMTP(r.ventities.v) : EntitiesInText();
		result->sendData = std::make_unique<internal::SendText>(qs(r.vmessage), entities, r.is_no_webpage());
		if (result->_type == Type::Photo) {
			result->createPhoto();
		} else if (result->_type == Type::Audio || result->_type == Type::File || result->_type == Type::Video || result->_type == Type::Sticker || result->_type == Type::Gif) {
			result->createDocument();
		}
		if (r.has_reply_markup()) {
			result->_mtpKeyboard = std::make_unique<MTPReplyMarkup>(r.vreply_markup);
		}
	} break;

	case mtpc_botInlineMessageMediaGeo: {
		// #TODO layer 72 save period and send live location?..
		auto &r = message->c_botInlineMessageMediaGeo();
		if (r.vgeo.type() == mtpc_geoPoint) {
			result->sendData = std::make_unique<internal::SendGeo>(r.vgeo.c_geoPoint());
		} else {
			badAttachment = true;
		}
		if (r.has_reply_markup()) {
			result->_mtpKeyboard = std::make_unique<MTPReplyMarkup>(r.vreply_markup);
		}
	} break;

	case mtpc_botInlineMessageMediaVenue: {
		auto &r = message->c_botInlineMessageMediaVenue();
		if (r.vgeo.type() == mtpc_geoPoint) {
			result->sendData = std::make_unique<internal::SendVenue>(r.vgeo.c_geoPoint(), qs(r.vvenue_id), qs(r.vprovider), qs(r.vtitle), qs(r.vaddress));
		} else {
			badAttachment = true;
		}
		if (r.has_reply_markup()) {
			result->_mtpKeyboard = std::make_unique<MTPReplyMarkup>(r.vreply_markup);
		}
	} break;

	case mtpc_botInlineMessageMediaContact: {
		auto &r = message->c_botInlineMessageMediaContact();
		result->sendData = std::make_unique<internal::SendContact>(qs(r.vfirst_name), qs(r.vlast_name), qs(r.vphone_number));
		if (r.has_reply_markup()) {
			result->_mtpKeyboard = std::make_unique<MTPReplyMarkup>(r.vreply_markup);
		}
	} break;

	default: {
		badAttachment = true;
	} break;
	}

	if (badAttachment || !result->sendData || !result->sendData->isValid()) {
		return nullptr;
	}

	if (result->_thumb->isNull() && !result->_thumb_url.isEmpty()) {
		result->_thumb = ImagePtr(result->_thumb_url);
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
		auto coords = location.latAsString() + ',' + location.lonAsString();
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
		return false;
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
				DocumentOpenClickHandler::doOpen(_document, nullptr, ActionOnLoadNone);
			}
			return false;
		}
		return true;
	}
	return true;
}

void Result::forget() {
	_thumb->forget();
	if (_document) {
		_document->forget();
	}
	if (_photo) {
		_photo->forget();
	}
}

void Result::openFile() {
	if (_document) {
		DocumentOpenClickHandler(_document).onClick(Qt::LeftButton);
	} else if (_photo) {
		PhotoOpenClickHandler(_photo).onClick(Qt::LeftButton);
	}
}

void Result::cancelFile() {
	if (_document) {
		DocumentCancelClickHandler(_document).onClick(Qt::LeftButton);
	} else if (_photo) {
		PhotoCancelClickHandler(_photo).onClick(Qt::LeftButton);
	}
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

void Result::addToHistory(History *history, MTPDmessage::Flags flags, MsgId msgId, UserId fromId, MTPint mtpDate, UserId viaBotId, MsgId replyToId, const QString &postAuthor) const {
	flags |= MTPDmessage_ClientFlag::f_from_inline_bot;

	MTPReplyMarkup markup = MTPnullMarkup;
	if (_mtpKeyboard) {
		flags |= MTPDmessage::Flag::f_reply_markup;
		markup = *_mtpKeyboard;
	}
	sendData->addToHistory(this, history, flags, msgId, fromId, mtpDate, viaBotId, replyToId, postAuthor, markup);
}

QString Result::getErrorOnSend(History *history) const {
	return sendData->getErrorOnSend(this, history);
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

// just to make unique_ptr see the destructors.
Result::~Result() {
}

void Result::createPhoto() {
	if (_photo) return;

	if (_thumb_url.isEmpty()) {
		QSize thumbsize = shrinkToKeepAspect(_width, _height, 100, 100);
		_thumb = ImagePtr(thumbsize.width(), thumbsize.height());
	} else {
		_thumb = ImagePtr(_thumb_url, QSize(320, 320));
	}
//	ImagePtr medium = ImagePtr(_content_url, QSize(320, 320));
	QSize mediumsize = shrinkToKeepAspect(_width, _height, 320, 320);
	ImagePtr medium = ImagePtr(mediumsize.width(), mediumsize.height());

	ImagePtr full = ImagePtr(_content_url, _width, _height);
	auto photoId = rand_value<PhotoId>();
	_photo = App::photoSet(photoId, 0, 0, unixtime(), _thumb, medium, full);
	_photo->thumb = _thumb;
}

void Result::createDocument() {
	if (_document) return;

	if (!_thumb_url.isEmpty()) {
		_thumb = ImagePtr(_thumb_url, QSize(90, 90));
	}

	QString mime = _content_type;

	QVector<MTPDocumentAttribute> attributes;
	auto dimensions = QSize(_width, _height);
	if (_type == Type::Gif) {
		auto filename = (mime == qstr("video/mp4") ? "animation.gif.mp4" : "animation.gif");
		attributes.push_back(MTP_documentAttributeFilename(MTP_string(filename)));
		attributes.push_back(MTP_documentAttributeAnimated());
		auto flags = MTPDdocumentAttributeVideo::Flags(0);
		attributes.push_back(MTP_documentAttributeVideo(MTP_flags(flags), MTP_int(_duration), MTP_int(_width), MTP_int(_height)));
	} else if (_type == Type::Video) {
		auto flags = MTPDdocumentAttributeVideo::Flags(0);
		attributes.push_back(MTP_documentAttributeVideo(MTP_flags(flags), MTP_int(_duration), MTP_int(_width), MTP_int(_height)));
	} else if (_type == Type::Audio) {
		auto flags = MTPDdocumentAttributeAudio::Flags(0);
		if (mime == qstr("audio/ogg")) {
			flags |= MTPDdocumentAttributeAudio::Flag::f_voice;
		} else {
			QStringList p = mimeTypeForName(mime).globPatterns();
			QString pattern = p.isEmpty() ? QString() : p.front();
			QString extension = pattern.isEmpty() ? qsl(".unknown") : pattern.replace('*', QString());
			QString filename = filedialogDefaultName(qsl("inline"), extension, QString(), true);
			attributes.push_back(MTP_documentAttributeFilename(MTP_string(filename)));
		}
		attributes.push_back(MTP_documentAttributeAudio(MTP_flags(flags), MTP_int(_duration), MTPstring(), MTPstring(), MTPbytes()));
	}

	auto documentId = rand_value<DocumentId>();
	_document = App::documentSet(documentId, nullptr, 0, 0, unixtime(), attributes, mime, _thumb, MTP::maindc(), 0, StorageImageLocation());
	_document->setContentUrl(_content_url);
}

void Result::createGame() {
	if (_game) return;

	auto gameId = rand_value<GameId>();
	_game = App::gameSet(gameId, nullptr, 0, QString(), _title, _description, _photo, _document);
}

} // namespace InlineBots
