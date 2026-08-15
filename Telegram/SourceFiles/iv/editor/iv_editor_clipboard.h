/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/iv_rich_page.h"

#include <memory>
#include <optional>
#include <variant>
#include <vector>

class QMimeData;

namespace Iv::Editor {

struct ClipboardOrigin {
	uint64 sessionId = 0;
	uint64 serial = 0;
};

struct ClipboardBlockData {
	ClipboardOrigin origin;
	std::vector<RichPage::Block> blocks;
};

struct ClipboardListItemsData {
	ClipboardOrigin origin;
	RichPage::ListKind listKind = RichPage::ListKind::Bullet;
	RichPage::OrderedListData orderedList;
	bool taskList = false;
	std::vector<RichPage::ListItem> items;
};

using ClipboardData = std::variant<ClipboardBlockData, ClipboardListItemsData>;

[[nodiscard]] QString ClipboardMimeType();
[[nodiscard]] std::unique_ptr<QMimeData> MimeDataFromClipboardData(
	ClipboardData data);
[[nodiscard]] std::optional<ClipboardData> ClipboardDataFromMimeData(
	const QMimeData *mimeData);

} // namespace Iv::Editor
