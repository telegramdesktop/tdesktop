/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/iv_markdown_prepare_native_blocks.h"
#include "base/unixtime.h"
#include "iv/iv_rich_page.h"
#include "iv/markdown/iv_markdown_prepare_links.h"
#include "lang/lang_keys.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace Iv::Markdown {
namespace {

using RichPage = Iv::RichPage;
using RichPageBlock = Iv::RichPage::Block;
using RichPageBlockKind = Iv::RichPage::BlockKind;
using RichPageListItem = Iv::RichPage::ListItem;
using RichPageRelatedArticle = Iv::RichPage::RelatedArticle;
using RichPageTableCell = Iv::RichPage::TableCell;
using RichPageTableRow = Iv::RichPage::TableRow;

struct NativeIvDepthContext {
	int listDepth = 0;
	int quoteDepth = 0;
};

[[nodiscard]] int CappedNativeIvListDepth(int depth) {
	return std::min(depth, std::max(PrepareLimitsForIv().visualListDepth, 0));
}

[[nodiscard]] int CappedNativeIvQuoteDepth(int depth) {
	return std::min(depth, std::max(PrepareLimitsForIv().visualQuoteDepth, 0));
}

[[nodiscard]] bool PrepareCanonicalNativeIvBlocks(
	const std::vector<RichPageBlock> &blocks,
	std::vector<PreparedBlock> *result,
	NativeIvPrepareState *state,
	PreparedEditBlockContainerPath container,
	NativeIvDepthContext depthContext);

[[nodiscard]] QString NativeIvDateText(TimeId date) {
	return langDateTimeFull(base::unixtime::parse(date));
}

[[nodiscard]] int ScaleNativeIvFormulaCap(
		int cap,
		int textSize,
		int baseTextSize) {
	if (cap <= 0) {
		return 0;
	}
	const auto numerator = int64(cap) * std::max(textSize, 1);
	const auto denominator = std::max(baseTextSize, 1);
	return std::max(int((numerator + denominator - 1) / denominator), 1);
}

[[nodiscard]] NativeIvRichTextContext NativeIvRichTextContextForTextSize(
		int textSize,
		const MarkdownPrepareDimensions &dimensions) {
	return {
		.textSize = textSize,
		.renderWidthCap = ScaleNativeIvFormulaCap(
			dimensions.displayMathMaxRenderWidth,
			textSize,
			dimensions.displayMathTextSize),
		.renderHeightCap = ScaleNativeIvFormulaCap(
			dimensions.displayMathMaxRenderHeight,
			textSize,
			dimensions.displayMathTextSize),
	};
}

[[nodiscard]] int NativeIvFlowTextSize(
		PreparedBlockKind kind,
		int headingLevel,
		const MarkdownPrepareDimensions &dimensions) {
	if (kind == PreparedBlockKind::Heading
		&& headingLevel >= 1
		&& headingLevel <= int(dimensions.headingTextSizes.size())) {
		return dimensions.headingTextSizes[headingLevel - 1];
	}
	return dimensions.bodyTextSize;
}

[[nodiscard]] PreparedLink PrepareNativeIvRelatedArticleLink(
		QString url,
		uint64 webpageId,
		QStringView renderedText) {
	auto result = PreparedLink();
	if (webpageId) {
		result.kind = PreparedLinkKind::InstantViewPage;
		result.webpageId = webpageId;
		NormalizePreparedUrlLink(&result, url);
	} else {
		result = ClassifiedLink(0, url, nullptr);
		if (result.kind == PreparedLinkKind::RejectedRelative
			|| result.kind == PreparedLinkKind::LocalFile) {
			result.kind = PreparedLinkKind::External;
			NormalizePreparedUrlLink(&result, url);
		}
	}
	FinalizePreparedUrlLink(&result, renderedText);
	return result;
}

[[nodiscard]] QString NativeIvDetailsAnchorId(NativeIvPrepareState *state) {
	return u"details-"_q + QString::number(++state->nextGeneratedId);
}

void SortPreparedIvRichText(PreparedIvRichText *text) {
	SortEntities(&text->text);
}

[[nodiscard]] TextWithEntities StripOneTrailingNewline(TextWithEntities text) {
	auto removed = 0;
	if (text.text.endsWith(u"\r\n"_q)) {
		removed = 2;
	} else if (!text.text.isEmpty()) {
		const auto last = text.text.back();
		if ((last == QChar(u'\n')) || (last == QChar(u'\r'))) {
			removed = 1;
		}
	}
	if (!removed) {
		return text;
	}
	const auto newLength = text.text.size() - removed;
	text.text.chop(removed);
	for (auto &entity : text.entities) {
		entity.updateTextEnd(newLength);
	}
	text.entities.erase(
		std::remove_if(
			text.entities.begin(),
			text.entities.end(),
			[](const EntityInText &entity) {
				return (entity.length() <= 0);
			}),
		text.entities.end());
	return text;
}

[[nodiscard]] bool RichTextHasMeaningfulContent(
		const RichPage::RichText &text) {
	return !text.text.text.trimmed().isEmpty()
		|| !text.anchorId.isEmpty()
		|| !text.anchorIds.empty();
}

[[nodiscard]] PreparedBlock EmptyParagraphBlock() {
	auto block = PreparedBlock();
	block.kind = PreparedBlockKind::Paragraph;
	return block;
}

[[nodiscard]] PreparedBlock PrepareNativeIvEmbedPostFallbackParagraph(
		QString url) {
	auto block = EmptyParagraphBlock();
	if (url.isEmpty()) {
		return block;
	}
	auto link = PreparedLink();
	link.index = 1;
	link.kind = PreparedLinkKind::External;
	NormalizePreparedUrlLink(&link, url);
	FinalizePreparedUrlLink(&link, url);
	block.text.text = std::move(url);
	block.text.entities.push_back(EntityInText(
		EntityType::CustomUrl,
		0,
		block.text.text.size(),
		InternalLinkData(link.index)));
	block.links.push_back(std::move(link));
	return block;
}

[[nodiscard]] PreparedBlock PrepareRuleBlock() {
	auto block = PreparedBlock();
	block.kind = PreparedBlockKind::Rule;
	return block;
}

void WrapPreparedIvRichTextItalic(PreparedIvRichText *prepared) {
	if (!prepared || prepared->text.text.isEmpty()) {
		return;
	}
	prepared->text.entities.push_back(EntityInText(
		EntityType::Italic,
		0,
		prepared->text.text.size()));
}

void ApplyNativeIvQuoteEditPlaceholderText(
	PreparedBlock *block,
	bool quoteAuthor);

bool AppendPreparedQuoteParagraph(
		std::vector<PreparedBlock> *result,
		PreparedIvRichText prepared,
		bool pullquote,
		bool supplementary = false,
		bool allowEmpty = false,
		bool quoteAuthor = false,
		std::optional<PreparedEditLeafSource> editLeaf = std::nullopt) {
	if (pullquote && !quoteAuthor) {
		WrapPreparedIvRichTextItalic(&prepared);
	}
	const auto count = result->size();
	if (!AppendPreparedIvRichBlock(
			result,
			PreparedBlockKind::Paragraph,
			0,
			std::move(prepared),
			QString(),
			allowEmpty,
			supplementary,
			std::nullopt,
			std::move(editLeaf))) {
		return false;
	}
	if (result->size() > count) {
		result->back().quoteAuthor = quoteAuthor;
		if (pullquote) {
			result->back().flowAlignment = TableAlignment::Center;
			result->back().pullquote = true;
		}
		if (allowEmpty) {
			ApplyNativeIvQuoteEditPlaceholderText(
				&result->back(),
				quoteAuthor);
		}
	}
	return true;
}

[[nodiscard]] PreparedOrderedListType ResolvePreparedOrderedListType(
		const std::optional<QString> &type) {
	if (!type.has_value()) {
		return PreparedOrderedListType::Decimal;
	}
	const auto &value = *type;
	if (value == u"a"_q
		|| value.compare(u"lower-alpha"_q, Qt::CaseInsensitive) == 0
		|| value.compare(u"lower-latin"_q, Qt::CaseInsensitive) == 0) {
		return PreparedOrderedListType::LowerAlpha;
	} else if (value == u"A"_q
		|| value.compare(u"upper-alpha"_q, Qt::CaseInsensitive) == 0
		|| value.compare(u"upper-latin"_q, Qt::CaseInsensitive) == 0) {
		return PreparedOrderedListType::UpperAlpha;
	} else if (value == u"i"_q
		|| value.compare(u"lower-roman"_q, Qt::CaseInsensitive) == 0) {
		return PreparedOrderedListType::LowerRoman;
	} else if (value == u"I"_q
		|| value.compare(u"upper-roman"_q, Qt::CaseInsensitive) == 0) {
		return PreparedOrderedListType::UpperRoman;
	}
	return PreparedOrderedListType::Decimal;
}

[[nodiscard]] QString PreparedOrderedAlphaText(int value, bool upper) {
	if (value <= 0) {
		return QString::number(value);
	}
	auto result = QString();
	while (value > 0) {
		--value;
		result.prepend(QChar((upper ? 'A' : 'a') + (value % 26)));
		value /= 26;
	}
	return result;
}

[[nodiscard]] QString PreparedOrderedRomanText(int value, bool upper) {
	if (value <= 0) {
		return QString::number(value);
	}
	struct RomanPart {
		int value = 0;
		const char *text = nullptr;
	};
	static constexpr RomanPart kParts[] = {
		RomanPart{ 1000, "M" },
		RomanPart{ 900, "CM" },
		RomanPart{ 500, "D" },
		RomanPart{ 400, "CD" },
		RomanPart{ 100, "C" },
		RomanPart{ 90, "XC" },
		RomanPart{ 50, "L" },
		RomanPart{ 40, "XL" },
		RomanPart{ 10, "X" },
		RomanPart{ 9, "IX" },
		RomanPart{ 5, "V" },
		RomanPart{ 4, "IV" },
		RomanPart{ 1, "I" },
	};
	auto result = QString();
	for (const auto &part : kParts) {
		while (value >= part.value) {
			result += QString::fromLatin1(part.text);
			value -= part.value;
		}
	}
	return upper ? result : result.toLower();
}

[[nodiscard]] QString FormatPreparedOrderedMarkerBody(
		int value,
		PreparedOrderedListType type) {
	switch (type) {
	case PreparedOrderedListType::LowerAlpha:
		return PreparedOrderedAlphaText(value, false);
	case PreparedOrderedListType::UpperAlpha:
		return PreparedOrderedAlphaText(value, true);
	case PreparedOrderedListType::LowerRoman:
		return PreparedOrderedRomanText(value, false);
	case PreparedOrderedListType::UpperRoman:
		return PreparedOrderedRomanText(value, true);
	case PreparedOrderedListType::Decimal:
		return QString::number(value);
	}
	return QString::number(value);
}

[[nodiscard]] QString FormatPreparedOrderedMarkerText(
		int value,
		PreparedOrderedListType type,
		ListDelimiter delimiter) {
	const auto suffix = (delimiter == ListDelimiter::Parenthesis)
		? u")"_q
		: u"."_q;
	return FormatPreparedOrderedMarkerBody(value, type) + suffix;
}

class NativeIvOrderedMarkerFormatter {
public:
	NativeIvOrderedMarkerFormatter(
		const PreparedBlock &list,
		bool editMode)
	: _listType(list.orderedType)
	, _delimiter(list.listDelimiter)
	, _reversed(list.orderedReversed)
	, _nextValue(list.startNumber)
	, _editMode(editMode) {
	}

