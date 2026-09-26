/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/iv_rich_page.h"

#include <optional>

namespace Iv::Editor {

[[nodiscard]] bool MediaBlockSupportsSpoiler(const RichPage::Block &block);

[[nodiscard]] bool MediaBlockHasSpoiler(const RichPage::Block &block);

[[nodiscard]] bool IsPhotoVideoBlockKind(RichPage::BlockKind kind);

[[nodiscard]] bool GroupingRichTextIsEmpty(const RichPage::RichText &text);

[[nodiscard]] bool IsGroupableMediaBlock(const RichPage::Block &block);

[[nodiscard]] std::optional<RichPage::GroupedMediaItem>
GroupedItemFromPhotoVideoBlock(const RichPage::Block &block);

[[nodiscard]] std::optional<RichPage::Block> PhotoVideoBlockFromGroupedItem(
	const RichPage::GroupedMediaItem &item);

} // namespace Iv::Editor
