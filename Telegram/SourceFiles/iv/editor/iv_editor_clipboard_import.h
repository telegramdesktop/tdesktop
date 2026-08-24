/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/editor/iv_editor_clipboard.h"
#include "iv/iv_rich_page.h"
#include "ui/chat/attach/attach_prepare.h"

#include <optional>

class QMimeData;

namespace Main {
class Session;
} // namespace Main

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

inline constexpr auto kImportedMediaPlaceholderLimit = 4096;

[[nodiscard]] constexpr uint64 ImportedMediaPlaceholderId(int index) {
	return 0xF000000000000000ULL + uint64(index);
}

[[nodiscard]] constexpr std::optional<int> ImportedMediaPlaceholderIndex(
		uint64 id) {
	const auto base = ImportedMediaPlaceholderId(0);
	const auto last = ImportedMediaPlaceholderId(
		kImportedMediaPlaceholderLimit - 1);
	return (id >= base && id <= last)
		? std::make_optional(int(id - base))
		: std::nullopt;
}

struct ImportedLocalMedia {
	QString path;
	QByteArray content;
};

struct BlocksImportResult {
	std::vector<RichPage::Block> blocks;
	std::vector<ImportedLocalMedia> localMedia;
	bool truncated = false;
};

[[nodiscard]] TableImportLimits TableImportLimitsFor(
	const RichMessageLimits &limits,
	int usedBlocks);

[[nodiscard]] bool MimeDataLooksLikeTable(not_null<const QMimeData*> data);

[[nodiscard]] std::optional<TableImportResult> TableFromMimeData(
	not_null<const QMimeData*> data,
	const TableImportLimits &limits);

[[nodiscard]] bool RichBlocksCarryStructure(
	const std::vector<RichPage::Block> &blocks);

[[nodiscard]] bool MimeDataHasRichStructure(
	not_null<Main::Session*> session,
	not_null<const QMimeData*> data,
	const RichMessageLimits &limits);

[[nodiscard]] bool TextHasMarkdownStructure(
	const QString &text,
	const RichMessageLimits &limits);

[[nodiscard]] std::optional<BlocksImportResult> BlocksFromMimeData(
	not_null<Main::Session*> session,
	not_null<const QMimeData*> data,
	const RichMessageLimits &limits,
	int usedBlocks);

[[nodiscard]] std::optional<BlocksImportResult> BlocksFromMarkdown(
	const QString &text,
	const RichMessageLimits &limits,
	int usedBlocks);

[[nodiscard]] bool MimeDataLooksLikeExportedHtml(
	not_null<const QMimeData*> data);

[[nodiscard]] std::optional<ClipboardData> BlockClipboardDataFromRichText(
	TextWithEntities text);

[[nodiscard]] std::optional<ClipboardData> BlockClipboardDataFromFieldTags(
	not_null<const QMimeData*> data);

[[nodiscard]] std::optional<Ui::PreparedList> PreparedMediaFromClipboard(
	not_null<const QMimeData*> data,
	bool premium);

[[nodiscard]] bool IsAcceptableDropMedia(not_null<const QMimeData*> data);

[[nodiscard]] bool CanPrepareMediaFromClipboard(
	not_null<const QMimeData*> data);

} // namespace Iv::Editor
