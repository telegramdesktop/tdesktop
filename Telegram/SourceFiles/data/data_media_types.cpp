/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_media_types.h"

#include "history/history_item.h"
#include "history/history_location_manager.h"
#include "history/view/history_view_element.h"
#include "history/media/history_media_photo.h"
#include "history/media/history_media_sticker.h"
#include "history/media/history_media_gif.h"
#include "history/media/history_media_video.h"
#include "history/media/history_media_document.h"
#include "history/media/history_media_contact.h"
#include "history/media/history_media_location.h"
#include "history/media/history_media_game.h"
#include "history/media/history_media_invoice.h"
#include "history/media/history_media_call.h"
#include "history/media/history_media_web_page.h"
#include "history/media/history_media_poll.h"
#include "ui/image/image.h"
#include "ui/image/image_source.h"
#include "ui/text_options.h"
#include "ui/emoji_config.h"
#include "storage/storage_shared_media.h"
#include "storage/localstorage.h"
#include "data/data_session.h"
#include "data/data_photo.h"
#include "data/data_document.h"
#include "data/data_game.h"
#include "data/data_web_page.h"
#include "data/data_poll.h"
#include "lang/lang_keys.h"
#include "auth_session.h"
#include "layout.h"

namespace Data {
namespace {

Call ComputeCallData(const MTPDmessageActionPhoneCall &call) {
	auto result = Call();
	result.finishReason = [&] {
		if (call.has_reason()) {
			switch (call.vreason.type()) {
			case mtpc_phoneCallDiscardReasonBusy:
				return CallFinishReason::Busy;
			case mtpc_phoneCallDiscardReasonDisconnect:
				return CallFinishReason::Disconnected;
			case mtpc_phoneCallDiscardReasonHangup:
				return CallFinishReason::Hangup;
			case mtpc_phoneCallDiscardReasonMissed:
				return CallFinishReason::Missed;
			}
			Unexpected("Call reason type.");
		}
		return CallFinishReason::Hangup;
	}();
	result.duration = call.has_duration() ? call.vduration.v : 0;;
	return result;
}

Invoice ComputeInvoiceData(const MTPDmessageMediaInvoice &data) {
	auto result = Invoice();
	result.isTest = data.is_test();
	result.amount = data.vtotal_amount.v;
	result.currency = qs(data.vcurrency);
	result.description = qs(data.vdescription);
	result.title = TextUtilities::SingleLine(qs(data.vtitle));
	if (data.has_receipt_msg_id()) {
		result.receiptMsgId = data.vreceipt_msg_id.v;
	}
	if (data.has_photo()) {
		result.photo = Auth().data().photoFromWeb(data.vphoto);
	}
	return result;
}

QString WithCaptionDialogsText(
		const QString &attachType,
		const QString &caption) {
	if (caption.isEmpty()) {
		return textcmdLink(1, TextUtilities::Clean(attachType));
	}

	auto captionText = TextUtilities::Clean(caption);
	auto attachTypeWrapped = textcmdLink(1, lng_dialogs_text_media_wrapped(
		lt_media,
		TextUtilities::Clean(attachType)));
	return lng_dialogs_text_media(
		lt_media_part,
		attachTypeWrapped,
		lt_caption,
		captionText);
}

QString WithCaptionNotificationText(
		const QString &attachType,
		const QString &caption) {
	if (caption.isEmpty()) {
		return attachType;
	}

	auto attachTypeWrapped = lng_dialogs_text_media_wrapped(
		lt_media,
		attachType);
	return lng_dialogs_text_media(
		lt_media_part,
		attachTypeWrapped,
		lt_caption,
		caption);
}

} // namespace

TextWithEntities WithCaptionClipboardText(
		const QString &attachType,
		TextWithEntities &&caption) {
	TextWithEntities result;
	result.text.reserve(5 + attachType.size() + caption.text.size());
	result.text.append(qstr("[ ")).append(attachType).append(qstr(" ]"));
	if (!caption.text.isEmpty()) {
		result.text.append(qstr("\n"));
		TextUtilities::Append(result, std::move(caption));
	}
	return result;
}

Media::Media(not_null<HistoryItem*> parent) : _parent(parent) {
}

not_null<HistoryItem*> Media::parent() const {
	return _parent;
}

DocumentData *Media::document() const {
	return nullptr;
}

PhotoData *Media::photo() const {
	return nullptr;
}

WebPageData *Media::webpage() const {
	return nullptr;
}

const SharedContact *Media::sharedContact() const {
	return nullptr;
}

const Call *Media::call() const {
	return nullptr;
}

GameData *Media::game() const {
	return nullptr;
}

const Invoice *Media::invoice() const {
	return nullptr;
}

LocationData *Media::location() const {
	return nullptr;
}

PollData *Media::poll() const {
	return nullptr;
}

bool Media::uploading() const {
	return false;
}

Storage::SharedMediaTypesMask Media::sharedMediaTypes() const {
	return {};
}

bool Media::canBeGrouped() const {
	return false;
}

QString Media::chatsListText() const {
	auto result = notificationText();
	return result.isEmpty()
		? QString()
		: textcmdLink(1, TextUtilities::Clean(std::move(result)));
}

bool Media::hasReplyPreview() const {
	return false;
}

Image *Media::replyPreview() const {
	return nullptr;
}

bool Media::allowsForward() const {
	return true;
}

bool Media::allowsEdit() const {
	return allowsEditCaption();
}

bool Media::allowsEditCaption() const {
	return false;
}

bool Media::allowsRevoke() const {
	return true;
}

bool Media::forwardedBecomesUnread() const {
	return false;
}

QString Media::errorTextForForward(not_null<ChannelData*> channel) const {
	return QString();
}

bool Media::consumeMessageText(const TextWithEntities &text) {
	return false;
}

TextWithEntities Media::consumedMessageText() const {
	return {};
}

std::unique_ptr<HistoryMedia> Media::createView(
		not_null<HistoryView::Element*> message) {
	return createView(message, message->data());
}

MediaPhoto::MediaPhoto(
	not_null<HistoryItem*> parent,
	not_null<PhotoData*> photo)
: Media(parent)
, _photo(photo) {
	Auth().data().registerPhotoItem(_photo, parent);
}

MediaPhoto::MediaPhoto(
	not_null<HistoryItem*> parent,
	not_null<PeerData*> chat,
	not_null<PhotoData*> photo)
: Media(parent)
, _photo(photo)
, _chat(chat) {
	Auth().data().registerPhotoItem(_photo, parent);
}

MediaPhoto::~MediaPhoto() {
	Auth().data().unregisterPhotoItem(_photo, parent());
}

std::unique_ptr<Media> MediaPhoto::clone(not_null<HistoryItem*> parent) {
	return _chat
		? std::make_unique<MediaPhoto>(parent, _chat, _photo)
		: std::make_unique<MediaPhoto>(parent, _photo);
}

PhotoData *MediaPhoto::photo() const {
	return _photo;
}

bool MediaPhoto::uploading() const {
	return _photo->uploading();
}

Storage::SharedMediaTypesMask MediaPhoto::sharedMediaTypes() const {
	using Type = Storage::SharedMediaType;
	if (_chat) {
		return Type::ChatPhoto;
	}
	return Storage::SharedMediaTypesMask{}
		.added(Type::Photo)
		.added(Type::PhotoVideo);
}

bool MediaPhoto::canBeGrouped() const {
	return true;
}

bool MediaPhoto::hasReplyPreview() const {
	return !_photo->thumb->isNull();
}

Image *MediaPhoto::replyPreview() const {
	return _photo->getReplyPreview(parent()->fullId());
}

QString MediaPhoto::notificationText() const {
	return WithCaptionNotificationText(
		lang(lng_in_dlg_photo),
		parent()->originalText().text);
	//return WithCaptionNotificationText(lang(lng_in_dlg_album), _caption);
}

QString MediaPhoto::chatsListText() const {
	return WithCaptionDialogsText(
		lang(lng_in_dlg_photo),
		parent()->originalText().text);
	//return WithCaptionDialogsText(lang(lng_in_dlg_album), _caption);
}

QString MediaPhoto::pinnedTextSubstring() const {
	return lang(lng_action_pinned_media_photo);
}

TextWithEntities MediaPhoto::clipboardText() const {
	return WithCaptionClipboardText(
		lang(lng_in_dlg_photo),
		parent()->clipboardText());
}

bool MediaPhoto::allowsEditCaption() const {
	return true;
}

QString MediaPhoto::errorTextForForward(
		not_null<ChannelData*> channel) const {
	if (channel->restricted(ChannelRestriction::f_send_media)) {
		return lang(lng_restricted_send_media);
	}
	return QString();
}

bool MediaPhoto::updateInlineResultMedia(const MTPMessageMedia &media) {
	if (media.type() != mtpc_messageMediaPhoto) {
		return false;
	}
	auto &data = media.c_messageMediaPhoto();
	if (data.has_photo() && !data.has_ttl_seconds()) {
		const auto photo = Auth().data().photo(data.vphoto);
		if (photo == _photo) {
			return true;
		} else {
			photo->collectLocalData(_photo);
		}
	} else {
		LOG(("API Error: "
			"Got MTPMessageMediaPhoto without photo "
			"or with ttl_seconds in updateInlineResultMedia()"));
	}
	return false;
}

bool MediaPhoto::updateSentMedia(const MTPMessageMedia &media) {
	if (media.type() != mtpc_messageMediaPhoto) {
		return false;
	}
	auto &mediaPhoto = media.c_messageMediaPhoto();
	if (!mediaPhoto.has_photo() || mediaPhoto.has_ttl_seconds()) {
		LOG(("Api Error: "
			"Got MTPMessageMediaPhoto without photo "
			"or with ttl_seconds in updateSentMedia()"));
		return false;
	}
	const auto &photo = mediaPhoto.vphoto;
	Auth().data().photoConvert(_photo, photo);

	if (photo.type() != mtpc_photo) {
		return false;
	}
	struct SizeData {
		char letter = 0;
		int width = 0;
		int height = 0;
		const MTPFileLocation *location = nullptr;
		QByteArray bytes;
	};
	const auto saveImageToCache = [](
			const ImagePtr &image,
			SizeData size) {
		Expects(size.location != nullptr);

		const auto key = StorageImageLocation(
			size.width,
			size.height,
			size.location->c_fileLocation());
		if (key.isNull() || image->isNull() || !image->loaded()) {
			return;
		}
		if (size.bytes.isEmpty()) {
			size.bytes = image->bytesForCache();
		}
		const auto length = size.bytes.size();
		if (!length || length > Storage::kMaxFileInMemory) {
			LOG(("App Error: Bad photo data for saving to cache."));
			return;
		}
		Auth().data().cache().putIfEmpty(
			Data::StorageCacheKey(key),
			Storage::Cache::Database::TaggedValue(
				std::move(size.bytes),
				Data::kImageCacheTag));
		image->replaceSource(
			std::make_unique<Images::StorageSource>(key, length));
	};
	auto &sizes = photo.c_photo().vsizes.v;
	auto max = 0;
	auto maxSize = SizeData();
	for (const auto &data : sizes) {
		const auto size = data.match([](const MTPDphotoSize &data) {
			return SizeData{
				data.vtype.v.isEmpty() ? char(0) : data.vtype.v[0],
				data.vw.v,
				data.vh.v,
				&data.vlocation,
				QByteArray()
			};
		}, [](const MTPDphotoCachedSize &data) {
			return SizeData{
				data.vtype.v.isEmpty() ? char(0) : data.vtype.v[0],
				data.vw.v,
				data.vh.v,
				&data.vlocation,
				qba(data.vbytes)
			};
		}, [](const MTPDphotoSizeEmpty &) {
			return SizeData();
		});
		if (!size.location || size.location->type() != mtpc_fileLocation) {
			continue;
		}
		if (size.letter == 's') {
			saveImageToCache(_photo->thumb, size);
		} else if (size.letter == 'm') {
			saveImageToCache(_photo->medium, size);
		} else if (size.letter == 'x' && max < 1) {
			max = 1;
			maxSize = size;
		} else if (size.letter == 'y' && max < 2) {
			max = 2;
			maxSize = size;
		//} else if (size.letter == 'w' && max < 3) {
		//	max = 3;
		//	maxSize = size;
		}
	}
	if (maxSize.location) {
		saveImageToCache(_photo->full, maxSize);
	}
	return true;
}

std::unique_ptr<HistoryMedia> MediaPhoto::createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent) {
	if (_chat) {
		return std::make_unique<HistoryPhoto>(
			message,
			_chat,
			_photo,
			st::msgServicePhotoWidth);
	}
	return std::make_unique<HistoryPhoto>(
		message,
		realParent,
		_photo);
}

