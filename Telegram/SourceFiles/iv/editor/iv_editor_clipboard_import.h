/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/iv_rich_page.h"

#include <optional>

class QMimeData;

namespace Iv::Editor {

struct TableImportLimits {
	int maxRows = 0;
	int maxColumns = 0;
	int maxCells = 0;
	int maxCellLength = 0;
};

struct TableImportResult {
	RichPage::Block block;
	bool truncated = false;
};

struct BlocksImportResult {
	std::vector<RichPage::Block> blocks;
	bool truncated = false;
};

[[nodiscard]] TableImportLimits TableImportLimitsFor(
	const RichMessageLimits &limits,
	int usedBlocks);

[[nodiscard]] bool MimeDataLooksLikeTable(not_null<const QMimeData*> data);

[[nodiscard]] std::optional<TableImportResult> TableFromMimeData(
	not_null<const QMimeData*> data,
	const TableImportLimits &limits);

[[nodiscard]] std::optional<BlocksImportResult> BlocksFromMimeData(
	not_null<const QMimeData*> data,
	const RichMessageLimits &limits,
	int usedBlocks);

} // namespace Iv::Editor
