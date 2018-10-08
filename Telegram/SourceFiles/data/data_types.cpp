/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_types.h"

#include "data/data_document.h"
#include "ui/widgets/input_fields.h"
#include "storage/cache/storage_cache_types.h"
#include "base/openssl_help.h"

namespace Data {
namespace {

constexpr auto kDocumentCacheTag = 0x0000000000000100ULL;
constexpr auto kDocumentCacheMask = 0x00000000000000FFULL;
constexpr auto kStorageCacheTag = 0x0000010000000000ULL;
constexpr auto kStorageCacheMask = 0x000000FFFFFFFFFFULL;
constexpr auto kWebDocumentCacheTag = 0x0000020000000000ULL;
constexpr auto kWebDocumentCacheMask = 0x000000FFFFFFFFFFULL;
constexpr auto kUrlCacheTag = 0x0000030000000000ULL;
constexpr auto kUrlCacheMask = 0x000000FFFFFFFFFFULL;
constexpr auto kGeoPointCacheTag = 0x0000040000000000ULL;
constexpr auto kGeoPointCacheMask = 0x000000FFFFFFFFFFULL;

} // namespace

Storage::Cache::Key DocumentCacheKey(int32 dcId, uint64 id) {
	return Storage::Cache::Key{
		Data::kDocumentCacheTag | (uint64(dcId) & Data::kDocumentCacheMask),
		id
	};
}

Storage::Cache::Key StorageCacheKey(const StorageImageLocation &location) {
	const auto dcId = uint64(location.dc()) & 0xFFULL;
	return Storage::Cache::Key{
		Data::kStorageCacheTag | (dcId << 32) | uint32(location.local()),
		location.volume()
	};
}

Storage::Cache::Key WebDocumentCacheKey(const WebFileLocation &location) {
	const auto dcId = uint64(location.dc()) & 0xFFULL;
	const auto url = location.url();
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

HistoryItem *FileClickHandler::getActionItem() const {
	return context()
		? App::histItemById(context())
		: nullptr;
}