MediaFile::MediaFile(
	not_null<HistoryItem*> parent,
	not_null<DocumentData*> document)
: Media(parent)
, _document(document)
, _emoji(document->sticker() ? document->sticker()->alt : QString()) {
	Auth().data().registerDocumentItem(_document, parent);

	if (!_emoji.isEmpty()) {
		if (const auto emoji = Ui::Emoji::Find(_emoji)) {
			_emoji = emoji->text();
		}
	}
}

MediaFile::~MediaFile() {
	Auth().data().unregisterDocumentItem(_document, parent());
}

std::unique_ptr<Media> MediaFile::clone(not_null<HistoryItem*> parent) {
	return std::make_unique<MediaFile>(parent, _document);
}

DocumentData *MediaFile::document() const {
	return _document;
}

bool MediaFile::uploading() const {
	return _document->uploading();
}

Storage::SharedMediaTypesMask MediaFile::sharedMediaTypes() const {
	using Type = Storage::SharedMediaType;
	if (_document->sticker()) {
		return {};
	} else if (_document->isVideoMessage()) {
		return Storage::SharedMediaTypesMask{}
			.added(Type::RoundFile)
			.added(Type::RoundVoiceFile);
	} else if (_document->isGifv()) {
		return Type::GIF;
	} else if (_document->isVideoFile()) {
		return Storage::SharedMediaTypesMask{}
			.added(Type::Video)
			.added(Type::PhotoVideo);
	} else if (_document->isVoiceMessage()) {
		return Storage::SharedMediaTypesMask{}
			.added(Type::VoiceFile)
			.added(Type::RoundVoiceFile);
	} else if (_document->isSharedMediaMusic()) {
		return Type::MusicFile;
	}
	return Type::File;
}

