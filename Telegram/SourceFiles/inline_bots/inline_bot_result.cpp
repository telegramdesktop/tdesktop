/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "inline_bots/inline_bot_result.h"

#include "api/api_text_entities.h"
#include "data/data_photo.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_file_origin.h"
#include "data/data_photo_media.h"
#include "data/data_document_media.h"
#include "inline_bots/inline_bot_layout_item.h"
#include "inline_bots/inline_bot_send_data.h"
#include "storage/file_download.h"
#include "core/file_utilities.h"
#include "core/mime_type.h"
#include "ui/image/image.h"
#include "ui/image/image_location_factory.h"
#include "mainwidget.h"
#include "main/main_session.h"

namespace InlineBots {
namespace {

const auto kVideoThumbMime = "video/mp4"_q;

QString GetContentUrl(const MTPWebDocument &document) {
	switch (document.type()) {
	case mtpc_webDocument:
		return qs(document.c_webDocument().vurl());
	case mtpc_webDocumentNoProxy:
		return qs(document.c_webDocumentNoProxy().vurl());
	}
	Unexpected("Type in GetContentUrl.");
}

} // namespace

Result::Result(not_null<Main::Session*> session, const Creator &creator)
: _session(session)
, _queryId(creator.queryId)
, _type(creator.type) {
}

std::unique_ptr<Result> Result::Create(
		not_null<Main::Session*> session,
		uint64 queryId,
		const MTPBotInlineResult &mtpData) {
	using Type = Result::Type;

	const auto type = [&] {
		static const auto kStringToTypeMap = base::flat_map<QString, Type>{
			{ u"photo"_q, Type::Photo },
			{ u"video"_q, Type::Video },
			{ u"audio"_q, Type::Audio },
			{ u"voice"_q, Type::Audio },
			{ u"sticker"_q, Type::Sticker },
			{ u"file"_q, Type::File },
			{ u"gif"_q, Type::Gif },
			{ u"article"_q, Type::Article },
			{ u"contact"_q, Type::Contact },
			{ u"venue"_q, Type::Venue },
			{ u"geo"_q, Type::Geo },
			{ u"game"_q, Type::Game },
		};
		const auto type = mtpData.match([](const auto &data) {
			return qs(data.vtype());
		});
		const auto i = kStringToTypeMap.find(type);
		return (i != kStringToTypeMap.end()) ? i->second : Type::Unknown;
	}();
	if (type == Type::Unknown) {
		return nullptr;
	}

	auto result = std::make_unique<Result>(
		session,
		Creator{ queryId, type });
	const MTPBotInlineMessage *message = nullptr;
	switch (mtpData.type()) {
	case mtpc_botInlineResult: {
		const auto &r = mtpData.c_botInlineResult();
		result->_id = qs(r.vid());
		result->_title = qs(r.vtitle().value_or_empty());
		result->_description = qs(r.vdescription().value_or_empty());
		result->_url = qs(r.vurl().value_or_empty());
		const auto thumbMime = [&] {
			if (const auto thumb = r.vthumb()) {
				return thumb->match([&](const auto &data) {
					return data.vmime_type().v;
				});
			}
			return QByteArray();
		}();
		const auto contentMime = [&] {
			if (const auto content = r.vcontent()) {
				return content->match([&](const auto &data) {
					return data.vmime_type().v;
				});
			}
			return QByteArray();
		}();
		const auto imageThumb = !thumbMime.isEmpty()
			&& (thumbMime != kVideoThumbMime);
		const auto videoThumb = !thumbMime.isEmpty() && !imageThumb;
		if (const auto content = r.vcontent()) {
			result->_content_url = GetContentUrl(*content);
			if (result->_type == Type::Photo) {
				result->_photo = session->data().photoFromWeb(
					*content,
					(imageThumb
						? Images::FromWebDocument(*r.vthumb())
						: ImageLocation()));
			} else if (contentMime != "text/html"_q) {
				result->_document = session->data().documentFromWeb(
					result->adjustAttributes(*content),
					(imageThumb
						? Images::FromWebDocument(*r.vthumb())
						: ImageLocation()),
					(videoThumb
						? Images::FromWebDocument(*r.vthumb())
						: ImageLocation()));
			}
		}
		if (!result->_photo && !result->_document && imageThumb) {
			result->_thumbnail.update(result->_session, ImageWithLocation{
				.location = Images::FromWebDocument(*r.vthumb())
			});
		}
		message = &r.vsend_message();
	} break;
	case mtpc_botInlineMediaResult: {
		const auto &r = mtpData.c_botInlineMediaResult();
		result->_id = qs(r.vid());
		result->_title = qs(r.vtitle().value_or_empty());
		result->_description = qs(r.vdescription().value_or_empty());
		if (const auto photo = r.vphoto()) {
			result->_photo = session->data().processPhoto(*photo);
		}
		if (const auto document = r.vdocument()) {
			result->_document = session->data().processDocument(*document);
		}
		message = &r.vsend_message();
	} break;
	}
	auto badAttachment = (result->_photo && result->_photo->isNull())
		|| (result->_document && result->_document->isNull());

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
		|| result->_type == Type::Sticker
		|| result->_type == Type::Gif) {
		if (!result->_document) {
			return nullptr;
		}
	}

