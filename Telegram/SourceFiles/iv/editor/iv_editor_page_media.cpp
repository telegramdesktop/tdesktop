/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/editor/iv_editor_page_media.h"

#include <algorithm>

namespace Iv::Editor {
namespace {

using Block = RichPage::Block;
using BlockKind = RichPage::BlockKind;
using RichText = RichPage::RichText;

} // namespace

bool MediaBlockSupportsSpoiler(const Block &block) {
	switch (block.kind) {
	case BlockKind::Photo:
	case BlockKind::Video:
	case BlockKind::Audio:
	case BlockKind::Map:
		return true;
	case BlockKind::GroupedMedia:
		return ranges::any_of(
			block.mediaItems,
			[](const RichPage::GroupedMediaItem &item) {
				return (item.kind == BlockKind::Photo)
					|| (item.kind == BlockKind::Video)
					|| (item.kind == BlockKind::Audio)
					|| (item.kind == BlockKind::Map);
			});
	default:
		return false;
	}
}

bool MediaBlockHasSpoiler(const Block &block) {
	if (block.kind == BlockKind::GroupedMedia) {
		auto any = false;
		for (const auto &item : block.mediaItems) {
			if ((item.kind != BlockKind::Photo)
				&& (item.kind != BlockKind::Video)
				&& (item.kind != BlockKind::Audio)
				&& (item.kind != BlockKind::Map)) {
				continue;
			}
			any = true;
			if (!item.spoiler) {
				return false;
			}
		}
		return any;
	}
	return block.spoiler;
}

bool IsPhotoVideoBlockKind(BlockKind kind) {
	return (kind == BlockKind::Photo) || (kind == BlockKind::Video);
}

bool GroupingRichTextIsEmpty(const RichText &text) {
	return text.text.text.trimmed().isEmpty()
		&& text.anchorId.isEmpty()
		&& text.anchorIds.empty();
}

bool IsGroupableMediaBlock(const Block &block) {
	if (IsPhotoVideoBlockKind(block.kind)) {
		return true;
	}
	return (block.kind == BlockKind::GroupedMedia)
		&& !block.mediaItems.empty()
		&& ranges::all_of(
			block.mediaItems,
			IsPhotoVideoBlockKind,
			&RichPage::GroupedMediaItem::kind);
}

std::optional<RichPage::GroupedMediaItem>
GroupedItemFromPhotoVideoBlock(const Block &block) {
	if (!IsPhotoVideoBlockKind(block.kind)) {
		return std::nullopt;
	}
	auto result = RichPage::GroupedMediaItem();
	result.kind = block.kind;
	result.photo = block.photo;
	result.document = block.document;
	result.photoId = block.photoId;
	result.documentId = block.documentId;
	result.width = block.width;
	result.height = block.height;
	result.autoplay = block.autoplay;
	result.loop = block.loop;
	result.spoiler = block.spoiler;
	return result;
}

std::optional<Block> PhotoVideoBlockFromGroupedItem(
		const RichPage::GroupedMediaItem &item) {
	if (!IsPhotoVideoBlockKind(item.kind)) {
		return std::nullopt;
	}
	auto result = Block();
	result.kind = item.kind;
	result.photo = item.photo;
	result.document = item.document;
	result.photoId = item.photoId;
	result.documentId = item.documentId;
	result.width = item.width;
	result.height = item.height;
	result.autoplay = item.autoplay;
	result.loop = item.loop;
	result.spoiler = item.spoiler;
	return result;
}

} // namespace Iv::Editor