bool MediaFile::canBeGrouped() const {
	return _document->isVideoFile();
}

bool MediaFile::hasReplyPreview() const {
	return !_document->thumb->isNull();
}

Image *MediaFile::replyPreview() const {
	return _document->getReplyPreview(parent()->fullId());
}

QString MediaFile::chatsListText() const {
	if (const auto sticker = _document->sticker()) {
		return Media::chatsListText();
	}
	const auto type = [&] {
		if (_document->isVideoMessage()) {
			return lang(lng_in_dlg_video_message);
		} else if (_document->isAnimation()) {
			return qsl("GIF");
		} else if (_document->isVideoFile()) {
			return lang(lng_in_dlg_video);
		} else if (_document->isVoiceMessage()) {
			return lang(lng_in_dlg_audio);
		} else if (const auto name = _document->composeNameString();
				!name.isEmpty()) {
			return name;
		} else if (_document->isAudioFile()) {
			return lang(lng_in_dlg_audio_file);
		}
		return lang(lng_in_dlg_file);
	}();
	return WithCaptionDialogsText(type, parent()->originalText().text);
}

QString MediaFile::notificationText() const {
	if (const auto sticker = _document->sticker()) {
		return _emoji.isEmpty()
			? lang(lng_in_dlg_sticker)
			: lng_in_dlg_sticker_emoji(lt_emoji, _emoji);
	}
	const auto type = [&] {
		if (_document->isVideoMessage()) {
			return lang(lng_in_dlg_video_message);
		} else if (_document->isAnimation()) {
			return qsl("GIF");
		} else if (_document->isVideoFile()) {
			return lang(lng_in_dlg_video);
		} else if (_document->isVoiceMessage()) {
			return lang(lng_in_dlg_audio);
		} else if (!_document->filename().isEmpty()) {
			return _document->filename();
		} else if (_document->isAudioFile()) {
			return lang(lng_in_dlg_audio_file);
		}
		return lang(lng_in_dlg_file);
	}();
	return WithCaptionNotificationText(type, parent()->originalText().text);
}

