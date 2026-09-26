/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/markdown/iv_markdown_media_block.h"
#include "iv/markdown/iv_markdown_prepare.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace Iv::Markdown {

using MediaBlockStorage = std::unordered_map<
	uint64,
	std::shared_ptr<MediaBlock>>;

void ClearMediaBlockStorage(MediaBlockStorage *blocks);

[[nodiscard]] MediaBlockStorage ReuseMediaBlocks(
	const std::vector<PreparedBlock> &oldPreparedBlocks,
	MediaBlockStorage *oldMediaBlocks,
	const std::vector<PreparedBlock> &newPreparedBlocks);

} // namespace Iv::Markdown
