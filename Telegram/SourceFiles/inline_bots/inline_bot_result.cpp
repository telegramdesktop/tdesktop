/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "inline_bots/inline_bot_result.h"

#include "data/data_photo.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "inline_bots/inline_bot_layout_item.h"
#include "inline_bots/inline_bot_send_data.h"
#include "storage/file_download.h"
#include "core/file_utilities.h"
#include "core/mime_type.h"
#include "mainwidget.h"
#include "auth_session.h"

namespace InlineBots {
namespace {

QString GetContentUrl(const MTPWebDocument &document) {
	switch (document.type()) {
	case mtpc_webDocument:
		return qs(document.c_webDocument().vurl);
	case mtpc_webDocumentNoProxy:
		return qs(document.c_webDocumentNoProxy().vurl);
	}
	Unexpected("Type in GetContentUrl.");
}

} // namespace

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
		const auto &r = mtpData.c_botInlineResult();
		result->_id = qs(r.vid);
		if (r.has_title()) result->_title = qs(r.vtitle);
		if (r.has_description()) result->_description = qs(r.vdescription);
		if (r.has_url()) result->_url = qs(r.vurl);
		if (r.has_thumb()) {
			result->_thumb = ImagePtr(r.vthumb, result->thumbBox());
		}
		if (r.has_content()) {
			result->_content_url = GetContentUrl(r.vcontent);
			if (result->_type == Type::Photo) {
				result->_photo = Auth().data().photoFromWeb(
					r.vcontent,
					result->_thumb);
			} else {
				result->_document = Auth().data().documentFromWeb(
					result->adjustAttributes(r.vcontent),
					result->_thumb);
			}
		}
		message = &r.vsend_message;
	} break;
	case mtpc_botInlineMediaResult: {
		const auto &r = mtpData.c_botInlineMediaResult();
		result->_id = qs(r.vid);
		if (r.has_title()) result->_title = qs(r.vtitle);
		if (r.has_description()) result->_description = qs(r.vdescription);
		if (r.has_photo()) {
			result->_photo = Auth().data().photo(r.vphoto);
		}
		if (r.has_document()) {
			result->_document = Auth().data().document(r.vdocument);
		}
		message = &r.vsend_message;
	} break;
	}
	auto badAttachment = (result->_photo && result->_photo->full->isNull())
		|| (result->_document && !result->_document->isValid());

	if (!message) {
		return nullptr;
	}

	// Ensure required media fields for layouts.
	if (result->_type == Type::Photo) {
		if (!result->_photo) {
			return nullptr;
		}
	} else if (result->_type == Type::Audio
		|| result->_type == Type::File
		|| result->_type == Type::Video
		|| result->_type == Type::Sticker
		|| result->_type == Type::Gif) {
		if (!result->_document) {
			return nullptr;
		}
	}

	switch (message->type()) {
	case mtpc_botInlineMessageMediaAuto: {
		auto &r = message->c_botInlineMessageMediaAuto();
		auto entities = r.has_entities()
			? TextUtilities::EntitiesFromMTP(r.ventities.v)
			: EntitiesInText();
		if (result->_type == Type::Photo) {
			if (!result->_photo) {
				return nullptr;
			}
			result->sendData = std::make_unique<internal::SendPhoto>(result->_photo, qs(r.vmessage), entities);
		} else if (result->_type == Type::Game) {
			result->createGame();
			result->sendData = std::make_unique<internal::SendGame>(result->_game);
		} else {
			if (!result->_document) {
				return nullptr;
			}
			result->sendData = std::make_unique<internal::SendFile>(result->_document, qs(r.vmessage), entities);
		}
		if (r.has_reply_markup()) {
			result->_mtpKeyboard = std::make_unique<MTPReplyMarkup>(r.vreply_markup);
		}
	} break;

	case mtpc_botInlineMessageText: {
		auto &r = message->c_botInlineMessageText();
		auto entities = r.has_entities()
			? TextUtilities::EntitiesFromMTP(r.ventities.v)
			: EntitiesInText();
		result->sendData = std::make_unique<internal::SendText>(qs(r.vmessage), entities, r.is_no_webpage());
		if (result->_type == Type::Photo) {
			if (!result->_photo) {
				return nullptr;
			}
		} else if (result->_type == Type::Audio
			|| result->_type == Type::File
			|| result->_type == Type::Video
			|| result->_type == Type::Sticker
			|| result->_type == Type::Gif) {
			if (!result->_document) {
				return nullptr;
			}
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

	LocationCoords coords;
	if (result->getLocationCoords(&coords)) {
		const auto scale = 1 + (cScale() * cIntRetinaFactor()) / 200;
		const auto zoom = 15 + (scale - 1);
		const auto w = st::inlineThumbSize / scale;
		const auto h = st::inlineThumbSize / scale;

		auto location = GeoPointLocation();
		location.lat = coords.lat();
		location.lon = coords.lon();
		location.access = coords.accessHash();
		location.width = w;
		location.height = h;
		location.zoom = zoom;
		location.scale = scale;
		result->_locationThumb = ImagePtr(location);
	}

	return result;
}

bool Result::onChoose(Layout::ItemBase *layout) {
	if (_photo && _type == Type::Photo) {
		if (_photo->medium->loaded() || _photo->thumb->loaded()) {
			return true;
		} else if (!_photo->medium->loading()) {
			_photo->thumb->loadEvenCancelled(Data::FileOrigin());
			_photo->medium->loadEvenCancelled(Data::FileOrigin());
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
				DocumentOpenClickHandler::Open(
					Data::FileOriginSavedGifs(),
					_document,
					nullptr,
					ActionOnLoadNone);
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
		DocumentOpenClickHandler(_document).onClick({});
	} else if (_photo) {
		PhotoOpenClickHandler(_photo).onClick({});
	}
}

void Result::cancelFile() {
	if (_document) {
		DocumentCancelClickHandler(_document).onClick({});
	} else if (_photo) {
		PhotoCancelClickHandler(_photo).onClick({});
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

void Result::createGame() {
	if (_game) return;

	const auto gameId = rand_value<GameId>();
	_game = Auth().data().game(
		gameId,
		0,
		QString(),
		_title,
		_description,
		_photo,
		_document);
}

QSize Result::thumbBox() const {
	return (_type == Type::Photo) ? QSize(100, 100) : QSize(90, 90);
}

MTPWebDocument Result::adjustAttributes(const MTPWebDocument &document) {
	switch (document.type()) {
	case mtpc_webDocument: {
		const auto &data = document.c_webDocument();
		return MTP_webDocument(
			data.vurl,
			data.vaccess_hash,
			data.vsize,
			data.vmime_type,
			adjustAttributes(data.vattributes, data.vmime_type));
	} break;

	case mtpc_webDocumentNoProxy: {
		const auto &data = document.c_webDocumentNoProxy();
		return MTP_webDocumentNoProxy(
			data.vurl,
			data.vsize,
			data.vmime_type,
			adjustAttributes(data.vattributes, data.vmime_type));
	} break;
	}
	Unexpected("Type in InlineBots::Result::adjustAttributes.");
}

MTPVector<MTPDocumentAttribute> Result::adjustAttributes(
		const MTPVector<MTPDocumentAttribute> &existing,
		const MTPstring &mimeType) {
	auto result = existing.v;
	const auto find = [&](mtpTypeId attributeType) {
		return ranges::find(
			result,
			attributeType,
			[](const MTPDocumentAttribute &value) { return value.type(); });
	};
	const auto exists = [&](mtpTypeId attributeType) {
		return find(attributeType) != result.cend();
	};
	const auto mime = qs(mimeType);
	if (_type == Type::Gif) {
		if (!exists(mtpc_documentAttributeFilename)) {
			auto filename = (mime == qstr("video/mp4")
				? "animation.gif.mp4"
				: "animation.gif");
			result.push_back(MTP_documentAttributeFilename(
				MTP_string(filename)));
		}
		if (!exists(mtpc_documentAttributeAnimated)) {
			result.push_back(MTP_documentAttributeAnimated());
		}
	} else if (_type == Type::Audio) {
		const auto audio = find(mtpc_documentAttributeAudio);
		if (audio != result.cend()) {
			using Flag = MTPDdocumentAttributeAudio::Flag;
			if (mime == qstr("audio/ogg")) {
				// We always treat audio/ogg as a voice message.
				// It was that way before we started to get attributes here.
				const auto &fields = audio->c_documentAttributeAudio();
				if (!(fields.vflags.v & Flag::f_voice)) {
					*audio = MTP_documentAttributeAudio(
						MTP_flags(fields.vflags.v | Flag::f_voice),
						fields.vduration,
						fields.vtitle,
						fields.vperformer,
						fields.vwaveform);
				}
			}

			const auto &fields = audio->c_documentAttributeAudio();
			if (!exists(mtpc_documentAttributeFilename)
				&& !(fields.vflags.v & Flag::f_voice)) {
				const auto p = Core::MimeTypeForName(mime).globPatterns();
				auto pattern = p.isEmpty() ? QString() : p.front();
				const auto extension = pattern.isEmpty()
					? qsl(".unknown")
					: pattern.replace('*', QString());
				const auto filename = filedialogDefaultName(
					qsl("inline"),
					extension,
					QString(),
					true);
				result.push_back(
					MTP_documentAttributeFilename(MTP_string(filename)));
			}
		}
	}
	return MTP_vector<MTPDocumentAttribute>(std::move(result));
}

} // namespace InlineBots