QString MediaFile::pinnedTextSubstring() const {
	if (const auto sticker = _document->sticker()) {
		if (!_emoji.isEmpty()) {
			return lng_action_pinned_media_emoji_sticker(lt_emoji, _emoji);
		}
		return lang(lng_action_pinned_media_sticker);
	} else if (_document->isAnimation()) {
		if (_document->isVideoMessage()) {
			return lang(lng_action_pinned_media_video_message);
		}
		return lang(lng_action_pinned_media_gif);
	} else if (_document->isVideoFile()) {
		return lang(lng_action_pinned_media_video);
	} else if (_document->isVoiceMessage()) {
		return lang(lng_action_pinned_media_voice);
	} else if (_document->isSong()) {
		return lang(lng_action_pinned_media_audio);
	}
	return lang(lng_action_pinned_media_file);
}

TextWithEntities MediaFile::clipboardText() const {
	const auto attachType = [&] {
		const auto name = _document->composeNameString();
		const auto addName = !name.isEmpty()
			? qstr(" : ") + name
			: QString();
		if (const auto sticker = _document->sticker()) {
			if (!_emoji.isEmpty()) {
				return lng_in_dlg_sticker_emoji(lt_emoji, _emoji);
			}
			return lang(lng_in_dlg_sticker);
		} else if (_document->isAnimation()) {
			if (_document->isVideoMessage()) {
				return lang(lng_in_dlg_video_message);
			}
			return qsl("GIF");
		} else if (_document->isVideoFile()) {
			return lang(lng_in_dlg_video);
		} else if (_document->isVoiceMessage()) {
			return lang(lng_in_dlg_audio) + addName;
		} else if (_document->isSong()) {
			return lang(lng_in_dlg_audio_file) + addName;
		}
		return lang(lng_in_dlg_file) + addName;
	}();
	return WithCaptionClipboardText(
		attachType,
		parent()->clipboardText());
}