	switch (message->type()) {
	case mtpc_botInlineMessageMediaAuto: {
		const auto &r = message->c_botInlineMessageMediaAuto();
		const auto message = qs(r.vmessage());
		const auto entities = Api::EntitiesFromMTP(
			session,
			r.ventities().value_or_empty());
		if (result->_type == Type::Photo) {
			if (!result->_photo) {
				return nullptr;
			}
			result->sendData = std::make_unique<internal::SendPhoto>(
				session,
				result->_photo,
				message,
				entities);
		} else if (result->_type == Type::Game) {
			result->createGame(session);
			result->sendData = std::make_unique<internal::SendGame>(
				session,
				result->_game);
		} else {
			if (!result->_document) {
				return nullptr;
			}
			result->sendData = std::make_unique<internal::SendFile>(
				session,
				result->_document,
				message,
				entities);
		}
		if (const auto markup = r.vreply_markup()) {
			result->_mtpKeyboard = std::make_unique<MTPReplyMarkup>(*markup);
		}
	} break;

	case mtpc_botInlineMessageText: {
		const auto &r = message->c_botInlineMessageText();
		result->sendData = std::make_unique<internal::SendText>(
			session,
			qs(r.vmessage()),
			Api::EntitiesFromMTP(session, r.ventities().value_or_empty()),
			r.is_no_webpage());
		if (const auto markup = r.vreply_markup()) {
			result->_mtpKeyboard = std::make_unique<MTPReplyMarkup>(*markup);
		}
	} break;

	case mtpc_botInlineMessageMediaGeo: {
		// #TODO layer 72 save period and send live location?..
		auto &r = message->c_botInlineMessageMediaGeo();
		if (r.vgeo().type() == mtpc_geoPoint) {
			result->sendData = std::make_unique<internal::SendGeo>(
				session,
				r.vgeo().c_geoPoint());
		} else {
			badAttachment = true;
		}
		if (const auto markup = r.vreply_markup()) {
			result->_mtpKeyboard = std::make_unique<MTPReplyMarkup>(*markup);
		}
	} break;

	case mtpc_botInlineMessageMediaVenue: {
		auto &r = message->c_botInlineMessageMediaVenue();
		if (r.vgeo().type() == mtpc_geoPoint) {
			result->sendData = std::make_unique<internal::SendVenue>(
				session,
				r.vgeo().c_geoPoint(),
				qs(r.vvenue_id()),
				qs(r.vprovider()),
				qs(r.vtitle()),
				qs(r.vaddress()));
		} else {
			badAttachment = true;
		}
		if (const auto markup = r.vreply_markup()) {
			result->_mtpKeyboard = std::make_unique<MTPReplyMarkup>(*markup);
		}
	} break;

	case mtpc_botInlineMessageMediaContact: {
		auto &r = message->c_botInlineMessageMediaContact();
		result->sendData = std::make_unique<internal::SendContact>(
			session,
			qs(r.vfirst_name()),
			qs(r.vlast_name()),
			qs(r.vphone_number()));
		if (const auto markup = r.vreply_markup()) {
			result->_mtpKeyboard = std::make_unique<MTPReplyMarkup>(*markup);
		}
	} break;

	default: {
		badAttachment = true;
	} break;
	}

