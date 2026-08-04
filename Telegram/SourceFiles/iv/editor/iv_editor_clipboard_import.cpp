/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/editor/iv_editor_clipboard_import.h"

#include "ui/text/text_entity.h"
#include "ui/text/text_html_tags.h"

#include <QtCore/QMimeData>

#include <array>

namespace Iv::Editor {
namespace {

constexpr auto kMaxCellLength = 4096;

[[nodiscard]] QString QtWindowsMimeName(const QString &name) {
	return u"application/x-qt-windows-mime;value=\"%1\""_q.arg(name);
}

[[nodiscard]] bool HasNativeFormat(
		not_null<const QMimeData*> data,
		const QString &name) {
	return data->hasFormat(name)
		|| data->hasFormat(QtWindowsMimeName(name));
}

[[nodiscard]] bool HasSpreadsheetHint(not_null<const QMimeData*> data) {
	static const auto kNames = std::array{
		u"Biff12"_q,
		u"Biff8"_q,
		u"Biff5"_q,
		u"XML Spreadsheet"_q,
	};
	for (const auto &name : kNames) {
		if (HasNativeFormat(data, name)) {
			return true;
		}
	}
	static const auto kPrefixes = std::array{
		u"application/x-libreoffice-internal-id-"_q,
		u"application/x-openoffice-embed-source-xml"_q,
	};
	for (const auto &format : data->formats()) {
		for (const auto &prefix : kPrefixes) {
			if (format.startsWith(prefix)) {
				return true;
			}
		}
	}
	return false;
}

[[nodiscard]] bool RangeTextIsEmpty(QStringView html, int from, int till) {
	if (from >= till) {
		return true;
	}
	const auto parsed = TextUtilities::TextWithTagsFromHtmlFragment(
		html.mid(from, till - from));
	return parsed.text.trimmed().isEmpty();
}

[[nodiscard]] bool TableCoversWholeHtml(
		QStringView html,
		const TextUtilities::HtmlTable &table) {
	return RangeTextIsEmpty(html, 0, table.sourceFrom)
		&& RangeTextIsEmpty(html, table.sourceTill, int(html.size()));
}

[[nodiscard]] RichPage::TableAlignment ConvertAlignment(
		TextUtilities::HtmlTableAlignment alignment) {
	using Source = TextUtilities::HtmlTableAlignment;
	switch (alignment) {
	case Source::Center: return RichPage::TableAlignment::Center;
	case Source::Right: return RichPage::TableAlignment::Right;
	case Source::Left:
	case Source::Default: break;
	}
	return RichPage::TableAlignment::Left;
}

[[nodiscard]] TextWithEntities ConvertImportedText(TextWithTags text) {
	return {
		std::move(text.text),
		TextUtilities::ConvertTextTagsToEntities(text.tags),
	};
}

[[nodiscard]] RichPage::Block ConvertTable(
		const TextUtilities::HtmlTable &table) {
	auto result = RichPage::Block();
	result.kind = RichPage::BlockKind::Table;
	result.bordered = true;
	result.text.text = ConvertImportedText(table.caption);
	result.tableRows.reserve(table.rows.size());
	for (const auto &row : table.rows) {
		auto converted = RichPage::TableRow();
		converted.cells.reserve(row.cells.size());
		for (const auto &cell : row.cells) {
			converted.cells.push_back({
				.text = {
					.text = {
						cell.text.text,
						TextUtilities::ConvertTextTagsToEntities(
							cell.text.tags),
					},
				},
				.colspan = cell.colspan,
				.rowspan = cell.rowspan,
				.header = cell.header,
				.alignment = ConvertAlignment(cell.alignment),
			});
		}
		result.tableRows.push_back(std::move(converted));
	}
	return result;
}

[[nodiscard]] RichPage::TaskState ConvertTaskState(
		TextUtilities::HtmlTaskState state) {
	using Source = TextUtilities::HtmlTaskState;
	switch (state) {
	case Source::Unchecked: return RichPage::TaskState::Unchecked;
	case Source::Checked: return RichPage::TaskState::Checked;
	case Source::None: break;
	}
	return RichPage::TaskState::None;
}

[[nodiscard]] std::vector<RichPage::Block> ConvertImportedBlocks(
	std::vector<TextUtilities::HtmlBlock> blocks);

[[nodiscard]] RichPage::Block ConvertImportedBlock(
		TextUtilities::HtmlBlock block) {
	using Kind = TextUtilities::HtmlBlockKind;
	auto result = RichPage::Block();
	result.anchorId = std::move(block.anchorId);
	switch (block.kind) {
	case Kind::Paragraph:
		result.kind = RichPage::BlockKind::Paragraph;
		result.text.text = ConvertImportedText(std::move(block.text));
		break;
	case Kind::Heading:
		result.kind = RichPage::BlockKind::Heading;
		result.headingLevel = std::clamp(block.headingLevel, 1, 6);
		result.text.text = ConvertImportedText(std::move(block.text));
		break;
	case Kind::Divider:
		result.kind = RichPage::BlockKind::Divider;
		break;
	case Kind::Quote:
	case Kind::Pullquote:
		result.kind = RichPage::BlockKind::Quote;
		result.pullquote = (block.kind == Kind::Pullquote);
		result.text.text = ConvertImportedText(std::move(block.text));
		result.caption.text = ConvertImportedText(std::move(block.caption));
		result.blocks = ConvertImportedBlocks(std::move(block.children));
		break;
	case Kind::Code:
		result.kind = RichPage::BlockKind::Code;
		result.language = std::move(block.language);
		result.text.text = ConvertImportedText(std::move(block.text));
		break;
	case Kind::Footer:
		result.kind = RichPage::BlockKind::Footer;
		result.text.text = ConvertImportedText(std::move(block.text));
		break;
	case Kind::List: {
		result.kind = RichPage::BlockKind::List;
		result.listKind = (block.listKind
			== TextUtilities::HtmlListKind::Ordered)
			? RichPage::ListKind::Ordered
			: RichPage::ListKind::Bullet;
		if (result.listKind == RichPage::ListKind::Ordered) {
			result.orderedList = RichPage::OrderedListData{
				.reversed = block.listReversed,
				.start = block.listStart,
				.type = (block.listType.isEmpty()
					? std::optional<QString>()
					: std::make_optional(block.listType)),
			};
		}
		result.listItems.reserve(block.items.size());
		for (auto &item : block.items) {
			auto converted = RichPage::ListItem();
			converted.taskState = ConvertTaskState(item.taskState);
			converted.number = RichPage::OrderedListItemData{
				.value = item.value,
			};
			converted.anchorId = std::move(item.anchorId);
			converted.text.text = ConvertImportedText(std::move(item.text));
			converted.blocks = ConvertImportedBlocks(std::move(item.blocks));
			result.listItems.push_back(std::move(converted));
		}
	} break;
	case Kind::Details:
		result.kind = RichPage::BlockKind::Details;
		result.open = block.detailsOpen;
		result.text.text = ConvertImportedText(std::move(block.text));
		result.blocks = ConvertImportedBlocks(std::move(block.children));
		if (result.blocks.empty()) {
			auto paragraph = RichPage::Block();
			paragraph.kind = RichPage::BlockKind::Paragraph;
			result.blocks.push_back(std::move(paragraph));
		}
		break;
	case Kind::Table:
		if (block.table) {
			result = ConvertTable(*block.table);
		}
		break;
	}
	return result;
}

std::vector<RichPage::Block> ConvertImportedBlocks(
		std::vector<TextUtilities::HtmlBlock> blocks) {
	auto result = std::vector<RichPage::Block>();
	result.reserve(blocks.size());
	for (auto &block : blocks) {
		auto converted = ConvertImportedBlock(std::move(block));
		if (converted.kind != RichPage::BlockKind::Unsupported) {
			result.push_back(std::move(converted));
		}
	}
	return result;
}

[[nodiscard]] TextUtilities::HtmlBlocksLimits BlocksImportLimitsFor(
		const RichMessageLimits &limits,
		int usedBlocks) {
	const auto blocks = limits.maxBlocks - usedBlocks - 1;
	const auto columns = limits.maxTableCols;
	if (blocks <= 0 || columns <= 0) {
		return { .maxBlocks = 0 };
	}
	const auto cells = std::min(
		int64(blocks) * columns,
		int64(std::numeric_limits<int>::max()));
	return {
		.maxBlocks = blocks,
		.maxDepth = limits.maxDepth,
		.maxBlockLength = limits.lengthLimit,
		.maxTotalLength = limits.lengthLimit,
		.table = {
			.maxRows = blocks,
			.maxColumns = columns,
			.maxCells = int(cells),
			.maxCellLength = kMaxCellLength,
		},
	};
}

[[nodiscard]] std::vector<std::vector<QString>> ParseDelimited(
		const QString &text,
		QChar delimiter,
		const TableImportLimits &limits,
		bool *truncated) {
	auto result = std::vector<std::vector<QString>>();
	auto row = std::vector<QString>();
	auto field = QString();
	auto quoted = false;
	auto cells = 0;
	const auto appendToField = [&](QChar ch) {
		if (field.size() < limits.maxCellLength) {
			field.append(ch);
		} else {
			*truncated = true;
		}
	};
	const auto finishField = [&] {
		row.push_back(base::take(field));
		++cells;
	};
	const auto finishRow = [&] {
		finishField();
		result.push_back(base::take(row));
	};
	for (auto i = 0, size = int(text.size()); i != size; ++i) {
		const auto ch = text[i];
		if (quoted) {
			if (ch != '"') {
				appendToField(ch);
			} else if (i + 1 != size && text[i + 1] == '"') {
				appendToField(ch);
				++i;
			} else {
				quoted = false;
			}
			continue;
		} else if (ch == '"' && field.isEmpty()) {
			quoted = true;
			continue;
		} else if (ch == delimiter) {
			finishField();
			if (cells >= limits.maxCells) {
				*truncated = true;
				result.push_back(base::take(row));
				return result;
			}
			continue;
		} else if (ch != '\r' && ch != '\n') {
			appendToField(ch);
			continue;
		}
		if (ch == '\r' && i + 1 != size && text[i + 1] == '\n') {
			++i;
		}
		finishRow();
		if ((int(result.size()) >= limits.maxRows)
			|| (cells >= limits.maxCells)) {
			if (i + 1 != size) {
				*truncated = true;
			}
			return result;
		}
	}
	if (!field.isEmpty() || !row.empty()) {
		finishRow();
	}
	return result;
}

[[nodiscard]] std::optional<TableImportResult> TableFromDelimitedText(
		const QString &text,
		const TableImportLimits &limits) {
	auto truncated = false;
	auto rows = ParseDelimited(text, u'\t', limits, &truncated);
	if (rows.empty()) {
		return std::nullopt;
	}
	auto columns = 0;
	for (const auto &row : rows) {
		columns = std::max(columns, int(row.size()));
	}
	if (columns > limits.maxColumns) {
		columns = limits.maxColumns;
		truncated = true;
	}
	if (columns < 1 || (rows.size() < 2 && columns < 2)) {
		return std::nullopt;
	}
	auto block = RichPage::Block();
	block.kind = RichPage::BlockKind::Table;
	block.bordered = true;
	block.tableRows.reserve(rows.size());
	for (auto &fields : rows) {
		auto row = RichPage::TableRow();
		row.cells.reserve(columns);
		for (auto i = 0; i != columns; ++i) {
			auto cell = RichPage::TableCell();
			if (i < int(fields.size())) {
				cell.text.text.text = std::move(fields[i]);
			}
			row.cells.push_back(std::move(cell));
		}
		block.tableRows.push_back(std::move(row));
	}
	return TableImportResult{
		.block = std::move(block),
		.truncated = truncated,
	};
}

} // namespace

TableImportLimits TableImportLimitsFor(
		const RichMessageLimits &limits,
		int usedBlocks) {
	const auto rows = limits.maxBlocks - usedBlocks - 1;
	const auto columns = limits.maxTableCols;
	if (rows <= 0 || columns <= 0) {
		return TableImportLimits();
	}
	const auto cells = std::min(
		int64(rows) * columns,
		int64(std::numeric_limits<int>::max()));
	return {
		.maxRows = rows,
		.maxColumns = columns,
		.maxCells = int(cells),
		.maxCellLength = kMaxCellLength,
	};
}

bool MimeDataLooksLikeTable(not_null<const QMimeData*> data) {
	if (data->hasHtml()) {
		return TextUtilities::HtmlContainsTable(data->html());
	}
	return data->hasText()
		&& HasSpreadsheetHint(data)
		&& data->text().contains(u'\t');
}

std::optional<TableImportResult> TableFromMimeData(
		not_null<const QMimeData*> data,
		const TableImportLimits &limits) {
	if ((limits.maxRows <= 0)
		|| (limits.maxColumns <= 0)
		|| (limits.maxCells <= 0)) {
		return std::nullopt;
	}
	if (data->hasHtml()) {
		const auto html = data->html();
		auto parsed = TextUtilities::TableFromHtml(html, {
			.maxRows = limits.maxRows,
			.maxColumns = limits.maxColumns,
			.maxCells = limits.maxCells,
			.maxCellLength = limits.maxCellLength,
		});
		if (!parsed) {
			return std::nullopt;
		} else if (parsed->rows.size() < 2 && parsed->columns < 2) {
			return std::nullopt;
		} else if (!TableCoversWholeHtml(html, *parsed)) {
			return std::nullopt;
		}
		return TableImportResult{
			.block = ConvertTable(*parsed),
			.truncated = parsed->truncated,
		};
	} else if (!data->hasText() || !HasSpreadsheetHint(data)) {
		return std::nullopt;
	}
	return TableFromDelimitedText(data->text(), limits);
}

std::optional<BlocksImportResult> BlocksFromMimeData(
		not_null<const QMimeData*> data,
		const RichMessageLimits &limits,
		int usedBlocks) {
	if (!data->hasHtml()) {
		return std::nullopt;
	}
	auto parsed = TextUtilities::BlocksFromHtml(
		data->html(),
		BlocksImportLimitsFor(limits, usedBlocks));
	if (!parsed) {
		return std::nullopt;
	}
	auto blocks = ConvertImportedBlocks(std::move(parsed->blocks));
	if (blocks.empty()) {
		return std::nullopt;
	}
	return BlocksImportResult{
		.blocks = std::move(blocks),
		.truncated = parsed->truncated,
	};
}

} // namespace Iv::Editor