bool MediaFile::allowsEditCaption() const {
	return !_document->isVideoMessage() && !_document->sticker();
}

bool MediaFile::forwardedBecomesUnread() const {
	return _document->isVoiceMessage()
		//|| _document->isVideoFile()
		|| _document->isVideoMessage();
}

QString MediaFile::errorTextForForward(
		not_null<ChannelData*> channel) const {
	if (const auto sticker = _document->sticker()) {
		if (channel->restricted(ChannelRestriction::f_send_stickers)) {
			return lang(lng_restricted_send_stickers);
		}
	} else if (_document->isAnimation()) {
		if (_document->isVideoMessage()) {
			if (channel->restricted(ChannelRestriction::f_send_media)) {
				return lang(lng_restricted_send_media);
			}
		} else {
			if (channel->restricted(ChannelRestriction::f_send_gifs)) {
				return lang(lng_restricted_send_gifs);
			}
		}
	} else if (channel->restricted(ChannelRestriction::f_send_media)) {
		return lang(lng_restricted_send_media);
	}
	return QString();
}

bool MediaFile::updateInlineResultMedia(const MTPMessageMedia &media) {
	if (media.type() != mtpc_messageMediaDocument) {
		return false;
	}
	auto &data = media.c_messageMediaDocument();
	if (data.has_document() && !data.has_ttl_seconds()) {
		const auto document = Auth().data().document(data.vdocument);
		if (document == _document) {
			return false;
		} else {
			document->collectLocalData(_document);
		}
	} else {
		LOG(("API Error: "
			"Got MTPMessageMediaDocument without document "
			"or with ttl_seconds in updateInlineResultMedia()"));
	}
	return false;
}

bool MediaFile::updateSentMedia(const MTPMessageMedia &media) {
	if (media.type() != mtpc_messageMediaDocument) {
		return false;
	}
	auto &data = media.c_messageMediaDocument();
	if (!data.has_document() || data.has_ttl_seconds()) {
		LOG(("Api Error: "
			"Got MTPMessageMediaDocument without document "
			"or with ttl_seconds in updateSentMedia()"));
		return false;
	}
	Auth().data().documentConvert(_document, data.vdocument);

	if (const auto good = _document->goodThumbnail()) {
		auto bytes = good->bytesForCache();
		if (const auto length = bytes.size()) {
			if (length > Storage::kMaxFileInMemory) {
				LOG(("App Error: Bad thumbnail data for saving to cache."));
			} else {
				Auth().data().cache().putIfEmpty(
					_document->goodThumbnailCacheKey(),
					Storage::Cache::Database::TaggedValue(
						std::move(bytes),
						Data::kImageCacheTag));
				_document->refreshGoodThumbnail();
			}
		}
	}

	return true;
}

std::unique_ptr<HistoryMedia> MediaFile::createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent) {
	if (_document->sticker()) {
		return std::make_unique<HistorySticker>(message, _document);
	} else if (_document->isAnimation()) {
		return std::make_unique<HistoryGif>(message, _document);
	} else if (_document->isVideoFile()) {
		return std::make_unique<HistoryVideo>(
			message,
			realParent,
			_document);
	}
	return std::make_unique<HistoryDocument>(message, _document);
}

MediaContact::MediaContact(
	not_null<HistoryItem*> parent,
	UserId userId,
	const QString &firstName,
	const QString &lastName,
	const QString &phoneNumber)
: Media(parent) {
	Auth().data().registerContactItem(userId, parent);

	_contact.userId = userId;
	_contact.firstName = firstName;
	_contact.lastName = lastName;
	_contact.phoneNumber = phoneNumber;
}

MediaContact::~MediaContact() {
	Auth().data().unregisterContactItem(_contact.userId, parent());
}

