/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/iv_markdown_media_reuse.h"

#include <cstring>
#include <optional>
#include <utility>

namespace Iv::Markdown {
namespace {

struct MediaReuseGroupedItem {
	PreparedMediaItemKind kind = PreparedMediaItemKind::Photo;
	uint64 backingId = 0;
	int width = 0;
	int height = 0;
	bool spoiler = false;

	friend inline bool operator==(
		const MediaReuseGroupedItem &,
		const MediaReuseGroupedItem &) = default;
};

struct MediaReuseKey {
	PreparedBlockKind kind = PreparedBlockKind::Paragraph;
	PreparedMediaItemKind mediaKind = PreparedMediaItemKind::Photo;
	PreparedGroupedMediaIntent groupedIntent
		= PreparedGroupedMediaIntent::Collage;
	uint64 backingId = 0;
	uint64 accessHash = 0;
	int width = 0;
	int height = 0;
	int zoom = 0;
	int duration = 0;
	double latitude = 0.;
	double longitude = 0.;
	QString title;
	QString performer;
	QString fileName;
	QString username;
	QString url;
	QString urlOverride;
	bool viewerOpen = false;
	bool spoiler = false;
	std::vector<MediaReuseGroupedItem> groupedItems;

	friend inline bool operator==(
		const MediaReuseKey &,
		const MediaReuseKey &) = default;
};

void MediaReuseHashCombine(size_t *result, size_t value) {
	*result = (*result * 1315423911U) ^ value;
}

template <typename Value>
void MediaReuseHashCombine(size_t *result, const Value &value) {
	MediaReuseHashCombine(result, size_t(qHash(value)));
}

[[nodiscard]] uint64 MediaReuseDoubleBits(double value) {
	if (value == 0.) {
		return 0;
	}
	auto result = uint64(0);
	std::memcpy(&result, &value, sizeof(result));
	return result;
}

struct MediaReuseKeyHasher {
	[[nodiscard]] size_t operator()(
			const MediaReuseKey &key) const noexcept;
};

size_t MediaReuseKeyHasher::operator()(
		const MediaReuseKey &key) const noexcept {
	auto result = size_t(0);
	MediaReuseHashCombine(&result, int(key.kind));
	MediaReuseHashCombine(&result, int(key.mediaKind));
	MediaReuseHashCombine(&result, int(key.groupedIntent));
	MediaReuseHashCombine(&result, key.backingId);
	MediaReuseHashCombine(&result, key.accessHash);
	MediaReuseHashCombine(&result, key.width);
	MediaReuseHashCombine(&result, key.height);
	MediaReuseHashCombine(&result, key.zoom);
	MediaReuseHashCombine(&result, key.duration);
	MediaReuseHashCombine(&result, MediaReuseDoubleBits(key.latitude));
	MediaReuseHashCombine(&result, MediaReuseDoubleBits(key.longitude));
	MediaReuseHashCombine(&result, key.title);
	MediaReuseHashCombine(&result, key.performer);
	MediaReuseHashCombine(&result, key.fileName);
	MediaReuseHashCombine(&result, key.username);
	MediaReuseHashCombine(&result, key.url);
	MediaReuseHashCombine(&result, key.urlOverride);
	MediaReuseHashCombine(&result, int(key.viewerOpen));
	MediaReuseHashCombine(&result, int(key.spoiler));
	for (const auto &item : key.groupedItems) {
		MediaReuseHashCombine(&result, int(item.kind));
		MediaReuseHashCombine(&result, item.backingId);
		MediaReuseHashCombine(&result, item.width);
		MediaReuseHashCombine(&result, item.height);
		MediaReuseHashCombine(&result, int(item.spoiler));
	}
	return result;
}

using MediaBlockReusePool = std::unordered_map<
	MediaReuseKey,
	std::vector<std::shared_ptr<MediaBlock>>,
	MediaReuseKeyHasher>;

[[nodiscard]] MediaReuseGroupedItem MediaReuseGroupedItemForPreparedMedia(
		const PreparedMediaItemData &media) {
	return {
		.kind = media.kind,
		.backingId = media.id,
		.width = media.width,
		.height = media.height,
		.spoiler = media.spoiler,
	};
}

[[nodiscard]] PreparedMediaBlockId MediaIdForPreparedBlock(
		const PreparedBlock &block) {
	switch (block.kind) {
	case PreparedBlockKind::Photo:
		return block.photo.id;
	case PreparedBlockKind::Video:
		return block.video.id;
	case PreparedBlockKind::Audio:
		return block.audio.id;
	case PreparedBlockKind::Map:
		return block.map.id;
	case PreparedBlockKind::Channel:
		return block.channel.id;
	case PreparedBlockKind::GroupedMedia:
		return block.groupedMedia.id;
	default:
		return {};
	}
}

[[nodiscard]] std::optional<MediaReuseKey> MediaKeyForPreparedBlock(
		const PreparedBlock &block) {
	switch (block.kind) {
	case PreparedBlockKind::Photo:
		return MediaReuseKey{
			.kind = block.kind,
			.backingId = block.photo.photoId,
			.width = block.photo.width,
			.height = block.photo.height,
			.urlOverride = block.photo.urlOverride,
			.viewerOpen = block.photo.viewerOpen,
			.spoiler = block.photo.spoiler,
		};
	case PreparedBlockKind::Video:
		return MediaReuseKey{
			.kind = block.kind,
			.mediaKind = block.video.media.kind,
			.backingId = block.video.media.id,
			.width = block.video.media.width,
			.height = block.video.media.height,
			.spoiler = block.video.media.spoiler,
		};
	case PreparedBlockKind::Audio:
		return MediaReuseKey{
			.kind = block.kind,
			.backingId = block.audio.documentId,
			.duration = block.audio.duration,
			.title = block.audio.title,
			.performer = block.audio.performer,
			.fileName = block.audio.fileName,
		};
	case PreparedBlockKind::Map:
		return MediaReuseKey{
			.kind = block.kind,
			.accessHash = block.map.accessHash,
			.width = block.map.width,
			.height = block.map.height,
			.zoom = block.map.zoom,
			.latitude = block.map.latitude,
			.longitude = block.map.longitude,
			.url = block.map.url,
		};
	case PreparedBlockKind::Channel:
		return MediaReuseKey{
			.kind = block.kind,
			.backingId = block.channel.channelId,
			.title = block.channel.title,
			.username = block.channel.username,
		};
	case PreparedBlockKind::GroupedMedia: {
		auto groupedItems = std::vector<MediaReuseGroupedItem>();
		groupedItems.reserve(block.groupedMedia.items.size());
		for (const auto &item : block.groupedMedia.items) {
			groupedItems.push_back(
				MediaReuseGroupedItemForPreparedMedia(item.media));
		}
		return MediaReuseKey{
			.kind = block.kind,
			.groupedIntent = block.groupedMedia.intent,
			.groupedItems = std::move(groupedItems),
		};
	}
	default:
		return std::nullopt;
	}
}

void ClearMediaBlockReusePool(MediaBlockReusePool *pool) {
	if (!pool) {
		return;
	}
	for (const auto &entry : *pool) {
		for (const auto &block : entry.second) {
			if (block) {
				block->setHost(nullptr);
			}
		}
	}
	pool->clear();
}

void CollectOldMediaBlocksForReuse(
		const std::vector<PreparedBlock> &blocks,
		MediaBlockStorage *oldBlocks,
		MediaBlockReusePool *pool) {
	if (!oldBlocks || !pool) {
		return;
	}
	for (const auto &block : blocks) {
		if (const auto id = MediaIdForPreparedBlock(block); id) {
			if (const auto key = MediaKeyForPreparedBlock(block)) {
				if (const auto i = oldBlocks->find(id.value);
					i != end(*oldBlocks)) {
					if (i->second) {
						(*pool)[*key].push_back(std::move(i->second));
					}
					oldBlocks->erase(i);
				}
			}
		}
		CollectOldMediaBlocksForReuse(block.children, oldBlocks, pool);
	}
}

void CollectReusedMediaBlocks(
		const std::vector<PreparedBlock> &blocks,
		MediaBlockReusePool *pool,
		MediaBlockStorage *reusedBlocks) {
	if (!pool || !reusedBlocks) {
		return;
	}
	for (const auto &block : blocks) {
		if (const auto id = MediaIdForPreparedBlock(block); id) {
			if (const auto key = MediaKeyForPreparedBlock(block)) {
				if (auto i = pool->find(*key); i != end(*pool)) {
					if (!i->second.empty()) {
						auto reused = std::move(i->second.front());
						i->second.erase(begin(i->second));
						if (reused) {
							reusedBlocks->emplace(id.value, std::move(reused));
						}
					}
					if (i->second.empty()) {
						pool->erase(i);
					}
				}
			}
		}
		CollectReusedMediaBlocks(block.children, pool, reusedBlocks);
	}
}

} // namespace

void ClearMediaBlockStorage(MediaBlockStorage *blocks) {
	if (!blocks) {
		return;
	}
	for (const auto &entry : *blocks) {
		if (entry.second) {
			entry.second->setHost(nullptr);
		}
	}
	blocks->clear();
}

MediaBlockStorage ReuseMediaBlocks(
		const std::vector<PreparedBlock> &oldPreparedBlocks,
		MediaBlockStorage *oldMediaBlocks,
		const std::vector<PreparedBlock> &newPreparedBlocks) {
	auto pool = MediaBlockReusePool();
	auto result = MediaBlockStorage();
	CollectOldMediaBlocksForReuse(
		oldPreparedBlocks,
		oldMediaBlocks,
		&pool);
	CollectReusedMediaBlocks(
		newPreparedBlocks,
		&pool,
		&result);
	ClearMediaBlockStorage(oldMediaBlocks);
	ClearMediaBlockReusePool(&pool);
	return result;
}

} // namespace Iv::Markdown
