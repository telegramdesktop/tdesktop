/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_types.h"

#include "data/data_document.h"
#include "data/data_session.h"
#include "ui/widgets/input_fields.h"
#include "storage/cache/storage_cache_types.h"
#include "base/openssl_help.h"
#include "main/main_session.h"

namespace Data {
namespace {

constexpr auto kDocumentCacheTag = 0x0000000000000100ULL;
constexpr auto kDocumentCacheMask = 0x00000000000000FFULL;
constexpr auto kDocumentThumbCacheTag = 0x0000000000000200ULL;
constexpr auto kDocumentThumbCacheMask = 0x00000000000000FFULL;
constexpr auto kWebDocumentCacheTag = 0x0000020000000000ULL;
constexpr auto kUrlCacheTag = 0x0000030000000000ULL;
constexpr auto kGeoPointCacheTag = 0x0000040000000000ULL;

} // namespace

Storage::Cache::Key DocumentCacheKey(int32 dcId, uint64 id) {
	return Storage::Cache::Key{
		Data::kDocumentCacheTag | (uint64(dcId) & Data::kDocumentCacheMask),
		id
	};
}

Storage::Cache::Key DocumentThumbCacheKey(int32 dcId, uint64 id) {
	const auto part = (uint64(dcId) & Data::kDocumentThumbCacheMask);
	return Storage::Cache::Key{
		Data::kDocumentThumbCacheTag | part,
		id
	};
}

Storage::Cache::Key WebDocumentCacheKey(const WebFileLocation &location) {
	const auto CacheDcId = 4; // The default production value. Doesn't matter.
	const auto dcId = uint64(CacheDcId) & 0xFFULL;
	const auto &url = location.url();
	const auto hash = openssl::Sha256(bytes::make_span(url));
	const auto bytes = bytes::make_span(hash);
	const auto bytes1 = bytes.subspan(0, sizeof(uint32));
	const auto bytes2 = bytes.subspan(sizeof(uint32), sizeof(uint64));
	const auto part1 = *reinterpret_cast<const uint32*>(bytes1.data());
	const auto part2 = *reinterpret_cast<const uint64*>(bytes2.data());
	return Storage::Cache::Key{
		Data::kWebDocumentCacheTag | (dcId << 32) | part1,
		part2
	};
}

Storage::Cache::Key UrlCacheKey(const QString &location) {
	const auto url = location.toUtf8();
	const auto hash = openssl::Sha256(bytes::make_span(url));
	const auto bytes = bytes::make_span(hash);
	const auto bytes1 = bytes.subspan(0, sizeof(uint32));
	const auto bytes2 = bytes.subspan(sizeof(uint32), sizeof(uint64));
	const auto bytes3 = bytes.subspan(
		sizeof(uint32) + sizeof(uint64),
		sizeof(uint16));
	const auto part1 = *reinterpret_cast<const uint32*>(bytes1.data());
	const auto part2 = *reinterpret_cast<const uint64*>(bytes2.data());
	const auto part3 = *reinterpret_cast<const uint16*>(bytes3.data());
	return Storage::Cache::Key{
		Data::kUrlCacheTag | (uint64(part3) << 32) | part1,
		part2
	};
}

Storage::Cache::Key GeoPointCacheKey(const GeoPointLocation &location) {
	const auto zoomscale = ((uint32(location.zoom) & 0x0FU) << 8)
		| (uint32(location.scale) & 0x0FU);
	const auto widthheight = ((uint32(location.width) & 0xFFFFU) << 16)
		| (uint32(location.height) & 0xFFFFU);
	return Storage::Cache::Key{
		Data::kGeoPointCacheTag | (uint64(zoomscale) << 32) | widthheight,
		(uint64(std::round(std::abs(location.lat + 360.) * 1000000)) << 32)
		| uint64(std::round(std::abs(location.lon + 360.) * 1000000))
	};
}

} // namespace Data

uint32 AudioMsgId::CreateExternalPlayId() {
	static auto Result = uint32(0);
	return ++Result ? Result : ++Result;
}

AudioMsgId AudioMsgId::ForVideo() {
	auto result = AudioMsgId();
	result._externalPlayId = CreateExternalPlayId();
	result._type = Type::Video;
	return result;
}

void AudioMsgId::setTypeFromAudio() {
	if (_audio->isVoiceMessage() || _audio->isVideoMessage()) {
		_type = Type::Voice;
	} else if (_audio->isVideoFile()) {
		_type = Type::Video;
	} else if (_audio->isAudioFile()) {
		_type = Type::Song;
	} else {
		_type = Type::Unknown;
	}
}

void MessageCursor::fillFrom(not_null<const Ui::InputField*> field) {
	const auto cursor = field->textCursor();
	position = cursor.position();
	anchor = cursor.anchor();
	const auto top = field->scrollTop().current();
	scroll = (top != field->scrollTopMax()) ? top : QFIXED_MAX;
}

void MessageCursor::applyTo(not_null<Ui::InputField*> field) {
	auto cursor = field->textCursor();
	cursor.setPosition(anchor, QTextCursor::MoveAnchor);
	cursor.setPosition(position, QTextCursor::KeepAnchor);
	field->setTextCursor(cursor);
	field->scrollTo(scroll);
}

PeerId PeerFromMessage(const MTPmessage &message) {
	return message.match([](const MTPDmessageEmpty &) {
		return PeerId(0);
	}, [](const auto &data) {
		return peerFromMTP(data.vpeer_id());
	});
}

MTPDmessage::Flags FlagsFromMessage(const MTPmessage &message) {
	return message.match([](const MTPDmessageEmpty &) {
		return MTPDmessage::Flags(0);
	}, [](const MTPDmessage &data) {
		return data.vflags().v;
	}, [](const MTPDmessageService &data) {
		return mtpCastFlags(data.vflags().v);
	});
}

MsgId IdFromMessage(const MTPmessage &message) {
	return message.match([](const auto &data) {
		return data.vid().v;
	});
}

TimeId DateFromMessage(const MTPmessage &message) {
	return message.match([](const MTPDmessageEmpty &) {
		return TimeId(0);
	}, [](const auto &message) {
		return message.vdate().v;
	});
}

bool GoodStickerDimensions(int width, int height) {
	// Show all .webp (except very large ones) as stickers,
	// allow to open them in media viewer to see details.
	constexpr auto kLargetsStickerSide = 2560;
	return (width > 0)
		&& (height > 0)
		&& (width * height <= kLargetsStickerSide * kLargetsStickerSide);
}
