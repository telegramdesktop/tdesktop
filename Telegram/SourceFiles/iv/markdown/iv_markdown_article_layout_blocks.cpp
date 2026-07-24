/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/iv_markdown_article_layout_blocks.h"
#include "iv/markdown/iv_markdown_media_block.h"
#include "iv/markdown/iv_markdown_article_text.h"
#include "iv/markdown/iv_markdown_prepare_links.h"
#include "iv/markdown/iv_markdown_prepare_serialize.h"
#include "spellcheck/spellcheck_highlight_syntax.h"

#include "lang/lang_keys.h"
#include "styles/style_iv.h"
#include "styles/style_widgets.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Iv::Markdown {
namespace {

constexpr auto kIvMarkedTextOptions = TextParseOptions{
	TextParseMultiline,
	0,
	0,
	Qt::LayoutDirectionAuto,
};

constexpr auto kIvMarkedTextOptionsRtl = TextParseOptions{
	TextParseMultiline,
	0,
	0,
	Qt::RightToLeft,
};

constexpr auto kCodeTabColumns = 4;
constexpr auto kCodeTrailingGuard = 0x2060;
constexpr auto kReadableTextColumns = 12;
constexpr auto kReadableTextLineHeights = 4;
constexpr auto kReadableCodeColumns = 16;

thread_local const LayoutContext *CurrentLayoutContext = nullptr;

[[nodiscard]] QString DetailsStateText(bool open) {
	return open
		? tr::lng_iv_details_state_expanded(tr::now)
		: tr::lng_iv_details_state_collapsed(tr::now);
}

[[nodiscard]] size_t CombineHash(size_t accumulator, size_t value) {
	return (accumulator * 1315423911U) ^ value;
}

[[nodiscard]] size_t HashPreparedEditBlockPath(
		const PreparedEditBlockPath &value) {
	auto result = CombineHash(0, size_t(value.container.steps.size() + 1));
	for (const auto &step : value.container.steps) {
		result = CombineHash(result, size_t(step.kind));
		result = CombineHash(result, size_t(step.blockIndex + 1));
		result = CombineHash(result, size_t(step.listItemIndex + 1));
	}
	return CombineHash(result, size_t(value.index + 1));
}

[[nodiscard]] size_t HashPreparedEditBlockSource(
		const PreparedEditBlockSource &value) {
	return HashPreparedEditBlockPath(value.path);
}

[[nodiscard]] size_t HashPreparedEditListItemSource(
		const PreparedEditListItemSource &value) {
	auto result = HashPreparedEditBlockPath(value.block);
	return CombineHash(result, size_t(value.listItemIndex + 1));
}

[[nodiscard]] size_t HashPreparedEditTableCellSource(
		const PreparedEditTableCellSource &value) {
	auto result = HashPreparedEditBlockPath(value.block);
	result = CombineHash(result, size_t(value.tableRowIndex + 1));
	result = CombineHash(result, size_t(value.tableCellIndex + 1));
	result = CombineHash(result, size_t(value.column + 1));
	result = CombineHash(result, size_t(value.colspan + 1));
	return CombineHash(result, size_t(value.rowspan + 1));
}

[[nodiscard]] size_t HashPreparedEditLeafSource(
		const PreparedEditLeafSource &value) {
	auto result = CombineHash(0, size_t(value.kind));
	result = CombineHash(result, HashPreparedEditBlockPath(value.block));
	result = CombineHash(result, size_t(value.listItemIndex + 1));
	result = CombineHash(result, size_t(value.tableRowIndex + 1));
	return CombineHash(result, size_t(value.tableCellIndex + 1));
}

[[nodiscard]] style::align CellAlign(TableAlignment alignment) {
	switch (alignment) {
	case TableAlignment::Center:
		return style::al_center;
	case TableAlignment::Right:
		return style::al_right;
	case TableAlignment::None:
	case TableAlignment::Left:
		return style::al_left;
	}
	return style::al_left;
}

[[nodiscard]] const style::TextStyle &TableCellTextStyle(
		const PreparedTableCell &prepared,
		const style::Markdown &st) {
	if (prepared.header) {
		return st.table.headerStyle;
	}
	return st.table.bodyStyle;
}

[[nodiscard]] const style::TextStyle &TableCellTextStyle(
		const LaidOutTableCell &cell,
		const style::Markdown &st) {
	if (cell.header) {
		return st.table.headerStyle;
	}
	return st.table.bodyStyle;
}

[[nodiscard]] int TableBorder(
		bool bordered,
		const style::Markdown &st) {
	return bordered ? st.table.border : 0;
}

[[nodiscard]] bool TextDependsOnMediaRuntime(
		const TextWithEntities &text) {
	for (const auto &entity : text.entities) {
		if (entity.type() != EntityType::CustomEmoji) {
			continue;
		}
		const auto parsed = ParseInlineTextObjectEntity(entity.data());
		if (parsed && (parsed->kind == InlineTextObjectKind::IvImage)) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] TextWithEntities DisplayMathFallbackText() {
	auto result = TextWithEntities::Simple(u"Invalid formula"_q);
	result.entities.push_back(EntityInText(
		EntityType::Italic,
		0,
		result.text.size()));
	return result;
}

[[nodiscard]] size_t TextStyleKey(const style::TextStyle &style) {
	return reinterpret_cast<size_t>(&style);
}

void SetPlainTextLeaf(
	Ui::Text::String *leaf,
	const style::TextStyle &textStyle,
	const QString &text,
	int minResizeWidth,
	bool rtl);

[[nodiscard]] CachedTextLeafSourceSignature MarkedTextLeafSourceSignature(
		TextWithEntities text,
		const style::TextStyle &textStyle,
		int minResizeWidth,
		bool rtl) {
	auto result = CachedTextLeafSourceSignature();
	result.dependsOnMediaRuntime = TextDependsOnMediaRuntime(text);
	result.text = std::move(text);
	result.minResizeWidth = minResizeWidth;
	result.styleKey = TextStyleKey(textStyle);
	result.rtl = rtl;
	return result;
}

[[nodiscard]] CachedTextLeafSourceSignature PlainTextLeafSourceSignature(
		const QString &text,
		const style::TextStyle &textStyle,
		int minResizeWidth,
		bool rtl) {
	return MarkedTextLeafSourceSignature(
		TextWithEntities::Simple(text),
		textStyle,
		minResizeWidth,
		rtl);
}

[[nodiscard]] CachedTextLeafSourceSignature CodeTextLeafSourceSignature(
		const PreparedBlock &prepared,
		const style::Markdown &st) {
	auto result = MarkedTextLeafSourceSignature(
		CodeBlockDisplayText(prepared.text),
		st.code,
		CodeTextMinResizeWidth(st),
		false);
	result.codeLanguage = prepared.codeLanguage;
	return result;
}

[[nodiscard]] CachedTextLeafKey BlockCachedTextLeafKey(
		CachedTextLeafSlot slot,
		const PreparedBlock &prepared,
		const std::vector<int> &preparedPath) {
	auto result = CachedTextLeafKey();
	result.slot = slot;
	if ((slot == CachedTextLeafSlot::Leaf
			|| slot == CachedTextLeafSlot::Placeholder
			|| slot == CachedTextLeafSlot::Fallback)
		&& prepared.editLeaf) {
		result.identityKind = CachedTextLeafIdentityKind::EditLeaf;
		result.editLeaf = *prepared.editLeaf;
		return result;
	}
	if ((slot == CachedTextLeafSlot::Marker) && prepared.editListItem) {
		result.identityKind = CachedTextLeafIdentityKind::EditListItem;
		result.editListItem = *prepared.editListItem;
		return result;
	}
	if (prepared.editBlock) {
		result.identityKind = CachedTextLeafIdentityKind::EditBlock;
		result.editBlock = *prepared.editBlock;
		return result;
	}
	if (prepared.editListItem) {
		result.identityKind = CachedTextLeafIdentityKind::EditListItem;
		result.editListItem = *prepared.editListItem;
		return result;
	}
	if (prepared.editLeaf) {
		result.identityKind = CachedTextLeafIdentityKind::EditLeaf;
		result.editLeaf = *prepared.editLeaf;
		return result;
	}
	result.preparedPath = preparedPath;
	return result;
}

[[nodiscard]] CachedTextLeafKey TableCellCachedTextLeafKey(
		CachedTextLeafSlot slot,
		const PreparedTableCell &prepared,
		const std::vector<int> &preparedPath,
		int tableRowIndex,
		int tableCellIndex) {
	auto result = CachedTextLeafKey();
	result.slot = slot;
	result.tableRowIndex = tableRowIndex;
	result.tableCellIndex = tableCellIndex;
	if (prepared.editLeaf) {
		result.identityKind = CachedTextLeafIdentityKind::EditLeaf;
		result.editLeaf = *prepared.editLeaf;
		return result;
	}
	if (prepared.editCell) {
		result.identityKind = CachedTextLeafIdentityKind::EditTableCell;
		result.editTableCell = *prepared.editCell;
		return result;
	}
	result.preparedPath = preparedPath;
	return result;
}

template <typename Builder, typename Consumer>
auto WithCachedTextLeaf(
		LayoutContext context,
		CachedTextLeafKey key,
		CachedTextLeafSourceSignature source,
		Builder &&builder,
		Consumer &&consumer) {
	if (const auto pool = context.cachedTextLeafs) {
		auto i = pool->entries.find(key);
		if (i == end(pool->entries)
			|| (i->second.source != source)
			|| i->second.leaf.isEmpty()) {
			auto entry = CachedTextLeafEntry();
			entry.source = source;
			builder(&entry.leaf, &entry.syntaxHighlightProcessId);
			i = pool->entries.insert_or_assign(
				std::move(key),
				std::move(entry)).first;
		}
		return consumer(i->second.leaf, i->second.syntaxHighlightProcessId);
	}
	auto leaf = Ui::Text::String();
	auto syntaxHighlightProcessId = Spellchecker::HighlightProcessId(0);
	builder(&leaf, &syntaxHighlightProcessId);
	return consumer(leaf, syntaxHighlightProcessId);
}

template <typename Builder>
void BuildOrReuseCachedTextLeaf(
		Ui::Text::String *leaf,
		Spellchecker::HighlightProcessId *syntaxHighlightProcessId,
		LayoutContext context,
		CachedTextLeafKey key,
		const CachedTextLeafSourceSignature &source,
		Builder &&builder) {
	if (const auto pool = context.cachedTextLeafs) {
		if (const auto i = pool->entries.find(key);
			i != end(pool->entries)
			&& (i->second.source == source)
			&& !i->second.leaf.isEmpty()) {
			if (syntaxHighlightProcessId) {
				*syntaxHighlightProcessId = i->second.syntaxHighlightProcessId;
			}
			*leaf = std::move(i->second.leaf);
			pool->entries.erase(i);
			SetTextLeafSpoilerLinkFilter(leaf, context.spoilerLinkFilter);
			return;
		}
	}
	builder(leaf, syntaxHighlightProcessId);
	SetTextLeafSpoilerLinkFilter(leaf, context.spoilerLinkFilter);
}

[[nodiscard]] LaidOutTableCell InitializeTableCellLayout(
		const PreparedTableCell &prepared,
		const std::vector<PreparedFormulaSlot> *formulas,
		InlineFormulaObjectCache *inlineFormulaObjects,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		const style::Markdown &st,
		int tableRowIndex,
		int tableCellIndex,
		LayoutContext context) {
	auto result = LaidOutTableCell();
	const auto &textStyle = TableCellTextStyle(prepared, st);
	const auto minResizeWidth = TableCellTextMinResizeWidth(textStyle, st);
	result.header = prepared.header;
	result.verticalAlignment = prepared.verticalAlignment;
	result.align = CellAlign(prepared.alignment);
	result.column = std::max(prepared.column, 0);
	result.colspan = std::max(prepared.colspan, 1);
	result.rowspan = std::max(prepared.rowspan, 1);
	result.editCell = prepared.editCell;
	result.editLeaf = prepared.editLeaf;
	BuildOrReuseCachedTextLeaf(
		&result.leaf,
		nullptr,
		context,
		TableCellCachedTextLeafKey(
			CachedTextLeafSlot::TableCellText,
			prepared,
			context.preparedPath,
			tableRowIndex,
			tableCellIndex),
		MarkedTextLeafSourceSignature(
			prepared.text,
			textStyle,
			minResizeWidth,
			context.rtl),
		[&](Ui::Text::String *leaf,
				Spellchecker::HighlightProcessId*) {
			SetTextLeaf(
				leaf,
				textStyle,
				st,
				prepared.text,
				formulas,
				inlineFormulaObjects,
				mediaRuntime,
				minResizeWidth,
				context.rtl,
				context.repaint,
				context.repaintRect);
			BindLinks(leaf, prepared.links);
		});
	BindLinks(&result.leaf, prepared.links);
	const auto usePlaceholder = prepared.text.text.isEmpty()
		&& !prepared.editPlaceholderText.isEmpty();
	if (usePlaceholder) {
		result.placeholderText = prepared.editPlaceholderText;
		BuildOrReuseCachedTextLeaf(
			&result.placeholderLeaf,
			nullptr,
			context,
			TableCellCachedTextLeafKey(
				CachedTextLeafSlot::TableCellPlaceholder,
				prepared,
				context.preparedPath,
				tableRowIndex,
				tableCellIndex),
			PlainTextLeafSourceSignature(
				result.placeholderText,
				textStyle,
				minResizeWidth,
				context.rtl),
			[&](Ui::Text::String *leaf,
					Spellchecker::HighlightProcessId*) {
				SetPlainTextLeaf(
					leaf,
					textStyle,
					result.placeholderText,
					minResizeWidth,
					context.rtl);
			});
	}
	return result;
}

[[nodiscard]] int TableSpanWidth(
		const std::vector<int> &columnWidths,
		int column,
		int colspan,
		int border) {
	const auto from = std::clamp(column, 0, int(columnWidths.size()));
	const auto to = std::clamp(column + colspan, 0, int(columnWidths.size()));
	if (from >= to) {
		return 0;
	}
	auto result = 0;
	for (auto current = from; current != to; ++current) {
		result += columnWidths[current];
	}
	return result + std::max(to - from - 1, 0) * border;
}

[[nodiscard]] int TableSpanHeight(
		const std::vector<int> &rowHeights,
		int row,
		int rowspan,
		int border) {
	const auto from = std::clamp(row, 0, int(rowHeights.size()));
	const auto to = std::clamp(row + rowspan, 0, int(rowHeights.size()));
	if (from >= to) {
		return 0;
	}
	auto result = 0;
	for (auto current = from; current != to; ++current) {
		result += rowHeights[current];
	}
	return result + std::max(to - from - 1, 0) * border;
}

void DistributeSizeDeficits(
		std::vector<int> *sizes,
		std::vector<int> deficits,
		int *extra) {
	auto remaining = 0;
	for (const auto deficit : deficits) {
		remaining += std::max(deficit, 0);
	}
	while (*extra > 0 && remaining > 0) {
		auto active = 0;
		for (const auto deficit : deficits) {
			if (deficit > 0) {
				++active;
			}
		}
		if (!active) {
			break;
		}
		const auto step = std::max(*extra / active, 1);
		for (auto i = 0, count = int(deficits.size()); i != count && *extra > 0; ++i) {
			if (deficits[i] <= 0) {
				continue;
			}
			const auto delta = std::min({ deficits[i], step, *extra });
			(*sizes)[i] += delta;
			deficits[i] -= delta;
			remaining -= delta;
			*extra -= delta;
		}
	}
}

void DistributeSpanDelta(
		std::vector<int> *sizes,
		int from,
		int to,
		int delta) {
	from = std::clamp(from, 0, int(sizes->size()));
	to = std::clamp(to, 0, int(sizes->size()));
	while (delta > 0 && from < to) {
		const auto active = to - from;
		const auto step = std::max(delta / active, 1);
		for (auto i = from; i != to && delta > 0; ++i) {
			const auto current = std::min(step, delta);
			(*sizes)[i] += current;
			delta -= current;
		}
	}
}

struct TableCellGeometryData {
	LaidOutTableCell *cell = nullptr;
	int minimumWidth = 0;
	int preferredWidth = 0;
	int preferredHeight = 0;
	int textHeight = 0;
	bool usePlaceholder = false;
};

struct TableRowGeometryData {
	std::vector<TableCellGeometryData> cells;
	bool header = false;
};

struct TableSpannedCellGeometryData {
	int row = 0;
	TableCellGeometryData *cell = nullptr;
};

[[nodiscard]] std::vector<int> ComputeTableColumnWidths(
		std::vector<TableRowGeometryData> &rows,
		int columnCount,
		int width,
		const style::Markdown &st,
		bool bordered,
		bool *overflowed) {
	const auto &padding = st.table.cellPadding;
	const auto border = TableBorder(bordered, st);
	const auto paddingWidth = padding.left() + padding.right();
	auto constraints = std::vector<TableCellMinimumWidthConstraint>();
	for (auto &row : rows) {
		for (auto &cellData : row.cells) {
			if (!cellData.cell || cellData.minimumWidth <= 0) {
				continue;
			}
			constraints.push_back({
				.column = cellData.cell->column,
				.colspan = cellData.cell->colspan,
				.minimumWidth = std::max(
					cellData.minimumWidth + paddingWidth,
					st.table.minColumnWidth),
			});
		}
	}
	auto result = ComputeTableColumnMinimumWidths(
		std::move(constraints),
		columnCount,
		st,
		bordered);
	auto singleColumnDeficits = std::vector<int>(std::max(columnCount, 0), 0);
	auto spannedCells = std::vector<TableSpannedCellGeometryData>();
	for (auto row = 0, rowCount = int(rows.size()); row != rowCount; ++row) {
		for (auto &cellData : rows[row].cells) {
			if (!cellData.cell) {
				continue;
			}
			const auto from = std::clamp(cellData.cell->column, 0, columnCount);
			const auto to = std::clamp(
				cellData.cell->column + cellData.cell->colspan,
				0,
				columnCount);
			if (from >= to) {
				continue;
			}
			const auto preferredWidth = cellData.preferredWidth
				+ padding.left()
				+ padding.right();
			if ((to - from) == 1) {
				singleColumnDeficits[from] = std::max(
					singleColumnDeficits[from],
					preferredWidth - result[from]);
			} else {
				spannedCells.push_back({ row, &cellData });
			}
		}
	}

	const auto availableWidth = std::max(width, 1);
	const auto minimumGridWidth = (columnCount > 0)
		? TableGridWidth(result, st, bordered)
		: TableMinimumGridWidth(columnCount, st, bordered);
	*overflowed = (minimumGridWidth > availableWidth);
	if (*overflowed || !columnCount) {
		return result;
	}

	auto extra = availableWidth - minimumGridWidth;
	DistributeSizeDeficits(&result, std::move(singleColumnDeficits), &extra);
	std::sort(
		spannedCells.begin(),
		spannedCells.end(),
		[](const TableSpannedCellGeometryData &a,
				const TableSpannedCellGeometryData &b) {
			const auto aSpan = a.cell ? a.cell->cell->colspan : 0;
			const auto bSpan = b.cell ? b.cell->cell->colspan : 0;
			return (aSpan < bSpan)
				|| ((aSpan == bSpan) && (a.row < b.row))
				|| ((aSpan == bSpan)
					&& (a.row == b.row)
					&& a.cell
					&& b.cell
					&& (a.cell->cell->column < b.cell->cell->column));
		});
	for (const auto &spanned : spannedCells) {
		if (!spanned.cell || !spanned.cell->cell || extra <= 0) {
			break;
		}
		const auto from = std::clamp(spanned.cell->cell->column, 0, columnCount);
		const auto to = std::clamp(
			spanned.cell->cell->column + spanned.cell->cell->colspan,
			0,
			columnCount);
		if (from >= to) {
			continue;
		}
		const auto preferredWidth = spanned.cell->preferredWidth
			+ padding.left()
			+ padding.right();
		const auto currentWidth = TableSpanWidth(
			result,
			from,
			to - from,
			border);
		const auto delta = std::min(
			std::max(preferredWidth - currentWidth, 0),
			extra);
		DistributeSpanDelta(&result, from, to, delta);
		extra -= delta;
	}

	if (extra > 0) {
		const auto base = extra / columnCount;
		const auto tail = extra % columnCount;
		for (auto column = 0; column != columnCount; ++column) {
			result[column] += base + ((column < tail) ? 1 : 0);
		}
	}
	return result;
}

void SetSimpleMarkedTextLeaf(
		Ui::Text::String *leaf,
		const style::TextStyle &textStyle,
		const TextWithEntities &text,
		int minResizeWidth,
		bool rtl) {
	*leaf = Ui::Text::String(TextMinResizeWidth(minResizeWidth));
	leaf->setMarkedText(
		textStyle,
		text,
		rtl ? kIvMarkedTextOptionsRtl : kIvMarkedTextOptions);
}

void SetPlainTextLeaf(
		Ui::Text::String *leaf,
		const style::TextStyle &textStyle,
		const QString &text,
		int minResizeWidth,
		bool rtl) {
	SetSimpleMarkedTextLeaf(
		leaf,
		textStyle,
		TextWithEntities::Simple(text),
		minResizeWidth,
		rtl);
}

void PopulateCodeBlockLeaf(
		Ui::Text::String *leaf,
		Spellchecker::HighlightProcessId *syntaxHighlightProcessId,
		const TextWithEntities &codeText,
		const std::vector<PreparedLink> &codeLinks,
		const QString &codeLanguage,
		const std::vector<PreparedFormulaSlot> *formulas,
		InlineFormulaObjectCache *inlineFormulaObjects,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		const style::Markdown &st,
		bool allowAsyncSyntaxHighlighting,
		CodeBlockSyntaxHighlightTracker *syntaxHighlightTracker,
		Fn<void()> repaint,
		Fn<void(QRect)> repaintRect,
		Fn<bool(const ClickContext&)> spoilerLinkFilter) {
	auto display = CodeBlockDisplayText(codeText);
	auto highlightRequest = TextWithEntities();
	highlightRequest.text = display.text;
	if (!highlightRequest.text.isEmpty()) {
		highlightRequest.entities.push_back(EntityInText(
			EntityType::Pre,
			0,
			highlightRequest.text.size(),
			codeLanguage));
	}
	const auto processId = allowAsyncSyntaxHighlighting
		? (syntaxHighlightTracker
			? syntaxHighlightTracker->tryHighlightSyntax(
				highlightRequest.text,
				codeLanguage,
				highlightRequest)
			: Spellchecker::TryHighlightSyntax(highlightRequest))
		: 0;
	for (const auto &entity : highlightRequest.entities) {
		if (entity.type() == EntityType::Colorized
			&& entity.length() > 0) {
			display.entities.push_back(entity);
		}
	}
	SortEntities(&display);
	SetTextLeaf(
		leaf,
		st.code,
		st,
		display,
		formulas,
		inlineFormulaObjects,
		mediaRuntime,
		CodeTextMinResizeWidth(st),
		false,
		std::move(repaint),
		std::move(repaintRect),
		std::move(spoilerLinkFilter));
	BindLinks(leaf, codeLinks);
	if (syntaxHighlightProcessId) {
		*syntaxHighlightProcessId = processId;
	}
}

[[nodiscard]] int LeafHeight(
		const Ui::Text::String &leaf,
		const style::TextStyle &textStyle,
		int width) {
	return std::max(
		leaf.countHeight(width),
		TextLineHeight(textStyle));
}

[[nodiscard]] int LeafHeightWithLineLimit(
		const Ui::Text::String &leaf,
		const style::TextStyle &textStyle,
		int width,
		int lines) {
	auto result = LeafHeight(leaf, textStyle, width);
	if (lines > 0) {
		result = std::min(result, lines * TextLineHeight(textStyle));
	}
	return result;
}

[[nodiscard]] QRect PaddedBand(
		int left,
		int width,
		QMargins padding) {
	const auto paddedLeft = left + padding.left();
	const auto paddedWidth = std::max(
		width - padding.left() - padding.right(),
		1);
	return QRect(paddedLeft, 0, paddedWidth, 0);
}

[[nodiscard]] QRect ArticleTextBand(
		int fallbackLeft,
		int fallbackWidth,
		const style::Markdown &st,
		const LayoutContext &context) {
	return context.useArticleBands
		? PaddedBand(
			context.articleLeft,
			context.articleWidth,
			st.textPadding)
		: QRect(fallbackLeft, 0, std::max(fallbackWidth, 1), 0);
}

[[nodiscard]] int LimitedMediaWidth(
		int availableWidth,
		int intrinsicWidth) {
	const auto scaledIntrinsic = style::ConvertScale(intrinsicWidth);
	const auto limit = (scaledIntrinsic > 0)
		? (2 * scaledIntrinsic)
		: availableWidth;
	return std::clamp(limit, 1, std::max(availableWidth, 1));
}

void ApplyMediaBlockGeometry(
		LaidOutBlock *block,
		QRect geometry,
		const style::Markdown &st,
		double mediaPixelScale) {
	if (!block->mediaBlock) {
		return;
	}
	block->mediaBlock->setMediaPixelScale(mediaPixelScale);
	block->mediaBlock->setGeometry(geometry);
	auto actual = block->mediaBlock->geometry();
	if (actual.width() < geometry.width() && actual.x() == geometry.x()) {
		geometry.moveLeft(
			geometry.x() + std::max((geometry.width() - actual.width()) / 2, 0));
		block->mediaBlock->setGeometry(geometry);
		actual = block->mediaBlock->geometry();
	}
	block->mediaRect = actual;

	// Media blocks without text report their top as the baseline, while
	// the consumers (like list item marker placement) expect a text line
	// baseline. Treat the media top as the top of a normal text line, so
	// that markers are placed as if a text line started at the media top.
	block->firstLineBaseline = std::max(
		block->mediaBlock->firstLineBaseline(),
		TextLineBaseline(st.body, block->mediaRect.y()));

	block->visibleMediaRect = block->mediaRect;
}

void FillMediaCaption(
		LaidOutBlock *block,
		const PreparedBlock &prepared,
		const std::vector<PreparedFormulaSlot> *formulas,
		InlineFormulaObjectCache *inlineFormulaObjects,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		const style::Markdown &st,
		LayoutContext context) {
	if (prepared.text.text.isEmpty() && !prepared.forceTextSegment) {
		return;
	}
	block->supplementary = prepared.supplementary;
	BuildOrReuseMarkedTextLeaf(
		&block->leaf,
		CachedTextLeafSlot::Leaf,
		prepared,
		st.body,
		st,
		prepared.text,
		prepared.links,
		formulas,
		inlineFormulaObjects,
		mediaRuntime,
		FlowTextMinResizeWidth(st.body),
		context);
	if (prepared.text.text.isEmpty() && !prepared.editPlaceholderText.isEmpty()) {
		BuildOrReuseEditPlaceholderLeaf(
			&block->placeholderText,
			&block->placeholderLeaf,
			prepared,
			prepared.editPlaceholderText,
			st.body,
			PlainTextMinResizeWidth(st.body),
			context);
	}
}

[[nodiscard]] LaidOutBlockLogicalGeometry ExtractLogicalGeometry(
		const LaidOutBlock &block) {
	return {
		.outer = block.outer,
		.headerRect = block.headerRect,
		.bodyRect = block.bodyRect,
		.iconRect = block.iconRect,
		.textRect = block.textRect,
		.labelRect = block.labelRect,
		.subtitleRect = block.subtitleRect,
		.actionRect = block.actionRect,
		.markerRect = block.markerRect,
		.contentRect = block.contentRect,
		.formulaRect = block.formulaRect,
		.tableRect = block.tableRect,
		.mediaRect = block.mediaRect,
		.thumbnailRect = block.thumbnailRect,
		.markerCenter = block.markerCenter,
	};
}

[[nodiscard]] LaidOutBlock FinalizeLaidOutBlock(LaidOutBlock block) {
	block.logicalGeometry = ExtractLogicalGeometry(block);
	return block;
}

void ClearBlockGeometry(LaidOutBlock *block) {
	if (!block) {
		return;
	}
	block->outer = QRect();
	block->headerRect = QRect();
	block->bodyRect = QRect();
	block->iconRect = QRect();
	block->textRect = QRect();
	block->labelRect = QRect();
	block->subtitleRect = QRect();
	block->actionRect = QRect();
	block->markerRect = QRect();
	block->contentRect = QRect();
	block->formulaRect = QRect();
	block->tableRect = QRect();
	block->mediaRect = QRect();
	block->thumbnailRect = QRect();
	block->visibleFormulaRect = QRect();
	block->scrollViewportRect = QRect();
	block->scrollLogicalContentRect = QRect();
	block->scrollScrollbarTrackRect = QRect();
	block->scrollScrollbarThumbRect = QRect();
	block->visibleTableRect = QRect();
	block->tableScrollbarTrackRect = QRect();
	block->tableScrollbarThumbRect = QRect();
	block->visibleMediaRect = QRect();
	block->markerCenter = QPoint();
	block->logicalGeometry = {};
	block->textWidth = 0;
	block->labelWidth = 0;
	block->subtitleWidth = 0;
	block->actionWidth = 0;
	block->markerWidth = 0;
	block->firstLineBaseline = -1;
	block->formulaAlign = style::al_left;
	block->overflowed = false;
	block->insideHorizontalScroll = false;
	block->horizontalScrollMax = 0;
	block->horizontalScrollAncestorShift = 0;
	block->tableColumnWidths.clear();
}

void ResetTableCellGeometry(LaidOutTableCell *cell) {
	if (!cell) {
		return;
	}
	cell->logicalOuter = QRect();
	cell->logicalTextRect = QRect();
	cell->outer = QRect();
	cell->textRect = QRect();
	cell->textWidth = 0;
}

void ResetTableRowGeometry(LaidOutTableRow *row) {
	if (!row) {
		return;
	}
	row->logicalOuter = QRect();
	row->outer = QRect();
	for (auto &cell : row->cells) {
		ResetTableCellGeometry(&cell);
	}
}

[[nodiscard]] bool MissingRetainedPlaceholderLeaf(
		bool usePlaceholder,
		const Ui::Text::String &leaf) {
	return usePlaceholder && leaf.isEmpty();
}

void FinishBlockGeometry(LaidOutBlock *block) {
	if (!block) {
		return;
	}
	block->logicalGeometry = ExtractLogicalGeometry(*block);
}

[[nodiscard]] int ScrollbarReserveHeight(
		bool scrollOwner,
		int horizontalScrollMax,
		const style::Markdown &st) {
	return (scrollOwner && (horizontalScrollMax > 0))
		? (st.table.scrollbarSkip + st.table.scrollbarHeight)
		: 0;
}

void CopyCachedTextLeaf(
		CachedTextLeafPool *pool,
		CachedTextLeafKey key,
		CachedTextLeafSourceSignature source,
		Ui::Text::String *leaf,
		Spellchecker::HighlightProcessId syntaxHighlightProcessId = 0) {
	if (!pool || !leaf || leaf->isEmpty()) {
		return;
	}
	pool->entries.insert_or_assign(
		std::move(key),
		CachedTextLeafEntry{
			.leaf = std::move(*leaf),
			.source = std::move(source),
			.syntaxHighlightProcessId = syntaxHighlightProcessId,
		});
	*leaf = Ui::Text::String();
}

void CopyBlockCachedTextLeafs(
		const PreparedBlock &prepared,
		LaidOutBlock &block,
		const style::Markdown &st,
		CachedTextLeafPool *pool,
		const std::vector<int> &preparedPath,
		bool rtl) {
	const auto copyBlockLeaf = [&](CachedTextLeafSlot slot,
			CachedTextLeafSourceSignature source,
			Ui::Text::String *leaf,
			Spellchecker::HighlightProcessId syntaxHighlightProcessId = 0) {
		CopyCachedTextLeaf(
			pool,
			BlockCachedTextLeafKey(slot, prepared, preparedPath),
			std::move(source),
			leaf,
			syntaxHighlightProcessId);
	};
	const auto copyTableCellLeaf = [&](
			CachedTextLeafSlot slot,
			const PreparedTableCell &preparedCell,
			int tableRowIndex,
			int tableCellIndex,
			CachedTextLeafSourceSignature source,
			Ui::Text::String *leaf) {
		CopyCachedTextLeaf(
			pool,
			TableCellCachedTextLeafKey(
				slot,
				preparedCell,
				preparedPath,
				tableRowIndex,
				tableCellIndex),
			std::move(source),
			leaf);
	};

	if (!block.marker.isEmpty()) {
		copyBlockLeaf(
			CachedTextLeafSlot::Marker,
			PlainTextLeafSourceSignature(
				ListMarkerText(prepared),
				st.body,
				PlainTextMinResizeWidth(st.body),
				false),
			&block.marker);
	}

	switch (prepared.kind) {
	case PreparedBlockKind::Paragraph:
	case PreparedBlockKind::Thinking:
	case PreparedBlockKind::Heading: {
		const auto &textStyle = TextStyleFor(prepared, st);
		const auto &placeholderStyle = EditPlaceholderTextStyleFor(
			prepared,
			st);
		copyBlockLeaf(
			CachedTextLeafSlot::Leaf,
			MarkedTextLeafSourceSignature(
				prepared.text,
				textStyle,
				FlowBlockMinimumWidth(prepared, st),
				rtl),
			&block.leaf);
		copyBlockLeaf(
			CachedTextLeafSlot::Placeholder,
			MarkedTextLeafSourceSignature(
				EditPlaceholderTextValue(
					prepared,
					prepared.editPlaceholderText),
				placeholderStyle,
				PlainTextMinResizeWidth(placeholderStyle),
				rtl),
			&block.placeholderLeaf);
	} break;
	case PreparedBlockKind::CodeBlock:
		copyBlockLeaf(
			CachedTextLeafSlot::Leaf,
			CodeTextLeafSourceSignature(prepared, st),
			&block.leaf,
			block.syntaxHighlightProcessId);
		copyBlockLeaf(
			CachedTextLeafSlot::Placeholder,
			PlainTextLeafSourceSignature(
				prepared.editPlaceholderText,
				st.code,
				PlainTextMinResizeWidth(st.code),
				rtl),
			&block.placeholderLeaf);
		break;
	case PreparedBlockKind::DisplayMath:
		copyBlockLeaf(
			CachedTextLeafSlot::Placeholder,
			PlainTextLeafSourceSignature(
				prepared.editPlaceholderText,
				st.displayMath.fallbackStyle,
				DisplayMathFallbackTextMinResizeWidth(st),
				rtl),
				&block.placeholderLeaf);
		copyBlockLeaf(
			CachedTextLeafSlot::Fallback,
			MarkedTextLeafSourceSignature(
				DisplayMathFallbackText(),
				st.displayMath.fallbackStyle,
				DisplayMathFallbackTextMinResizeWidth(st),
				false),
			&block.fallbackLeaf);
		break;
	case PreparedBlockKind::Table: {
		copyBlockLeaf(
			CachedTextLeafSlot::Leaf,
			MarkedTextLeafSourceSignature(
				prepared.text,
				st.body,
				FlowTextMinResizeWidth(st.body),
				rtl),
			&block.leaf);
		copyBlockLeaf(
			CachedTextLeafSlot::Placeholder,
			PlainTextLeafSourceSignature(
				prepared.editPlaceholderText,
				st.body,
				PlainTextMinResizeWidth(st.body),
				rtl),
			&block.placeholderLeaf);
		const auto rowCount = std::min(
			int(prepared.tableRows.size()),
			int(block.tableRows.size()));
		for (auto rowIndex = 0; rowIndex != rowCount; ++rowIndex) {
			const auto cellCount = std::min(
				int(prepared.tableRows[rowIndex].cells.size()),
				int(block.tableRows[rowIndex].cells.size()));
			for (auto cellIndex = 0; cellIndex != cellCount; ++cellIndex) {
				const auto &preparedCell = prepared.tableRows[rowIndex].cells[cellIndex];
				auto &cell = block.tableRows[rowIndex].cells[cellIndex];
				const auto &textStyle = preparedCell.header
					? st.table.headerStyle
					: st.table.bodyStyle;
				const auto minResizeWidth = TableCellTextMinResizeWidth(
					textStyle,
					st);
				copyTableCellLeaf(
					CachedTextLeafSlot::TableCellText,
					preparedCell,
					rowIndex,
					cellIndex,
					MarkedTextLeafSourceSignature(
						preparedCell.text,
						textStyle,
						minResizeWidth,
						rtl),
					&cell.leaf);
				copyTableCellLeaf(
					CachedTextLeafSlot::TableCellPlaceholder,
					preparedCell,
					rowIndex,
					cellIndex,
					PlainTextLeafSourceSignature(
						preparedCell.editPlaceholderText,
						textStyle,
						minResizeWidth,
						rtl),
					&cell.placeholderLeaf);
			}
		}
	} break;
	case PreparedBlockKind::Details:
		copyBlockLeaf(
			CachedTextLeafSlot::Leaf,
			MarkedTextLeafSourceSignature(
				prepared.text,
				st.details.summaryStyle,
				FlowTextMinResizeWidth(st.details.summaryStyle),
				rtl),
			&block.leaf);
		copyBlockLeaf(
			CachedTextLeafSlot::Placeholder,
			PlainTextLeafSourceSignature(
				prepared.editPlaceholderText,
				st.details.summaryStyle,
				PlainTextMinResizeWidth(st.details.summaryStyle),
				rtl),
			&block.placeholderLeaf);
		copyBlockLeaf(
			CachedTextLeafSlot::Action,
			PlainTextLeafSourceSignature(
				DetailsStateText(prepared.detailsOpen),
				st.details.summaryStyle,
				PlainTextMinResizeWidth(st.details.summaryStyle),
				false),
			&block.actionLeaf);
		break;
	case PreparedBlockKind::Placeholder:
		copyBlockLeaf(
			CachedTextLeafSlot::Label,
			PlainTextLeafSourceSignature(
				prepared.placeholder.label,
				st.placeholder.labelStyle,
				PlainTextMinResizeWidth(st.placeholder.labelStyle),
				rtl),
			&block.labelLeaf);
		copyBlockLeaf(
			CachedTextLeafSlot::Leaf,
			MarkedTextLeafSourceSignature(
				prepared.text,
				st.body,
				FlowTextMinResizeWidth(st.body),
				rtl),
			&block.leaf);
		copyBlockLeaf(
			CachedTextLeafSlot::Placeholder,
			PlainTextLeafSourceSignature(
				prepared.editPlaceholderText,
				st.body,
				PlainTextMinResizeWidth(st.body),
				rtl),
			&block.placeholderLeaf);
		break;
	case PreparedBlockKind::RelatedArticle:
		copyBlockLeaf(
			CachedTextLeafSlot::Label,
			PlainTextLeafSourceSignature(
				prepared.relatedArticle.title,
				st.relatedArticle.titleStyle,
				PlainTextMinResizeWidth(st.relatedArticle.titleStyle),
				rtl),
				&block.labelLeaf);
		copyBlockLeaf(
			CachedTextLeafSlot::Subtitle,
			PlainTextLeafSourceSignature(
				prepared.relatedArticle.description,
				st.relatedArticle.subtitleStyle,
				PlainTextMinResizeWidth(st.relatedArticle.subtitleStyle),
				rtl),
				&block.subtitleLeaf);
		copyBlockLeaf(
			CachedTextLeafSlot::Action,
			PlainTextLeafSourceSignature(
				prepared.relatedArticle.footer,
				st.relatedArticle.footerStyle,
				PlainTextMinResizeWidth(st.relatedArticle.footerStyle),
				rtl),
			&block.actionLeaf);
		break;
	case PreparedBlockKind::EmbedPost:
		copyBlockLeaf(
			CachedTextLeafSlot::Label,
			PlainTextLeafSourceSignature(
				prepared.embedPost.author,
				st.embedPost.authorStyle,
				PlainTextMinResizeWidth(st.embedPost.authorStyle),
				rtl),
			&block.labelLeaf);
		copyBlockLeaf(
			CachedTextLeafSlot::Subtitle,
			PlainTextLeafSourceSignature(
				prepared.embedPost.dateText,
				st.embedPost.dateStyle,
				PlainTextMinResizeWidth(st.embedPost.dateStyle),
				rtl),
			&block.subtitleLeaf);
		copyBlockLeaf(
			CachedTextLeafSlot::Leaf,
			MarkedTextLeafSourceSignature(
				prepared.text,
				st.body,
				FlowTextMinResizeWidth(st.body),
				rtl),
			&block.leaf);
		copyBlockLeaf(
			CachedTextLeafSlot::Placeholder,
			PlainTextLeafSourceSignature(
				prepared.editPlaceholderText,
				st.body,
				PlainTextMinResizeWidth(st.body),
				rtl),
			&block.placeholderLeaf);
		break;
	case PreparedBlockKind::Photo:
	case PreparedBlockKind::Video:
	case PreparedBlockKind::Audio:
	case PreparedBlockKind::Map:
	case PreparedBlockKind::Channel:
	case PreparedBlockKind::GroupedMedia:
		copyBlockLeaf(
			CachedTextLeafSlot::Leaf,
			MarkedTextLeafSourceSignature(
				prepared.text,
				st.body,
				FlowTextMinResizeWidth(st.body),
				rtl),
			&block.leaf);
		copyBlockLeaf(
			CachedTextLeafSlot::Placeholder,
			PlainTextLeafSourceSignature(
				prepared.editPlaceholderText,
				st.body,
				PlainTextMinResizeWidth(st.body),
				rtl),
			&block.placeholderLeaf);
		break;
	case PreparedBlockKind::Rule:
	case PreparedBlockKind::Quote:
	case PreparedBlockKind::List:
	case PreparedBlockKind::ListItem:
		break;
	}
}

void CopyBlockCachedTextLeafs(
		const std::vector<PreparedBlock> &preparedBlocks,
		std::vector<LaidOutBlock> *blocks,
		const style::Markdown &st,
		CachedTextLeafPool *pool,
		std::vector<int> *preparedPath,
		bool rtl) {
	if (!pool || !blocks || !preparedPath) {
		return;
	}
	const auto count = std::min(int(preparedBlocks.size()), int(blocks->size()));
	for (auto i = 0; i != count; ++i) {
		preparedPath->push_back(i);
		CopyBlockCachedTextLeafs(
			preparedBlocks[i],
			(*blocks)[i],
			st,
			pool,
			*preparedPath,
			rtl);
		preparedPath->pop_back();
	}
}

} // namespace

bool TextNeedsRetainedLeaf(const QString &text) {
	const auto size = int(text.size());
	for (auto i = 0; i != size; ++i) {
		const auto ch = text[i];
		if (Ui::Text::IsTrimmed(ch)
			|| Ui::Text::IsReplacedBySpace(ch)
			|| Ui::Text::IsDiacritic(ch)
			|| ch.isLowSurrogate()) {
			continue;
		} else if (!ch.isHighSurrogate()) {
			return true;
		} else if (i + 1 != size && text[i + 1].isLowSurrogate()) {
			if (QChar::surrogateToUcs4(ch, text[i + 1]) < 0xE0000) {
				return true;
			}
			++i;
		}
	}
	return false;
}

bool MissingRetainedLeaf(
		const QString &text,
		const Ui::Text::String &leaf) {
	return TextNeedsRetainedLeaf(text) && leaf.isEmpty();
}

void BuildOrReuseMarkedTextLeaf(
		Ui::Text::String *leaf,
		CachedTextLeafSlot slot,
		const PreparedBlock &prepared,
		const style::TextStyle &textStyle,
		const style::Markdown &st,
		const TextWithEntities &text,
		const std::vector<PreparedLink> &links,
		const std::vector<PreparedFormulaSlot> *formulas,
		InlineFormulaObjectCache *inlineFormulaObjects,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		int minResizeWidth,
		LayoutContext context) {
	BuildOrReuseCachedTextLeaf(
		leaf,
		nullptr,
		context,
		BlockCachedTextLeafKey(slot, prepared, context.preparedPath),
		MarkedTextLeafSourceSignature(
			text,
			textStyle,
			minResizeWidth,
			context.rtl),
		[&](Ui::Text::String *leaf,
				Spellchecker::HighlightProcessId*) {
			SetTextLeaf(
				leaf,
				textStyle,
				st,
				text,
				formulas,
				inlineFormulaObjects,
				mediaRuntime,
				minResizeWidth,
				context.rtl,
				context.repaint,
				context.repaintRect);
			BindLinks(leaf, links);
		});
	BindLinks(leaf, links);
}

void BuildOrReusePlainTextLeaf(
		Ui::Text::String *leaf,
		CachedTextLeafSlot slot,
		const PreparedBlock &prepared,
		const style::TextStyle &textStyle,
		const QString &text,
		int minResizeWidth,
		LayoutContext context) {
	BuildOrReuseCachedTextLeaf(
		leaf,
		nullptr,
		context,
		BlockCachedTextLeafKey(slot, prepared, context.preparedPath),
		PlainTextLeafSourceSignature(
			text,
			textStyle,
			minResizeWidth,
			context.rtl),
		[&](Ui::Text::String *leaf,
				Spellchecker::HighlightProcessId*) {
			SetPlainTextLeaf(
				leaf,
				textStyle,
				text,
				minResizeWidth,
				context.rtl);
		});
}

void BuildOrReuseEditPlaceholderLeaf(
		QString *placeholderText,
		Ui::Text::String *placeholderLeaf,
		const PreparedBlock &prepared,
		const QString &text,
		const style::TextStyle &textStyle,
		int minResizeWidth,
		LayoutContext context) {
	if (text.isEmpty()) {
		return;
	}
	*placeholderText = text;
	const auto value = EditPlaceholderTextValue(prepared, text);
	BuildOrReuseCachedTextLeaf(
		placeholderLeaf,
		nullptr,
		context,
		BlockCachedTextLeafKey(
			CachedTextLeafSlot::Placeholder,
			prepared,
			context.preparedPath),
		MarkedTextLeafSourceSignature(
			value,
			textStyle,
			minResizeWidth,
			context.rtl),
		[&](Ui::Text::String *leaf,
				Spellchecker::HighlightProcessId*) {
			SetSimpleMarkedTextLeaf(
				leaf,
				textStyle,
				value,
				minResizeWidth,
				context.rtl);
		});
}

void CopyCachedTextLeafs(
		const std::vector<PreparedBlock> &preparedBlocks,
		std::vector<LaidOutBlock> *blocks,
		const style::Markdown &st,
		CachedTextLeafPool *pool,
		bool rtl) {
	if (!pool) {
		return;
	}
	pool->entries.clear();
	auto preparedPath = std::vector<int>();
	CopyBlockCachedTextLeafs(
		preparedBlocks,
		blocks,
		st,
		pool,
		&preparedPath,
		rtl);
}

size_t CachedTextLeafKeyHasher::operator()(
		const CachedTextLeafKey &value) const noexcept {
	auto result = CombineHash(0, size_t(value.slot));
	result = CombineHash(result, size_t(value.identityKind));
	switch (value.identityKind) {
	case CachedTextLeafIdentityKind::PreparedPath:
		result = CombineHash(result, size_t(value.preparedPath.size() + 1));
		for (const auto step : value.preparedPath) {
			result = CombineHash(result, size_t(step + 1));
		}
		result = CombineHash(result, size_t(value.tableRowIndex + 1));
		return CombineHash(result, size_t(value.tableCellIndex + 1));
	case CachedTextLeafIdentityKind::EditBlock:
		return CombineHash(result, HashPreparedEditBlockSource(value.editBlock));
	case CachedTextLeafIdentityKind::EditListItem:
		return CombineHash(
			result,
			HashPreparedEditListItemSource(value.editListItem));
	case CachedTextLeafIdentityKind::EditTableCell:
		return CombineHash(
			result,
			HashPreparedEditTableCellSource(value.editTableCell));
	case CachedTextLeafIdentityKind::EditLeaf:
		return CombineHash(result, HashPreparedEditLeafSource(value.editLeaf));
	}
	return result;
}

LayoutContextScope::LayoutContextScope(const LayoutContext &context)
: _previous(CurrentLayoutContext) {
	CurrentLayoutContext = &context;
}

LayoutContextScope::~LayoutContextScope() {
	CurrentLayoutContext = _previous;
}

void FillMediaCaptionContent(
		LaidOutBlock *block,
		const PreparedBlock &prepared,
		const std::vector<PreparedFormulaSlot> *formulas,
		InlineFormulaObjectCache *inlineFormulaObjects,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		const style::Markdown &st,
		LayoutContext context) {
	FillMediaCaption(
		block,
		prepared,
		formulas,
		inlineFormulaObjects,
		mediaRuntime,
		st,
		context);
}

void LayoutMediaCaption(
		LaidOutBlock *block,
		const PreparedBlock &prepared,
		const std::vector<PreparedFormulaSlot> *formulas,
		InlineFormulaObjectCache *inlineFormulaObjects,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		int skip,
		int *bottom,
		LayoutContext context) {
	FillMediaCaption(
		block,
		prepared,
		formulas,
		inlineFormulaObjects,
		mediaRuntime,
		st,
		context);
	const auto counted = LayoutMediaCaptionGeometry(
		block,
		prepared,
		st,
		left,
		top,
		width,
		skip,
		bottom,
		context);
	Expects(counted);
}

bool LayoutMediaCaptionGeometry(
		LaidOutBlock *block,
		const PreparedBlock &prepared,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		int skip,
		int *bottom,
		LayoutContext context) {
	if (!block || !bottom) {
		return false;
	}
	if (prepared.text.text.isEmpty() && !prepared.forceTextSegment) {
		return true;
	}
	if (MissingRetainedLeaf(prepared.text.text, block->leaf)) {
		return false;
	}
	block->supplementary = prepared.supplementary;
	const auto textBand = ArticleTextBand(left, width, st, context);
	block->textWidth = std::max(textBand.width(), 1);
	block->textRect = QRect(
		textBand.x(),
		top + skip,
		block->textWidth,
		ResolveEditableHeight(
			std::max(
				block->leaf.countHeight(block->textWidth),
				TextLineHeight(st.body)),
			context));
	*bottom = block->textRect.y() + block->textRect.height();
	return true;
}

[[nodiscard]] int SingleDigitOrderedMarkerWidth(
		const style::Markdown &st) {
	return std::max(
		st.body.font->width(u"8."_q),
		st.body.font->width(u"8)"_q));
}

QString CodeBlockDisplayText(const QString &text) {
	auto result = QString();
	result.reserve(text.size());

	auto column = 0;
	for (const auto ch : text) {
		if (ch == QChar::Tabulation) {
			const auto count = kCodeTabColumns - (column % kCodeTabColumns);
			for (auto i = 0; i != count; ++i) {
				result.append(QChar::Space);
			}
			column += count;
			continue;
		}
		result.append(ch);
		if (Ui::Text::IsNewline(ch)) {
			column = 0;
		} else {
			++column;
		}
	}
	if (result.isEmpty() || Ui::Text::IsTrimmed(result.back())) {
		result.append(QChar(kCodeTrailingGuard));
	}
	return result;
}

TextWithEntities CodeBlockDisplayText(TextWithEntities text) {
	auto result = TextWithEntities();
	const auto sourceLength = int(text.text.size());
	result.text.reserve(sourceLength);
	result.entities.reserve(text.entities.size());

	auto offsets = std::vector<int>();
	offsets.reserve(sourceLength + 1);
	auto column = 0;
	for (auto i = 0; i != sourceLength; ++i) {
		offsets.push_back(result.text.size());
		const auto ch = text.text[i];
		if (ch == QChar::Tabulation) {
			const auto count = kCodeTabColumns - (column % kCodeTabColumns);
			for (auto j = 0; j != count; ++j) {
				result.text.append(QChar::Space);
			}
			column += count;
			continue;
		}
		result.text.append(ch);
		if (Ui::Text::IsNewline(ch)) {
			column = 0;
		} else {
			++column;
		}
	}
	offsets.push_back(result.text.size());

	for (const auto &entity : text.entities) {
		const auto from = entity.offset();
		const auto length = entity.length();
		if (entity.type() == EntityType::Pre
			|| from < 0
			|| from >= sourceLength
			|| length <= 0) {
			continue;
		}
		const auto till = from + std::min(length, sourceLength - from);
		if (till <= from) {
			continue;
		}
		const auto displayFrom = offsets[from];
		const auto displayTill = offsets[till];
		if (displayTill <= displayFrom) {
			continue;
		}
		result.entities.push_back(EntityInText(
			entity.type(),
			displayFrom,
			displayTill - displayFrom,
			entity.data()));
	}
	if (result.text.isEmpty() || Ui::Text::IsTrimmed(result.text.back())) {
		result.text.append(QChar(kCodeTrailingGuard));
	}
	return result;
}

bool IsFlowKind(PreparedBlockKind kind) {
	return (kind == PreparedBlockKind::Paragraph)
		|| (kind == PreparedBlockKind::Thinking)
		|| (kind == PreparedBlockKind::Heading);
}

bool IsAnchorOnlyBlock(const PreparedBlock &block) {
	return (block.kind == PreparedBlockKind::Paragraph)
		&& !block.anchorId.isEmpty()
		&& !TextNeedsRetainedLeaf(block.text.text)
		&& block.text.entities.empty()
		&& block.links.empty()
		&& block.children.empty();
}

QString ListMarkerText(const PreparedBlock &block) {
	if (block.listKind == ListKind::Ordered) {
		if (!block.orderedMarkerText.isEmpty()) {
			return FormatPreparedOrderedRawMarkerText(
				block.orderedMarkerText,
				block.listDelimiter);
		}
		if (!block.articleOrderedMarkerText.isEmpty()) {
			return FormatPreparedOrderedRawMarkerText(
				block.articleOrderedMarkerText,
				block.listDelimiter);
		}
		const auto delimiter = (block.listDelimiter == ListDelimiter::Parenthesis)
			? u")"_q
			: u"."_q;
		return QString::number(block.orderedNumber) + delimiter;
	}
	return QString();
}

int TextLineHeight(const style::TextStyle &style) {
	return std::max(style.lineHeight, style.font->height);
}

int TextLineAscent(const style::TextStyle &style) {
	if (style.qtextEditLineMetrics) {
		const auto lineHeight = QFixed(TextLineHeight(style));
		const auto leading = std::max(style.font->fleading, QFixed());
		return std::clamp(
			(lineHeight * 4 / 5) - leading,
			QFixed(),
			lineHeight).toInt();
	}
	const auto lineHeight = TextLineHeight(style);
	const auto textTop = std::max(lineHeight - style.font->height, 0) / 2;
	return textTop + style.font->ascent;
}

int TextLineBaseline(
		const style::TextStyle &style,
		int top) {
	return top + TextLineAscent(style);
}

int ResolveEditableHeight(
		int naturalHeight,
		LayoutContext context) {
	const auto state = context.editableHeightOverride;
	if (!state) {
		return naturalHeight;
	}
	const auto index = state->nextEditableIndex++;
	return (index == state->editableIndex)
		? std::max(state->height, 1)
		: naturalHeight;
}

[[nodiscard]] int LeafFirstLineBaseline(
		const Ui::Text::String &leaf,
		const QRect &textRect,
		const style::TextStyle &style) {
	const auto lines = leaf.countLinesGeometry(textRect.width());
	return textRect.y() + (lines.empty()
		? TextLineBaseline(style)
		: lines.front().baseline);
}

QPoint BulletMarkerCenter(
		int left,
		int baseline,
		const style::Markdown &st) {
	const auto &list = st.list;
	const auto lineHeight = TextLineHeight(st.body);
	const auto markerWidth = SingleDigitOrderedMarkerWidth(st);
	const auto nominalBaseline = TextLineBaseline(st.body);
	return QPoint(
		left + list.markerWidth - list.bulletLeftShift - ((markerWidth + 1) / 2),
		baseline + (lineHeight / 2) - nominalBaseline);
}

QMargins BlockquotePadding(const style::QuoteStyle &style) {
	return style.padding
		+ QMargins(0, style.header + style.verticalSkip, 0, style.verticalSkip);
}

int HorizontalMarginsWidth(QMargins margins) {
	return std::max(margins.left(), 0) + std::max(margins.right(), 0);
}

Ui::Text::GeometryDescriptor TextGeometry(int width) {
	return Ui::Text::SimpleGeometry(std::max(width, 1), 0, 0, false);
}

int TextMinResizeWidth(int width) {
	return std::max(width, 1);
}

int ReadableTextMinWidth(const style::TextStyle &style) {
	return std::max({
		style.font->spacew * kReadableTextColumns,
		TextLineHeight(style) * kReadableTextLineHeights,
		1,
	});
}

int FlowTextMinResizeWidth(const style::TextStyle &style) {
	return ReadableTextMinWidth(style);
}

int LeafMinimumWidth(const Ui::Text::String &leaf) {
	return leaf.isEmpty()
		? 0
		: std::min(leaf.minResizeWidth(), leaf.maxWidth());
}

int FlowBlockMinimumWidth(
		const PreparedBlock &prepared,
		const style::Markdown &st) {
	if (IsAnchorOnlyBlock(prepared)
		|| (prepared.text.text.isEmpty() && !prepared.forceTextSegment)) {
		return 1;
	}
	return ReadableTextMinWidth(TextStyleFor(prepared, st));
}

int FlowBlockPreferredWidth(
		const PreparedBlock &prepared,
		const std::vector<PreparedFormulaSlot> &formulas,
		InlineFormulaObjectCache *inlineFormulaObjects,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		const style::Markdown &st,
		LayoutContext context) {
	if (IsAnchorOnlyBlock(prepared)) {
		return 1;
	}
	const auto &textStyle = TextStyleFor(prepared, st);
	const auto minResizeWidth = FlowBlockMinimumWidth(prepared, st);
	const auto usePlaceholder = prepared.text.text.isEmpty()
		&& !prepared.editPlaceholderText.isEmpty();
	if (!usePlaceholder) {
		return WithCachedTextLeaf(
			context,
			BlockCachedTextLeafKey(
				CachedTextLeafSlot::Leaf,
				prepared,
				context.preparedPath),
			MarkedTextLeafSourceSignature(
				prepared.text,
				textStyle,
				minResizeWidth,
				context.rtl),
			[&](Ui::Text::String *leaf,
					Spellchecker::HighlightProcessId*) {
				SetTextLeaf(
					leaf,
					textStyle,
					st,
					prepared.text,
					&formulas,
					inlineFormulaObjects,
					mediaRuntime,
					minResizeWidth,
					context.rtl,
					context.repaint,
					context.repaintRect);
				BindLinks(leaf, prepared.links);
			},
			[](const Ui::Text::String &leaf,
					Spellchecker::HighlightProcessId) {
				return leaf.maxWidth();
			});
	}
	const auto &placeholderStyle = EditPlaceholderTextStyleFor(prepared, st);
	const auto value = EditPlaceholderTextValue(
		prepared,
		prepared.editPlaceholderText);
	return WithCachedTextLeaf(
		context,
		BlockCachedTextLeafKey(
			CachedTextLeafSlot::Placeholder,
			prepared,
			context.preparedPath),
		MarkedTextLeafSourceSignature(
			value,
			placeholderStyle,
			PlainTextMinResizeWidth(placeholderStyle),
			context.rtl),
		[&](Ui::Text::String *leaf,
				Spellchecker::HighlightProcessId*) {
			SetSimpleMarkedTextLeaf(
				leaf,
				placeholderStyle,
				value,
				PlainTextMinResizeWidth(placeholderStyle),
				context.rtl);
		},
		[](const Ui::Text::String &leaf,
				Spellchecker::HighlightProcessId) {
			return leaf.maxWidth();
		});
}

int FlowBlockContentMinimumWidth(
		const PreparedBlock &prepared,
		const std::vector<PreparedFormulaSlot> &formulas,
		InlineFormulaObjectCache *inlineFormulaObjects,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		const style::Markdown &st,
		LayoutContext context) {
	const auto base = FlowBlockMinimumWidth(prepared, st);
	if (IsAnchorOnlyBlock(prepared) || prepared.text.text.isEmpty()) {
		return base;
	}
	const auto &textStyle = TextStyleFor(prepared, st);
	const auto minResizeWidth = FlowBlockMinimumWidth(prepared, st);
	return WithCachedTextLeaf(
		context,
		BlockCachedTextLeafKey(
			CachedTextLeafSlot::Leaf,
			prepared,
			context.preparedPath),
		MarkedTextLeafSourceSignature(
			prepared.text,
			textStyle,
			minResizeWidth,
			context.rtl),
		[&](Ui::Text::String *leaf,
				Spellchecker::HighlightProcessId*) {
			SetTextLeaf(
				leaf,
				textStyle,
				st,
				prepared.text,
				&formulas,
				inlineFormulaObjects,
				mediaRuntime,
				minResizeWidth,
				context.rtl,
				context.repaint,
				context.repaintRect);
			BindLinks(leaf, prepared.links);
		},
		[&](const Ui::Text::String &leaf,
				Spellchecker::HighlightProcessId) {
			return std::max(base, LeafMinimumWidth(leaf));
		});
}

int DetailsSummaryContentMinimumWidth(
		const PreparedBlock &prepared,
		const std::vector<PreparedFormulaSlot> &formulas,
		InlineFormulaObjectCache *inlineFormulaObjects,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		const style::Markdown &st,
		LayoutContext context) {
	const auto base = ReadableTextMinWidth(st.details.summaryStyle);
	if (prepared.text.text.isEmpty()) {
		return base;
	}
	const auto minResizeWidth = FlowTextMinResizeWidth(
		st.details.summaryStyle);
	return WithCachedTextLeaf(
		context,
		BlockCachedTextLeafKey(
			CachedTextLeafSlot::Leaf,
			prepared,
			context.preparedPath),
		MarkedTextLeafSourceSignature(
			prepared.text,
			st.details.summaryStyle,
			minResizeWidth,
			context.rtl),
		[&](Ui::Text::String *leaf,
				Spellchecker::HighlightProcessId*) {
			SetTextLeaf(
				leaf,
				st.details.summaryStyle,
				st,
				prepared.text,
				&formulas,
				inlineFormulaObjects,
				mediaRuntime,
				minResizeWidth,
				context.rtl,
				context.repaint,
				context.repaintRect);
			BindLinks(leaf, prepared.links);
		},
		[&](const Ui::Text::String &leaf,
				Spellchecker::HighlightProcessId) {
			return std::max(base, LeafMinimumWidth(leaf));
		});
}

int CodeBlockMinimumWidth(const style::Markdown &st) {
	const auto padding = BlockquotePadding(st.code.pre);
	const auto content = std::max({
		st.code.font->spacew * kReadableCodeColumns,
		ReadableTextMinWidth(st.code),
		1,
	});
	return HorizontalMarginsWidth(padding) + content;
}

int CodeTextMinResizeWidth(const style::Markdown &st) {
	return std::max(
		CodeBlockMinimumWidth(st)
			- HorizontalMarginsWidth(BlockquotePadding(st.code.pre)),
		1);
}

int CodeBlockPreferredWidth(
		const PreparedBlock &prepared,
		const style::Markdown &st,
		LayoutContext context) {
	const auto padding = BlockquotePadding(st.code.pre);
	const auto usePlaceholder = prepared.text.text.isEmpty()
		&& !prepared.editPlaceholderText.isEmpty();
	if (usePlaceholder) {
		return HorizontalMarginsWidth(padding) + WithCachedTextLeaf(
			context,
			BlockCachedTextLeafKey(
				CachedTextLeafSlot::Placeholder,
				prepared,
				context.preparedPath),
			PlainTextLeafSourceSignature(
				prepared.editPlaceholderText,
				st.code,
				PlainTextMinResizeWidth(st.code),
				context.rtl),
			[&](Ui::Text::String *leaf,
					Spellchecker::HighlightProcessId*) {
				SetPlainTextLeaf(
					leaf,
					st.code,
					prepared.editPlaceholderText,
					PlainTextMinResizeWidth(st.code),
					context.rtl);
			},
			[](const Ui::Text::String &leaf,
					Spellchecker::HighlightProcessId) {
				return leaf.maxWidth();
			});
	}
	return HorizontalMarginsWidth(padding) + WithCachedTextLeaf(
		context,
		BlockCachedTextLeafKey(
			CachedTextLeafSlot::Leaf,
			prepared,
			context.preparedPath),
		CodeTextLeafSourceSignature(prepared, st),
		[&](Ui::Text::String *leaf,
				Spellchecker::HighlightProcessId *syntaxHighlightProcessId) {
			PopulateCodeBlockLeaf(
				leaf,
				syntaxHighlightProcessId,
				prepared.text,
				prepared.links,
				prepared.codeLanguage,
				nullptr,
				nullptr,
				nullptr,
				st,
				context.allowAsyncSyntaxHighlighting,
				context.syntaxHighlightTracker,
				context.repaint,
				context.repaintRect,
				context.spoilerLinkFilter);
		},
		[](const Ui::Text::String &leaf,
				Spellchecker::HighlightProcessId) {
			return leaf.maxWidth();
		});
}

int DisplayMathMinimumWidth(
		const PreparedBlock &prepared,
		const std::vector<PreparedFormulaSlot> &formulas,
		const style::Markdown &st) {
	const auto &padding = st.displayMath.padding;
	if (const auto formula = PreparedFormulaFor(formulas, prepared.formulaIndex);
		formula && formula->measured.success) {
		return HorizontalMarginsWidth(padding)
			+ std::max(formula->measured.logicalSize.width(), 1);
	}
	const auto &fallbackPadding = st.displayMath.fallbackPadding;
	auto fallbackText = DisplayMathFallbackText();
	auto leaf = Ui::Text::String(ReadableTextMinWidth(st.displayMath.fallbackStyle));
	leaf.setMarkedText(
		st.displayMath.fallbackStyle,
		std::move(fallbackText),
		kIvMarkedTextOptions);
	return HorizontalMarginsWidth(padding)
		+ HorizontalMarginsWidth(fallbackPadding)
		+ std::max({
			leaf.maxWidth(),
			ReadableTextMinWidth(st.displayMath.fallbackStyle),
			1,
		});
}

int DisplayMathPreferredWidth(
		const PreparedBlock &prepared,
		const std::vector<PreparedFormulaSlot> &formulas,
		const style::Markdown &st,
		LayoutContext context) {
	const auto &padding = st.displayMath.padding;
	if (const auto formula = PreparedFormulaFor(formulas, prepared.formulaIndex);
		formula && formula->measured.success) {
		return HorizontalMarginsWidth(padding)
			+ std::max(formula->measured.logicalSize.width(), 1);
	}
	const auto &fallbackPadding = st.displayMath.fallbackPadding;
	const auto usePlaceholder = prepared.formulaTex.trimmed().isEmpty()
		&& !prepared.editPlaceholderText.isEmpty();
	if (usePlaceholder) {
		return HorizontalMarginsWidth(padding)
			+ HorizontalMarginsWidth(fallbackPadding)
			+ WithCachedTextLeaf(
				context,
				BlockCachedTextLeafKey(
					CachedTextLeafSlot::Placeholder,
					prepared,
					context.preparedPath),
				PlainTextLeafSourceSignature(
					prepared.editPlaceholderText,
					st.displayMath.fallbackStyle,
					DisplayMathFallbackTextMinResizeWidth(st),
					context.rtl),
				[&](Ui::Text::String *leaf,
						Spellchecker::HighlightProcessId*) {
					SetPlainTextLeaf(
						leaf,
						st.displayMath.fallbackStyle,
						prepared.editPlaceholderText,
						DisplayMathFallbackTextMinResizeWidth(st),
						context.rtl);
				},
				[](const Ui::Text::String &leaf,
						Spellchecker::HighlightProcessId) {
					return leaf.maxWidth();
				});
	}
	return HorizontalMarginsWidth(padding)
		+ HorizontalMarginsWidth(fallbackPadding)
		+ WithCachedTextLeaf(
			context,
			BlockCachedTextLeafKey(
				CachedTextLeafSlot::Fallback,
				prepared,
				context.preparedPath),
			MarkedTextLeafSourceSignature(
				DisplayMathFallbackText(),
				st.displayMath.fallbackStyle,
				DisplayMathFallbackTextMinResizeWidth(st),
				false),
			[&](Ui::Text::String *leaf,
					Spellchecker::HighlightProcessId*) {
				*leaf = Ui::Text::String(
					TextMinResizeWidth(
						DisplayMathFallbackTextMinResizeWidth(st)));
				leaf->setMarkedText(
					st.displayMath.fallbackStyle,
					DisplayMathFallbackText(),
					kIvMarkedTextOptions);
			},
			[](const Ui::Text::String &leaf,
					Spellchecker::HighlightProcessId) {
				return leaf.maxWidth();
			});
}

int PlainTextMinResizeWidth(const style::TextStyle &style) {
	return std::max(style.font->spacew, 1);
}

int DisplayMathFallbackTextMinResizeWidth(const style::Markdown &st) {
	return PlainTextMinResizeWidth(st.displayMath.fallbackStyle);
}

int ContainerMinimumWidth(
		int contentMinimumWidth,
		QMargins padding) {
	return std::max(contentMinimumWidth, 1) + HorizontalMarginsWidth(padding);
}

int TableMinimumGridWidth(
		int columnCount,
		const style::Markdown &st,
		bool bordered) {
	if (columnCount <= 0) {
		return 0;
	}
	const auto border = TableBorder(bordered, st);
	const auto minimum = st.table.minColumnWidth;
	return border + columnCount * (minimum + border);
}

int TableCellTextMinResizeWidth(
		const style::TextStyle &textStyle,
		const style::Markdown &st) {
	const auto &padding = st.table.cellPadding;
	return std::max({
		st.table.minColumnWidth - padding.left() - padding.right(),
		textStyle.font->spacew,
		1,
	});
}

std::vector<int> ComputeTableColumnMinimumWidths(
		std::vector<TableCellMinimumWidthConstraint> constraints,
		int columnCount,
		const style::Markdown &st,
		bool bordered) {
	// Fully empty columns may shrink below st.table.minColumnWidth,
	// to the cell padding and some non-zero content width.
	const auto &padding = st.table.cellPadding;
	const auto emptyMinimum = padding.left()
		+ padding.right()
		+ std::max(st.table.bodyStyle.font->spacew, 1);
	auto result = std::vector<int>(std::max(columnCount, 0), emptyMinimum);
	if (result.empty()) {
		return result;
	}
	const auto border = TableBorder(bordered, st);
	auto spanned = std::vector<TableCellMinimumWidthConstraint>();
	for (const auto &constraint : constraints) {
		const auto from = std::clamp(constraint.column, 0, columnCount);
		const auto to = std::clamp(
			constraint.column + constraint.colspan,
			0,
			columnCount);
		if (from >= to) {
			continue;
		} else if ((to - from) == 1) {
			result[from] = std::max(result[from], constraint.minimumWidth);
		} else {
			spanned.push_back(constraint);
		}
	}
	std::sort(
		spanned.begin(),
		spanned.end(),
		[](const TableCellMinimumWidthConstraint &a,
				const TableCellMinimumWidthConstraint &b) {
			return (a.colspan < b.colspan)
				|| ((a.colspan == b.colspan) && (a.column < b.column));
		});
	for (const auto &constraint : spanned) {
		const auto from = std::clamp(constraint.column, 0, columnCount);
		const auto to = std::clamp(
			constraint.column + constraint.colspan,
			0,
			columnCount);
		const auto current = TableSpanWidth(
			result,
			from,
			to - from,
			border);
		const auto deficit = constraint.minimumWidth - current;
		if (deficit > 0) {
			DistributeSpanDelta(&result, from, to, deficit);
		}
	}
	return result;
}

int TableGridWidth(
		const std::vector<int> &columnWidths,
		const style::Markdown &st,
		bool bordered) {
	if (columnWidths.empty()) {
		return 0;
	}
	const auto border = TableBorder(bordered, st);
	auto result = border;
	for (const auto columnWidth : columnWidths) {
		result += columnWidth + border;
	}
	return result;
}

QRect TableCellHitRect(
		const LaidOutBlock &block,
		const LaidOutTableCell &cell) {
	if (cell.outer.isEmpty() || block.tableBorder <= 0) {
		return cell.outer;
	}
	const auto expanded = cell.outer.adjusted(
		0,
		0,
		block.tableBorder,
		block.tableBorder);
	if (block.visibleTableRect.contains(cell.outer)) {
		return expanded.intersected(block.visibleTableRect);
	}
	return expanded;
}

int TableBlockContentMinimumWidth(
		const PreparedBlock &prepared,
		const std::vector<PreparedFormulaSlot> &formulas,
		InlineFormulaObjectCache *inlineFormulaObjects,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		const style::Markdown &st,
		LayoutContext context) {
	const auto captionMinimum = prepared.text.text.isEmpty()
		? 1
		: [&] {
			const auto base = FlowBlockMinimumWidth(prepared, st);
			const auto minResizeWidth = FlowTextMinResizeWidth(st.body);
			return WithCachedTextLeaf(
				context,
				BlockCachedTextLeafKey(
					CachedTextLeafSlot::Leaf,
					prepared,
					context.preparedPath),
				MarkedTextLeafSourceSignature(
					prepared.text,
					st.body,
					minResizeWidth,
					context.rtl),
				[&](Ui::Text::String *leaf,
						Spellchecker::HighlightProcessId*) {
					SetTextLeaf(
						leaf,
						st.body,
						st,
						prepared.text,
						&formulas,
						inlineFormulaObjects,
						mediaRuntime,
						minResizeWidth,
						context.rtl,
						context.repaint,
						context.repaintRect);
					BindLinks(leaf, prepared.links);
				},
				[&](const Ui::Text::String &leaf,
						Spellchecker::HighlightProcessId) {
					return std::max(base, LeafMinimumWidth(leaf));
				});
		}();
	const auto columnCount = prepared.tableColumnCount;
	if (columnCount <= 0 || prepared.tableRows.empty()) {
		return std::max(
			captionMinimum,
			TableMinimumGridWidth(columnCount, st, prepared.tableBordered));
	}
	const auto &padding = st.table.cellPadding;
	const auto paddingWidth = padding.left() + padding.right();
	auto constraints = std::vector<TableCellMinimumWidthConstraint>();
	for (auto rowIndex = 0, rowCount = int(prepared.tableRows.size());
			rowIndex != rowCount;
			++rowIndex) {
		const auto &row = prepared.tableRows[rowIndex];
		for (auto cellIndex = 0, cellCount = int(row.cells.size());
				cellIndex != cellCount;
				++cellIndex) {
			const auto &cell = row.cells[cellIndex];
			const auto usePlaceholder = cell.text.text.isEmpty()
				&& !cell.editPlaceholderText.isEmpty();
			if (cell.text.text.isEmpty() && !usePlaceholder) {
				continue;
			}
			const auto &textStyle = TableCellTextStyle(cell, st);
			const auto minResizeWidth = TableCellTextMinResizeWidth(
				textStyle,
				st);
			const auto leafMinimum = usePlaceholder
				? WithCachedTextLeaf(
					context,
					TableCellCachedTextLeafKey(
						CachedTextLeafSlot::TableCellPlaceholder,
						cell,
						context.preparedPath,
						rowIndex,
						cellIndex),
					PlainTextLeafSourceSignature(
						cell.editPlaceholderText,
						textStyle,
						minResizeWidth,
						context.rtl),
					[&](Ui::Text::String *leaf,
							Spellchecker::HighlightProcessId*) {
						SetPlainTextLeaf(
							leaf,
							textStyle,
							cell.editPlaceholderText,
							minResizeWidth,
							context.rtl);
					},
					[](const Ui::Text::String &leaf,
							Spellchecker::HighlightProcessId) {
						return LeafMinimumWidth(leaf);
					})
				: WithCachedTextLeaf(
					context,
					TableCellCachedTextLeafKey(
						CachedTextLeafSlot::TableCellText,
						cell,
						context.preparedPath,
						rowIndex,
						cellIndex),
					MarkedTextLeafSourceSignature(
						cell.text,
						textStyle,
						minResizeWidth,
						context.rtl),
					[&](Ui::Text::String *leaf,
							Spellchecker::HighlightProcessId*) {
						SetTextLeaf(
							leaf,
							textStyle,
							st,
							cell.text,
							&formulas,
							inlineFormulaObjects,
							mediaRuntime,
							minResizeWidth,
							context.rtl,
							context.repaint,
							context.repaintRect);
						BindLinks(leaf, cell.links);
					},
					[](const Ui::Text::String &leaf,
							Spellchecker::HighlightProcessId) {
						return LeafMinimumWidth(leaf);
					});
			if (leafMinimum > 0) {
				constraints.push_back({
					.column = std::max(cell.column, 0),
					.colspan = std::max(cell.colspan, 1),
					.minimumWidth = std::max(
						leafMinimum + paddingWidth,
						st.table.minColumnWidth),
				});
			}
		}
	}
	const auto columns = ComputeTableColumnMinimumWidths(
		std::move(constraints),
		columnCount,
		st,
		prepared.tableBordered);
	return std::max(
		captionMinimum,
		TableGridWidth(columns, st, prepared.tableBordered));
}

int RetainedTableBlockMinimumWidth(
		const PreparedBlock &prepared,
		const LaidOutBlock &block,
		const style::Markdown &st) {
	const auto &captionLeaf = block.placeholderLeaf.isEmpty()
		? block.leaf
		: block.placeholderLeaf;
	const auto captionMinimum = prepared.text.text.isEmpty()
		? 1
		: std::max(
			FlowBlockMinimumWidth(prepared, st),
			LeafMinimumWidth(captionLeaf));
	const auto columnCount = prepared.tableColumnCount;
	if (columnCount <= 0 || block.tableRows.empty()) {
		return std::max(
			captionMinimum,
			TableMinimumGridWidth(columnCount, st, prepared.tableBordered));
	}
	const auto &padding = st.table.cellPadding;
	const auto paddingWidth = padding.left() + padding.right();
	auto constraints = std::vector<TableCellMinimumWidthConstraint>();
	const auto rowCount = int(std::min(
		prepared.tableRows.size(),
		block.tableRows.size()));
	for (auto rowIndex = 0; rowIndex != rowCount; ++rowIndex) {
		const auto &preparedCells = prepared.tableRows[rowIndex].cells;
		const auto &cells = block.tableRows[rowIndex].cells;
		const auto cellCount = int(std::min(
			preparedCells.size(),
			cells.size()));
		for (auto cellIndex = 0; cellIndex != cellCount; ++cellIndex) {
			const auto &preparedCell = preparedCells[cellIndex];
			const auto &cell = cells[cellIndex];
			const auto usePlaceholder = preparedCell.text.text.isEmpty()
				&& !preparedCell.editPlaceholderText.isEmpty();
			const auto &displayLeaf = usePlaceholder
				? cell.placeholderLeaf
				: cell.leaf;
			const auto leafMinimum = LeafMinimumWidth(displayLeaf);
			if (leafMinimum > 0) {
				constraints.push_back({
					.column = cell.column,
					.colspan = cell.colspan,
					.minimumWidth = std::max(
						leafMinimum + paddingWidth,
						st.table.minColumnWidth),
				});
			}
		}
	}
	const auto columns = ComputeTableColumnMinimumWidths(
		std::move(constraints),
		columnCount,
		st,
		prepared.tableBordered);
	return std::max(
		captionMinimum,
		TableGridWidth(columns, st, prepared.tableBordered));
}

int BlockSkip(
		const PreparedBlock &block,
		const style::Markdown &st) {
	if (IsAnchorOnlyBlock(block)) {
		return 0;
	}
	const auto &skips = st.blockSkips;
	switch (block.kind) {
	case PreparedBlockKind::Paragraph:
	case PreparedBlockKind::Thinking:
		return skips.paragraph;
	case PreparedBlockKind::Heading:
		return skips.heading;
	case PreparedBlockKind::CodeBlock:
		return skips.code;
	case PreparedBlockKind::Rule:
		return skips.rule;
	case PreparedBlockKind::List:
	case PreparedBlockKind::ListItem:
		return skips.paragraph;
	case PreparedBlockKind::Quote:
		return skips.quote;
	case PreparedBlockKind::DisplayMath:
		return skips.displayMath;
	case PreparedBlockKind::Table:
		return skips.table;
	case PreparedBlockKind::Photo:
		return skips.photo;
	case PreparedBlockKind::Video:
		return skips.video;
	case PreparedBlockKind::Audio:
		return skips.audio;
	case PreparedBlockKind::Map:
		return skips.map;
	case PreparedBlockKind::Channel:
		return skips.channel;
	case PreparedBlockKind::RelatedArticle:
		return skips.relatedArticle;
	case PreparedBlockKind::EmbedPost:
		return skips.embedPost;
	case PreparedBlockKind::Placeholder:
		return skips.placeholder;
	case PreparedBlockKind::Details:
		return skips.paragraph;
	case PreparedBlockKind::GroupedMedia:
		return skips.groupedMedia;
	}
	return 0;
}

int BlockSkip(
		const PreparedBlock &previous,
		const PreparedBlock &block,
		LayoutContext context,
		const style::Markdown &st) {
	if (previous.kind == PreparedBlockKind::Details
		&& block.kind == PreparedBlockKind::Details) {
		return 0;
	}
	if (context.tightList
		&& IsFlowKind(previous.kind)
		&& IsFlowKind(block.kind)) {
		return 0;
	}
	return BlockSkip(block, st);
}

const style::TextStyle &TextStyleFor(
		const PreparedBlock &block,
		const style::Markdown &st) {
	if (block.kind == PreparedBlockKind::CodeBlock) {
		return st.code;
	} else if (block.quoteAuthor) {
		return st.quoteAuthorStyle;
	} else if (block.footer) {
		return st.footer;
	} else if (block.kind != PreparedBlockKind::Heading) {
		return st.body;
	}
	switch (std::clamp(block.headingLevel, 1, 6)) {
	case 1: return st.heading1;
	case 2: return st.heading2;
	case 3: return st.heading3;
	case 4: return st.heading4;
	case 5: return st.heading5;
	case 6: return st.heading6;
	}
	return st.heading6;
}

const style::TextStyle &EditPlaceholderTextStyleFor(
		const PreparedBlock &block,
		const style::Markdown &st) {
	return block.quoteAuthor ? st.body : TextStyleFor(block, st);
}

TextWithEntities EditPlaceholderTextValue(
		const PreparedBlock &block,
		const QString &text) {
	auto result = TextWithEntities::Simple(text);
	if (block.pullquote && !block.quoteAuthor && !result.text.isEmpty()) {
		result.entities.push_back(EntityInText(
			EntityType::Italic,
			0,
			result.text.size()));
	}
	return result;
}

void ApplyPreparedEditSources(
		LaidOutBlock *block,
		const PreparedBlock &prepared) {
	block->editBlock = prepared.editBlock;
	block->editListItem = prepared.editListItem;
	block->editLeaf = prepared.editLeaf;
}

void RepopulateCodeBlockLeaf(
		LaidOutBlock &block,
		const std::vector<PreparedFormulaSlot> *formulas,
		InlineFormulaObjectCache *inlineFormulaObjects,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		const style::Markdown &st,
		bool allowAsyncSyntaxHighlighting,
		CodeBlockSyntaxHighlightTracker *syntaxHighlightTracker,
		Fn<void()> repaint,
		Fn<void(QRect)> repaintRect,
		Fn<bool(const ClickContext&)> spoilerLinkFilter) {
	PopulateCodeBlockLeaf(
		&block.leaf,
		&block.syntaxHighlightProcessId,
		block.codeText,
		block.codeLinks,
		block.codeLanguage,
		formulas,
		inlineFormulaObjects,
		mediaRuntime,
		st,
		allowAsyncSyntaxHighlighting,
		syntaxHighlightTracker,
		std::move(repaint),
		std::move(repaintRect),
		std::move(spoilerLinkFilter));
}

void UpdateLaidOutLeafContent(
		LaidOutBlock *block,
		const PreparedBlock &prepared,
		const std::vector<PreparedFormulaSlot> *formulas,
		InlineFormulaObjectCache *inlineFormulaObjects,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		const style::Markdown &st,
		LayoutContext context) {
	if (!block) {
		return;
	}
	block->placeholderText = QString();
	block->placeholderLeaf = Ui::Text::String();
	block->fallbackLeaf = Ui::Text::String();
	switch (prepared.kind) {
	case PreparedBlockKind::Paragraph:
	case PreparedBlockKind::Thinking:
	case PreparedBlockKind::Heading: {
		const auto &textStyle = TextStyleFor(prepared, st);
		const auto &placeholderStyle = EditPlaceholderTextStyleFor(
			prepared,
			st);
		BuildOrReuseMarkedTextLeaf(
			&block->leaf,
			CachedTextLeafSlot::Leaf,
			prepared,
			textStyle,
			st,
			prepared.text,
			prepared.links,
			formulas,
			inlineFormulaObjects,
			mediaRuntime,
			FlowBlockMinimumWidth(prepared, st),
			context);
		if (prepared.text.text.isEmpty()
			&& !prepared.editPlaceholderText.isEmpty()) {
			BuildOrReuseEditPlaceholderLeaf(
				&block->placeholderText,
				&block->placeholderLeaf,
				prepared,
				prepared.editPlaceholderText,
				placeholderStyle,
				PlainTextMinResizeWidth(placeholderStyle),
				context);
		}
	} break;
	case PreparedBlockKind::CodeBlock:
		block->codeText = prepared.text;
		block->codeLinks = prepared.links;
		block->copyText = prepared.text.text;
		block->codeLanguage = prepared.codeLanguage;
		RepopulateCodeBlockLeaf(
			*block,
			formulas,
			inlineFormulaObjects,
			mediaRuntime,
			st,
			context.allowAsyncSyntaxHighlighting,
			context.syntaxHighlightTracker,
			context.repaint,
			context.repaintRect,
			context.spoilerLinkFilter);
		if (prepared.text.text.isEmpty()
			&& !prepared.editPlaceholderText.isEmpty()) {
			BuildOrReuseEditPlaceholderLeaf(
				&block->placeholderText,
				&block->placeholderLeaf,
				prepared,
				prepared.editPlaceholderText,
				st.code,
				PlainTextMinResizeWidth(st.code),
				context);
		}
		break;
	case PreparedBlockKind::DisplayMath:
		block->copyText = prepared.formulaTex;
		block->formulaIndex = prepared.formulaIndex;
		if (const auto formula = formulas
				? PreparedFormulaFor(*formulas, prepared.formulaIndex)
				: nullptr;
			!formula || !formula->measured.success) {
			if (prepared.formulaTex.trimmed().isEmpty()
				&& !prepared.editPlaceholderText.isEmpty()) {
				BuildOrReuseEditPlaceholderLeaf(
					&block->placeholderText,
					&block->placeholderLeaf,
					prepared,
					prepared.editPlaceholderText,
					st.displayMath.fallbackStyle,
					DisplayMathFallbackTextMinResizeWidth(st),
					context);
			} else {
				auto fallbackContext = context;
				fallbackContext.rtl = false;
				BuildOrReuseMarkedTextLeaf(
					&block->fallbackLeaf,
					CachedTextLeafSlot::Fallback,
					prepared,
					st.displayMath.fallbackStyle,
					st,
					DisplayMathFallbackText(),
					{},
					nullptr,
					nullptr,
					nullptr,
					DisplayMathFallbackTextMinResizeWidth(st),
					fallbackContext);
			}
		}
		break;
	case PreparedBlockKind::Table:
	case PreparedBlockKind::Photo:
	case PreparedBlockKind::Video:
	case PreparedBlockKind::Audio:
	case PreparedBlockKind::Map:
	case PreparedBlockKind::Channel:
	case PreparedBlockKind::GroupedMedia:
	case PreparedBlockKind::EmbedPost:
		BuildOrReuseMarkedTextLeaf(
			&block->leaf,
			CachedTextLeafSlot::Leaf,
			prepared,
			st.body,
			st,
			prepared.text,
			prepared.links,
			formulas,
			inlineFormulaObjects,
			mediaRuntime,
			FlowTextMinResizeWidth(st.body),
			context);
		if (prepared.text.text.isEmpty()
			&& !prepared.editPlaceholderText.isEmpty()) {
			BuildOrReuseEditPlaceholderLeaf(
				&block->placeholderText,
				&block->placeholderLeaf,
				prepared,
				prepared.editPlaceholderText,
				st.body,
				PlainTextMinResizeWidth(st.body),
				context);
		}
		break;
	case PreparedBlockKind::Details:
		BuildOrReuseMarkedTextLeaf(
			&block->leaf,
			CachedTextLeafSlot::Leaf,
			prepared,
			st.details.summaryStyle,
			st,
			prepared.text,
			prepared.links,
			formulas,
			inlineFormulaObjects,
			mediaRuntime,
			FlowTextMinResizeWidth(st.details.summaryStyle),
			context);
		if (prepared.text.text.isEmpty()
			&& !prepared.editPlaceholderText.isEmpty()) {
			BuildOrReuseEditPlaceholderLeaf(
				&block->placeholderText,
				&block->placeholderLeaf,
				prepared,
				prepared.editPlaceholderText,
				st.details.summaryStyle,
				PlainTextMinResizeWidth(st.details.summaryStyle),
				context);
		}
		break;
	case PreparedBlockKind::Rule:
	case PreparedBlockKind::List:
	case PreparedBlockKind::ListItem:
	case PreparedBlockKind::Quote:
	case PreparedBlockKind::Placeholder:
	case PreparedBlockKind::RelatedArticle:
		break;
	}
}

void UpdateLaidOutLeafContent(
		LaidOutTableCell *cell,
		const PreparedTableCell &prepared,
		const std::vector<PreparedFormulaSlot> *formulas,
		InlineFormulaObjectCache *inlineFormulaObjects,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		const style::Markdown &st,
		int tableRowIndex,
		int tableCellIndex,
		LayoutContext context) {
	if (!cell) {
		return;
	}
	const auto &textStyle = TableCellTextStyle(prepared, st);
	const auto minResizeWidth = TableCellTextMinResizeWidth(textStyle, st);
	cell->header = prepared.header;
	cell->verticalAlignment = prepared.verticalAlignment;
	cell->align = CellAlign(prepared.alignment);
	cell->column = std::max(prepared.column, 0);
	cell->colspan = std::max(prepared.colspan, 1);
	cell->rowspan = std::max(prepared.rowspan, 1);
	cell->placeholderText = QString();
	cell->placeholderLeaf = Ui::Text::String();
	cell->cachedPreferredWidth = -1;
	cell->cachedPreferredHeight = 0;
	BuildOrReuseCachedTextLeaf(
		&cell->leaf,
		nullptr,
		context,
		TableCellCachedTextLeafKey(
			CachedTextLeafSlot::TableCellText,
			prepared,
			context.preparedPath,
			tableRowIndex,
			tableCellIndex),
		MarkedTextLeafSourceSignature(
			prepared.text,
			textStyle,
			minResizeWidth,
			context.rtl),
		[&](Ui::Text::String *leaf,
				Spellchecker::HighlightProcessId*) {
			SetTextLeaf(
				leaf,
				textStyle,
				st,
				prepared.text,
				formulas,
				inlineFormulaObjects,
				mediaRuntime,
				minResizeWidth,
				context.rtl,
				context.repaint,
				context.repaintRect);
			BindLinks(leaf, prepared.links);
		});
	BindLinks(&cell->leaf, prepared.links);
	if (prepared.text.text.isEmpty()
		&& !prepared.editPlaceholderText.isEmpty()) {
		cell->placeholderText = prepared.editPlaceholderText;
		BuildOrReuseCachedTextLeaf(
			&cell->placeholderLeaf,
			nullptr,
			context,
			TableCellCachedTextLeafKey(
				CachedTextLeafSlot::TableCellPlaceholder,
				prepared,
				context.preparedPath,
				tableRowIndex,
				tableCellIndex),
			PlainTextLeafSourceSignature(
				cell->placeholderText,
				textStyle,
				minResizeWidth,
				context.rtl),
			[&](Ui::Text::String *leaf,
					Spellchecker::HighlightProcessId*) {
			SetPlainTextLeaf(
				leaf,
				textStyle,
				cell->placeholderText,
				minResizeWidth,
				context.rtl);
			});
	}
}

[[nodiscard]] std::optional<int> LayoutFlowBlockGeometry(
	const PreparedBlock &prepared,
	LaidOutBlock *block,
	const style::Markdown &st,
	int left,
	int top,
	int width,
	int logicalWidth,
	bool scrollOwner,
	LayoutContext context,
	bool allowMissingLeaf = false);
[[nodiscard]] std::optional<int> LayoutCodeBlockGeometry(
	const PreparedBlock &prepared,
	LaidOutBlock *block,
	const style::Markdown &st,
	int left,
	int top,
	int width,
	int logicalWidth,
	bool scrollOwner,
	LayoutContext context);
[[nodiscard]] std::optional<int> LayoutRuleBlockGeometry(
	LaidOutBlock *block,
	const style::Markdown &st,
	int left,
	int top,
	int width);
[[nodiscard]] std::optional<int> LayoutDisplayMathBlockGeometry(
	const PreparedBlock &prepared,
	const std::vector<PreparedFormulaSlot> &formulas,
	LaidOutBlock *block,
	const style::Markdown &st,
	int left,
	int top,
	int width,
	int logicalWidth,
	bool scrollOwner,
	LayoutContext context);
[[nodiscard]] std::optional<int> LayoutTableBlockGeometry(
	const PreparedBlock &prepared,
	LaidOutBlock *block,
	const style::Markdown &st,
	int left,
	int top,
	int width,
	int logicalWidth,
	bool scrollOwner,
	LayoutContext context);
[[nodiscard]] std::optional<int> LayoutPlaceholderBlockGeometry(
	const PreparedBlock &prepared,
	LaidOutBlock *block,
	const style::Markdown &st,
	int left,
	int top,
	int width,
	LayoutContext context);
[[nodiscard]] std::optional<int> LayoutRelatedArticleBlockGeometry(
	const PreparedBlock &prepared,
	LaidOutBlock *block,
	const style::Markdown &st,
	int left,
	int top,
	int width,
	LayoutContext context);
[[nodiscard]] std::optional<int> LayoutFramedMediaBlockGeometry(
	LaidOutBlock *block,
	const PreparedBlock &prepared,
	const style::Markdown &st,
	int left,
	int top,
	int width,
	QMargins padding,
	int captionSkip,
	bool limitWidth,
	bool centerMedia,
	int intrinsicWidth,
	LayoutContext context);
[[nodiscard]] std::optional<int> LayoutCardMediaBlockGeometry(
	LaidOutBlock *block,
	const PreparedBlock &prepared,
	const style::Markdown &st,
	int left,
	int top,
	int width,
	QMargins padding,
	int captionSkip,
	LayoutContext context);

LaidOutBlock LayoutFlowBlock(
		const PreparedBlock &prepared,
		const std::vector<PreparedFormulaSlot> *formulas,
		InlineFormulaObjectCache *inlineFormulaObjects,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		int logicalWidth,
		bool scrollOwner,
		LayoutContext context) {
	auto block = LaidOutBlock();
	ApplyPreparedEditSources(&block, prepared);
	block.kind = prepared.kind;
	block.anchorId = prepared.anchorId;
	block.anchorIds = prepared.anchorIds;
	block.headingLevel = prepared.headingLevel;
	block.supplementary = prepared.supplementary;
	block.pullquote = prepared.pullquote;
	block.quoteAuthor = prepared.quoteAuthor;
	block.footer = prepared.footer;
	block.flowTextAlign = CellAlign(prepared.flowAlignment);
	const auto &textStyle = TextStyleFor(prepared, st);
	const auto &placeholderStyle = EditPlaceholderTextStyleFor(prepared, st);
	if (!IsAnchorOnlyBlock(prepared)) {
		BuildOrReuseMarkedTextLeaf(
			&block.leaf,
			CachedTextLeafSlot::Leaf,
			prepared,
			textStyle,
			st,
			prepared.text,
			prepared.links,
			formulas,
			inlineFormulaObjects,
			mediaRuntime,
			FlowBlockMinimumWidth(prepared, st),
			context);
		const auto usePlaceholder = prepared.text.text.isEmpty()
			&& !prepared.editPlaceholderText.isEmpty();
		if (usePlaceholder) {
			BuildOrReuseEditPlaceholderLeaf(
				&block.placeholderText,
				&block.placeholderLeaf,
				prepared,
				prepared.editPlaceholderText,
				placeholderStyle,
				PlainTextMinResizeWidth(placeholderStyle),
				context);
		}
	}
	auto bottom = LayoutFlowBlockGeometry(
		prepared,
		&block,
		st,
		left,
		top,
		width,
		logicalWidth,
		scrollOwner,
		context);
	if (!bottom) {
		if (!IsAnchorOnlyBlock(prepared)) {
			if (MissingRetainedLeaf(prepared.text.text, block.leaf)) {
				SetTextLeaf(
					&block.leaf,
					textStyle,
					st,
					prepared.text,
					formulas,
					inlineFormulaObjects,
					mediaRuntime,
					FlowBlockMinimumWidth(prepared, st),
					context.rtl,
					context.repaint,
					context.repaintRect);
				SetTextLeafSpoilerLinkFilter(
					&block.leaf,
					context.spoilerLinkFilter);
				BindLinks(&block.leaf, prepared.links);
			}
			const auto usePlaceholder = prepared.text.text.isEmpty()
				&& !prepared.editPlaceholderText.isEmpty();
			if (usePlaceholder && block.placeholderLeaf.isEmpty()) {
				block.placeholderText = prepared.editPlaceholderText;
				SetSimpleMarkedTextLeaf(
					&block.placeholderLeaf,
					placeholderStyle,
					EditPlaceholderTextValue(
						prepared,
						block.placeholderText),
					PlainTextMinResizeWidth(placeholderStyle),
					context.rtl);
			}
		}
		bottom = LayoutFlowBlockGeometry(
			prepared,
			&block,
			st,
			left,
			top,
			width,
			logicalWidth,
			scrollOwner,
			context,
			true);
	}
	Expects(bottom.has_value());
	return FinalizeLaidOutBlock(std::move(block));
}

LaidOutBlock LayoutCodeBlock(
		const PreparedBlock &prepared,
		const std::vector<PreparedFormulaSlot> *formulas,
		InlineFormulaObjectCache *inlineFormulaObjects,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		int logicalWidth,
		bool scrollOwner,
		bool allowAsyncSyntaxHighlighting,
		CodeBlockSyntaxHighlightTracker *syntaxHighlightTracker,
		LayoutContext context) {
	auto block = LaidOutBlock();
	ApplyPreparedEditSources(&block, prepared);
	block.kind = PreparedBlockKind::CodeBlock;
	block.anchorId = prepared.anchorId;
	block.anchorIds = prepared.anchorIds;
	block.codeText = prepared.text;
	block.codeLinks = prepared.links;
	block.copyText = block.codeText.text;
	block.codeLanguage = prepared.codeLanguage;
	BuildOrReuseCachedTextLeaf(
		&block.leaf,
		&block.syntaxHighlightProcessId,
		context,
		BlockCachedTextLeafKey(
			CachedTextLeafSlot::Leaf,
			prepared,
			context.preparedPath),
		CodeTextLeafSourceSignature(prepared, st),
		[&](Ui::Text::String *leaf,
				Spellchecker::HighlightProcessId *syntaxHighlightProcessId) {
			PopulateCodeBlockLeaf(
				leaf,
				syntaxHighlightProcessId,
				block.codeText,
				block.codeLinks,
				block.codeLanguage,
				formulas,
				inlineFormulaObjects,
				mediaRuntime,
				st,
				allowAsyncSyntaxHighlighting,
				syntaxHighlightTracker,
				context.repaint,
				context.repaintRect,
				context.spoilerLinkFilter);
		});
	BindLinks(&block.leaf, block.codeLinks);
	if (!block.syntaxHighlightProcessId
		&& allowAsyncSyntaxHighlighting
		&& syntaxHighlightTracker) {
		auto highlightRequest = TextWithEntities();
		highlightRequest.text = CodeBlockDisplayText(block.codeText).text;
		if (!highlightRequest.text.isEmpty()) {
			highlightRequest.entities.push_back(EntityInText(
				EntityType::Pre,
				0,
				highlightRequest.text.size(),
				block.codeLanguage));
		}
		block.syntaxHighlightProcessId = syntaxHighlightTracker->tryHighlightSyntax(
			highlightRequest.text,
			block.codeLanguage,
			highlightRequest);
	}
	const auto usePlaceholder = prepared.text.text.isEmpty()
		&& !prepared.editPlaceholderText.isEmpty();
	if (usePlaceholder) {
		BuildOrReuseEditPlaceholderLeaf(
			&block.placeholderText,
			&block.placeholderLeaf,
			prepared,
			prepared.editPlaceholderText,
			st.code,
			PlainTextMinResizeWidth(st.code),
			context);
	}
	const auto bottom = LayoutCodeBlockGeometry(
		prepared,
		&block,
		st,
		left,
		top,
		width,
		logicalWidth,
		scrollOwner,
		context);
	Expects(bottom.has_value());
	return FinalizeLaidOutBlock(std::move(block));
}

LaidOutBlock LayoutRuleBlock(
		const PreparedBlock &prepared,
		const style::Markdown &st,
		int left,
		int top,
		int width) {
	auto block = LaidOutBlock();
	ApplyPreparedEditSources(&block, prepared);
	block.kind = PreparedBlockKind::Rule;
	const auto bottom = LayoutRuleBlockGeometry(
		&block,
		st,
		left,
		top,
		width);
	Expects(bottom.has_value());
	return FinalizeLaidOutBlock(std::move(block));
}

LaidOutBlock LayoutDisplayMathBlock(
		const PreparedBlock &prepared,
		const std::vector<PreparedFormulaSlot> &formulas,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		int logicalWidth,
		bool scrollOwner,
		LayoutContext context) {
	auto block = LaidOutBlock();
	ApplyPreparedEditSources(&block, prepared);
	block.kind = PreparedBlockKind::DisplayMath;
	block.anchorId = prepared.anchorId;
	block.anchorIds = prepared.anchorIds;
	block.formulaIndex = prepared.formulaIndex;
	block.copyText = prepared.formulaTex;

	const auto formula = PreparedFormulaFor(formulas, prepared.formulaIndex);
	const auto usePlaceholder = prepared.formulaTex.trimmed().isEmpty()
		&& !prepared.editPlaceholderText.isEmpty();
	if (!(formula && formula->measured.success)) {
		if (usePlaceholder) {
			BuildOrReuseEditPlaceholderLeaf(
				&block.placeholderText,
				&block.placeholderLeaf,
				prepared,
				prepared.editPlaceholderText,
				st.displayMath.fallbackStyle,
				DisplayMathFallbackTextMinResizeWidth(st),
				context);
		} else {
			auto fallbackContext = context;
			fallbackContext.rtl = false;
			BuildOrReuseMarkedTextLeaf(
				&block.fallbackLeaf,
				CachedTextLeafSlot::Fallback,
				prepared,
				st.displayMath.fallbackStyle,
				st,
				DisplayMathFallbackText(),
				{},
				nullptr,
				nullptr,
				nullptr,
				DisplayMathFallbackTextMinResizeWidth(st),
				fallbackContext);
		}
	}
	const auto bottom = LayoutDisplayMathBlockGeometry(
		prepared,
		formulas,
		&block,
		st,
		left,
		top,
		width,
		logicalWidth,
		scrollOwner,
		context);
	Expects(bottom.has_value());
	return FinalizeLaidOutBlock(std::move(block));
}

LaidOutBlock LayoutTableBlock(
		const PreparedBlock &prepared,
		std::vector<PreparedFormulaSlot> *formulas,
		InlineFormulaObjectCache *inlineFormulaObjects,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		int logicalWidth,
		bool scrollOwner,
		LayoutContext context) {
	auto block = LaidOutBlock();
	ApplyPreparedEditSources(&block, prepared);
	block.kind = PreparedBlockKind::Table;
	block.anchorId = prepared.anchorId;
	block.anchorIds = prepared.anchorIds;
	block.tableBordered = prepared.tableBordered;
	block.tableStriped = prepared.tableStriped;
	block.supplementary = prepared.supplementary;
	block.flowTextAlign = style::al_center;

	FillMediaCaption(
		&block,
		prepared,
		formulas,
		inlineFormulaObjects,
		mediaRuntime,
		st,
		context);

	block.tableRows.reserve(prepared.tableRows.size());
	for (auto rowIndex = 0, rowCount = int(prepared.tableRows.size());
			rowIndex != rowCount;
			++rowIndex) {
		const auto &preparedRow = prepared.tableRows[rowIndex];
		auto row = LaidOutTableRow();
		row.header = preparedRow.header;
		row.editRow = preparedRow.editRow;
		row.cells.reserve(preparedRow.cells.size());
		for (auto cellIndex = 0, cellCount = int(preparedRow.cells.size());
				cellIndex != cellCount;
				++cellIndex) {
			auto cell = InitializeTableCellLayout(
				preparedRow.cells[cellIndex],
				formulas,
				inlineFormulaObjects,
				mediaRuntime,
				st,
				rowIndex,
				cellIndex,
				context);
			row.cells.push_back(std::move(cell));
		}
		block.tableRows.push_back(std::move(row));
	}

	const auto bottom = LayoutTableBlockGeometry(
		prepared,
		&block,
		st,
		left,
		top,
		width,
		logicalWidth,
		scrollOwner,
		context);
	Expects(bottom.has_value());
	return FinalizeLaidOutBlock(std::move(block));
}

LaidOutBlock LayoutPlaceholderBlock(
		const PreparedBlock &prepared,
		std::vector<PreparedFormulaSlot> *formulas,
		InlineFormulaObjectCache *inlineFormulaObjects,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		LayoutContext context) {
	auto block = LaidOutBlock();
	ApplyPreparedEditSources(&block, prepared);
	block.kind = PreparedBlockKind::Placeholder;
	block.anchorId = prepared.anchorId;
	block.anchorIds = prepared.anchorIds;
	block.copyText = prepared.placeholder.copyText;
	block.labelText = prepared.placeholder.label;
	block.placeholderId = prepared.placeholder.id;
	if (block.placeholderId && context.placeholderRuntimeFactory) {
		block.placeholderRuntime = context.placeholderRuntimeFactory(
			block.placeholderId);
	}
	block.supplementary = prepared.supplementary;

	const auto &style = st.placeholder;
	BuildOrReusePlainTextLeaf(
		&block.labelLeaf,
		CachedTextLeafSlot::Label,
		prepared,
		style.labelStyle,
		block.labelText,
		PlainTextMinResizeWidth(style.labelStyle),
		context);
	if (prepared.placeholder.embed) {
		block.activation.kind = MediaActivationKind::Embed;
		block.activation.embed = *prepared.placeholder.embed;
		block.activation.placeholderId = block.placeholderId;
	}
	FillMediaCaption(
		&block,
		prepared,
		formulas,
		inlineFormulaObjects,
		mediaRuntime,
		st,
		context);
	const auto bottom = LayoutPlaceholderBlockGeometry(
		prepared,
		&block,
		st,
		left,
		top,
		width,
		context);
	Expects(bottom.has_value());
	return FinalizeLaidOutBlock(std::move(block));
}

LaidOutBlock LayoutRelatedArticleBlock(
		const PreparedBlock &prepared,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		LayoutContext context) {
	auto block = LaidOutBlock();
	ApplyPreparedEditSources(&block, prepared);
	block.kind = PreparedBlockKind::RelatedArticle;
	block.anchorId = prepared.anchorId;
	block.anchorIds = prepared.anchorIds;
	block.copyText = prepared.relatedArticle.copyText;
	block.labelText = prepared.relatedArticle.title;
	block.preparedLink = prepared.relatedArticle.link;
	block.preparedLinkHandler = CreatePreparedLinkHandler(
		prepared.relatedArticle.link);
	if (prepared.relatedArticle.photoId && mediaRuntime) {
		block.photoRuntime = mediaRuntime->resolvePhoto(
			prepared.relatedArticle.photoId);
	}

	const auto &card = st.relatedArticle;
	block.thumbnailPhotoId = prepared.relatedArticle.photoId;
	if (!prepared.relatedArticle.title.isEmpty()) {
		BuildOrReusePlainTextLeaf(
			&block.labelLeaf,
			CachedTextLeafSlot::Label,
			prepared,
			card.titleStyle,
			prepared.relatedArticle.title,
			PlainTextMinResizeWidth(card.titleStyle),
			context);
	}
	if (!prepared.relatedArticle.description.isEmpty()) {
		BuildOrReusePlainTextLeaf(
			&block.subtitleLeaf,
			CachedTextLeafSlot::Subtitle,
			prepared,
			card.subtitleStyle,
			prepared.relatedArticle.description,
			PlainTextMinResizeWidth(card.subtitleStyle),
			context);
	}
	if (!prepared.relatedArticle.footer.isEmpty()) {
		BuildOrReusePlainTextLeaf(
			&block.actionLeaf,
			CachedTextLeafSlot::Action,
			prepared,
			card.footerStyle,
			prepared.relatedArticle.footer,
			PlainTextMinResizeWidth(card.footerStyle),
			context);
	}

	const auto bottom = LayoutRelatedArticleBlockGeometry(
		prepared,
		&block,
		st,
		left,
		top,
		width,
		context);
	Expects(bottom.has_value());
	return FinalizeLaidOutBlock(std::move(block));
}

LaidOutBlock LayoutPhotoBlock(
		const PreparedBlock &prepared,
		std::vector<PreparedFormulaSlot> *formulas,
		InlineFormulaObjectCache *inlineFormulaObjects,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		LayoutContext context) {
	auto block = LaidOutBlock();
	ApplyPreparedEditSources(&block, prepared);
	block.kind = PreparedBlockKind::Photo;
	block.anchorId = prepared.anchorId;
	block.anchorIds = prepared.anchorIds;
	block.supplementary = prepared.supplementary;
	if (context.mediaBlockFactory) {
		block.mediaBlock = context.mediaBlockFactory(prepared);
	}
	if (block.mediaBlock) {
		block.copyText = block.mediaBlock->selectionData().copyText;
	}
	FillMediaCaption(
		&block,
		prepared,
		formulas,
		inlineFormulaObjects,
		mediaRuntime,
		st,
		context);
	const auto bottom = LayoutFramedMediaBlockGeometry(
		&block,
		prepared,
		st,
		left,
		top,
		width,
		st.photo.padding,
		st.photo.captionSkip,
		true,
		true,
		prepared.photo.width,
		context);
	Expects(bottom.has_value());
	return FinalizeLaidOutBlock(std::move(block));
}

LaidOutBlock LayoutVideoBlock(
		const PreparedBlock &prepared,
		std::vector<PreparedFormulaSlot> *formulas,
		InlineFormulaObjectCache *inlineFormulaObjects,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		LayoutContext context) {
	auto block = LaidOutBlock();
	ApplyPreparedEditSources(&block, prepared);
	block.kind = PreparedBlockKind::Video;
	block.anchorId = prepared.anchorId;
	block.anchorIds = prepared.anchorIds;
	block.supplementary = prepared.supplementary;
	if (context.mediaBlockFactory) {
		block.mediaBlock = context.mediaBlockFactory(prepared);
	}
	if (block.mediaBlock) {
		block.copyText = block.mediaBlock->selectionData().copyText;
	}
	FillMediaCaption(
		&block,
		prepared,
		formulas,
		inlineFormulaObjects,
		mediaRuntime,
		st,
		context);
	const auto bottom = LayoutFramedMediaBlockGeometry(
		&block,
		prepared,
		st,
		left,
		top,
		width,
		st.photo.padding,
		st.photo.captionSkip,
		true,
		true,
		prepared.video.media.width,
		context);
	Expects(bottom.has_value());
	return FinalizeLaidOutBlock(std::move(block));
}

LaidOutBlock LayoutAudioBlock(
		const PreparedBlock &prepared,
		std::vector<PreparedFormulaSlot> *formulas,
		InlineFormulaObjectCache *inlineFormulaObjects,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		LayoutContext context) {
	auto block = LaidOutBlock();
	ApplyPreparedEditSources(&block, prepared);
	block.kind = PreparedBlockKind::Audio;
	block.anchorId = prepared.anchorId;
	block.anchorIds = prepared.anchorIds;
	block.supplementary = prepared.supplementary;
	if (context.mediaBlockFactory) {
		block.mediaBlock = context.mediaBlockFactory(prepared);
	}
	if (block.mediaBlock) {
		block.copyText = block.mediaBlock->selectionData().copyText;
	}
	FillMediaCaption(
		&block,
		prepared,
		formulas,
		inlineFormulaObjects,
		mediaRuntime,
		st,
		context);
	const auto bottom = LayoutCardMediaBlockGeometry(
		&block,
		prepared,
		st,
		left,
		top,
		width,
		st.audio.padding,
		st.audio.captionSkip,
		context);
	Expects(bottom.has_value());
	return FinalizeLaidOutBlock(std::move(block));
}

LaidOutBlock LayoutMapBlock(
		const PreparedBlock &prepared,
		std::vector<PreparedFormulaSlot> *formulas,
		InlineFormulaObjectCache *inlineFormulaObjects,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		LayoutContext context) {
	auto block = LaidOutBlock();
	ApplyPreparedEditSources(&block, prepared);
	block.kind = PreparedBlockKind::Map;
	block.anchorId = prepared.anchorId;
	block.anchorIds = prepared.anchorIds;
	block.supplementary = prepared.supplementary;
	if (context.mediaBlockFactory) {
		block.mediaBlock = context.mediaBlockFactory(prepared);
	}
	if (block.mediaBlock) {
		block.copyText = block.mediaBlock->selectionData().copyText;
	}
	FillMediaCaption(
		&block,
		prepared,
		formulas,
		inlineFormulaObjects,
		mediaRuntime,
		st,
		context);
	const auto bottom = LayoutFramedMediaBlockGeometry(
		&block,
		prepared,
		st,
		left,
		top,
		width,
		st.photo.padding,
		st.photo.captionSkip,
		false,
		false,
		0,
		context);
	Expects(bottom.has_value());
	return FinalizeLaidOutBlock(std::move(block));
}

LaidOutBlock LayoutChannelBlock(
		const PreparedBlock &prepared,
		std::vector<PreparedFormulaSlot> *formulas,
		InlineFormulaObjectCache *inlineFormulaObjects,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		LayoutContext context) {
	auto block = LaidOutBlock();
	ApplyPreparedEditSources(&block, prepared);
	block.kind = PreparedBlockKind::Channel;
	block.anchorId = prepared.anchorId;
	block.anchorIds = prepared.anchorIds;
	block.supplementary = prepared.supplementary;
	if (context.mediaBlockFactory) {
		block.mediaBlock = context.mediaBlockFactory(prepared);
	}
	if (block.mediaBlock) {
		block.copyText = block.mediaBlock->selectionData().copyText;
	}
	FillMediaCaption(
		&block,
		prepared,
		formulas,
		inlineFormulaObjects,
		mediaRuntime,
		st,
		context);
	const auto bottom = LayoutCardMediaBlockGeometry(
		&block,
		prepared,
		st,
		left,
		top,
		width,
		st.channel.padding,
		st.audio.captionSkip,
		context);
	Expects(bottom.has_value());
	return FinalizeLaidOutBlock(std::move(block));
}

LaidOutBlock LayoutGroupedMediaBlock(
		const PreparedBlock &prepared,
		const std::vector<PreparedFormulaSlot> *formulas,
		InlineFormulaObjectCache *inlineFormulaObjects,
		const std::shared_ptr<MediaRuntime> &mediaRuntime,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		LayoutContext context) {
	auto block = LaidOutBlock();
	ApplyPreparedEditSources(&block, prepared);
	block.kind = PreparedBlockKind::GroupedMedia;
	block.anchorId = prepared.anchorId;
	block.anchorIds = prepared.anchorIds;
	block.supplementary = prepared.supplementary;
	if (context.mediaBlockFactory) {
		block.mediaBlock = context.mediaBlockFactory(prepared);
	}
	if (block.mediaBlock) {
		block.copyText = block.mediaBlock->selectionData().copyText;
	}
	FillMediaCaption(
		&block,
		prepared,
		formulas,
		inlineFormulaObjects,
		mediaRuntime,
		st,
		context);
	const auto bottom = LayoutFramedMediaBlockGeometry(
		&block,
		prepared,
		st,
		left,
		top,
		width,
		st.groupedMedia.padding,
		st.groupedMedia.captionSkip,
		false,
		false,
		0,
		context);
	Expects(bottom.has_value());
	return FinalizeLaidOutBlock(std::move(block));
}

[[nodiscard]] std::optional<int> LayoutFlowBlockGeometry(
		const PreparedBlock &prepared,
		LaidOutBlock *block,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		int logicalWidth,
		bool scrollOwner,
		LayoutContext context,
		bool allowMissingLeaf) {
	if (!block) {
		return std::nullopt;
	}
	const auto usePlaceholder = prepared.text.text.isEmpty()
		&& !prepared.editPlaceholderText.isEmpty();
	if (!allowMissingLeaf
		&& (MissingRetainedLeaf(prepared.text.text, block->leaf)
			|| MissingRetainedPlaceholderLeaf(
				usePlaceholder,
				block->placeholderLeaf))) {
		return std::nullopt;
	}
	ClearBlockGeometry(block);
	const auto visibleWidth = std::max(width, 1);
	block->textWidth = std::max(
		(logicalWidth > 0) ? logicalWidth : visibleWidth,
		1);
	if (IsAnchorOnlyBlock(prepared)) {
		block->textRect = QRect(left, top, block->textWidth, 0);
		block->contentRect = QRect(left, top, visibleWidth, 0);
		block->outer = block->contentRect;
		FinishBlockGeometry(block);
		return block->outer.y() + block->outer.height();
	}
	const auto &textStyle = TextStyleFor(prepared, st);
	const auto &displayLeaf = usePlaceholder
		? block->placeholderLeaf
		: block->leaf;
	const auto height = ResolveEditableHeight(
		LeafHeight(displayLeaf, textStyle, block->textWidth),
		context);
	block->textRect = QRect(left, top, block->textWidth, height);
	block->contentRect = QRect(left, top, visibleWidth, height);
	block->overflowed = (block->textWidth > visibleWidth);
	block->horizontalScrollMax = scrollOwner
		? std::max(block->textWidth - visibleWidth, 0)
		: 0;
	if (scrollOwner) {
		block->scrollViewportRect = block->contentRect;
		block->scrollLogicalContentRect = block->textRect;
		if (block->horizontalScrollMax > 0) {
			block->scrollScrollbarTrackRect = QRect(
				left,
				top + height + st.table.scrollbarSkip,
				visibleWidth,
				st.table.scrollbarHeight);
		}
	}
	block->outer = QRect(
		left,
		top,
		visibleWidth,
		height + ScrollbarReserveHeight(
			scrollOwner,
			block->horizontalScrollMax,
			st));
	block->firstLineBaseline = LeafFirstLineBaseline(
		displayLeaf,
		block->textRect,
		textStyle);
	FinishBlockGeometry(block);
	return block->outer.y() + block->outer.height();
}

[[nodiscard]] std::optional<int> LayoutCodeBlockGeometry(
		const PreparedBlock &prepared,
		LaidOutBlock *block,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		int logicalWidth,
		bool scrollOwner,
		LayoutContext context) {
	if (!block) {
		return std::nullopt;
	}
	const auto usePlaceholder = prepared.text.text.isEmpty()
		&& !prepared.editPlaceholderText.isEmpty();
	if (MissingRetainedLeaf(prepared.text.text, block->leaf)
		|| MissingRetainedPlaceholderLeaf(usePlaceholder, block->placeholderLeaf)) {
		return std::nullopt;
	}
	ClearBlockGeometry(block);
	const auto &pre = st.code.pre;
	const auto padding = BlockquotePadding(pre);
	const auto outerWidth = std::max(
		width,
		padding.left() + padding.right() + 1);
	const auto logicalOuterWidth = std::max(
		(logicalWidth > 0) ? logicalWidth : outerWidth,
		padding.left() + padding.right() + 1);
	const auto viewportWidth = std::max(
		outerWidth - padding.left() - padding.right(),
		1);
	block->textWidth = std::max(
		logicalOuterWidth - padding.left() - padding.right(),
		1);
	const auto &displayLeaf = usePlaceholder
		? block->placeholderLeaf
		: block->leaf;
	const auto height = ResolveEditableHeight(
		LeafHeight(displayLeaf, st.code, block->textWidth),
		context);
	block->overflowed = (block->textWidth > viewportWidth);
	block->horizontalScrollMax = scrollOwner
		? std::max(block->textWidth - viewportWidth, 0)
		: 0;
	const auto outerHeight = padding.top()
		+ height
		+ padding.bottom()
		+ ScrollbarReserveHeight(
			scrollOwner,
			block->horizontalScrollMax,
			st);
	block->outer = QRect(left, top, outerWidth, outerHeight);
	block->headerRect = QRect(left, top, outerWidth, pre.header);
	block->bodyRect = QRect(
		left,
		top + pre.header,
		outerWidth,
		std::max(outerHeight - pre.header, 0));
	block->contentRect = QRect(
		left + padding.left(),
		top + padding.top(),
		viewportWidth,
		height);
	block->textRect = QRect(
		left + padding.left(),
		top + padding.top(),
		block->textWidth,
		height);
	if (scrollOwner) {
		block->scrollViewportRect = block->contentRect;
		block->scrollLogicalContentRect = block->textRect;
		if (block->horizontalScrollMax > 0) {
			block->scrollScrollbarTrackRect = QRect(
				block->contentRect.x(),
				block->contentRect.y()
					+ block->contentRect.height()
					+ st.table.scrollbarSkip,
				block->contentRect.width(),
				st.table.scrollbarHeight);
		}
	}
	block->firstLineBaseline = LeafFirstLineBaseline(
		displayLeaf,
		block->textRect,
		st.code);
	FinishBlockGeometry(block);
	return block->outer.y() + block->outer.height();
}

[[nodiscard]] std::optional<int> LayoutRuleBlockGeometry(
		LaidOutBlock *block,
		const style::Markdown &st,
		int left,
		int top,
		int width) {
	if (!block) {
		return std::nullopt;
	}
	ClearBlockGeometry(block);
	block->outer = QRect(left, top, std::max(width, 1), st.rule.height);
	block->textRect = block->outer;
	FinishBlockGeometry(block);
	return block->outer.y() + block->outer.height();
}

[[nodiscard]] std::optional<int> LayoutDisplayMathBlockGeometry(
		const PreparedBlock &prepared,
		const std::vector<PreparedFormulaSlot> &formulas,
		LaidOutBlock *block,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		int logicalWidth,
		bool scrollOwner,
		LayoutContext context) {
	if (!block) {
		return std::nullopt;
	}
	const auto formula = PreparedFormulaFor(formulas, prepared.formulaIndex);
	const auto usePlaceholder = prepared.formulaTex.trimmed().isEmpty()
		&& !prepared.editPlaceholderText.isEmpty();
	if (!formula || !formula->measured.success) {
		if (usePlaceholder) {
			if (block->placeholderLeaf.isEmpty()) {
				return std::nullopt;
			}
		} else if (block->fallbackLeaf.isEmpty()) {
			return std::nullopt;
		}
	}
	ClearBlockGeometry(block);
	const auto &padding = st.displayMath.padding;
	const auto contentLeft = left + padding.left();
	const auto contentTop = top + padding.top();
	const auto contentWidth = std::max(
		width - padding.left() - padding.right(),
		1);
	const auto contentLogicalWidth = std::max(
		((logicalWidth > 0) ? logicalWidth : std::max(width, 1))
			- padding.left()
			- padding.right(),
		1);
	auto formulaWidth = 0;
	auto formulaHeight = 0;
	if (formula && formula->measured.success) {
		formulaWidth = std::max(formula->measured.logicalSize.width(), 1);
		formulaHeight = std::max(formula->measured.logicalSize.height(), 1);
	} else {
		const auto &fallbackPadding = st.displayMath.fallbackPadding;
		const auto fallbackPaddingWidth = fallbackPadding.left()
			+ fallbackPadding.right();
		const auto &displayLeaf = usePlaceholder
			? block->placeholderLeaf
			: block->fallbackLeaf;
		block->textWidth = std::max(
			contentLogicalWidth - fallbackPaddingWidth,
			1);
		block->textWidth = std::min(
			block->textWidth,
			std::max(displayLeaf.maxWidth(), 1));
		auto textHeight = LeafHeight(
			displayLeaf,
			st.displayMath.fallbackStyle,
			block->textWidth);
		formulaWidth = std::min(
			block->textWidth + fallbackPaddingWidth,
			contentLogicalWidth);
		block->textWidth = std::max(formulaWidth - fallbackPaddingWidth, 1);
		textHeight = LeafHeight(
			displayLeaf,
			st.displayMath.fallbackStyle,
			block->textWidth);
		formulaHeight = fallbackPadding.top()
			+ textHeight
			+ fallbackPadding.bottom();
		block->textRect.setSize(QSize(block->textWidth, textHeight));
	}
	const auto centered = (st.displayMath.align == ::style::al_center)
		&& (formulaWidth <= contentLogicalWidth);
	block->formulaAlign = centered ? ::style::al_center : ::style::al_left;
	block->contentRect = QRect(
		contentLeft,
		contentTop,
		contentWidth,
		formulaHeight);
	block->formulaRect = QRect(
		centered
			? (contentLeft + ((contentLogicalWidth - formulaWidth) / 2))
			: contentLeft,
		contentTop,
		formulaWidth,
		formulaHeight);
	block->visibleFormulaRect = block->formulaRect.intersected(block->contentRect);
	block->outer = QRect(
		left,
		top,
		std::max(width, 1),
		padding.top()
			+ formulaHeight
			+ padding.bottom());
	block->overflowed = (block->formulaRect.width()
		> block->visibleFormulaRect.width());
	block->horizontalScrollMax = scrollOwner
		? std::max(block->formulaRect.width() - block->contentRect.width(), 0)
		: 0;
	if (scrollOwner) {
		block->scrollViewportRect = block->contentRect;
		block->scrollLogicalContentRect = block->formulaRect;
		if (block->horizontalScrollMax > 0) {
			block->scrollScrollbarTrackRect = QRect(
				block->contentRect.x(),
				block->contentRect.y()
					+ block->contentRect.height()
					+ st.table.scrollbarSkip,
				block->contentRect.width(),
				st.table.scrollbarHeight);
			block->outer.setHeight(
				block->outer.height()
					+ ScrollbarReserveHeight(
						scrollOwner,
						block->horizontalScrollMax,
						st));
		}
	}
	block->outer.setHeight(ResolveEditableHeight(
		block->outer.height(),
		context));
	if (!(formula && formula->measured.success)) {
		const auto &fallbackPadding = st.displayMath.fallbackPadding;
		const auto &displayLeaf = usePlaceholder
			? block->placeholderLeaf
			: block->fallbackLeaf;
		block->textRect.moveTo(
			block->formulaRect.x() + fallbackPadding.left(),
			block->formulaRect.y() + fallbackPadding.top());
		block->firstLineBaseline = LeafFirstLineBaseline(
			displayLeaf,
			block->textRect,
			st.displayMath.fallbackStyle);
	}
	FinishBlockGeometry(block);
	return block->outer.y() + block->outer.height();
}

[[nodiscard]] std::optional<int> LayoutTableBlockGeometry(
		const PreparedBlock &prepared,
		LaidOutBlock *block,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		int logicalWidth,
		bool scrollOwner,
		LayoutContext context) {
	if (!block || prepared.tableRows.size() != block->tableRows.size()) {
		return std::nullopt;
	}
	const auto columnCount = prepared.tableColumnCount;
	for (auto rowIndex = 0, rowCount = int(prepared.tableRows.size());
			rowIndex != rowCount;
			++rowIndex) {
		const auto &preparedRow = prepared.tableRows[rowIndex];
		const auto &row = block->tableRows[rowIndex];
		if (preparedRow.cells.size() != row.cells.size()) {
			return std::nullopt;
		}
		for (auto cellIndex = 0, cellCount = int(preparedRow.cells.size());
				cellIndex != cellCount;
				++cellIndex) {
			const auto &preparedCell = preparedRow.cells[cellIndex];
			const auto &cell = row.cells[cellIndex];
			const auto usePlaceholder = preparedCell.text.text.isEmpty()
				&& !preparedCell.editPlaceholderText.isEmpty();
			if (preparedCell.header != cell.header
				|| std::max(preparedCell.column, 0) != cell.column
				|| std::max(preparedCell.colspan, 1) != cell.colspan
				|| std::max(preparedCell.rowspan, 1) != cell.rowspan
				|| preparedCell.verticalAlignment != cell.verticalAlignment
				|| MissingRetainedLeaf(preparedCell.text.text, cell.leaf)
				|| MissingRetainedPlaceholderLeaf(
					usePlaceholder,
					cell.placeholderLeaf)) {
				return std::nullopt;
			}
		}
	}
	const auto useTitlePlaceholder = prepared.text.text.isEmpty()
		&& !prepared.editPlaceholderText.isEmpty();
	const auto hasTitle = !prepared.text.text.isEmpty()
		|| prepared.forceTextSegment;
	if ((hasTitle && MissingRetainedLeaf(prepared.text.text, block->leaf))
		|| MissingRetainedPlaceholderLeaf(
			useTitlePlaceholder,
			block->placeholderLeaf)) {
		return std::nullopt;
	}
	ClearBlockGeometry(block);
	for (auto &row : block->tableRows) {
		ResetTableRowGeometry(&row);
	}
	auto tableTop = top;
	if (hasTitle) {
		const auto &displayLeaf = useTitlePlaceholder
			? block->placeholderLeaf
			: block->leaf;
		block->textWidth = std::max(width, 1);
		block->textRect = QRect(
			left,
			top,
			block->textWidth,
			ResolveEditableHeight(
				std::max(
					displayLeaf.countHeight(block->textWidth),
					TextLineHeight(st.body)),
				context));
		block->firstLineBaseline = LeafFirstLineBaseline(
			displayLeaf,
			block->textRect,
			st.body);
		tableTop = block->textRect.y()
			+ block->textRect.height()
			+ st.table.captionSkip;
	}
	const auto visibleWidth = std::max(width, 1);
	const auto layoutWidth = std::max(
		(logicalWidth > 0) ? logicalWidth : visibleWidth,
		1);
	if (!columnCount || prepared.tableRows.empty()) {
		if (!block->textRect.isEmpty()) {
			block->contentRect = block->textRect;
			block->outer = block->textRect;
		} else {
			block->outer = QRect(left, top, visibleWidth, 0);
		}
		FinishBlockGeometry(block);
		return block->outer.y() + block->outer.height();
	}
	auto rows = std::vector<TableRowGeometryData>();
	rows.reserve(prepared.tableRows.size());
	for (auto rowIndex = 0, rowCount = int(prepared.tableRows.size());
			rowIndex != rowCount;
			++rowIndex) {
		const auto &preparedRow = prepared.tableRows[rowIndex];
		auto row = TableRowGeometryData();
		row.header = preparedRow.header;
		row.cells.reserve(preparedRow.cells.size());
		for (auto cellIndex = 0, cellCount = int(preparedRow.cells.size());
				cellIndex != cellCount;
				++cellIndex) {
			auto &cell = block->tableRows[rowIndex].cells[cellIndex];
			const auto &preparedCell = preparedRow.cells[cellIndex];
			const auto usePlaceholder = preparedCell.text.text.isEmpty()
				&& !preparedCell.editPlaceholderText.isEmpty();
			const auto &displayLeaf = usePlaceholder
				? cell.placeholderLeaf
				: cell.leaf;
			const auto &textStyle = TableCellTextStyle(cell, st);
			auto cellData = TableCellGeometryData();
			cellData.cell = &cell;
			cellData.usePlaceholder = usePlaceholder;
			cellData.minimumWidth = LeafMinimumWidth(displayLeaf);
			cellData.preferredWidth = displayLeaf.maxWidth();
			if (cell.cachedPreferredWidth != cellData.preferredWidth) {
				cell.cachedPreferredWidth = cellData.preferredWidth;
				cell.cachedPreferredHeight = std::max(
					displayLeaf.countHeight(
						std::max(cellData.preferredWidth, 1),
						true),
					TextLineHeight(textStyle));
			}
			cellData.preferredHeight = cell.cachedPreferredHeight;
			row.cells.push_back(std::move(cellData));
		}
		rows.push_back(std::move(row));
	}
	block->tableColumnWidths = ComputeTableColumnWidths(
		rows,
		columnCount,
		layoutWidth,
		st,
		block->tableBordered,
		&block->overflowed);
	const auto &padding = st.table.cellPadding;
	const auto border = TableBorder(block->tableBordered, st);
	block->tableBorder = border;
	auto tableWidth = border;
	for (const auto columnWidth : block->tableColumnWidths) {
		tableWidth += columnWidth + border;
	}
	auto columnLefts = std::vector<int>(columnCount, left + border);
	auto x = left + border;
	for (auto column = 0; column != columnCount; ++column) {
		columnLefts[column] = x;
		x += block->tableColumnWidths[column] + border;
	}
	auto rowHeights = std::vector<int>(rows.size(), 0);
	auto rowSpans = std::vector<TableSpannedCellGeometryData>();
	for (auto rowIndex = 0, rowCount = int(rows.size()); rowIndex != rowCount; ++rowIndex) {
		for (auto &cellData : rows[rowIndex].cells) {
			if (!cellData.cell) {
				continue;
			}
			const auto spanWidth = TableSpanWidth(
				block->tableColumnWidths,
				cellData.cell->column,
				cellData.cell->colspan,
				border);
			cellData.cell->textWidth = std::max(
				spanWidth - padding.left() - padding.right(),
				1);
			const auto &textStyle = TableCellTextStyle(*cellData.cell, st);
			const auto &displayLeaf = cellData.usePlaceholder
				? cellData.cell->placeholderLeaf
				: cellData.cell->leaf;
			const auto naturalTextHeight = (cellData.cell->textWidth
				>= cellData.preferredWidth)
				? cellData.preferredHeight
				: std::max(
					displayLeaf.countHeight(
						cellData.cell->textWidth,
						true),
					TextLineHeight(textStyle));
			cellData.textHeight = ResolveEditableHeight(
				naturalTextHeight,
				context);
			const auto outerHeight = cellData.textHeight
				+ padding.top()
				+ padding.bottom();
			if (cellData.cell->rowspan == 1) {
				rowHeights[rowIndex] = std::max(rowHeights[rowIndex], outerHeight);
			} else {
				rowSpans.push_back({ rowIndex, &cellData });
			}
		}
	}
	std::sort(
		rowSpans.begin(),
		rowSpans.end(),
		[](const TableSpannedCellGeometryData &a,
				const TableSpannedCellGeometryData &b) {
			const auto aSpan = a.cell ? a.cell->cell->rowspan : 0;
			const auto bSpan = b.cell ? b.cell->cell->rowspan : 0;
			return (aSpan < bSpan)
				|| ((aSpan == bSpan) && (a.row < b.row))
				|| ((aSpan == bSpan)
					&& (a.row == b.row)
					&& a.cell
					&& b.cell
					&& (a.cell->cell->column < b.cell->cell->column));
		});
	for (const auto &spanned : rowSpans) {
		if (!spanned.cell || !spanned.cell->cell) {
			continue;
		}
		const auto outerHeight = spanned.cell->textHeight
			+ padding.top()
			+ padding.bottom();
		const auto currentHeight = TableSpanHeight(
			rowHeights,
			spanned.row,
			spanned.cell->cell->rowspan,
			border);
		DistributeSpanDelta(
			&rowHeights,
			spanned.row,
			spanned.row + spanned.cell->cell->rowspan,
			std::max(outerHeight - currentHeight, 0));
	}
	auto y = tableTop + border;
	for (auto rowIndex = 0, rowCount = int(rows.size()); rowIndex != rowCount; ++rowIndex) {
		auto &rowData = rows[rowIndex];
		const auto rowHeight = rowHeights[rowIndex];
		auto &row = block->tableRows[rowIndex];
		row.header = rowData.header;
		row.logicalOuter = QRect(
			left + border,
			y,
			std::max(tableWidth - (2 * border), 0),
			rowHeight);
		row.outer = row.logicalOuter;
		for (auto cellIndex = 0, cellCount = int(rowData.cells.size());
				cellIndex != cellCount;
				++cellIndex) {
			auto &cellData = rowData.cells[cellIndex];
			if (!cellData.cell) {
				continue;
			}
			auto &cell = *cellData.cell;
			const auto column = std::clamp(cell.column, 0, columnCount - 1);
			const auto spanWidth = TableSpanWidth(
				block->tableColumnWidths,
				cell.column,
				cell.colspan,
				border);
			const auto spanHeight = TableSpanHeight(
				rowHeights,
				rowIndex,
				cell.rowspan,
				border);
			const auto cellTop = y;
			const auto contentHeight = std::max(
				spanHeight - padding.top() - padding.bottom(),
				0);
			auto textTop = cellTop + padding.top();
			switch (cell.verticalAlignment) {
			case PreparedTableCellVerticalAlignment::Middle:
				textTop += std::max((contentHeight - cellData.textHeight) / 2, 0);
				break;
			case PreparedTableCellVerticalAlignment::Bottom:
				textTop = cellTop
					+ spanHeight
					- padding.bottom()
					- cellData.textHeight;
				break;
			case PreparedTableCellVerticalAlignment::Top:
				break;
			}
			cell.logicalOuter = QRect(
				columnLefts[column],
				cellTop,
				spanWidth,
				spanHeight);
			cell.logicalTextRect = QRect(
				columnLefts[column] + padding.left(),
				textTop,
				cell.textWidth,
				cellData.textHeight);
			cell.outer = cell.logicalOuter;
			cell.textRect = cell.logicalTextRect;
		}
		y += rowHeight + border;
	}
	const auto tableHeight = std::max(y - tableTop, border);
	block->tableRect = QRect(left, tableTop, tableWidth, tableHeight);
	block->visibleTableRect = QRect(
		left,
		tableTop,
		std::min(tableWidth, visibleWidth),
		tableHeight);
	block->overflowed = (block->tableRect.width()
		> block->visibleTableRect.width());
	block->horizontalScrollMax = scrollOwner
		? std::max(
			block->tableRect.width() - block->visibleTableRect.width(),
			0)
		: 0;
	auto tableContentRect = block->visibleTableRect;
	if (scrollOwner) {
		block->scrollLogicalContentRect = block->tableRect;
		block->scrollViewportRect = block->visibleTableRect;
		if (block->horizontalScrollMax > 0) {
			block->scrollScrollbarTrackRect = QRect(
				block->visibleTableRect.x(),
				block->tableRect.y()
					+ block->tableRect.height()
					+ st.table.scrollbarSkip,
				block->visibleTableRect.width(),
				st.table.scrollbarHeight);
		}
	}
	if (block->horizontalScrollMax > 0) {
		block->tableScrollbarTrackRect = QRect(
			block->visibleTableRect.x(),
			block->tableRect.y()
				+ block->tableRect.height()
				+ st.table.scrollbarSkip,
			block->visibleTableRect.width(),
			st.table.scrollbarHeight);
		block->tableScrollbarThumbRect = QRect();
		block->scrollScrollbarThumbRect = block->tableScrollbarThumbRect;
		tableContentRect = tableContentRect.united(block->tableScrollbarTrackRect);
	}
	block->contentRect = block->textRect.isEmpty()
		? tableContentRect
		: tableContentRect.isEmpty()
		? block->textRect
		: block->textRect.united(tableContentRect);
	block->outer = block->contentRect;
	if (!block->textRect.isEmpty()) {
		FinishBlockGeometry(block);
		return block->outer.y() + block->outer.height();
	}
	for (auto rowIndex = 0, rowCount = int(prepared.tableRows.size());
			rowIndex != rowCount;
			++rowIndex) {
		for (auto cellIndex = 0, cellCount = int(prepared.tableRows[rowIndex].cells.size());
				cellIndex != cellCount;
				++cellIndex) {
			auto &cell = block->tableRows[rowIndex].cells[cellIndex];
			const auto &preparedCell = prepared.tableRows[rowIndex].cells[cellIndex];
			const auto usePlaceholder = preparedCell.text.text.isEmpty()
				&& !preparedCell.editPlaceholderText.isEmpty();
			const auto &displayLeaf = usePlaceholder
				? cell.placeholderLeaf
				: cell.leaf;
			if (displayLeaf.isEmpty()) {
				continue;
			}
			block->firstLineBaseline = LeafFirstLineBaseline(
				displayLeaf,
				cell.textRect,
				TableCellTextStyle(cell, st));
			FinishBlockGeometry(block);
			return block->outer.y() + block->outer.height();
		}
	}
	FinishBlockGeometry(block);
	return block->outer.y() + block->outer.height();
}

[[nodiscard]] std::optional<int> LayoutPlaceholderBlockGeometry(
		const PreparedBlock &prepared,
		LaidOutBlock *block,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		LayoutContext context) {
	if (!block
		|| MissingRetainedLeaf(prepared.placeholder.label, block->labelLeaf)) {
		return std::nullopt;
	}
	ClearBlockGeometry(block);
	const auto &style = st.placeholder;
	const auto blockWidth = std::max(width, 1);
	const auto contentLeft = left + style.padding.left();
	const auto contentWidth = std::max(
		blockWidth - style.padding.left() - style.padding.right(),
		1);
	block->labelWidth = contentWidth;
	const auto labelHeight = std::max(
		block->labelLeaf.countHeight(contentWidth),
		TextLineHeight(style.labelStyle));
	const auto mediaHeight = std::max(
		style.minHeight,
		labelHeight + style.padding.top() + style.padding.bottom());
	block->mediaRect = QRect(left, top, blockWidth, mediaHeight);
	block->visibleMediaRect = block->mediaRect;
	if (block->placeholderRuntime
		&& block->placeholderRuntime->ripple
		&& block->placeholderRuntime->rippleSize != block->mediaRect.size()) {
		block->placeholderRuntime->ripple = nullptr;
		block->placeholderRuntime->rippleSize = QSize();
	}
	block->labelRect = QRect(
		contentLeft,
		top + std::max((mediaHeight - labelHeight) / 2, 0),
		contentWidth,
		labelHeight);
	block->firstLineBaseline = LeafFirstLineBaseline(
		block->labelLeaf,
		block->labelRect,
		style.labelStyle);
	auto bottom = top + mediaHeight;
	if (!LayoutMediaCaptionGeometry(
			block,
			prepared,
			st,
			contentLeft,
			bottom,
			contentWidth,
			style.captionSkip,
			&bottom,
			context)) {
		return std::nullopt;
	}
	block->contentRect = QRect(
		left,
		top,
		blockWidth,
		std::max(bottom - top, mediaHeight));
	block->outer = block->contentRect;
	FinishBlockGeometry(block);
	return block->outer.y() + block->outer.height();
}

[[nodiscard]] std::optional<int> LayoutRelatedArticleBlockGeometry(
		const PreparedBlock &prepared,
		LaidOutBlock *block,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		LayoutContext context) {
	if (!block
		|| MissingRetainedLeaf(prepared.relatedArticle.title, block->labelLeaf)
		|| MissingRetainedLeaf(
			prepared.relatedArticle.description,
			block->subtitleLeaf)
		|| MissingRetainedLeaf(prepared.relatedArticle.footer, block->actionLeaf)) {
		return std::nullopt;
	}
	ClearBlockGeometry(block);
	const auto &card = st.relatedArticle;
	const auto blockWidth = std::max(width, 1);
	const auto hasThumbnail = (prepared.relatedArticle.photoId != 0);
	const auto thumbnailSize = hasThumbnail
		? std::max(card.thumbnailSize, 1)
		: 0;
	const auto thumbnailSkip = hasThumbnail ? card.thumbnailSkip : 0;
	const auto contentLeft = left + card.padding.left();
	const auto contentWidth = std::max(
		blockWidth
			- card.padding.left()
			- card.padding.right()
			- thumbnailSize
			- thumbnailSkip,
		1);
	auto titleHeight = 0;
	if (!prepared.relatedArticle.title.isEmpty()) {
		block->labelWidth = contentWidth;
		titleHeight = LeafHeightWithLineLimit(
			block->labelLeaf,
			card.titleStyle,
			block->labelWidth,
			card.titleLines);
	}
	auto subtitleHeight = 0;
	if (!prepared.relatedArticle.description.isEmpty()) {
		block->subtitleWidth = contentWidth;
		subtitleHeight = LeafHeightWithLineLimit(
			block->subtitleLeaf,
			card.subtitleStyle,
			block->subtitleWidth,
			card.subtitleLines);
	}
	auto footerHeight = 0;
	if (!prepared.relatedArticle.footer.isEmpty()) {
		block->actionWidth = contentWidth;
		footerHeight = LeafHeightWithLineLimit(
			block->actionLeaf,
			card.footerStyle,
			block->actionWidth,
			card.footerLines);
	}
	auto textHeight = 0;
	if (titleHeight) {
		textHeight += titleHeight;
	}
	if (subtitleHeight) {
		textHeight += subtitleHeight + (textHeight ? card.textSkip : 0);
	}
	if (footerHeight) {
		textHeight += footerHeight + (textHeight ? card.footerSkip : 0);
	}
	const auto cardContentHeight = std::max(textHeight, thumbnailSize);
	const auto cardHeight = card.padding.top()
		+ cardContentHeight
		+ card.padding.bottom()
		+ card.separator;
	block->mediaRect = QRect(left, top, blockWidth, cardHeight);
	block->visibleMediaRect = block->mediaRect;
	if (hasThumbnail) {
		block->thumbnailRect = QRect(
			left + blockWidth - card.padding.right() - thumbnailSize,
			top + card.padding.top()
				+ std::max((cardContentHeight - thumbnailSize) / 2, 0),
			thumbnailSize,
			thumbnailSize);
	}
	auto textTop = top + card.padding.top()
		+ std::max((cardContentHeight - textHeight) / 2, 0);
	if (titleHeight) {
		block->labelRect = QRect(
			contentLeft,
			textTop,
			block->labelWidth,
			titleHeight);
		textTop += titleHeight;
	}
	if (subtitleHeight) {
		textTop += block->labelRect.isEmpty() ? 0 : card.textSkip;
		block->subtitleRect = QRect(
			contentLeft,
			textTop,
			block->subtitleWidth,
			subtitleHeight);
		textTop += subtitleHeight;
	}
	if (footerHeight) {
		textTop += (block->labelRect.isEmpty() && block->subtitleRect.isEmpty())
			? 0
			: card.footerSkip;
		block->actionRect = QRect(
			contentLeft,
			textTop,
			block->actionWidth,
			footerHeight);
	}
	const auto setBaseline = [&](const Ui::Text::String &leaf,
			QRect rect,
			const style::TextStyle &textStyle) {
		if (rect.isEmpty() || leaf.isEmpty()) {
			return false;
		}
		block->firstLineBaseline = LeafFirstLineBaseline(leaf, rect, textStyle);
		return true;
	};
	if (!setBaseline(block->labelLeaf, block->labelRect, card.titleStyle)
		&& !setBaseline(
			block->subtitleLeaf,
			block->subtitleRect,
			card.subtitleStyle)
		&& !setBaseline(
			block->actionLeaf,
			block->actionRect,
			card.footerStyle)) {
		block->firstLineBaseline = top + card.padding.top();
	}
	block->contentRect = block->mediaRect;
	block->outer = block->mediaRect;
	FinishBlockGeometry(block);
	return block->outer.y() + block->outer.height();
}

[[nodiscard]] std::optional<int> LayoutFramedMediaBlockGeometry(
		LaidOutBlock *block,
		const PreparedBlock &prepared,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		QMargins padding,
		int captionSkip,
		bool limitWidth,
		bool centerMedia,
		int intrinsicWidth,
		LayoutContext context) {
	if (!block) {
		return std::nullopt;
	}
	ClearBlockGeometry(block);
	const auto blockWidth = std::max(width, 1);
	const auto availableLeft = left + padding.left();
	const auto mediaTop = top + padding.top();
	const auto availableWidth = std::max(
		blockWidth - padding.left() - padding.right(),
		1);
	const auto mediaWidth = limitWidth
		? LimitedMediaWidth(availableWidth, intrinsicWidth)
		: availableWidth;
	const auto mediaLeft = centerMedia
		? (availableLeft + std::max((availableWidth - mediaWidth) / 2, 0))
		: availableLeft;
	const auto mediaHeight = block->mediaBlock
		? block->mediaBlock->resizeGetHeight(mediaWidth)
		: 0;
	block->mediaRect = QRect(mediaLeft, mediaTop, mediaWidth, mediaHeight);
	block->visibleMediaRect = block->mediaRect;
	if (block->mediaBlock) {
		ApplyMediaBlockGeometry(
			block,
			block->mediaRect,
			st,
			context.mediaPixelScale);
	}
	auto bottom = block->mediaRect.y() + block->mediaRect.height()
		+ padding.bottom();
	if (!LayoutMediaCaptionGeometry(
			block,
			prepared,
			st,
			block->mediaRect.x(),
			bottom,
			std::max(block->mediaRect.width(), 1),
			captionSkip,
			&bottom,
			context)) {
		return std::nullopt;
	}
	block->contentRect = QRect(
		block->mediaRect.x(),
		block->mediaRect.y(),
		block->mediaRect.width(),
		std::max(bottom - block->mediaRect.y(), block->mediaRect.height()));
	block->outer = QRect(
		left,
		top,
		blockWidth,
		std::max(bottom - top, block->mediaRect.height()));
	FinishBlockGeometry(block);
	return block->outer.y() + block->outer.height();
}

[[nodiscard]] std::optional<int> LayoutCardMediaBlockGeometry(
		LaidOutBlock *block,
		const PreparedBlock &prepared,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		QMargins padding,
		int captionSkip,
		LayoutContext context) {
	if (!block) {
		return std::nullopt;
	}
	ClearBlockGeometry(block);
	const auto blockWidth = std::max(width, 1);
	const auto cardHeight = block->mediaBlock
		? block->mediaBlock->resizeGetHeight(blockWidth)
		: 0;
	block->mediaRect = QRect(left, top, blockWidth, cardHeight);
	block->visibleMediaRect = block->mediaRect;
	if (block->mediaBlock) {
		ApplyMediaBlockGeometry(
			block,
			block->mediaRect,
			st,
			context.mediaPixelScale);
	}
	auto bottom = top + cardHeight;
	if (!LayoutMediaCaptionGeometry(
			block,
			prepared,
			st,
			left + padding.left(),
			bottom,
			std::max(blockWidth - padding.left() - padding.right(), 1),
			captionSkip,
			&bottom,
			context)) {
		return std::nullopt;
	}
	block->contentRect = QRect(
		left,
		top,
		blockWidth,
		std::max(bottom - top, cardHeight));
	block->outer = block->contentRect;
	FinishBlockGeometry(block);
	return block->outer.y() + block->outer.height();
}

std::optional<int> RecountSimpleLaidOutBlock(
		const PreparedBlock &prepared,
		const std::vector<PreparedFormulaSlot> &formulas,
		LaidOutBlock *block,
		const style::Markdown &st,
		int left,
		int top,
		int width,
		int logicalWidth,
		bool scrollOwner,
		LayoutContext context) {
	switch (prepared.kind) {
	case PreparedBlockKind::Paragraph:
	case PreparedBlockKind::Thinking:
	case PreparedBlockKind::Heading:
		return LayoutFlowBlockGeometry(
			prepared,
			block,
			st,
			left,
			top,
			width,
			logicalWidth,
			scrollOwner,
			context);
	case PreparedBlockKind::CodeBlock:
		return LayoutCodeBlockGeometry(
			prepared,
			block,
			st,
			left,
			top,
			width,
			logicalWidth,
			scrollOwner,
			context);
	case PreparedBlockKind::Rule:
		return LayoutRuleBlockGeometry(block, st, left, top, width);
	case PreparedBlockKind::DisplayMath:
		return LayoutDisplayMathBlockGeometry(
			prepared,
			formulas,
			block,
			st,
			left,
			top,
			width,
			logicalWidth,
			scrollOwner,
			context);
	case PreparedBlockKind::Table:
		return LayoutTableBlockGeometry(
			prepared,
			block,
			st,
			left,
			top,
			width,
			logicalWidth,
			scrollOwner,
			context);
	case PreparedBlockKind::Photo:
		if (!block
			|| !block->mediaBlock
			|| !block->mediaBlock->alive()) {
			return std::nullopt;
		}
		return LayoutFramedMediaBlockGeometry(
			block,
			prepared,
			st,
			left,
			top,
			width,
			st.photo.padding,
			st.photo.captionSkip,
			true,
			true,
			prepared.photo.width,
			context);
	case PreparedBlockKind::Video:
		if (!block
			|| !block->mediaBlock
			|| !block->mediaBlock->alive()) {
			return std::nullopt;
		}
		return LayoutFramedMediaBlockGeometry(
			block,
			prepared,
			st,
			left,
			top,
			width,
			st.photo.padding,
			st.photo.captionSkip,
			true,
			true,
			prepared.video.media.width,
			context);
	case PreparedBlockKind::Audio:
		if (!block
			|| !block->mediaBlock
			|| !block->mediaBlock->alive()) {
			return std::nullopt;
		}
		return LayoutCardMediaBlockGeometry(
			block,
			prepared,
			st,
			left,
			top,
			width,
			st.audio.padding,
			st.audio.captionSkip,
			context);
	case PreparedBlockKind::Map:
		if (!block
			|| !block->mediaBlock
			|| !block->mediaBlock->alive()) {
			return std::nullopt;
		}
		return LayoutFramedMediaBlockGeometry(
			block,
			prepared,
			st,
			left,
			top,
			width,
			st.photo.padding,
			st.photo.captionSkip,
			false,
			false,
			0,
			context);
	case PreparedBlockKind::Channel:
		if (!block
			|| !block->mediaBlock
			|| !block->mediaBlock->alive()) {
			return std::nullopt;
		}
		return LayoutCardMediaBlockGeometry(
			block,
			prepared,
			st,
			left,
			top,
			width,
			st.channel.padding,
			st.audio.captionSkip,
			context);
	case PreparedBlockKind::GroupedMedia:
		if (!block
			|| !block->mediaBlock
			|| !block->mediaBlock->alive()) {
			return std::nullopt;
		}
		return LayoutFramedMediaBlockGeometry(
			block,
			prepared,
			st,
			left,
			top,
			width,
			st.groupedMedia.padding,
			st.groupedMedia.captionSkip,
			false,
			false,
			0,
			context);
	case PreparedBlockKind::RelatedArticle:
		return LayoutRelatedArticleBlockGeometry(
			prepared,
			block,
			st,
			left,
			top,
			width,
			context);
	case PreparedBlockKind::Placeholder:
		return LayoutPlaceholderBlockGeometry(
			prepared,
			block,
			st,
			left,
			top,
			width,
			context);
	case PreparedBlockKind::List:
	case PreparedBlockKind::ListItem:
	case PreparedBlockKind::Quote:
	case PreparedBlockKind::Details:
	case PreparedBlockKind::EmbedPost:
		break;
	}
	return std::nullopt;
}

} // namespace Iv::Markdown