	void apply(
			const RichPageListItem &item,
			PreparedBlock *block) {
		const auto type = item.number.type.has_value()
			? ResolvePreparedOrderedListType(item.number.type)
			: _listType;
		const auto value = item.number.value.value_or(_nextValue);
		const auto raw = item.number.rawText();
		block->orderedType = type;
		block->orderedReversed = _reversed;
		block->orderedNumber = value;
		block->articleOrderedMarkerText = FormatPreparedOrderedRawMarkerText(
			raw,
			_delimiter);
		block->orderedMarkerText = _editMode
			? FormatPreparedOrderedMarkerText(value, type, _delimiter)
			: raw.isEmpty()
			? FormatPreparedOrderedMarkerText(value, type, _delimiter)
			: QString();
		_nextValue = value + (_reversed ? -1 : 1);
	}

private:
	PreparedOrderedListType _listType = PreparedOrderedListType::Decimal;
	ListDelimiter _delimiter = ListDelimiter::Period;
	bool _reversed = false;
	int _nextValue = 1;
	bool _editMode = false;
};

using NativeIvTableOccupancyRow = std::vector<char>;
using NativeIvTableOccupancyGrid = std::vector<NativeIvTableOccupancyRow>;

[[nodiscard]] int NormalizeNativeIvTableSpan(int span) {
	return std::max(span, 1);
}

[[nodiscard]] int ClampNativeIvTableRowspan(
		int rawRowspan,
		int row,
		int rowCount) {
	if ((row < 0) || (row >= rowCount) || (rowCount <= 0)) {
		return 0;
	}
	const auto remainingRows = int64(rowCount) - row;
	return int(std::min<int64>(
		NormalizeNativeIvTableSpan(rawRowspan),
		remainingRows));
}

[[nodiscard]] int ClampNativeIvTableColspan(
		int rawColspan,
		int column,
		int maxColumns) {
	if ((column < 0) || (column >= maxColumns) || (maxColumns <= 0)) {
		return 0;
	}
	const auto remainingColumns = int64(maxColumns) - column;
	return int(std::min<int64>(
		NormalizeNativeIvTableSpan(rawColspan),
		remainingColumns));
}

[[nodiscard]] bool CanOccupyNativeIvTableSlots(
		const NativeIvTableOccupancyGrid &occupancy,
		int row,
		int column,
		int rowspan,
		int colspan) {
	if ((row < 0)
		|| (column < 0)
		|| (rowspan <= 0)
		|| (colspan <= 0)
		|| (row >= int(occupancy.size()))) {
		return false;
	}
	const auto rowLimit = int(std::min<int64>(
		int64(row) + rowspan,
		occupancy.size()));
	const auto columnLimit64 = int64(column) + colspan;
	if (columnLimit64 <= column) {
		return false;
	}
	const auto columnLimit = int(std::min<int64>(
		columnLimit64,
		std::numeric_limits<int>::max()));
	for (auto currentRow = row; currentRow < rowLimit; ++currentRow) {
		const auto &occupied = occupancy[currentRow];
		const auto occupiedLimit = std::min(columnLimit, int(occupied.size()));
		for (auto currentColumn = column;
			currentColumn < occupiedLimit;
			++currentColumn) {
			if (occupied[currentColumn]) {
				return false;
			}
		}
	}
	return true;
}

[[nodiscard]] int FirstAvailableNativeIvTableColumn(
		const NativeIvTableOccupancyGrid &occupancy,
		int row,
		int rowspan,
		int colspan,
		int maxColumns) {
	if ((row < 0)
		|| (row >= int(occupancy.size()))
		|| (rowspan <= 0)
		|| (colspan <= 0)
		|| (maxColumns <= 0)) {
		return -1;
	}
	for (auto column = 0; column < maxColumns; ++column) {
		const auto effectiveColspan = ClampNativeIvTableColspan(
			colspan,
			column,
			maxColumns);
		if (effectiveColspan <= 0) {
			continue;
		}
		if (CanOccupyNativeIvTableSlots(
				occupancy,
				row,
				column,
				rowspan,
				effectiveColspan)) {
			return column;
		}
	}
	return -1;
}

void MarkNativeIvTableSlots(
		NativeIvTableOccupancyGrid *occupancy,
		int row,
		int column,
		int rowspan,
		int colspan) {
	if ((row < 0)
		|| (column < 0)
		|| (rowspan <= 0)
		|| (colspan <= 0)
		|| (row >= int(occupancy->size()))) {
		return;
	}
	const auto rowLimit = int(std::min<int64>(
		int64(row) + rowspan,
		occupancy->size()));
	const auto columnLimit64 = int64(column) + colspan;
	if (columnLimit64 <= column) {
		return;
	}
	const auto columnLimit = int(std::min<int64>(
		columnLimit64,
		std::numeric_limits<int>::max()));
	for (auto currentRow = row; currentRow < rowLimit; ++currentRow) {
		auto &occupied = (*occupancy)[currentRow];
		if (columnLimit > int(occupied.size())) {
			occupied.resize(columnLimit, false);
		}
		for (auto currentColumn = column;
			currentColumn < columnLimit;
			++currentColumn) {
			occupied[currentColumn] = true;
		}
	}
}

[[nodiscard]] int NativeIvTableColumnCount(
		const NativeIvTableOccupancyGrid &occupancy) {
	auto result = 0;
	for (const auto &row : occupancy) {
		result = std::max(result, int(row.size()));
	}
	return result;
}

[[maybe_unused]] [[nodiscard]] PreparedEditBlockContainerPath BlockChildrenContainer(
		PreparedEditBlockPath block) {
	auto result = std::move(block.container);
	result.steps.push_back({
		.kind = PreparedEditBlockContainerKind::BlockChildren,
		.blockIndex = block.index,
	});
	return result;
}

[[maybe_unused]] [[nodiscard]] PreparedEditBlockContainerPath ListItemChildrenContainer(
		PreparedEditBlockPath block,
		int listItemIndex) {
	auto result = std::move(block.container);
	result.steps.push_back({
		.kind = PreparedEditBlockContainerKind::ListItemChildren,
		.blockIndex = block.index,
		.listItemIndex = listItemIndex,
	});
	return result;
}

[[nodiscard]] PreparedEditBlockSource BlockSource(
		PreparedEditBlockPath block) {
	return { .path = std::move(block) };
}

[[maybe_unused]] [[nodiscard]] PreparedEditListItemSource ListItemSource(
		PreparedEditBlockPath block,
		int listItemIndex) {
	return {
		.block = std::move(block),
		.listItemIndex = listItemIndex,
	};
}

[[maybe_unused]] [[nodiscard]] PreparedEditTableRowSource TableRowSource(
		PreparedEditBlockPath block,
		int tableRowIndex) {
	return {
		.block = std::move(block),
		.tableRowIndex = tableRowIndex,
	};
}

[[maybe_unused]] [[nodiscard]] PreparedEditTableCellSource TableCellSource(
		PreparedEditBlockPath block,
		int tableRowIndex,
		int tableCellIndex,
		int column,
		int colspan,
		int rowspan) {
	return {
		.block = std::move(block),
		.tableRowIndex = tableRowIndex,
		.tableCellIndex = tableCellIndex,
		.column = column,
		.colspan = colspan,
		.rowspan = rowspan,
	};
}

[[nodiscard]] PreparedEditLeafSource BlockTextLeafSource(
		PreparedEditBlockPath block) {
	return {
		.kind = PreparedEditLeafKind::BlockText,
		.block = std::move(block),
	};
}

[[nodiscard]] PreparedEditLeafSource BlockCaptionLeafSource(
		PreparedEditBlockPath block) {
	return {
		.kind = PreparedEditLeafKind::BlockCaption,
		.block = std::move(block),
	};
}

[[nodiscard]] PreparedEditLeafSource ListItemTextLeafSource(
		PreparedEditBlockPath block,
		int listItemIndex) {
	return {
		.kind = PreparedEditLeafKind::ListItemText,
		.block = std::move(block),
		.listItemIndex = listItemIndex,
	};
}

[[maybe_unused]] [[nodiscard]] PreparedEditLeafSource TableCellTextLeafSource(
		PreparedEditBlockPath block,
		int tableRowIndex,
		int tableCellIndex) {
	return {
		.kind = PreparedEditLeafKind::TableCellText,
		.block = std::move(block),
		.tableRowIndex = tableRowIndex,
		.tableCellIndex = tableCellIndex,
	};
}

[[nodiscard]] PreparedEditLeafSource MathFormulaLeafSource(
		PreparedEditBlockPath block) {
	return {
		.kind = PreparedEditLeafKind::MathFormula,
		.block = std::move(block),
	};
}

void ApplyBlockCaptionEditSource(
		PreparedBlock *block,
		PreparedEditBlockPath path) {
	block->editBlock = BlockSource(path);
	block->editLeaf = BlockCaptionLeafSource(path);
}

[[nodiscard]] QString NativeIvEditPlaceholderText(
		PreparedBlockKind kind,
		PreparedEditLeafKind leafKind,
		int headingLevel) {
	switch (leafKind) {
	case PreparedEditLeafKind::BlockCaption:
		return tr::lng_photo_caption(tr::now);
	case PreparedEditLeafKind::TableCellText:
		return tr::lng_article_placeholder_cell(tr::now);
	case PreparedEditLeafKind::MathFormula:
		return u"x^2 + y^2"_q;
	case PreparedEditLeafKind::BlockText:
		if (kind == PreparedBlockKind::Table) {
			return tr::lng_article_placeholder_title(tr::now);
		} else if (kind == PreparedBlockKind::Heading) {
			return HeadingLevelLabel(headingLevel);
		} else if (kind == PreparedBlockKind::Details) {
			return tr::lng_article_table_header(tr::now);
		}
		return QString();
	}
	return QString();
}

void ApplyNativeIvEditPlaceholderText(PreparedBlock *block) {
	if (!block->editLeaf) {
		return;
	} else if (block->quoteAuthor
		&& (block->editLeaf->kind == PreparedEditLeafKind::BlockCaption)) {
		block->editPlaceholderText = tr::lng_article_placeholder_author(tr::now);
		return;
	} else if (block->footer
		&& (block->editLeaf->kind == PreparedEditLeafKind::BlockText)) {
		block->editPlaceholderText = tr::lng_article_insert_footer(tr::now);
		return;
	}
	block->editPlaceholderText = NativeIvEditPlaceholderText(
		block->kind,
		block->editLeaf->kind,
		block->headingLevel);
}

void ApplyNativeIvQuoteEditPlaceholderText(
		PreparedBlock *block,
		bool quoteAuthor) {
	block->editPlaceholderText = quoteAuthor
		? tr::lng_article_placeholder_author(tr::now)
		: tr::lng_article_placeholder_quote(tr::now);
}

void RefreshPreparedNativeIvQuotePlaceholder(
		PreparedBlock *block,
		NativeIvPrepareState *state) {
	block->editPlaceholderText = QString();
	if (state->editMode && block->editLeaf) {
		ApplyNativeIvQuoteEditPlaceholderText(block, block->quoteAuthor);
	}
}

void ApplyNativeIvEditPlaceholderText(PreparedTableCell *cell) {
	if (!cell->editLeaf) {
		return;
	}
	cell->editPlaceholderText = cell->header
		? tr::lng_article_table_header(tr::now)
		: NativeIvEditPlaceholderText(
			PreparedBlockKind::Table,
			cell->editLeaf->kind,
			0);
}

[[nodiscard]] const std::vector<RichPageBlock> *ResolveCanonicalNativeIvContainer(
		const RichPage &page,
		const PreparedEditBlockContainerPath &container) {
	auto blocks = &page.blocks;
	for (const auto &step : container.steps) {
		if (step.kind == PreparedEditBlockContainerKind::Root) {
			blocks = &page.blocks;
			continue;
		}
		if (step.blockIndex < 0 || step.blockIndex >= int(blocks->size())) {
			return nullptr;
		}
		const auto &block = (*blocks)[step.blockIndex];
		switch (step.kind) {
		case PreparedEditBlockContainerKind::BlockChildren:
			blocks = &block.blocks;
			break;
		case PreparedEditBlockContainerKind::ListItemChildren:
			if (step.listItemIndex < 0
				|| step.listItemIndex >= int(block.listItems.size())) {
				return nullptr;
			}
			blocks = &block.listItems[step.listItemIndex].blocks;
			break;
		case PreparedEditBlockContainerKind::Root:
			blocks = &page.blocks;
			break;
		}
	}
	return blocks;
}

[[nodiscard]] const RichPageBlock *ResolveCanonicalNativeIvBlock(
		const RichPage &page,
		const PreparedEditBlockPath &path) {
	const auto container = ResolveCanonicalNativeIvContainer(page, path.container);
	if (!container || path.index < 0 || path.index >= int(container->size())) {
		return nullptr;
	}
	return &(*container)[path.index];
}

[[nodiscard]] PreparedBlock *FindPreparedNativeIvBlockByPath(
		std::vector<PreparedBlock> *blocks,
		const PreparedEditBlockPath &path) {
	for (auto &block : *blocks) {
		if (block.editBlock && block.editBlock->path == path) {
			return &block;
		}
		if (const auto nested = FindPreparedNativeIvBlockByPath(
				&block.children,
				path)) {
			return nested;
		}
	}
	return nullptr;
}

[[nodiscard]] PreparedBlock *FindPreparedNativeIvLeafBlock(
		PreparedBlock *block,
		const PreparedEditLeafSource &source) {
	if (!block) {
		return nullptr;
	}
	if (block->editLeaf && (*block->editLeaf == source)) {
		return block;
	}
	for (auto &child : block->children) {
		if (const auto nested = FindPreparedNativeIvLeafBlock(&child, source)) {
			return nested;
		}
	}
	return nullptr;
}

[[nodiscard]] PreparedTableCell *FindPreparedNativeIvLeafCell(
		PreparedBlock *block,
		const PreparedEditLeafSource &source) {
	if (!block) {
		return nullptr;
	}
	for (auto &row : block->tableRows) {
		for (auto &cell : row.cells) {
			if (cell.editLeaf && (*cell.editLeaf == source)) {
				return &cell;
			}
		}
	}
	return nullptr;
}

void StripPreparedDetailsSummaryLinks(PreparedIvRichText *summary) {
	if (summary->links.empty()) {
		return;
	}
	const auto from = std::remove_if(
		summary->text.entities.begin(),
		summary->text.entities.end(),
		[](const EntityInText &entity) {
			return entity.type() == EntityType::CustomUrl;
		});
	summary->text.entities.erase(from, summary->text.entities.end());
	summary->links.clear();
}

void RefreshPreparedNativeIvBlockPlaceholder(
		PreparedBlock *block,
		NativeIvPrepareState *state) {
	block->editPlaceholderText = QString();
	if (state->editMode && block->editLeaf) {
		ApplyNativeIvEditPlaceholderText(block);
	}
}

void RefreshPreparedNativeIvCellPlaceholder(
		PreparedTableCell *cell,
		NativeIvPrepareState *state) {
	cell->editPlaceholderText = QString();
	if (state->editMode && cell->editLeaf) {
		ApplyNativeIvEditPlaceholderText(cell);
	}
}

void RefreshPreparedNativeIvMediaCaptionPlaceholder(
		PreparedBlock *block,
		NativeIvPrepareState *state) {
	block->editPlaceholderText = QString();
	if (state->editMode && block->editLeaf) {
		ApplyNativeIvEditPlaceholderText(block);
	}
}

void RefreshPreparedNativeIvPlaceholderCopyText(PreparedBlock *block) {
	if (block->kind != PreparedBlockKind::Placeholder) {
		return;
	}
	block->placeholder.copyText = block->text.text.isEmpty()
		? block->placeholder.label
		: (block->placeholder.label + u"\n"_q + block->text.text);
}

[[nodiscard]] bool PreparePreparedNativeIvRichText(
		const RichPage::RichText &text,
		PreparedIvRichText *prepared,
		NativeIvPrepareState *state,
		NativeIvRichTextContext context = {}) {
	return PrepareNativeIvRichText(text, prepared, nullptr, state, context);
}

[[nodiscard]] NativeInstantViewLeafUpdateResult UpdatePreparedNativeIvBlockText(
		PreparedBlock *preparedBlock,
		const RichPageBlock &canonicalBlock,
		const PreparedEditLeafSource &source,
		NativeIvPrepareState *state,
		NativeIvPreparedLeafFormulaRange *formulaRange) {
	formulaRange->from = state->nextFormulaIndex;
	formulaRange->till = state->nextFormulaIndex;
	switch (canonicalBlock.kind) {
	case RichPageBlockKind::Heading:
	case RichPageBlockKind::Paragraph:
	case RichPageBlockKind::Footer: {
		if (!preparedBlock->editLeaf || (*preparedBlock->editLeaf != source)) {
			return NativeInstantViewLeafUpdateResult::Unsupported;
		}
		auto prepared = PreparedIvRichText();
		const auto kind = (canonicalBlock.kind == RichPageBlockKind::Heading)
			? PreparedBlockKind::Heading
			: PreparedBlockKind::Paragraph;
		const auto context = NativeIvRichTextContextForTextSize(
			NativeIvFlowTextSize(
				kind,
				canonicalBlock.headingLevel,
				state->dimensions),
			state->dimensions);
		if (!PreparePreparedNativeIvRichText(
				canonicalBlock.text,
				&prepared,
				state,
				context)) {
			return NativeInstantViewLeafUpdateResult::Failed;
		}
		SortPreparedIvRichText(&prepared);
		preparedBlock->text = std::move(prepared.text);
		preparedBlock->links = std::move(prepared.links);
		RefreshPreparedNativeIvBlockPlaceholder(preparedBlock, state);
	} break;
	case RichPageBlockKind::Code: {
		if (!preparedBlock->editLeaf || (*preparedBlock->editLeaf != source)) {
			return NativeInstantViewLeafUpdateResult::Unsupported;
		}
		auto prepared = PreparedIvRichText();
		if (!PreparePreparedNativeIvRichText(
				canonicalBlock.text,
				&prepared,
				state,
				{ .dropClickHandlers = true })) {
			return NativeInstantViewLeafUpdateResult::Failed;
		}
		SortPreparedIvRichText(&prepared);
		preparedBlock->text = StripOneTrailingNewline(std::move(prepared.text));
		preparedBlock->links = std::move(prepared.links);
		RefreshPreparedNativeIvBlockPlaceholder(preparedBlock, state);
	} break;
	case RichPageBlockKind::Quote: {
		const auto target = FindPreparedNativeIvLeafBlock(preparedBlock, source);
		if (!target) {
			return NativeInstantViewLeafUpdateResult::Unsupported;
		}
		auto prepared = PreparedIvRichText();
		if (!PreparePreparedNativeIvRichText(
				canonicalBlock.text,
				&prepared,
				state)) {
			return NativeInstantViewLeafUpdateResult::Failed;
		}
		if (canonicalBlock.pullquote) {
			WrapPreparedIvRichTextItalic(&prepared);
		}
		SortPreparedIvRichText(&prepared);
		target->text = std::move(prepared.text);
		target->links = std::move(prepared.links);
		RefreshPreparedNativeIvQuotePlaceholder(target, state);
	} break;
	case RichPageBlockKind::Table: {
		if (!preparedBlock->editLeaf || (*preparedBlock->editLeaf != source)) {
			return NativeInstantViewLeafUpdateResult::Unsupported;
		}
		auto prepared = PreparedIvRichText();
		if (!PreparePreparedNativeIvRichText(
				canonicalBlock.text,
				&prepared,
				state)) {
			return NativeInstantViewLeafUpdateResult::Failed;
		}
		SortPreparedIvRichText(&prepared);
		preparedBlock->text = std::move(prepared.text);
		preparedBlock->links = std::move(prepared.links);
	} break;
	case RichPageBlockKind::Details: {
		if (!preparedBlock->editLeaf || (*preparedBlock->editLeaf != source)) {
			return NativeInstantViewLeafUpdateResult::Unsupported;
		}
		auto prepared = PreparedIvRichText();
		if (!PreparePreparedNativeIvRichText(
				canonicalBlock.text,
				&prepared,
				state)) {
			return NativeInstantViewLeafUpdateResult::Failed;
		}
		StripPreparedDetailsSummaryLinks(&prepared);
		SortPreparedIvRichText(&prepared);
		preparedBlock->text = std::move(prepared.text);
		preparedBlock->links = std::move(prepared.links);
		RefreshPreparedNativeIvBlockPlaceholder(preparedBlock, state);
	} break;
	case RichPageBlockKind::Math: {
		if (!preparedBlock->editLeaf || (*preparedBlock->editLeaf != source)) {
			return NativeInstantViewLeafUpdateResult::Unsupported;
		}
		if (canonicalBlock.formula.trimmed().isEmpty() && !state->editMode) {
			return NativeInstantViewLeafUpdateResult::Unsupported;
		}
		auto formulaIndex = preparedBlock->formulaIndex;
		if (formulaIndex >= 0 && formulaIndex < int(state->result.formulas.size())) {
			auto slot = PreparedFormulaSlot();
			slot.trimmedTex = canonicalBlock.formula.trimmed();
			slot.kind = MathKind::Display;
			slot.textSize = state->dimensions.displayMathTextSize;
			slot.renderWidthCap = state->dimensions.displayMathMaxRenderWidth;
			slot.renderHeightCap = state->dimensions.displayMathMaxRenderHeight;
			slot.present = true;
			state->result.formulas[formulaIndex] = std::move(slot);
		} else {
			auto prepared = PreparedBlock();
			prepared.kind = PreparedBlockKind::DisplayMath;
			prepared.formulaTex = canonicalBlock.formula;
			prepared.mathKind = MathKind::Display;
			formulaIndex = state->rememberFormula(prepared);
		}
		preparedBlock->formulaTex = canonicalBlock.formula;
		preparedBlock->formulaIndex = formulaIndex;
		RefreshPreparedNativeIvBlockPlaceholder(preparedBlock, state);
		formulaRange->from = formulaIndex;
		formulaRange->till = formulaIndex + 1;
	} break;
	default:
		return NativeInstantViewLeafUpdateResult::Unsupported;
	}
	if (canonicalBlock.kind != RichPageBlockKind::Math) {
		formulaRange->till = state->nextFormulaIndex;
	}
	return NativeInstantViewLeafUpdateResult::Updated;
}

[[nodiscard]] NativeInstantViewLeafUpdateResult UpdatePreparedNativeIvBlockCaption(
		PreparedBlock *preparedBlock,
		const RichPageBlock &canonicalBlock,
		const PreparedEditLeafSource &source,
		NativeIvPrepareState *state,
		NativeIvPreparedLeafFormulaRange *formulaRange) {
	formulaRange->from = state->nextFormulaIndex;
	formulaRange->till = state->nextFormulaIndex;
	switch (canonicalBlock.kind) {
	case RichPageBlockKind::Quote: {
		const auto target = FindPreparedNativeIvLeafBlock(preparedBlock, source);
		if (!target) {
			return NativeInstantViewLeafUpdateResult::Unsupported;
		}
		auto prepared = PreparedIvRichText();
		if (!PreparePreparedNativeIvRichText(
				canonicalBlock.caption,
				&prepared,
				state)) {
			return NativeInstantViewLeafUpdateResult::Failed;
		}
		SortPreparedIvRichText(&prepared);
		target->text = std::move(prepared.text);
		target->links = std::move(prepared.links);
		RefreshPreparedNativeIvQuotePlaceholder(target, state);
	} break;
	case RichPageBlockKind::Photo:
	case RichPageBlockKind::Video:
	case RichPageBlockKind::Audio:
	case RichPageBlockKind::Map:
	case RichPageBlockKind::GroupedMedia: {
		if (!preparedBlock->editLeaf || (*preparedBlock->editLeaf != source)) {
			return NativeInstantViewLeafUpdateResult::Unsupported;
		}
		auto prepared = PreparedIvRichText();
		if (!PreparePreparedNativeIvRichText(
				canonicalBlock.caption,
				&prepared,
				state)) {
			return NativeInstantViewLeafUpdateResult::Failed;
		}
		SortPreparedIvRichText(&prepared);
		preparedBlock->text = std::move(prepared.text);
		preparedBlock->links = std::move(prepared.links);
		if (preparedBlock->kind == PreparedBlockKind::Photo) {
			preparedBlock->photo.caption = preparedBlock->text;
		} else if (preparedBlock->kind == PreparedBlockKind::Video) {
			preparedBlock->video.caption = preparedBlock->text;
		} else if (preparedBlock->kind == PreparedBlockKind::GroupedMedia) {
			preparedBlock->groupedMedia.caption = preparedBlock->text;
		}
		RefreshPreparedNativeIvMediaCaptionPlaceholder(preparedBlock, state);
		RefreshPreparedNativeIvPlaceholderCopyText(preparedBlock);
	} break;
	default:
		return NativeInstantViewLeafUpdateResult::Unsupported;
	}
	formulaRange->till = state->nextFormulaIndex;
	return NativeInstantViewLeafUpdateResult::Updated;
}

[[nodiscard]] NativeInstantViewLeafUpdateResult UpdatePreparedNativeIvListItemText(
		PreparedBlock *preparedBlock,
		const RichPageListItem &canonicalItem,
		const PreparedEditLeafSource &source,
		NativeIvPrepareState *state,
		NativeIvPreparedLeafFormulaRange *formulaRange) {
	const auto target = FindPreparedNativeIvLeafBlock(preparedBlock, source);
	if (!target) {
		return NativeInstantViewLeafUpdateResult::Unsupported;
	}
	formulaRange->from = state->nextFormulaIndex;
	formulaRange->till = state->nextFormulaIndex;
	auto prepared = PreparedIvRichText();
	if (!PreparePreparedNativeIvRichText(canonicalItem.text, &prepared, state)) {
		return NativeInstantViewLeafUpdateResult::Failed;
	}
	SortPreparedIvRichText(&prepared);
	target->text = std::move(prepared.text);
	target->links = std::move(prepared.links);
	RefreshPreparedNativeIvBlockPlaceholder(target, state);
	formulaRange->till = state->nextFormulaIndex;
	return NativeInstantViewLeafUpdateResult::Updated;
}

[[nodiscard]] NativeInstantViewLeafUpdateResult UpdatePreparedNativeIvTableCellText(
		PreparedBlock *preparedBlock,
		const RichPageTableCell &canonicalCell,
		const PreparedEditLeafSource &source,
		NativeIvPrepareState *state,
		NativeIvPreparedLeafFormulaRange *formulaRange) {
	const auto target = FindPreparedNativeIvLeafCell(preparedBlock, source);
	if (!target) {
		return NativeInstantViewLeafUpdateResult::Unsupported;
	}
	formulaRange->from = state->nextFormulaIndex;
	formulaRange->till = state->nextFormulaIndex;
	auto prepared = PreparedIvRichText();
	const auto context = NativeIvRichTextContextForTextSize(
		canonicalCell.header
			? state->dimensions.tableHeaderTextSize
			: state->dimensions.tableBodyTextSize,
		state->dimensions);
	if (!PreparePreparedNativeIvRichText(
			canonicalCell.text,
			&prepared,
			state,
			context)) {
		return NativeInstantViewLeafUpdateResult::Failed;
	}
	SortPreparedIvRichText(&prepared);
	target->text = std::move(prepared.text);
	target->links = std::move(prepared.links);
	RefreshPreparedNativeIvCellPlaceholder(target, state);
	formulaRange->till = state->nextFormulaIndex;
	return NativeInstantViewLeafUpdateResult::Updated;
}

using PrepareCanonicalMediaBlock = bool (*)(
	const RichPageBlock &data,
	std::vector<PreparedBlock> *result,
	NativeIvPrepareState *state);

[[nodiscard]] bool PrepareCanonicalNativeIvMediaBlock(
		const RichPageBlock &data,
		std::vector<PreparedBlock> *result,
		NativeIvPrepareState *state,
		PreparedEditBlockPath path,
		PrepareCanonicalMediaBlock prepare) {
	const auto count = result->size();
	if (!prepare(data, result, state)) {
		return false;
	}
	if (result->size() > count) {
		ApplyBlockCaptionEditSource(&result->back(), std::move(path));
		if (state->editMode) {
			ApplyNativeIvEditPlaceholderText(&result->back());
		}
	}
	return true;
}

[[nodiscard]] bool PrepareCanonicalNativeIvGroupedMediaBlock(
		const RichPageBlock &data,
		std::vector<PreparedBlock> *result,
		NativeIvPrepareState *state,
		PreparedEditBlockPath path) {
	const auto count = result->size();
	if (!PrepareNativeIvGroupedMediaBlock(data, result, state)) {
		return false;
	}
	if (result->size() > count) {
		ApplyBlockCaptionEditSource(&result->back(), std::move(path));
		if (state->editMode) {
			ApplyNativeIvEditPlaceholderText(&result->back());
		}
	}
	return true;
}

void ClearPreparedEditSources(std::vector<PreparedBlock> *blocks) {
	for (auto &block : *blocks) {
		block.editBlock.reset();
		block.editListItem.reset();
		block.editLeaf.reset();
		for (auto &row : block.tableRows) {
			row.editRow.reset();
			for (auto &cell : row.cells) {
				cell.editCell.reset();
				cell.editLeaf.reset();
			}
		}
		ClearPreparedEditSources(&block.children);
	}
}

[[nodiscard]] bool PrepareNativeIvRichPlaceholderBlock(
		QString label,
		const RichPage::RichText &caption,
		QString anchorId,
		std::optional<EmbedRequest> embed,
		std::vector<PreparedBlock> *result,
		NativeIvPrepareState *state) {
	auto prepared = PreparedIvRichText();
	if (!PrepareNativeIvRichText(
			caption,
			&prepared,
			&anchorId,
			state)) {
		return state->result.failure.failed()
			? false
			: PrepareNativeIvPlainPlaceholderBlock(std::move(label), result);
	}
	SortPreparedIvRichText(&prepared);
	auto block = PreparedBlock();
	block.kind = PreparedBlockKind::Placeholder;
	block.text = std::move(prepared.text);
	block.links = std::move(prepared.links);
	block.anchorId = std::move(anchorId);
	block.anchorIds = std::move(prepared.anchorIds);
	block.supplementary = true;
	block.placeholder.label = std::move(label);
	block.placeholder.copyText = block.text.text.isEmpty()
		? block.placeholder.label
		: (block.placeholder.label + u"\n"_q + block.text.text);
	block.placeholder.embed = std::move(embed);
	if (block.placeholder.embed) {
		block.placeholder.id = { .value = uint64(++state->nextGeneratedId) };
	}
	result->push_back(std::move(block));
	return true;
}

[[nodiscard]] auto EmbedRequestFromCanonicalBlock(
		const RichPageBlock &block) -> std::optional<EmbedRequest> {
	auto request = EmbedRequest{
		.width = block.width,
		.height = block.height,
		.fullWidth = block.fullWidth,
		.fixedHeight = block.fixedHeight,
		.allowScrolling = block.allowScrolling,
	};
	if (!block.url.isEmpty()) {
		request.url = block.url;
	} else if (!block.html.isEmpty()) {
		request.html = block.html;
	} else {
		return std::nullopt;
	}
	return request;
}

[[nodiscard]] bool AppendNativeIvFlowBlock(
		std::vector<PreparedBlock> *result,
		PreparedBlockKind kind,
		int headingLevel,
		const RichPage::RichText &text,
		QString anchorId,
		std::optional<PreparedEditBlockPath> path,
		NativeIvPrepareState *state,
		bool allowEmpty = false,
		bool footer = false) {
	auto prepared = PreparedIvRichText();
	const auto context = NativeIvRichTextContextForTextSize(
		NativeIvFlowTextSize(kind, headingLevel, state->dimensions),
		state->dimensions);
	if (!PrepareNativeIvRichText(
			text,
			&prepared,
			&anchorId,
			state,
			context)) {
		return false;
	}
	auto editBlock = std::optional<PreparedEditBlockSource>();
	auto editLeaf = std::optional<PreparedEditLeafSource>();
	if (path) {
		editBlock = BlockSource(*path);
		editLeaf = BlockTextLeafSource(*path);
	}
	const auto count = result->size();
	const auto appended = AppendPreparedIvRichBlock(
		result,
		kind,
		headingLevel,
		std::move(prepared),
		std::move(anchorId),
		allowEmpty || state->editMode,
		false,
		std::move(editBlock),
		std::move(editLeaf));
	if (appended && (result->size() > count)) {
		result->back().footer = footer;
		if (state->editMode) {
			ApplyNativeIvEditPlaceholderText(&result->back());
		}
	}
	return appended;
}

[[nodiscard]] bool PrepareCanonicalNativeIvQuoteBlock(
		const RichPageBlock &data,
		std::vector<PreparedBlock> *result,
		NativeIvPrepareState *state,
		PreparedEditBlockPath path,
		NativeIvDepthContext depthContext) {
	auto block = PreparedBlock();
	block.kind = PreparedBlockKind::Quote;
	block.anchorId = data.anchorId;
	block.pullquote = data.pullquote;
	block.actualDepth = depthContext.quoteDepth;
	block.visualDepth = CappedNativeIvQuoteDepth(block.actualDepth);
	block.depthClamped = (block.actualDepth > block.visualDepth);
	block.editBlock = BlockSource(path);
	auto body = PreparedIvRichText();
	if (!PrepareNativeIvRichText(data.text, &body, &block.anchorId, state)) {
		return false;
	}
	if (data.blocks.empty() && !AppendPreparedQuoteParagraph(
			&block.children,
			std::move(body),
			data.pullquote,
			false,
			state->editMode,
			false,
			BlockTextLeafSource(path))) {
		return false;
	}
	auto childContext = depthContext;
	++childContext.quoteDepth;
	if (!data.blocks.empty() && !PrepareCanonicalNativeIvBlocks(
			data.blocks,
			&block.children,
			state,
			BlockChildrenContainer(path),
			childContext)) {
		return false;
	}
	auto cite = PreparedIvRichText();
	if (!PrepareNativeIvRichText(data.caption, &cite, &block.anchorId, state)) {
		return false;
	}
	const auto quoteAuthorEmpty = cite.text.text.isEmpty();
	const auto includeQuoteAuthor = state->editMode || !quoteAuthorEmpty;
	if (includeQuoteAuthor && !AppendPreparedQuoteParagraph(
			&block.children,
			std::move(cite),
			data.pullquote,
			true,
			state->editMode,
			true,
			BlockCaptionLeafSource(path))) {
		return false;
	}
	if (block.children.empty()) {
		block.children.push_back(EmptyParagraphBlock());
		if (data.pullquote) {
			block.children.back().flowAlignment = TableAlignment::Center;
			block.children.back().pullquote = true;
		}
	}
	result->push_back(std::move(block));
	return true;
}

[[nodiscard]] TaskState NativeIvTaskStateFromRichPage(
		RichPage::TaskState state) {
	switch (state) {
	case RichPage::TaskState::Unchecked:
		return TaskState::Unchecked;
	case RichPage::TaskState::Checked:
		return TaskState::Checked;
	case RichPage::TaskState::None:
		break;
	}
	return TaskState::None;
}

[[nodiscard]] bool PrepareCanonicalNativeIvList(
		const std::vector<RichPageListItem> &items,
		PreparedBlock *result,
		NativeIvPrepareState *state,
		PreparedEditBlockPath path,
		NativeIvDepthContext depthContext) {
	result->actualDepth = depthContext.listDepth;
	result->visualDepth = CappedNativeIvListDepth(result->actualDepth);
	result->depthClamped = (result->actualDepth > result->visualDepth);
	result->editBlock = BlockSource(path);
	auto orderedFormatter = std::optional<NativeIvOrderedMarkerFormatter>();
	if (result->listKind == ListKind::Ordered) {
		orderedFormatter.emplace(*result, state->editMode);
	}
	for (auto i = 0, count = int(items.size()); i != count; ++i) {
		const auto &item = items[i];
		auto block = PreparedBlock();
		block.kind = PreparedBlockKind::ListItem;
		block.listKind = result->listKind;
		block.listDelimiter = result->listDelimiter;
		block.taskState = NativeIvTaskStateFromRichPage(item.taskState);
		block.actualDepth = result->actualDepth;
		block.visualDepth = result->visualDepth;
		block.depthClamped = result->depthClamped;
		block.editListItem = ListItemSource(path, i);
		if (orderedFormatter.has_value()) {
			orderedFormatter->apply(item, &block);
		}
		if (RichTextHasMeaningfulContent(item.text)) {
			auto prepared = PreparedIvRichText();
			auto anchorId = item.anchorId;
			if (!PrepareNativeIvRichText(
					item.text,
					&prepared,
					&anchorId,
					state)) {
				return false;
			}
			block.anchorId = std::move(anchorId);
			if (!AppendPreparedIvRichBlock(
					&block.children,
					PreparedBlockKind::Paragraph,
					0,
					std::move(prepared),
					QString(),
					state->editMode,
					false,
					std::nullopt,
					ListItemTextLeafSource(path, i))) {
				return false;
			}
			if (state->editMode && !block.children.empty()) {
				ApplyNativeIvEditPlaceholderText(&block.children.back());
			}
		}
		auto childContext = depthContext;
		++childContext.listDepth;
		if (!item.blocks.empty() && !PrepareCanonicalNativeIvBlocks(
				item.blocks,
				&block.children,
				state,
				ListItemChildrenContainer(path, i),
				childContext)) {
			return false;
		}
		if (block.children.empty()) {
			block.children.push_back(EmptyParagraphBlock());
			block.children.back().editLeaf = ListItemTextLeafSource(path, i);
			if (state->editMode) {
				ApplyNativeIvEditPlaceholderText(&block.children.back());
			}
		}
		result->children.push_back(std::move(block));
	}
	return true;
}

[[nodiscard]] TableAlignment NativeIvTableAlignment(
		const RichPageTableCell &cell) {
	switch (cell.alignment) {
	case RichPage::TableAlignment::Center:
		return TableAlignment::Center;
	case RichPage::TableAlignment::Right:
		return TableAlignment::Right;
	case RichPage::TableAlignment::Left:
		break;
	}
	return TableAlignment::Left;
}

[[nodiscard]] PreparedTableCellVerticalAlignment NativeIvTableVerticalAlignment(
		const RichPageTableCell &cell) {
	switch (cell.verticalAlignment) {
	case RichPage::TableVerticalAlignment::Middle:
		return PreparedTableCellVerticalAlignment::Middle;
	case RichPage::TableVerticalAlignment::Bottom:
		return PreparedTableCellVerticalAlignment::Bottom;
	case RichPage::TableVerticalAlignment::Top:
		break;
	}
	return PreparedTableCellVerticalAlignment::Top;
}

[[nodiscard]] bool PrepareCanonicalNativeIvTableBlock(
		const RichPageBlock &data,
		std::vector<PreparedBlock> *result,
		NativeIvPrepareState *state,
		PreparedEditBlockPath path) {
	auto block = PreparedBlock();
	block.kind = PreparedBlockKind::Table;
	block.tableBordered = data.bordered;
	block.tableStriped = data.striped;
	block.editBlock = BlockSource(path);
	auto title = PreparedIvRichText();
	block.anchorId = data.anchorId;
	if (!PrepareNativeIvRichText(data.text, &title, &block.anchorId, state)) {
		return false;
	}
	SortPreparedIvRichText(&title);
	block.text = std::move(title.text);
	block.links = std::move(title.links);
	block.anchorIds = std::move(title.anchorIds);
	if (!block.text.text.isEmpty() || state->editMode) {
		block.editLeaf = BlockTextLeafSource(path);
	}
	if (state->editMode) {
		block.forceTextSegment = true;
		ApplyNativeIvEditPlaceholderText(&block);
	}

	const auto &limits = state->tableRenderLimits;
	const auto rowCount = std::min(int(data.tableRows.size()), limits.maxRows);
	auto occupancy = NativeIvTableOccupancyGrid(rowCount);
	block.tableRows.reserve(rowCount);
	auto occupiedSlotCountSoFar = int64(0);

	for (auto rowIndex = 0; rowIndex != rowCount; ++rowIndex) {
		const auto &row = data.tableRows[rowIndex];
		auto preparedRow = PreparedTableRow();
		preparedRow.editRow = TableRowSource(path, rowIndex);
		preparedRow.cells.reserve(std::min(int(row.cells.size()), limits.maxColumns));
		for (auto cellIndex = 0, cellCount = int(row.cells.size());
				cellIndex != cellCount;
				++cellIndex) {
			const auto &cell = row.cells[cellIndex];
			auto preparedCell = PreparedTableCell();
			const auto normalizedColspan = NormalizeNativeIvTableSpan(cell.colspan);
			const auto rowspan = ClampNativeIvTableRowspan(
				cell.rowspan,
				rowIndex,
				rowCount);
			if (rowspan <= 0) {
				continue;
			}
			const auto column = FirstAvailableNativeIvTableColumn(
				occupancy,
				rowIndex,
				rowspan,
				normalizedColspan,
				limits.maxColumns);
			if (column < 0) {
				continue;
			}
			const auto colspan = ClampNativeIvTableColspan(
				normalizedColspan,
				column,
				limits.maxColumns);
			if (colspan <= 0) {
				continue;
			}
			const auto occupiedSlotGrowth = int64(rowspan) * colspan;
			if (occupiedSlotGrowth > limits.maxCells
				|| (occupiedSlotCountSoFar + occupiedSlotGrowth) > limits.maxCells) {
				continue;
			}
			preparedCell.column = column;
			preparedCell.alignment = NativeIvTableAlignment(cell);
			preparedCell.header = cell.header;
			preparedCell.verticalAlignment = NativeIvTableVerticalAlignment(cell);
			preparedCell.colspan = colspan;
			preparedCell.rowspan = rowspan;
			preparedCell.editCell = TableCellSource(
				path,
				rowIndex,
				cellIndex,
				column,
				colspan,
				rowspan);
			preparedCell.editLeaf = TableCellTextLeafSource(
				path,
				rowIndex,
				cellIndex);
			if (state->editMode) {
				ApplyNativeIvEditPlaceholderText(&preparedCell);
			}
			if (!cell.text.text.empty()) {
				auto rich = PreparedIvRichText();
				const auto context = NativeIvRichTextContextForTextSize(
					cell.header
						? state->dimensions.tableHeaderTextSize
						: state->dimensions.tableBodyTextSize,
					state->dimensions);
				if (!PrepareNativeIvRichText(
						cell.text,
						&rich,
						nullptr,
						state,
						context)) {
					return false;
				}
				SortPreparedIvRichText(&rich);
				preparedCell.text = std::move(rich.text);
				preparedCell.links = std::move(rich.links);
			}
			MarkNativeIvTableSlots(
				&occupancy,
				rowIndex,
				column,
				rowspan,
				colspan);
			occupiedSlotCountSoFar += occupiedSlotGrowth;
			preparedRow.cells.push_back(std::move(preparedCell));
		}
		preparedRow.header = !preparedRow.cells.empty()
			&& std::all_of(
				preparedRow.cells.begin(),
				preparedRow.cells.end(),
				[](const PreparedTableCell &cell) {
					return cell.header;
				});
		block.tableRows.push_back(std::move(preparedRow));
	}

	block.tableColumnCount = NativeIvTableColumnCount(occupancy);
	block.tableAlignments.resize(block.tableColumnCount, TableAlignment::Left);
	for (const auto &preparedRow : block.tableRows) {
		for (const auto &preparedCell : preparedRow.cells) {
			const auto from = std::max(preparedCell.column, 0);
			const auto to = std::min(
				preparedCell.column + preparedCell.colspan,
				block.tableColumnCount);
			for (auto column = from; column != to; ++column) {
				block.tableAlignments[column] = preparedCell.alignment;
			}
		}
	}
	result->push_back(std::move(block));
	return true;
}

[[nodiscard]] bool PrepareCanonicalNativeIvDetailsBlock(
		const RichPageBlock &data,
		std::vector<PreparedBlock> *result,
		NativeIvPrepareState *state,
		PreparedEditBlockPath path,
		NativeIvDepthContext depthContext) {
	auto summary = PreparedIvRichText();
	auto anchorId = NativeIvDetailsAnchorId(state);
	if (!PrepareNativeIvRichText(
			data.text,
			&summary,
			&anchorId,
			state)) {
		return false;
	}
	if (!summary.links.empty()) {
		const auto from = std::remove_if(
			summary.text.entities.begin(),
			summary.text.entities.end(),
			[](const EntityInText &entity) {
				return entity.type() == EntityType::CustomUrl;
			});
		summary.text.entities.erase(from, summary.text.entities.end());
		summary.links.clear();
	}
	SortPreparedIvRichText(&summary);
	auto block = PreparedBlock();
	block.kind = PreparedBlockKind::Details;
	block.anchorId = std::move(anchorId);
	block.detailsOpen = data.open;
	block.collapsed = state->editMode ? false : !data.open;
	block.text = std::move(summary.text);
	block.links = std::move(summary.links);
	block.anchorIds = std::move(summary.anchorIds);
	block.editBlock = BlockSource(path);
	block.editLeaf = BlockTextLeafSource(path);
	if (state->editMode) {
		ApplyNativeIvEditPlaceholderText(&block);
	}
	return PrepareCanonicalNativeIvBlocks(
		data.blocks,
		&block.children,
		state,
		BlockChildrenContainer(path),
		depthContext)
		? (result->push_back(std::move(block)), true)
		: false;
}

[[nodiscard]] QString NativeIvRelatedArticleFooterText(
		const RichPageRelatedArticle &article) {
	if (article.publishedDate && !article.author.isEmpty()) {
		return article.author + u", "_q + NativeIvDateText(article.publishedDate);
	} else if (article.publishedDate) {
		return NativeIvDateText(article.publishedDate);
	}
	return article.author;
}

[[nodiscard]] bool PrepareCanonicalNativeIvRelatedArticlesBlock(
		const RichPageBlock &data,
		std::vector<PreparedBlock> *result,
		NativeIvPrepareState *state) {
	auto related = std::vector<PreparedBlock>();
	related.reserve(data.relatedArticles.size());
	for (const auto &article : data.relatedArticles) {
		auto prepared = PreparedBlock();
		prepared.kind = PreparedBlockKind::RelatedArticle;
		prepared.relatedArticle.title = article.title.trimmed();
		prepared.relatedArticle.description = article.description.trimmed();
		prepared.relatedArticle.footer = NativeIvRelatedArticleFooterText(article);
		prepared.relatedArticle.photoId = article.photoId;
		if (prepared.relatedArticle.title.isEmpty()
			&& prepared.relatedArticle.description.isEmpty()
			&& prepared.relatedArticle.footer.isEmpty()) {
			prepared.relatedArticle.title = article.url.trimmed();
		}
		const auto linkText = !prepared.relatedArticle.title.isEmpty()
			? prepared.relatedArticle.title
			: !prepared.relatedArticle.description.isEmpty()
			? prepared.relatedArticle.description
			: prepared.relatedArticle.footer;
		prepared.relatedArticle.link = PrepareNativeIvRelatedArticleLink(
			article.url,
			article.webpageId,
			linkText);
		const auto appendLine = [&](QString *copyText, const QString &line) {
			if (line.isEmpty()) {
				return;
			} else if (!copyText->isEmpty()) {
				copyText->append(QChar('\n'));
			}
			copyText->append(line);
		};
		appendLine(&prepared.relatedArticle.copyText, prepared.relatedArticle.title);
		appendLine(
			&prepared.relatedArticle.copyText,
			prepared.relatedArticle.description);
		appendLine(&prepared.relatedArticle.copyText, prepared.relatedArticle.footer);
		if (prepared.relatedArticle.copyText.isEmpty()) {
			prepared.relatedArticle.copyText = prepared.relatedArticle.link.target;
		}
		related.push_back(std::move(prepared));
	}
	if (related.empty()) {
		return true;
	}

	auto title = PreparedIvRichText();
	auto anchorId = data.anchorId;
	if (!PrepareNativeIvRichText(data.text, &title, &anchorId, state)) {
		return false;
	}
	SortPreparedIvRichText(&title);
	if (!title.text.text.isEmpty()) {
		if (!AppendPreparedIvRichBlock(
				result,
				PreparedBlockKind::Heading,
				4,
				std::move(title),
				std::move(anchorId),
				state->editMode)) {
			return false;
		}
	} else if (!anchorId.isEmpty() && related.front().anchorId.isEmpty()) {
		related.front().anchorId = std::move(anchorId);
	}
	result->insert(
		result->end(),
		std::make_move_iterator(related.begin()),
		std::make_move_iterator(related.end()));
	return true;
}

[[nodiscard]] bool PrepareCanonicalNativeIvEmbedPostBlock(
		const RichPageBlock &data,
		std::vector<PreparedBlock> *result,
		NativeIvPrepareState *state,
		NativeIvDepthContext depthContext) {
	auto caption = PreparedIvRichText();
	auto block = PreparedBlock();
	block.kind = PreparedBlockKind::EmbedPost;
	block.embedPost.url = data.url.trimmed();
	block.embedPost.authorPhotoId = data.photoId;
	block.embedPost.author = data.author.trimmed();
	if (data.date) {
		block.embedPost.dateText = NativeIvDateText(data.date);
	}
	auto anchorId = data.anchorId;
	if (!PrepareNativeIvRichText(data.caption, &caption, &anchorId, state)) {
		return false;
	}
	SortPreparedIvRichText(&caption);
	block.text = std::move(caption.text);
	block.links = std::move(caption.links);
	block.anchorId = std::move(anchorId);
	block.anchorIds = std::move(caption.anchorIds);
	block.supplementary = true;
	if (data.blocks.empty()) {
		block.children.push_back(
			PrepareNativeIvEmbedPostFallbackParagraph(block.embedPost.url));
	} else if (!PrepareCanonicalNativeIvBlocks(
			data.blocks,
			&block.children,
			state,
			PreparedEditBlockContainerPath(),
			depthContext)) {
		return false;
	} else {
		ClearPreparedEditSources(&block.children);
	}
	result->push_back(std::move(block));
	return true;
}

[[nodiscard]] bool PrepareCanonicalNativeIvBlock(
		const RichPageBlock &block,
		std::vector<PreparedBlock> *result,
		NativeIvPrepareState *state,
		PreparedEditBlockPath path,
		NativeIvDepthContext depthContext) {
	if (state->blocked()) {
		return false;
	}
	switch (block.kind) {
	case RichPageBlockKind::Unsupported:
		return true;
	case RichPageBlockKind::Heading:
		return AppendNativeIvFlowBlock(
			result,
			PreparedBlockKind::Heading,
			block.headingLevel,
			block.text,
			block.anchorId,
			path,
			state);
	case RichPageBlockKind::Paragraph:
	case RichPageBlockKind::Footer:
		return AppendNativeIvFlowBlock(
			result,
			PreparedBlockKind::Paragraph,
			0,
			block.text,
			block.anchorId,
			path,
			state,
			false,
			(block.kind == RichPageBlockKind::Footer));
	case RichPageBlockKind::Thinking:
		return AppendNativeIvFlowBlock(
			result,
			PreparedBlockKind::Thinking,
			0,
			block.text,
			block.anchorId,
			std::nullopt,
			state);
	case RichPageBlockKind::AuthorDate: {
		auto prepared = PreparedIvRichText();
		auto anchorId = block.anchorId;
		if (!PrepareNativeIvRichText(
				block.text,
				&prepared,
				&anchorId,
				state)) {
			return false;
		}
		if (block.date) {
			if (!prepared.text.text.isEmpty()) {
				prepared.text.append(u" \u2022 "_q);
			}
			prepared.text.append(NativeIvDateText(block.date));
		}
		return AppendPreparedIvRichBlock(
			result,
			PreparedBlockKind::Paragraph,
			0,
			std::move(prepared),
			std::move(anchorId),
			false,
			true);
	}
	case RichPageBlockKind::Code: {
		auto prepared = PreparedIvRichText();
		auto anchorId = block.anchorId;
		if (!PrepareNativeIvRichText(
				block.text,
				&prepared,
				&anchorId,
				state,
				{ .dropClickHandlers = true })) {
			return false;
		}
		auto code = PreparedBlock();
		code.kind = PreparedBlockKind::CodeBlock;
		code.anchorId = std::move(anchorId);
		code.codeLanguage = block.language;
		SortPreparedIvRichText(&prepared);
		code.text = StripOneTrailingNewline(std::move(prepared.text));
		code.links = std::move(prepared.links);
		code.anchorIds = std::move(prepared.anchorIds);
		code.editBlock = BlockSource(path);
		code.editLeaf = BlockTextLeafSource(path);
		if (state->editMode) {
			ApplyNativeIvEditPlaceholderText(&code);
		}
		result->push_back(std::move(code));
		return true;
	}
	case RichPageBlockKind::Divider: {
		auto prepared = PrepareRuleBlock();
		prepared.editBlock = BlockSource(path);
		result->push_back(std::move(prepared));
		return true;
	}
	case RichPageBlockKind::Anchor: {
		auto prepared = PreparedIvRichText();
		auto editBlock = std::optional<PreparedEditBlockSource>();
		editBlock = BlockSource(path);
		return AppendPreparedIvRichBlock(
			result,
			PreparedBlockKind::Paragraph,
			0,
			std::move(prepared),
			block.anchorId,
			true,
			false,
			std::move(editBlock));
	}
	case RichPageBlockKind::List: {
		auto prepared = PreparedBlock();
		prepared.kind = PreparedBlockKind::List;
		prepared.listKind = (block.listKind == RichPage::ListKind::Ordered)
			? ListKind::Ordered
			: ListKind::Bullet;
		if (prepared.listKind == ListKind::Ordered) {
			prepared.listDelimiter = ListDelimiter::Period;
			prepared.orderedType = ResolvePreparedOrderedListType(
				block.orderedList.type);
			prepared.orderedReversed = block.orderedList.reversed;
			prepared.startNumber = block.orderedList.start.value_or(
				prepared.orderedReversed
					? int(block.listItems.size())
					: 1);
		}
		return PrepareCanonicalNativeIvList(
			block.listItems,
			&prepared,
			state,
			path,
			depthContext)
			? (result->push_back(std::move(prepared)), true)
			: false;
	}
	case RichPageBlockKind::Quote:
		return PrepareCanonicalNativeIvQuoteBlock(
			block,
			result,
			state,
			path,
			depthContext);
	case RichPageBlockKind::Photo:
		return PrepareCanonicalNativeIvMediaBlock(
			block,
			result,
			state,
			path,
			PrepareNativeIvPhotoBlock);
	case RichPageBlockKind::Video:
		return PrepareCanonicalNativeIvMediaBlock(
			block,
			result,
			state,
			path,
			PrepareNativeIvVideoBlock);
	case RichPageBlockKind::Embed:
		return PrepareNativeIvRichPlaceholderBlock(
			tr::lng_iv_click_to_view(tr::now),
			block.caption,
			block.anchorId,
			EmbedRequestFromCanonicalBlock(block),
			result,
			state);
	case RichPageBlockKind::EmbedPost:
		return PrepareCanonicalNativeIvEmbedPostBlock(
			block,
			result,
			state,
			depthContext);
	case RichPageBlockKind::GroupedMedia:
		return PrepareCanonicalNativeIvGroupedMediaBlock(
			block,
			result,
			state,
			path);
	case RichPageBlockKind::Channel:
		return PrepareNativeIvChannelBlock(block, result, state);
	case RichPageBlockKind::Audio:
		return PrepareCanonicalNativeIvMediaBlock(
			block,
			result,
			state,
			path,
			PrepareNativeIvAudioBlock);
	case RichPageBlockKind::Math:
		if (block.formula.trimmed().isEmpty() && !state->editMode) {
			return true;
		} else {
			auto prepared = PreparedBlock();
			prepared.kind = PreparedBlockKind::DisplayMath;
			prepared.formulaTex = block.formula;
			prepared.mathKind = MathKind::Display;
			prepared.editBlock = BlockSource(path);
			prepared.editLeaf = MathFormulaLeafSource(path);
			if (state->editMode) {
				ApplyNativeIvEditPlaceholderText(&prepared);
			}
			prepared.formulaIndex = state->rememberFormula(prepared);
			result->push_back(std::move(prepared));
			return true;
		}
	case RichPageBlockKind::Table:
		return PrepareCanonicalNativeIvTableBlock(block, result, state, path);
	case RichPageBlockKind::Details:
		return PrepareCanonicalNativeIvDetailsBlock(
			block,
			result,
			state,
			path,
			depthContext);
	case RichPageBlockKind::RelatedArticles:
		return PrepareCanonicalNativeIvRelatedArticlesBlock(block, result, state);
	case RichPageBlockKind::Map:
		return PrepareCanonicalNativeIvMediaBlock(
			block,
			result,
			state,
			path,
			PrepareNativeIvMapBlock);
	}
	return true;
}

[[nodiscard]] bool PrepareCanonicalNativeIvBlocks(
		const std::vector<RichPageBlock> &blocks,
		std::vector<PreparedBlock> *result,
		NativeIvPrepareState *state,
		PreparedEditBlockContainerPath container,
		NativeIvDepthContext depthContext) {
	for (auto i = 0, count = int(blocks.size()); i != count; ++i) {
		const auto path = PreparedEditBlockPath{
			.container = container,
			.index = i,
		};
		if (!PrepareCanonicalNativeIvBlock(
				blocks[i],
				result,
				state,
				path,
				depthContext)) {
			if (state->result.failure.failed()) {
				return false;
			}
			(void)PrepareNativeIvPlainPlaceholderBlock(
				u"Unsupported Block"_q,
				result);
		}
	}
	return !state->result.failure.failed();
}

} // namespace