std::unique_ptr<Media> MediaContact::clone(not_null<HistoryItem*> parent) {
	return std::make_unique<MediaContact>(
		parent,
		_contact.userId,
		_contact.firstName,
		_contact.lastName,
		_contact.phoneNumber);
}

const SharedContact *MediaContact::sharedContact() const {
	return &_contact;
}

QString MediaContact::notificationText() const {
	return lang(lng_in_dlg_contact);
}

QString MediaContact::pinnedTextSubstring() const {
	return lang(lng_action_pinned_media_contact);
}

TextWithEntities MediaContact::clipboardText() const {
	const auto text = qsl("[ ") + lang(lng_in_dlg_contact) + qsl(" ]\n")
		+ lng_full_name(
			lt_first_name,
			_contact.firstName,
			lt_last_name,
			_contact.lastName).trimmed()
		+ '\n'
		+ _contact.phoneNumber;
	return { text, EntitiesInText() };
}

bool MediaContact::updateInlineResultMedia(const MTPMessageMedia &media) {
	return false;
}

bool MediaContact::updateSentMedia(const MTPMessageMedia &media) {
	if (media.type() != mtpc_messageMediaContact) {
		return false;
	}
	if (_contact.userId != media.c_messageMediaContact().vuser_id.v) {
		Auth().data().unregisterContactItem(_contact.userId, parent());
		_contact.userId = media.c_messageMediaContact().vuser_id.v;
		Auth().data().registerContactItem(_contact.userId, parent());
	}
	return true;
}

std::unique_ptr<HistoryMedia> MediaContact::createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent) {
	return std::make_unique<HistoryContact>(
		message,
		_contact.userId,
		_contact.firstName,
		_contact.lastName,
		_contact.phoneNumber);
}

MediaLocation::MediaLocation(
	not_null<HistoryItem*> parent,
	const LocationCoords &coords)
: MediaLocation(parent, coords, QString(), QString()) {
}

MediaLocation::MediaLocation(
	not_null<HistoryItem*> parent,
	const LocationCoords &coords,
	const QString &title,
	const QString &description)
: Media(parent)
, _location(Auth().data().location(coords))
, _title(title)
, _description(description) {
}

std::unique_ptr<Media> MediaLocation::clone(not_null<HistoryItem*> parent) {
	return std::make_unique<MediaLocation>(
		parent,
		_location->coords,
		_title,
		_description);
}

LocationData *MediaLocation::location() const {
	return _location;
}

QString MediaLocation::chatsListText() const {
	return WithCaptionDialogsText(lang(lng_maps_point), _title);
}

QString MediaLocation::notificationText() const {
	return WithCaptionNotificationText(lang(lng_maps_point), _title);
}

QString MediaLocation::pinnedTextSubstring() const {
	return lang(lng_action_pinned_media_location);
}

TextWithEntities MediaLocation::clipboardText() const {
	TextWithEntities result = {
		qsl("[ ") + lang(lng_maps_point) + qsl(" ]\n"),
		EntitiesInText()
	};
	auto titleResult = TextUtilities::ParseEntities(
		TextUtilities::Clean(_title),
		Ui::WebpageTextTitleOptions().flags);
	auto descriptionResult = TextUtilities::ParseEntities(
		TextUtilities::Clean(_description),
		TextParseLinks | TextParseMultiline | TextParseRichText);
	if (!titleResult.text.isEmpty()) {
		TextUtilities::Append(result, std::move(titleResult));
		result.text.append('\n');
	}
	if (!descriptionResult.text.isEmpty()) {
		TextUtilities::Append(result, std::move(descriptionResult));
		result.text.append('\n');
	}
	result.text += LocationClickHandler(_location->coords).dragText();
	return result;
}

bool MediaLocation::updateInlineResultMedia(const MTPMessageMedia &media) {
	return false;
}

bool MediaLocation::updateSentMedia(const MTPMessageMedia &media) {
	return false;
}

std::unique_ptr<HistoryMedia> MediaLocation::createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent) {
	return std::make_unique<HistoryLocation>(
		message,
		_location,
		_title,
		_description);
}

MediaCall::MediaCall(
	not_null<HistoryItem*> parent,
	const MTPDmessageActionPhoneCall &call)
: Media(parent)
, _call(ComputeCallData(call)) {
}

std::unique_ptr<Media> MediaCall::clone(not_null<HistoryItem*> parent) {
	Unexpected("Clone of call media.");
}

const Call *MediaCall::call() const {
	return &_call;
}