	if (badAttachment || !result->sendData || !result->sendData->isValid()) {
		return nullptr;
	}

	if (const auto point = result->getLocationPoint()) {
		const auto scale = 1 + (cScale() * cIntRetinaFactor()) / 200;
		const auto zoom = 15 + (scale - 1);
		const auto w = st::inlineThumbSize / scale;
		const auto h = st::inlineThumbSize / scale;

		auto location = GeoPointLocation();
		location.lat = point->lat();
		location.lon = point->lon();
		location.access = point->accessHash();
		location.width = w;
		location.height = h;
		location.zoom = zoom;
		location.scale = scale;
		result->_locationThumbnail.update(result->_session, ImageWithLocation{
			.location = ImageLocation({ location }, w, h)
		});
	}

	return result;
}

bool Result::onChoose(Layout::ItemBase *layout) {
	if (_photo && _type == Type::Photo) {
		const auto media = _photo->activeMediaView();
		if (!media || media->image(Data::PhotoSize::Thumbnail)) {
			return true;
		} else if (!_photo->loading(Data::PhotoSize::Thumbnail)) {
			_photo->load(
				Data::PhotoSize::Thumbnail,
				Data::FileOrigin());
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
			const auto media = _document->activeMediaView();
			const auto preview = Data::VideoPreviewState(media.get());
			if (!media || preview.loaded()) {
				return true;
			} else if (!preview.usingThumbnail()) {
				if (preview.loading()) {
					_document->cancel();
				} else {
					DocumentSaveClickHandler::Save(
						Data::FileOriginSavedGifs(),
						_document);
				}
			}
			return false;
		}
		return true;
	}
	return true;
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
	if (!_thumbnail.empty()
		|| _photo
		|| (_document && _document->hasThumbnail())) {
		return true;
	} else if (_type == Type::Contact) {
		return true;
	} else if (sendData->hasLocationCoords()) {
		return true;
	}
	return false;
};

void Result::addToHistory(
		History *history,
		MTPDmessage::Flags flags,
		MTPDmessage_ClientFlags clientFlags,
		MsgId msgId,
		PeerId fromId,
		MTPint mtpDate,
		UserId viaBotId,
		MsgId replyToId,
		const QString &postAuthor) const {
	clientFlags |= MTPDmessage_ClientFlag::f_from_inline_bot;

	auto markup = MTPReplyMarkup();
	if (_mtpKeyboard) {
		flags |= MTPDmessage::Flag::f_reply_markup;
		markup = *_mtpKeyboard;
	}
	sendData->addToHistory(
		this,
		history,
		flags,
		clientFlags,
		msgId,
		fromId,
		mtpDate,
		viaBotId,
		replyToId,
		postAuthor,
		markup);
}

QString Result::getErrorOnSend(History *history) const {
	return sendData->getErrorOnSend(this, history);
}

std::optional<Data::LocationPoint> Result::getLocationPoint() const {
	return sendData->getLocationPoint();
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

void Result::createGame(not_null<Main::Session*> session) {
	if (_game) {
		return;
	}

	const auto gameId = rand_value<GameId>();
	_game = session->data().game(
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
			data.vurl(),
			data.vaccess_hash(),
			data.vsize(),
			data.vmime_type(),
			adjustAttributes(data.vattributes(), data.vmime_type()));
	} break;

	case mtpc_webDocumentNoProxy: {
		const auto &data = document.c_webDocumentNoProxy();
		return MTP_webDocumentNoProxy(
			data.vurl(),
			data.vsize(),
			data.vmime_type(),
			adjustAttributes(data.vattributes(), data.vmime_type()));
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
				if (!(fields.vflags().v & Flag::f_voice)) {
					*audio = MTP_documentAttributeAudio(
						MTP_flags(fields.vflags().v | Flag::f_voice),
						fields.vduration(),
						MTP_bytes(fields.vtitle().value_or_empty()),
						MTP_bytes(fields.vperformer().value_or_empty()),
						MTP_bytes(fields.vwaveform().value_or_empty()));
				}
			}

			const auto &fields = audio->c_documentAttributeAudio();
			if (!exists(mtpc_documentAttributeFilename)
				&& !(fields.vflags().v & Flag::f_voice)) {
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