QString FormatPreparedOrderedRawMarkerText(
		const QString &raw,
		ListDelimiter delimiter) {
	if (raw.isEmpty() || raw.endsWith('.') || raw.endsWith(')')) {
		return raw;
	}
	const auto suffix = (delimiter == ListDelimiter::Parenthesis)
		? u")"_q
		: u"."_q;
	return raw + suffix;
}

bool PrepareNativeIvBlocks(
		const Iv::RichPage &page,
		std::vector<PreparedBlock> *result,
		NativeIvPrepareState *state) {
	return PrepareCanonicalNativeIvBlocks(
		page.blocks,
		result,
		state,
		PreparedEditBlockContainerPath(),
		NativeIvDepthContext());
}

NativeInstantViewLeafUpdateResult UpdatePreparedNativeIvLeaf(
		std::vector<PreparedBlock> *blocks,
		const Iv::RichPage &page,
		const PreparedEditLeafSource &source,
		NativeIvPrepareState *state,
		NativeIvPreparedLeafFormulaRange *formulaRange) {
	if (!blocks || !state || !formulaRange) {
		return NativeInstantViewLeafUpdateResult::Failed;
	}
	const auto canonicalBlock = ResolveCanonicalNativeIvBlock(page, source.block);
	const auto preparedBlock = FindPreparedNativeIvBlockByPath(blocks, source.block);
	if (!canonicalBlock || !preparedBlock) {
		return NativeInstantViewLeafUpdateResult::Unsupported;
	}
	switch (source.kind) {
	case PreparedEditLeafKind::BlockText:
	case PreparedEditLeafKind::MathFormula:
	case PreparedEditLeafKind::BlockCaption:
		break;
	case PreparedEditLeafKind::ListItemText:
		if (source.listItemIndex < 0
			|| source.listItemIndex >= int(canonicalBlock->listItems.size())) {
			return NativeInstantViewLeafUpdateResult::Unsupported;
		}
		break;
	case PreparedEditLeafKind::TableCellText:
		if (source.tableRowIndex < 0
			|| source.tableRowIndex >= int(canonicalBlock->tableRows.size())) {
			return NativeInstantViewLeafUpdateResult::Unsupported;
		}
		if (source.tableCellIndex < 0
			|| source.tableCellIndex
				>= int(canonicalBlock->tableRows[source.tableRowIndex].cells.size())) {
			return NativeInstantViewLeafUpdateResult::Unsupported;
		}
		break;
	}
	switch (source.kind) {
	case PreparedEditLeafKind::BlockText:
	case PreparedEditLeafKind::MathFormula:
		return UpdatePreparedNativeIvBlockText(
			preparedBlock,
			*canonicalBlock,
			source,
			state,
			formulaRange);
	case PreparedEditLeafKind::BlockCaption:
		return UpdatePreparedNativeIvBlockCaption(
			preparedBlock,
			*canonicalBlock,
			source,
			state,
			formulaRange);
	case PreparedEditLeafKind::ListItemText:
		return UpdatePreparedNativeIvListItemText(
			preparedBlock,
			canonicalBlock->listItems[source.listItemIndex],
			source,
			state,
			formulaRange);
	case PreparedEditLeafKind::TableCellText:
		return UpdatePreparedNativeIvTableCellText(
			preparedBlock,
			canonicalBlock->tableRows[source.tableRowIndex]
				.cells[source.tableCellIndex],
			source,
			state,
			formulaRange);
	}
	return NativeInstantViewLeafUpdateResult::Unsupported;
}

} // namespace Iv::Markdown