QString MediaCall::notificationText() const {
	auto result = Text(parent(), _call.finishReason);
	if (_call.duration > 0) {
		result = lng_call_type_and_duration(
			lt_type,
			result,
			lt_duration,
			formatDurationWords(_call.duration));
	}
	return result;
}

QString MediaCall::pinnedTextSubstring() const {
	return QString();
}

TextWithEntities MediaCall::clipboardText() const {
	return { qsl("[ ") + notificationText() + qsl(" ]"), EntitiesInText() };
}

bool MediaCall::allowsForward() const {
	return false;
}

bool MediaCall::allowsRevoke() const {
	return false;
}

bool MediaCall::updateInlineResultMedia(const MTPMessageMedia &media) {
	return false;
}

bool MediaCall::updateSentMedia(const MTPMessageMedia &media) {
	return false;
}

std::unique_ptr<HistoryMedia> MediaCall::createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent) {
	return std::make_unique<HistoryCall>(message, &_call);
}

QString MediaCall::Text(
		not_null<HistoryItem*> item,
		CallFinishReason reason) {
	if (item->out()) {
		return lang(reason == CallFinishReason::Missed
			? lng_call_cancelled
			: lng_call_outgoing);
	} else if (reason == CallFinishReason::Missed) {
		return lang(lng_call_missed);
	} else if (reason == CallFinishReason::Busy) {
		return lang(lng_call_declined);
	}
	return lang(lng_call_incoming);
}

MediaWebPage::MediaWebPage(
	not_null<HistoryItem*> parent,
	not_null<WebPageData*> page)
: Media(parent)
, _page(page) {
	Auth().data().registerWebPageItem(_page, parent);
}

MediaWebPage::~MediaWebPage() {
	Auth().data().unregisterWebPageItem(_page, parent());
}

std::unique_ptr<Media> MediaWebPage::clone(not_null<HistoryItem*> parent) {
	return std::make_unique<MediaWebPage>(parent, _page);
}

DocumentData *MediaWebPage::document() const {
	return _page->document;
}

PhotoData *MediaWebPage::photo() const {
	return _page->photo;
}

WebPageData *MediaWebPage::webpage() const {
	return _page;
}

bool MediaWebPage::hasReplyPreview() const {
	if (const auto document = MediaWebPage::document()) {
		return !document->thumb->isNull();
	} else if (const auto photo = MediaWebPage::photo()) {
		return !photo->thumb->isNull();
	}
	return false;
}

Image *MediaWebPage::replyPreview() const {
	if (const auto document = MediaWebPage::document()) {
		return document->getReplyPreview(parent()->fullId());
	} else if (const auto photo = MediaWebPage::photo()) {
		return photo->getReplyPreview(parent()->fullId());
	}
	return nullptr;
}

QString MediaWebPage::chatsListText() const {
	return notificationText();
}

QString MediaWebPage::notificationText() const {
	return parent()->originalText().text;
}

QString MediaWebPage::pinnedTextSubstring() const {
	return QString();
}

TextWithEntities MediaWebPage::clipboardText() const {
	return TextWithEntities();
}

bool MediaWebPage::allowsEdit() const {
	return true;
}

bool MediaWebPage::updateInlineResultMedia(const MTPMessageMedia &media) {
	return false;
}

bool MediaWebPage::updateSentMedia(const MTPMessageMedia &media) {
	return false;
}

std::unique_ptr<HistoryMedia> MediaWebPage::createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent) {
	return std::make_unique<HistoryWebPage>(message, _page);
}

MediaGame::MediaGame(
	not_null<HistoryItem*> parent,
	not_null<GameData*> game)
: Media(parent)
, _game(game) {
}

std::unique_ptr<Media> MediaGame::clone(not_null<HistoryItem*> parent) {
	return std::make_unique<MediaGame>(parent, _game);
}

bool MediaGame::hasReplyPreview() const {
	if (const auto document = _game->document) {
		return !document->thumb->isNull();
	} else if (const auto photo = _game->photo) {
		return !photo->thumb->isNull();
	}
	return false;
}

Image *MediaGame::replyPreview() const {
	if (const auto document = _game->document) {
		return document->getReplyPreview(parent()->fullId());
	} else if (const auto photo = _game->photo) {
		return photo->getReplyPreview(parent()->fullId());
	}
	return nullptr;
}

QString MediaGame::notificationText() const {
	// Add a game controller emoji before game title.
	auto result = QString();
	result.reserve(_game->title.size() + 3);
	result.append(
		QChar(0xD83C)
	).append(
		QChar(0xDFAE)
	).append(
		QChar(' ')
	).append(_game->title);
	return result;
}

