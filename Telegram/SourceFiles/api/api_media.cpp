/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_media.h"

#include "data/data_document.h"
#include "data/stickers/data_stickers_set.h"
#include "history/history_item.h"

namespace Api {
namespace {

MTPVector<MTPDocumentAttribute> ComposeSendingDocumentAttributes(
		not_null<DocumentData*> document) {
	const auto filenameAttribute = MTP_documentAttributeFilename(
		MTP_string(document->filename()));
	const auto dimensions = document->dimensions;
	auto attributes = QVector<MTPDocumentAttribute>(1, filenameAttribute);
	if (dimensions.width() > 0 && dimensions.height() > 0) {
		const auto duration = document->getDuration();
		if (duration >= 0 && !document->hasMimeType(qstr("image/gif"))) {
			auto flags = MTPDdocumentAttributeVideo::Flags(0);
			using VideoFlag = MTPDdocumentAttributeVideo::Flag;
			if (document->isVideoMessage()) {
				flags |= VideoFlag::f_round_message;
			}
			if (document->supportsStreaming()) {
				flags |= VideoFlag::f_supports_streaming;
			}
			attributes.push_back(MTP_documentAttributeVideo(
				MTP_flags(flags),
				MTP_int(duration),
				MTP_int(dimensions.width()),
				MTP_int(dimensions.height())));
		} else {
			attributes.push_back(MTP_documentAttributeImageSize(
				MTP_int(dimensions.width()),
				MTP_int(dimensions.height())));
		}
	}
	if (document->type == AnimatedDocument) {
		attributes.push_back(MTP_documentAttributeAnimated());
	} else if (document->type == StickerDocument && document->sticker()) {
		attributes.push_back(MTP_documentAttributeSticker(
			MTP_flags(0),
			MTP_string(document->sticker()->alt),
			Data::InputStickerSet(document->sticker()->set),
			MTPMaskCoords()));
	} else if (const auto song = document->song()) {
		const auto flags = MTPDdocumentAttributeAudio::Flag::f_title
			| MTPDdocumentAttributeAudio::Flag::f_performer;
		attributes.push_back(MTP_documentAttributeAudio(
			MTP_flags(flags),
			MTP_int(song->duration),
			MTP_string(song->title),
			MTP_string(song->performer),
			MTPstring()));
	} else if (const auto voice = document->voice()) {
		const auto flags = MTPDdocumentAttributeAudio::Flag::f_voice
			| MTPDdocumentAttributeAudio::Flag::f_waveform;
		attributes.push_back(MTP_documentAttributeAudio(
			MTP_flags(flags),
			MTP_int(voice->duration),
			MTPstring(),
			MTPstring(),
			MTP_bytes(documentWaveformEncode5bit(voice->waveform))));
	}
	return MTP_vector<MTPDocumentAttribute>(attributes);
}

} // namespace

MTPInputMedia PrepareUploadedPhoto(
		const MTPInputFile &file,
		std::vector<MTPInputDocument> attachedStickers) {
	const auto flags = attachedStickers.empty()
		? MTPDinputMediaUploadedPhoto::Flags(0)
		: MTPDinputMediaUploadedPhoto::Flag::f_stickers;
	return MTP_inputMediaUploadedPhoto(
		MTP_flags(flags),
		file,
		MTP_vector<MTPInputDocument>(ranges::to<QVector>(attachedStickers)),
		MTP_int(0));
}

MTPInputMedia PrepareUploadedDocument(
		not_null<HistoryItem*> item,
		const MTPInputFile &file,
		const std::optional<MTPInputFile> &thumb,
		std::vector<MTPInputDocument> attachedStickers) {
	if (!item || !item->media() || !item->media()->document()) {
		return MTP_inputMediaEmpty();
	}
	const auto emptyFlag = MTPDinputMediaUploadedDocument::Flags(0);
	using DocFlags = MTPDinputMediaUploadedDocument::Flag;
	const auto flags = emptyFlag
		| (thumb ? DocFlags::f_thumb : emptyFlag)
		| (item->groupId() ? DocFlags::f_nosound_video : emptyFlag)
		| (attachedStickers.empty() ? DocFlags::f_stickers : emptyFlag);
	const auto document = item->media()->document();
	return MTP_inputMediaUploadedDocument(
		MTP_flags(flags),
		file,
		thumb.value_or(MTPInputFile()),
		MTP_string(document->mimeString()),
		ComposeSendingDocumentAttributes(document),
		MTP_vector<MTPInputDocument>(ranges::to<QVector>(attachedStickers)),
		MTP_int(0));
}

bool HasAttachedStickers(MTPInputMedia media) {
	return media.match([&](const MTPDinputMediaUploadedPhoto &photo) -> bool {
		return (photo.vflags().v
			& MTPDinputMediaUploadedPhoto::Flag::f_stickers);
	}, [&](const MTPDinputMediaUploadedDocument &document) -> bool {
		return (document.vflags().v
			& MTPDinputMediaUploadedDocument::Flag::f_stickers);
	}, [](const auto &d) {
		return false;
	});
}

} // namespace Api
