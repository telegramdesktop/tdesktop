/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_media.h"

#include "api/api_common.h"
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
		if (document->hasDuration() && !document->hasMimeType(u"image/gif"_q)) {
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
				MTP_double(document->duration() / 1000.),
				MTP_int(dimensions.width()),
				MTP_int(dimensions.height()),
				MTPint(), // preload_prefix_size
				MTPdouble(), // video_start_ts
				MTPstring())); // video_codec
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
			MTP_int(document->duration() / 1000),
			MTP_string(song->title),
			MTP_string(song->performer),
			MTPstring()));
	} else if (const auto voice = document->voice()) {
		const auto flags = MTPDdocumentAttributeAudio::Flag::f_voice
			| MTPDdocumentAttributeAudio::Flag::f_waveform;
		attributes.push_back(MTP_documentAttributeAudio(
			MTP_flags(flags),
			MTP_int(document->duration() / 1000),
			MTPstring(),
			MTPstring(),
			MTP_bytes(documentWaveformEncode5bit(voice->waveform))));
	}
	return MTP_vector<MTPDocumentAttribute>(attributes);
}

} // namespace

MTPInputMedia PrepareUploadedPhoto(
		not_null<HistoryItem*> item,
		RemoteFileInfo info) {
	using Flag = MTPDinputMediaUploadedPhoto::Flag;
	const auto spoiler = item->media() && item->media()->hasSpoiler();
	const auto ttlSeconds = item->media()
		? item->media()->ttlSeconds()
		: 0;
	const auto flags = (spoiler ? Flag::f_spoiler : Flag())
		| (info.attachedStickers.empty() ? Flag() : Flag::f_stickers)
		| (ttlSeconds ? Flag::f_ttl_seconds : Flag());
	return MTP_inputMediaUploadedPhoto(
		MTP_flags(flags),
		info.file,
		MTP_vector<MTPInputDocument>(
			ranges::to<QVector<MTPInputDocument>>(info.attachedStickers)),
		MTP_int(ttlSeconds));
}

MTPInputMedia PrepareUploadedDocument(
		not_null<HistoryItem*> item,
		RemoteFileInfo info) {
	if (!item || !item->media() || !item->media()->document()) {
		return MTP_inputMediaEmpty();
	}
	using Flag = MTPDinputMediaUploadedDocument::Flag;
	const auto spoiler = item->media() && item->media()->hasSpoiler();
	const auto ttlSeconds = item->media()
		? item->media()->ttlSeconds()
		: 0;
	const auto flags = (spoiler ? Flag::f_spoiler : Flag())
		| (info.thumb ? Flag::f_thumb : Flag())
		| (item->groupId() ? Flag::f_nosound_video : Flag())
		| (info.attachedStickers.empty() ? Flag::f_stickers : Flag())
		| (ttlSeconds ? Flag::f_ttl_seconds : Flag())
		| (info.videoCover ? Flag::f_video_cover : Flag());
	const auto document = item->media()->document();
	return MTP_inputMediaUploadedDocument(
		MTP_flags(flags),
		info.file,
		info.thumb.value_or(MTPInputFile()),
		MTP_string(document->mimeString()),
		ComposeSendingDocumentAttributes(document),
		MTP_vector<MTPInputDocument>(
			ranges::to<QVector<MTPInputDocument>>(info.attachedStickers)),
		info.videoCover.value_or(MTPInputPhoto()),
		MTP_int(0), // video_timestamp
		MTP_int(ttlSeconds));
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