GameData *MediaGame::game() const {
	return _game;
}

QString MediaGame::pinnedTextSubstring() const {
	const auto title = _game->title;
	return lng_action_pinned_media_game(lt_game, title);
}

TextWithEntities MediaGame::clipboardText() const {
	return TextWithEntities();
}

QString MediaGame::errorTextForForward(
		not_null<ChannelData*> channel) const {
	if (channel->restricted(ChannelRestriction::f_send_games)) {
		return lang(lng_restricted_send_inline);
	}
	return QString();
}

bool MediaGame::consumeMessageText(const TextWithEntities &text) {
	_consumedText = text;
	return true;
}

TextWithEntities MediaGame::consumedMessageText() const {
	return _consumedText;
}

bool MediaGame::updateInlineResultMedia(const MTPMessageMedia &media) {
	return updateSentMedia(media);
}

bool MediaGame::updateSentMedia(const MTPMessageMedia &media) {
	if (media.type() != mtpc_messageMediaGame) {
		return false;
	}
	Auth().data().gameConvert(_game, media.c_messageMediaGame().vgame);
	return true;
}

std::unique_ptr<HistoryMedia> MediaGame::createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent) {
	return std::make_unique<HistoryGame>(message, _game, _consumedText);
}

MediaInvoice::MediaInvoice(
	not_null<HistoryItem*> parent,
	const MTPDmessageMediaInvoice &data)
: Media(parent)
, _invoice(ComputeInvoiceData(data)) {
}

MediaInvoice::MediaInvoice(
	not_null<HistoryItem*> parent,
	const Invoice &data)
: Media(parent)
, _invoice(data) {
}

std::unique_ptr<Media> MediaInvoice::clone(not_null<HistoryItem*> parent) {
	return std::make_unique<MediaInvoice>(parent, _invoice);
}

const Invoice *MediaInvoice::invoice() const {
	return &_invoice;
}

bool MediaInvoice::hasReplyPreview() const {
	if (const auto photo = _invoice.photo) {
		return !photo->thumb->isNull();
	}
	return false;
}

Image *MediaInvoice::replyPreview() const {
	if (const auto photo = _invoice.photo) {
		return photo->getReplyPreview(parent()->fullId());
	}
	return nullptr;
}

QString MediaInvoice::notificationText() const {
	return _invoice.title;
}

QString MediaInvoice::pinnedTextSubstring() const {
	return QString();
}

TextWithEntities MediaInvoice::clipboardText() const {
	return TextWithEntities();
}

bool MediaInvoice::updateInlineResultMedia(const MTPMessageMedia &media) {
	return true;
}

bool MediaInvoice::updateSentMedia(const MTPMessageMedia &media) {
	return true;
}

std::unique_ptr<HistoryMedia> MediaInvoice::createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent) {
	return std::make_unique<HistoryInvoice>(message, &_invoice);
}

MediaPoll::MediaPoll(
	not_null<HistoryItem*> parent,
	not_null<PollData*> poll)
: Media(parent)
, _poll(poll) {
}

MediaPoll::~MediaPoll() {
}

std::unique_ptr<Media> MediaPoll::clone(not_null<HistoryItem*> parent) {
	return std::make_unique<MediaPoll>(parent, _poll);
}

PollData *MediaPoll::poll() const {
	return _poll;
}

QString MediaPoll::notificationText() const {
	return _poll->question;
}

QString MediaPoll::pinnedTextSubstring() const {
	return QChar(171) + _poll->question + QChar(187);
}

TextWithEntities MediaPoll::clipboardText() const {
	const auto text = qsl("[ ")
		+ lang(lng_in_dlg_poll)
		+ qsl(" : ")
		+ _poll->question
		+ qsl(" ]")
		+ ranges::accumulate(
			ranges::view::all(
				_poll->answers
			) | ranges::view::transform(
				[](const PollAnswer &answer) { return "\n- " + answer.text; }
			),
			QString());
	return { text, EntitiesInText() };
}

bool MediaPoll::updateInlineResultMedia(const MTPMessageMedia &media) {
	return false;
}

bool MediaPoll::updateSentMedia(const MTPMessageMedia &media) {
	return false;
}

std::unique_ptr<HistoryMedia> MediaPoll::createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent) {
	return std::make_unique<HistoryPoll>(message, _poll);
}

} // namespace Data
