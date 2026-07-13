/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/iv_markdown_article.h"

#include "base/algorithm.h"
#include "iv/markdown/iv_markdown_article_layout_structure.h"
#include "iv/markdown/iv_markdown_article_paint.h"
#include "iv/markdown/iv_markdown_article_selection.h"
#include "iv/markdown/iv_markdown_article_text.h"
#include "iv/markdown/iv_markdown_media_reuse.h"
#include "iv/markdown/iv_markdown_prepare_links.h"
#include "iv/markdown/iv_markdown_prepare_serialize.h"
#include "lang/lang_keys.h"
#include "ui/style/style_core_color.h"
#include "ui/style/style_core_scale.h"
#include "ui/widgets/checkbox.h"
#include "ui/basic_click_handlers.h"
#include "ui/dynamic_image.h"

#include "styles/style_iv.h"
#include "styles/style_widgets.h"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Iv::Markdown {
namespace {

struct PendingHighlightKey {
	QString text;
	QString language;
};

[[nodiscard]] bool operator==(
		const PendingHighlightKey &a,
		const PendingHighlightKey &b) {
	return (a.text == b.text) && (a.language == b.language);
}

struct PendingHighlightKeyHasher {
	[[nodiscard]] size_t operator()(
			const PendingHighlightKey &key) const noexcept;
};

size_t PendingHighlightKeyHasher::operator()(
		const PendingHighlightKey &key) const noexcept {
	auto result = size_t(qHash(key.text));
	result = (result * 1315423911U) ^ size_t(qHash(key.language));
	return result;
}

struct PendingHighlightEntry {
	PendingHighlightKey key;
	std::vector<LaidOutBlock*> blocks;
};

struct RelatedArticleImageState {
	std::shared_ptr<Ui::DynamicImage> thumbnailImage;
	std::shared_ptr<Ui::DynamicImage> previousThumbnailImage;
	std::shared_ptr<Ui::DynamicImage> fullImage;
	std::shared_ptr<Ui::DynamicImage> previousFullImage;
};

[[nodiscard]] size_t CombineHash(size_t accumulator, size_t value) {
	return (accumulator * 1315423911U) ^ value;
}

struct MarkdownArticleScrollOwnerIdentity {
	std::optional<PreparedEditBlockPath> blockPath;
	std::vector<int> preparedPath;

	friend inline bool operator==(
			const MarkdownArticleScrollOwnerIdentity &a,
			const MarkdownArticleScrollOwnerIdentity &b) {
		return (a.blockPath == b.blockPath)
			&& (a.preparedPath == b.preparedPath);
	}
};

struct MarkdownArticleScrollOwnerIdentityHasher {
	[[nodiscard]] size_t operator()(
			const MarkdownArticleScrollOwnerIdentity &value) const noexcept {
		auto result = CombineHash(0, value.blockPath ? 1 : 0);
		if (value.blockPath) {
			for (const auto &step : value.blockPath->container.steps) {
				result = CombineHash(
					result,
					static_cast<size_t>(step.kind));
				result = CombineHash(
					result,
					size_t(step.blockIndex + 1));
				result = CombineHash(
					result,
					size_t(step.listItemIndex + 1));
			}
			result = CombineHash(
				result,
				size_t(value.blockPath->index + 1));
		}
		result = CombineHash(result, size_t(value.preparedPath.size() + 1));
		for (const auto step : value.preparedPath) {
			result = CombineHash(result, size_t(step + 1));
		}
		return result;
	}
};

struct MarkdownArticleHorizontalScrollLookup {
	MarkdownArticleHorizontalScrollHit hit;
	MarkdownArticleScrollOwnerIdentity identity;
	const LaidOutBlock *block = nullptr;
};

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

[[nodiscard]] QString DetailsStateText(bool open) {
	return open
		? tr::lng_iv_details_state_expanded(tr::now)
		: tr::lng_iv_details_state_collapsed(tr::now);
}

[[nodiscard]] size_t TextStyleKey(const style::TextStyle &style) {
	return reinterpret_cast<size_t>(&style);
}

[[nodiscard]] CachedTextLeafSourceSignature MarkedTextLeafSourceSignature(
		TextWithEntities text,
		const style::TextStyle &textStyle,
		int minResizeWidth) {
	auto result = CachedTextLeafSourceSignature();
	result.dependsOnMediaRuntime = TextDependsOnMediaRuntime(text);
	result.text = std::move(text);
	result.minResizeWidth = minResizeWidth;
	result.styleKey = TextStyleKey(textStyle);
	return result;
}

[[nodiscard]] CachedTextLeafSourceSignature PlainTextLeafSourceSignature(
		const QString &text,
		const style::TextStyle &textStyle,
		int minResizeWidth) {
	return MarkedTextLeafSourceSignature(
		TextWithEntities::Simple(text),
		textStyle,
		minResizeWidth);
}

[[nodiscard]] CachedTextLeafSourceSignature CodeTextLeafSourceSignature(
		const PreparedBlock &prepared,
		const style::Markdown &st) {
	auto result = MarkedTextLeafSourceSignature(
		CodeBlockDisplayText(prepared.text),
		st.code,
		CodeTextMinResizeWidth(st));
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

void StoreCachedTextLeaf(
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

void PruneMediaRuntimeBoundCachedTextLeafs(CachedTextLeafPool *pool) {
	if (!pool) {
		return;
	}
	for (auto i = pool->entries.begin(); i != pool->entries.end();) {
		if (i->second.source.dependsOnMediaRuntime) {
			i = pool->entries.erase(i);
		} else {
			++i;
		}
	}
}

void HarvestCachedTextLeafs(
	const std::vector<PreparedBlock> &preparedBlocks,
	std::vector<LaidOutBlock> *blocks,
	const style::Markdown &st,
	CachedTextLeafPool *pool,
	std::vector<int> *preparedPath);

void RebuildCachedTextLeafs(
		const std::vector<PreparedBlock> &preparedBlocks,
		std::vector<LaidOutBlock> *blocks,
		const style::Markdown &st,
		CachedTextLeafPool *pool) {
	if (!pool) {
		return;
	}
	pool->entries.clear();
	auto preparedPath = std::vector<int>();
	HarvestCachedTextLeafs(
		preparedBlocks,
		blocks,
		st,
		pool,
		&preparedPath);
}

void HarvestCachedTextLeafs(
		const PreparedBlock &prepared,
		LaidOutBlock *block,
		const style::Markdown &st,
		CachedTextLeafPool *pool,
		const std::vector<int> &preparedPath) {
	const auto storeBlockLeaf = [&](CachedTextLeafSlot slot,
			CachedTextLeafSourceSignature source,
			Ui::Text::String *leaf) {
		StoreCachedTextLeaf(
			pool,
			BlockCachedTextLeafKey(slot, prepared, preparedPath),
			std::move(source),
			leaf);
	};
	const auto storeTableCellLeaf = [&](
			CachedTextLeafSlot slot,
			const PreparedTableCell &preparedCell,
			int tableRowIndex,
			int tableCellIndex,
			CachedTextLeafSourceSignature source,
			Ui::Text::String *leaf) {
		StoreCachedTextLeaf(
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

	if (!block->marker.isEmpty()) {
		storeBlockLeaf(
			CachedTextLeafSlot::Marker,
			PlainTextLeafSourceSignature(
				ListMarkerText(prepared),
				st.body,
				PlainTextMinResizeWidth(st.body)),
			&block->marker);
	}

	switch (prepared.kind) {
	case PreparedBlockKind::Paragraph:
	case PreparedBlockKind::Thinking:
	case PreparedBlockKind::Heading:
	{
		const auto &textStyle = TextStyleFor(prepared, st);
		const auto &placeholderStyle = EditPlaceholderTextStyleFor(
			prepared,
			st);
		storeBlockLeaf(
			CachedTextLeafSlot::Leaf,
			MarkedTextLeafSourceSignature(
				prepared.text,
				textStyle,
				FlowBlockMinimumWidth(prepared, st)),
			&block->leaf);
		storeBlockLeaf(
			CachedTextLeafSlot::Placeholder,
			PlainTextLeafSourceSignature(
				prepared.editPlaceholderText,
				placeholderStyle,
				PlainTextMinResizeWidth(placeholderStyle)),
			&block->placeholderLeaf);
	} break;
	case PreparedBlockKind::CodeBlock:
		StoreCachedTextLeaf(
			pool,
			BlockCachedTextLeafKey(
				CachedTextLeafSlot::Leaf,
				prepared,
				preparedPath),
			CodeTextLeafSourceSignature(prepared, st),
			&block->leaf,
			block->syntaxHighlightProcessId);
		block->syntaxHighlightProcessId = 0;
		storeBlockLeaf(
			CachedTextLeafSlot::Placeholder,
			PlainTextLeafSourceSignature(
				prepared.editPlaceholderText,
				st.code,
				PlainTextMinResizeWidth(st.code)),
			&block->placeholderLeaf);
		break;
	case PreparedBlockKind::DisplayMath:
		storeBlockLeaf(
			CachedTextLeafSlot::Placeholder,
			PlainTextLeafSourceSignature(
				prepared.editPlaceholderText,
				st.displayMath.fallbackStyle,
				DisplayMathFallbackTextMinResizeWidth(st)),
			&block->placeholderLeaf);
		storeBlockLeaf(
			CachedTextLeafSlot::Fallback,
			MarkedTextLeafSourceSignature(
				DisplayMathFallbackText(),
				st.displayMath.fallbackStyle,
				DisplayMathFallbackTextMinResizeWidth(st)),
			&block->fallbackLeaf);
		break;
	case PreparedBlockKind::Table: {
		storeBlockLeaf(
			CachedTextLeafSlot::Leaf,
			MarkedTextLeafSourceSignature(
				prepared.text,
				st.body,
				FlowTextMinResizeWidth(st.body)),
			&block->leaf);
		storeBlockLeaf(
			CachedTextLeafSlot::Placeholder,
			PlainTextLeafSourceSignature(
				prepared.editPlaceholderText,
				st.body,
				PlainTextMinResizeWidth(st.body)),
			&block->placeholderLeaf);
		const auto rowCount = std::min(
			int(prepared.tableRows.size()),
			int(block->tableRows.size()));
		for (auto rowIndex = 0; rowIndex != rowCount; ++rowIndex) {
			const auto cellCount = std::min(
				int(prepared.tableRows[rowIndex].cells.size()),
				int(block->tableRows[rowIndex].cells.size()));
			for (auto cellIndex = 0; cellIndex != cellCount; ++cellIndex) {
				const auto &preparedCell
					= prepared.tableRows[rowIndex].cells[cellIndex];
				auto &cell = block->tableRows[rowIndex].cells[cellIndex];
				const auto &textStyle = preparedCell.header
					? st.table.headerStyle
					: st.table.bodyStyle;
				const auto minResizeWidth = TableCellTextMinResizeWidth(
					textStyle,
					st);
				storeTableCellLeaf(
					CachedTextLeafSlot::TableCellText,
					preparedCell,
					rowIndex,
					cellIndex,
					MarkedTextLeafSourceSignature(
						preparedCell.text,
						textStyle,
						minResizeWidth),
					&cell.leaf);
				storeTableCellLeaf(
					CachedTextLeafSlot::TableCellPlaceholder,
					preparedCell,
					rowIndex,
					cellIndex,
					PlainTextLeafSourceSignature(
						preparedCell.editPlaceholderText,
						textStyle,
						minResizeWidth),
					&cell.placeholderLeaf);
			}
		}
	} break;
	case PreparedBlockKind::Details:
		storeBlockLeaf(
			CachedTextLeafSlot::Leaf,
			MarkedTextLeafSourceSignature(
				prepared.text,
				st.details.summaryStyle,
				FlowTextMinResizeWidth(st.details.summaryStyle)),
			&block->leaf);
		storeBlockLeaf(
			CachedTextLeafSlot::Placeholder,
			PlainTextLeafSourceSignature(
				prepared.editPlaceholderText,
				st.details.summaryStyle,
				PlainTextMinResizeWidth(st.details.summaryStyle)),
			&block->placeholderLeaf);
		storeBlockLeaf(
			CachedTextLeafSlot::Action,
			PlainTextLeafSourceSignature(
				DetailsStateText(prepared.detailsOpen),
				st.details.summaryStyle,
				PlainTextMinResizeWidth(st.details.summaryStyle)),
			&block->actionLeaf);
		break;
	case PreparedBlockKind::Placeholder:
		storeBlockLeaf(
			CachedTextLeafSlot::Label,
			PlainTextLeafSourceSignature(
				prepared.placeholder.label,
				st.placeholder.labelStyle,
				PlainTextMinResizeWidth(st.placeholder.labelStyle)),
			&block->labelLeaf);
		storeBlockLeaf(
			CachedTextLeafSlot::Leaf,
			MarkedTextLeafSourceSignature(
				prepared.text,
				st.body,
				FlowTextMinResizeWidth(st.body)),
			&block->leaf);
		storeBlockLeaf(
			CachedTextLeafSlot::Placeholder,
			PlainTextLeafSourceSignature(
				prepared.editPlaceholderText,
				st.body,
				PlainTextMinResizeWidth(st.body)),
			&block->placeholderLeaf);
		break;
	case PreparedBlockKind::RelatedArticle:
		storeBlockLeaf(
			CachedTextLeafSlot::Label,
			PlainTextLeafSourceSignature(
				prepared.relatedArticle.title,
				st.relatedArticle.titleStyle,
				PlainTextMinResizeWidth(st.relatedArticle.titleStyle)),
			&block->labelLeaf);
		storeBlockLeaf(
			CachedTextLeafSlot::Subtitle,
			PlainTextLeafSourceSignature(
				prepared.relatedArticle.description,
				st.relatedArticle.subtitleStyle,
				PlainTextMinResizeWidth(st.relatedArticle.subtitleStyle)),
			&block->subtitleLeaf);
		storeBlockLeaf(
			CachedTextLeafSlot::Action,
			PlainTextLeafSourceSignature(
				prepared.relatedArticle.footer,
				st.relatedArticle.footerStyle,
				PlainTextMinResizeWidth(st.relatedArticle.footerStyle)),
			&block->actionLeaf);
		break;
	case PreparedBlockKind::EmbedPost:
		storeBlockLeaf(
			CachedTextLeafSlot::Label,
			PlainTextLeafSourceSignature(
				prepared.embedPost.author,
				st.embedPost.authorStyle,
				PlainTextMinResizeWidth(st.embedPost.authorStyle)),
			&block->labelLeaf);
		storeBlockLeaf(
			CachedTextLeafSlot::Subtitle,
			PlainTextLeafSourceSignature(
				prepared.embedPost.dateText,
				st.embedPost.dateStyle,
				PlainTextMinResizeWidth(st.embedPost.dateStyle)),
			&block->subtitleLeaf);
		break;
	case PreparedBlockKind::Photo:
	case PreparedBlockKind::Video:
	case PreparedBlockKind::Audio:
	case PreparedBlockKind::Map:
	case PreparedBlockKind::Channel:
	case PreparedBlockKind::GroupedMedia:
		storeBlockLeaf(
			CachedTextLeafSlot::Leaf,
			MarkedTextLeafSourceSignature(
				prepared.text,
				st.body,
				FlowTextMinResizeWidth(st.body)),
			&block->leaf);
		storeBlockLeaf(
			CachedTextLeafSlot::Placeholder,
			PlainTextLeafSourceSignature(
				prepared.editPlaceholderText,
				st.body,
				PlainTextMinResizeWidth(st.body)),
			&block->placeholderLeaf);
		break;
	case PreparedBlockKind::Rule:
	case PreparedBlockKind::Quote:
	case PreparedBlockKind::List:
	case PreparedBlockKind::ListItem:
		break;
	}

	auto childPath = preparedPath;
	HarvestCachedTextLeafs(prepared.children, &block->children, st, pool, &childPath);
}

void HarvestCachedTextLeafs(
		const std::vector<PreparedBlock> &preparedBlocks,
		std::vector<LaidOutBlock> *blocks,
		const style::Markdown &st,
		CachedTextLeafPool *pool,
		std::vector<int> *preparedPath) {
	if (!blocks || !pool || !preparedPath) {
		return;
	}
	const auto count = std::min(int(preparedBlocks.size()), int(blocks->size()));
	for (auto i = 0; i != count; ++i) {
		preparedPath->push_back(i);
		HarvestCachedTextLeafs(
			preparedBlocks[i],
			&(*blocks)[i],
			st,
			pool,
			*preparedPath);
		preparedPath->pop_back();
	}
}

void StoreRelatedArticleImageState(
		const LaidOutBlock &block,
		std::unordered_map<uint64, RelatedArticleImageState> *states) {
	if (block.thumbnailPhotoId) {
		(*states)[block.thumbnailPhotoId] = {
			.thumbnailImage = block.thumbnailImage,
			.previousThumbnailImage = block.previousThumbnailImage,
			.fullImage = block.fullImage,
			.previousFullImage = block.previousFullImage,
		};
	}
	for (const auto &child : block.children) {
		StoreRelatedArticleImageState(child, states);
	}
}

void StoreRelatedArticleImageStates(
		const std::vector<LaidOutBlock> &blocks,
		std::unordered_map<uint64, RelatedArticleImageState> *states) {
	for (const auto &block : blocks) {
		StoreRelatedArticleImageState(block, states);
	}
}

void RestoreRelatedArticleImageState(
		LaidOutBlock *block,
		const std::unordered_map<uint64, RelatedArticleImageState> &states) {
	if (block->thumbnailPhotoId) {
		if (const auto i = states.find(block->thumbnailPhotoId);
			i != end(states)) {
			block->thumbnailImage = i->second.thumbnailImage;
			block->previousThumbnailImage = i->second.previousThumbnailImage;
			block->fullImage = i->second.fullImage;
			block->previousFullImage = i->second.previousFullImage;
			block->subscribedThumbnailImage.reset();
			block->thumbnailRequestSize = QSize();
			block->subscribedFullImage.reset();
			block->fullRequestSize = QSize();
		}
	}
	for (auto &child : block->children) {
		RestoreRelatedArticleImageState(&child, states);
	}
}

void RestoreRelatedArticleImageStates(
		std::vector<LaidOutBlock> *blocks,
		const std::unordered_map<uint64, RelatedArticleImageState> &states) {
	for (auto &block : *blocks) {
		RestoreRelatedArticleImageState(&block, states);
	}
}

[[nodiscard]] bool IsDisplayMathSegment(const SelectableSegment &segment) {
	return (segment.kind == SelectableSegmentKind::DisplayMath);
}

[[nodiscard]] bool IsEditableSegment(const SelectableSegment &segment) {
	return segment.isTextLeaf() || IsDisplayMathSegment(segment);
}

[[nodiscard]] QColor MarkBgColorForStyle(const style::Markdown &st) {
	auto result = st.textPalette.markBg->c;
	result.setAlphaF(result.alphaF() * std::clamp(
		st.markBgOpacity,
		0.,
		1.));
	return result;
}

template <typename T>
[[nodiscard]] int CompareValues(const T &a, const T &b) {
	return (a < b) ? -1 : (b < a) ? 1 : 0;
}

[[nodiscard]] int ComparePreparedEditBlockContainerStep(
		const PreparedEditBlockContainerStep &a,
		const PreparedEditBlockContainerStep &b) {
	if (const auto result = CompareValues(int(a.kind), int(b.kind));
		result != 0) {
		return result;
	}
	if (const auto result = CompareValues(a.blockIndex, b.blockIndex);
		result != 0) {
		return result;
	}
	return CompareValues(a.listItemIndex, b.listItemIndex);
}

[[nodiscard]] int ComparePreparedEditBlockContainerPath(
		const PreparedEditBlockContainerPath &a,
		const PreparedEditBlockContainerPath &b) {
	const auto count = std::min(a.steps.size(), b.steps.size());
	for (auto i = size_t(0); i != count; ++i) {
		if (const auto result = ComparePreparedEditBlockContainerStep(
				a.steps[i],
				b.steps[i]);
			result != 0) {
			return result;
		}
	}
	return CompareValues(a.steps.size(), b.steps.size());
}

[[nodiscard]] int ComparePreparedEditBlockPath(
		const PreparedEditBlockPath &a,
		const PreparedEditBlockPath &b) {
	if (const auto result = ComparePreparedEditBlockContainerPath(
			a.container,
			b.container);
		result != 0) {
		return result;
	}
	return CompareValues(a.index, b.index);
}

[[nodiscard]] int ComparePreparedEditListItemSource(
		const PreparedEditListItemSource &a,
		const PreparedEditListItemSource &b) {
	if (const auto result = ComparePreparedEditBlockPath(a.block, b.block);
		result != 0) {
		return result;
	}
	return CompareValues(a.listItemIndex, b.listItemIndex);
}

struct PreparedEditListItemSourceLess {
	[[nodiscard]] bool operator()(
			const PreparedEditListItemSource &a,
			const PreparedEditListItemSource &b) const {
		return ComparePreparedEditListItemSource(a, b) < 0;
	}
};

using TaskMarkerRippleRuntimeMap = std::map<
	PreparedEditListItemSource,
	std::shared_ptr<TaskMarkerRippleRuntime>,
	PreparedEditListItemSourceLess>;
using TaskMarkerSourceSet = std::set<
	PreparedEditListItemSource,
	PreparedEditListItemSourceLess>;

void CollectPlaceholderIds(
		const std::vector<LaidOutBlock> &blocks,
		std::unordered_set<uint64> *result) {
	if (!result) {
		return;
	}
	for (const auto &block : blocks) {
		if (block.placeholderId) {
			result->emplace(block.placeholderId.value);
		}
		CollectPlaceholderIds(block.children, result);
	}
}

[[nodiscard]] LaidOutBlock *FindPlaceholderBlock(
		std::vector<LaidOutBlock> *blocks,
		PreparedPlaceholderBlockId id) {
	if (!blocks || !id) {
		return nullptr;
	}
	for (auto &block : *blocks) {
		if (block.placeholderId.value == id.value) {
			return &block;
		}
		if (const auto child = FindPlaceholderBlock(&block.children, id)) {
			return child;
		}
	}
	return nullptr;
}

void CollectTaskMarkerSources(
		const std::vector<LaidOutBlock> &blocks,
		TaskMarkerSourceSet *result) {
	if (!result) {
		return;
	}
	for (const auto &block : blocks) {
		if (block.taskState != TaskState::None && block.editListItem) {
			result->emplace(*block.editListItem);
		}
		CollectTaskMarkerSources(block.children, result);
	}
}

[[nodiscard]] LaidOutBlock *FindListItemBlock(
		std::vector<LaidOutBlock> *blocks,
		const PreparedEditListItemSource &source) {
	if (!blocks) {
		return nullptr;
	}
	for (auto &block : *blocks) {
		if (block.kind == PreparedBlockKind::ListItem
			&& block.editListItem
			&& (*block.editListItem == source)) {
			return &block;
		}
		if (const auto child = FindListItemBlock(&block.children, source)) {
			return child;
		}
	}
	return nullptr;
}

[[nodiscard]] const LaidOutBlock *FindListItemBlock(
		const std::vector<LaidOutBlock> &blocks,
		const PreparedEditListItemSource &source) {
	for (const auto &block : blocks) {
		if (block.kind == PreparedBlockKind::ListItem
			&& block.editListItem
			&& (*block.editListItem == source)) {
			return &block;
		}
		if (const auto child = FindListItemBlock(block.children, source)) {
			return child;
		}
	}
	return nullptr;
}

[[nodiscard]] QRect TaskMarkerRippleRect(
		const LaidOutBlock &block,
		const style::Markdown &st) {
	if (block.markerRect.isEmpty()) {
		return {};
	}
	const auto size = (block.taskMarkerRippleRuntime
			&& !block.taskMarkerRippleRuntime->rippleSize.isEmpty())
		? block.taskMarkerRippleRuntime->rippleSize
		: (block.markerRect.size() + QSize(
			st.list.taskCheck.rippleAreaPadding * 2,
			st.list.taskCheck.rippleAreaPadding * 2));
	return QRect(
		block.markerRect.topLeft() + st::defaultCheckbox.rippleAreaPosition,
		size);
}

[[nodiscard]] const LaidOutBlock *FindPlaceholderBlock(
		const std::vector<LaidOutBlock> &blocks,
		PreparedPlaceholderBlockId id) {
	if (!id) {
		return nullptr;
	}
	for (const auto &block : blocks) {
		if (block.placeholderId.value == id.value) {
			return &block;
		}
		if (const auto child = FindPlaceholderBlock(block.children, id)) {
			return child;
		}
	}
	return nullptr;
}

void AppendRevealLine(
		std::vector<MarkdownArticleRevealLine> *lines,
		int left,
		int width,
		int bottom,
		int baseline,
		bool rtl) {
	if (width < 0) {
		return;
	}
	const auto previousBottom = lines->empty()
		? std::numeric_limits<int>::lowest()
		: lines->back().bottom;
	if (bottom <= previousBottom) {
		return;
	}
	lines->push_back({
		.left = left,
		.width = width,
		.bottom = bottom,
		.rtl = rtl,
		.baseline = baseline,
	});
}

void AppendGenericRevealBand(
		std::vector<MarkdownArticleRevealLine> *lines,
		QRect rect) {
	if (rect.isEmpty()) {
		return;
	}
	const auto bottom = rect.y() + rect.height();
	AppendRevealLine(
		lines,
		rect.x(),
		rect.width(),
		bottom,
		bottom,
		false);
}

[[nodiscard]] QRect ClipRectToViewport(QRect rect, QRect viewport);

void AppendTextRevealLines(
		std::vector<MarkdownArticleRevealLine> *lines,
		const Ui::Text::String &leaf,
		QRect textRect,
		QRect visibleRect,
		int textWidth) {
	if (textRect.isEmpty() || visibleRect.isEmpty() || (textWidth <= 0)) {
		return;
	}
	const auto geometry = leaf.countLinesGeometry(textWidth, true);
	const auto viewportLeft = visibleRect.x();
	const auto viewportRight = visibleRect.x() + visibleRect.width();
	for (const auto &line : geometry) {
		const auto left = textRect.x() + line.left;
		const auto right = left + line.width;
		const auto visibleLeft = std::clamp(
			left,
			viewportLeft,
			viewportRight);
		const auto visibleRight = std::clamp(
			right,
			viewportLeft,
			viewportRight);
		AppendRevealLine(
			lines,
			visibleLeft,
			visibleRight - visibleLeft,
			textRect.y() + line.bottom,
			textRect.y() + line.baseline,
			line.rtl);
	}
}

void AppendBlocksRevealLines(
		const std::vector<LaidOutBlock> &blocks,
		const style::Markdown &st,
		std::vector<MarkdownArticleRevealLine> *lines);

void AppendTableRowRevealLines(
		std::vector<MarkdownArticleRevealLine> *lines,
		const LaidOutBlock &block,
		const style::Markdown &st) {
	if (block.visibleTableRect.isEmpty()) {
		return;
	}
	const auto border = std::max(block.tableBordered ? st.table.border : 0, 0);
	const auto tableBottom = block.visibleTableRect.y()
		+ block.visibleTableRect.height();
	for (const auto &row : block.tableRows) {
		if (row.outer.height() <= 0) {
			continue;
		}
		const auto bottom = std::min(
			row.outer.y() + row.outer.height() + border,
			tableBottom);
		AppendRevealLine(
			lines,
			block.visibleTableRect.x(),
			block.visibleTableRect.width(),
			bottom,
			bottom,
			false);
	}
}

void AppendTableRevealLines(
		std::vector<MarkdownArticleRevealLine> *lines,
		const LaidOutBlock &block,
		const style::Markdown &st) {
	AppendTextRevealLines(
		lines,
		block.leaf,
		block.textRect,
		ClipRectToViewport(block.textRect, block.outer),
		block.textWidth);
	AppendTableRowRevealLines(lines, block, st);
}

void AppendMediaRevealLines(
		std::vector<MarkdownArticleRevealLine> *lines,
		const LaidOutBlock &block) {
	AppendGenericRevealBand(lines, block.visibleMediaRect);
	AppendTextRevealLines(
		lines,
		block.leaf,
		block.textRect,
		ClipRectToViewport(block.textRect, block.outer),
		block.textWidth);
}

void AppendEmbedPostRevealLines(
		std::vector<MarkdownArticleRevealLine> *lines,
		const LaidOutBlock &block,
		const style::Markdown &st) {
	if (!block.headerRect.isEmpty()) {
		const auto bottom = block.headerRect.y() + block.headerRect.height();
		AppendRevealLine(
			lines,
			block.mediaRect.x(),
			block.mediaRect.width(),
			bottom,
			bottom,
			false);
	} else if (block.children.empty()) {
		AppendGenericRevealBand(lines, block.mediaRect);
	}
	AppendBlocksRevealLines(block.children, st, lines);
	AppendTextRevealLines(
		lines,
		block.leaf,
		block.textRect,
		ClipRectToViewport(block.textRect, block.outer),
		block.textWidth);
}

void AppendDetailsRevealLines(
		std::vector<MarkdownArticleRevealLine> *lines,
		const LaidOutBlock &block,
		const style::Markdown &st) {
	AppendTextRevealLines(
		lines,
		block.leaf,
		block.textRect,
		ClipRectToViewport(block.textRect, block.headerRect),
		block.textWidth);
	AppendBlocksRevealLines(block.children, st, lines);
}

void AppendBlockRevealLines(
		std::vector<MarkdownArticleRevealLine> *lines,
		const LaidOutBlock &block,
		const style::Markdown &st) {
	switch (block.kind) {
	case PreparedBlockKind::Paragraph:
	case PreparedBlockKind::Thinking:
	case PreparedBlockKind::Heading:
	case PreparedBlockKind::CodeBlock:
		AppendTextRevealLines(
			lines,
			block.leaf,
			block.textRect,
			(block.kind == PreparedBlockKind::CodeBlock)
				? ClipRectToViewport(block.textRect, block.contentRect)
				: ClipRectToViewport(block.textRect, block.outer),
			block.textWidth);
		break;
	case PreparedBlockKind::Rule:
		AppendGenericRevealBand(lines, block.outer);
		break;
	case PreparedBlockKind::List:
	case PreparedBlockKind::ListItem:
	case PreparedBlockKind::Quote:
		AppendBlocksRevealLines(block.children, st, lines);
		break;
	case PreparedBlockKind::DisplayMath:
		AppendGenericRevealBand(lines, block.visibleFormulaRect);
		break;
	case PreparedBlockKind::Table:
		AppendTableRevealLines(lines, block, st);
		break;
	case PreparedBlockKind::Details:
		AppendDetailsRevealLines(lines, block, st);
		break;
	case PreparedBlockKind::Photo:
	case PreparedBlockKind::Video:
	case PreparedBlockKind::Audio:
	case PreparedBlockKind::Map:
	case PreparedBlockKind::Channel:
	case PreparedBlockKind::GroupedMedia:
	case PreparedBlockKind::Placeholder:
		AppendMediaRevealLines(lines, block);
		break;
	case PreparedBlockKind::RelatedArticle:
		AppendGenericRevealBand(lines, block.visibleMediaRect);
		break;
	case PreparedBlockKind::EmbedPost:
		AppendEmbedPostRevealLines(lines, block, st);
		break;
	}
}

void AppendBlocksRevealLines(
		const std::vector<LaidOutBlock> &blocks,
		const style::Markdown &st,
		std::vector<MarkdownArticleRevealLine> *lines) {
	for (const auto &block : blocks) {
		AppendBlockRevealLines(lines, block, st);
	}
}

[[nodiscard]] std::vector<MarkdownArticleRevealLine> CollectRevealLines(
		const std::vector<LaidOutBlock> &blocks,
		const style::Markdown &st) {
	auto result = std::vector<MarkdownArticleRevealLine>();
	AppendBlocksRevealLines(blocks, st, &result);
	return result;
}

[[nodiscard]] PendingHighlightKey PendingHighlightKeyForBlock(
		const LaidOutBlock &block) {
	return {
		.text = CodeBlockDisplayText(block.copyText),
		.language = block.codeLanguage,
	};
}

[[nodiscard]] Ui::Text::StateResult TextStateAtLeaf(
		const Ui::Text::String &leaf,
		QRect rect,
		int width,
		QPoint point,
		Ui::Text::StateRequest::Flags flags,
		style::align align = style::al_left,
		bool clampToRect = false) {
	if (rect.isEmpty()) {
		return {};
	}
	if (!rect.contains(point)) {
		if (!clampToRect) {
			return {};
		}
		point.setX(std::clamp(point.x(), rect.left(), rect.right()));
		point.setY(std::clamp(point.y(), rect.top(), rect.bottom()));
	}
	auto request = Ui::Text::StateRequest();
	request.align = align;
	request.flags = flags | Ui::Text::StateRequest::Flag::BreakEverywhere;
	const auto availableWidth = std::max(width, 1);
	return leaf.getState(
		point - rect.topLeft(),
		TextGeometry(availableWidth),
		request);
}

[[nodiscard]] SegmentSpan FullSegmentSpan(
		const std::vector<SelectableSegment> &segments) {
	return { 0, int(segments.size()) };
}

void RebuildVisibleSegmentLookup(
		const std::vector<SelectableSegment> &segments,
		std::vector<int> *tops,
		std::vector<int> *bottoms) {
	if (!tops || !bottoms) {
		return;
	}
	tops->clear();
	bottoms->clear();
	tops->reserve(segments.size());
	bottoms->reserve(segments.size());
	auto runningBottom = std::numeric_limits<int>::lowest();
	for (const auto &segment : segments) {
		tops->push_back(segment.outerRect.top());
		runningBottom = std::max(runningBottom, segment.outerRect.bottom());
		bottoms->push_back(runningBottom);
	}
}

[[nodiscard]] SegmentSpan LookupVisibleSegmentSpan(
		const std::vector<int> &tops,
		const std::vector<int> &bottoms,
		LogicalVisibleRange range) {
	if (tops.empty() || bottoms.empty() || (range.bottom <= range.top)) {
		return {};
	}
	const auto from = int(std::lower_bound(
		bottoms.begin(),
		bottoms.end(),
		range.top) - bottoms.begin());
	if (from >= int(tops.size())) {
		return {};
	}
	const auto till = int(std::upper_bound(
		tops.begin() + from,
		tops.end(),
		range.bottom - 1) - tops.begin());
	return (from < till) ? SegmentSpan{ from, till } : SegmentSpan();
}

[[nodiscard]] std::optional<PreparedLink> PreparedLinkForMediaActivation(
		const MediaActivation &activation) {
	if (activation.kind != MediaActivationKind::ExternalUrl
		|| activation.url.isEmpty()) {
		return std::nullopt;
	}
	if (const auto prepared = ClassifiedLink(0, activation.url, nullptr);
		prepared.kind == PreparedLinkKind::External) {
		return prepared;
	}
	return PreparedLink{
		.kind = PreparedLinkKind::External,
		.target = activation.url,
		.copyText = UrlClickHandler::EncodeForOpening(activation.url),
		.entityType = EntityType::Url,
		.shown = EntityLinkShown::Full,
	};
}

[[nodiscard]] std::optional<PreparedLink> PreparedLinkForDetailsBlock(
		const SelectableSegment &segment) {
	if (!segment.block
		|| segment.block->kind != PreparedBlockKind::Details
		|| segment.block->anchorId.isEmpty()) {
		return std::nullopt;
	}
	return PreparedLink{
		.kind = PreparedLinkKind::ToggleDetails,
		.target = segment.block->anchorId,
	};
}

[[nodiscard]] int ComputeScrollTo(
		int toFrom,
		int toTill,
		int toMin,
		int toMax,
		int current,
		int size) {
	if (toFrom < toMin) {
		toFrom = toMin;
	} else if (toFrom > toMax) {
		toFrom = toMax;
	}
	const auto exact = (toTill < 0);

	const auto curBottom = current + size;
	auto scToFrom = toFrom;
	if (!exact && toFrom >= current) {
		if (toTill < toFrom) {
			toTill = toFrom;
		}
		if (toTill <= curBottom) {
			return current;
		}

		scToFrom = toTill - size;
		if (scToFrom > toFrom) {
			scToFrom = toFrom;
		}
		if (scToFrom == current) {
			return current;
		}
	} else {
		scToFrom = toFrom;
	}
	return scToFrom;
}

[[nodiscard]] QRect LogicalTextRectForSegment(const SelectableSegment &segment) {
	if (segment.cell) {
		return segment.cell->textRect;
	} else if (!segment.block) {
		return segment.textRect;
	} else if (segment.leaf == &segment.block->leaf) {
		return segment.block->textRect;
	} else if (segment.leaf == &segment.block->labelLeaf) {
		return segment.block->labelRect;
	} else if (segment.leaf == &segment.block->subtitleLeaf) {
		return segment.block->subtitleRect;
	} else if (segment.leaf == &segment.block->actionLeaf) {
		return segment.block->actionRect;
	}
	return segment.textRect;
}

[[nodiscard]] MarkdownArticleHitTestResult HitSegmentBoundary(
		const SelectableSegment &segment,
		int offset) {
	auto result = MarkdownArticleHitTestResult();
	result.segmentIndex = segment.index;
	result.forcedOffset = std::clamp(offset, 0, SegmentLength(segment));
	result.state.uponSymbol = true;
	result.state.afterSymbol = (result.forcedOffset > 0);
	return result;
}

[[nodiscard]] MarkdownArticleHitTestResult HitTextSegment(
		const SelectableSegment &segment,
		QPoint point,
		Ui::Text::StateRequest::Flags flags) {
	if (!segment.isTextLeaf() || !segment.outerRect.contains(point)) {
		return {};
	}
	const auto insideText = segment.textRect.contains(point);
	if (!insideText
		&& !(flags & Ui::Text::StateRequest::Flag::LookupSymbol)) {
		return {};
	}
	auto result = MarkdownArticleHitTestResult();
	result.segmentIndex = segment.index;
	const auto logicalRect = LogicalTextRectForSegment(segment);
	result.state = TextStateAtLeaf(
		*segment.leaf,
		logicalRect,
		segment.textWidth,
		point,
		flags,
		segment.align,
		!insideText);
	if (!insideText) {
		result.state.link = nullptr;
	}
	result.preparedLink = ExtractPreparedLink(result.state.link);
	if (!result.preparedLink
		&& (flags & Ui::Text::StateRequest::Flag::LookupLink)) {
		if (const auto prepared = PreparedLinkForDetailsBlock(segment)) {
			result.preparedLink = prepared;
			if (!insideText) {
				result.state.link = CreatePreparedLinkHandler(*prepared);
			}
		}
	}
	result.direct = true;
	return result;
}

[[nodiscard]] MarkdownArticleHitTestResult HitCodeBlockHeader(
		const SelectableSegment &segment,
		QPoint point,
		Ui::Text::StateRequest::Flags flags) {
	if (segment.kind != SelectableSegmentKind::CodeBlock
		|| !segment.block
		|| !(flags & Ui::Text::StateRequest::Flag::LookupSymbol)
		|| !segment.block->headerRect.contains(point)) {
		return {};
	}
	auto result = MarkdownArticleHitTestResult();
	result.segmentIndex = segment.index;
	result.forcedOffset = 0;
	result.state.uponSymbol = true;
	result.direct = true;
	result.codeHeaderCopy = true;
	return result;
}

[[nodiscard]] MarkdownArticleHitTestResult HitBlockSegment(
		const SelectableSegment &segment,
		QPoint point,
		Ui::Text::StateRequest::Flags flags) {
	if (segment.isTextLeaf()
		|| !(flags & Ui::Text::StateRequest::Flag::LookupSymbol)
		|| !segment.outerRect.contains(point)) {
		return {};
	}
	const auto after = (point.y() > segment.outerRect.center().y())
		|| ((point.y() == segment.outerRect.center().y())
			&& (point.x() >= segment.outerRect.center().x()));
	auto result = HitSegmentBoundary(
		segment,
		after ? SegmentLength(segment) : 0);
	const auto applyActivation = [&](const MediaActivation &activation) {
		result.mediaActivation = activation;
		if (const auto prepared = PreparedLinkForMediaActivation(activation)) {
			result.preparedLink = prepared;
			result.state.link = CreatePreparedLinkHandler(*prepared);
		}
	};
	if (segment.block) {
		if (segment.block->kind == PreparedBlockKind::RelatedArticle
			&& segment.block->preparedLink) {
			result.preparedLink = segment.block->preparedLink;
			result.state.link = segment.block->preparedLinkHandler;
			result.mediaActivation = {};
		} else if (segment.block->mediaBlock) {
			if (const auto link = segment.block->mediaBlock->linkAt(point)) {
				result.state.link = link;
				result.preparedLink = std::nullopt;
				result.mediaActivation = {};
			} else {
				applyActivation(segment.block->mediaBlock->activationAt(point));
			}
		} else {
			applyActivation(segment.block->activation);
			if (result.mediaActivation.kind == MediaActivationKind::Embed
				&& segment.block->placeholderRuntime) {
				result.state.link = segment.block->placeholderRuntime->clickHandler;
				result.placeholderLocalPoint = point
					- segment.block->mediaRect.topLeft();
			}
		}
	}
	result.direct = true;
	return result;
}

[[nodiscard]] MarkdownArticleHitTestResult HitSegmentFallback(
		const std::vector<SelectableSegment> &segments,
		SegmentSpan span,
		QPoint point) {
	if (segments.empty() || span.empty()) {
		return {};
	}
	for (auto i = span.from; i != span.till; ++i) {
		const auto &segment = segments[i];
		const auto &rect = segment.outerRect;
		if (point.y() < rect.top()) {
			return HitSegmentBoundary(segment, 0);
		}
		if (point.y() <= rect.bottom()) {
			if (point.x() < rect.left()) {
				return HitSegmentBoundary(segment, 0);
			} else if (point.x() > rect.right()) {
				return HitSegmentBoundary(
					segment,
					SegmentLength(segment));
			}
		}
	}
	return HitSegmentBoundary(
		segments[span.till - 1],
		SegmentLength(segments[span.till - 1]));
}

[[nodiscard]] bool ContainsPoint(QRect rect, QPoint point) {
	return !rect.isEmpty() && rect.contains(point);
}

struct ActiveHorizontalScrollOwnerState {
	QRect viewport;
	int shift = 0;
};

[[nodiscard]] QRect TranslateRect(QRect rect, int shift) {
	return rect.isEmpty() ? rect : rect.translated(shift, 0);
}

[[nodiscard]] QRect ClipRectToViewport(QRect rect, QRect viewport) {
	if (rect.isEmpty() || viewport.isEmpty()) {
		return QRect();
	}
	const auto left = std::max(rect.x(), viewport.x());
	const auto right = std::min(
		rect.x() + rect.width(),
		viewport.x() + viewport.width());
	return QRect(
		left,
		rect.y(),
		std::max(right - left, 0),
		rect.height());
}

[[nodiscard]] QPoint TranslatePoint(QPoint point, int shift) {
	return point + QPoint(shift, 0);
}

[[nodiscard]] QPoint ClampPointToViewport(QPoint point, QRect viewport) {
	if (viewport.isEmpty()) {
		return point;
	}
	const auto right = viewport.x() + std::max(viewport.width() - 1, 0);
	point.setX(std::clamp(point.x(), viewport.x(), right));
	return point;
}

[[nodiscard]] QRect BaseVisibleTableRect(const LaidOutBlock &block) {
	if (!block.scrollViewportRect.isEmpty()) {
		return block.scrollViewportRect;
	} else if (block.logicalGeometry.tableRect.isEmpty()) {
		return QRect();
	}
	return QRect(
		block.logicalGeometry.tableRect.x(),
		block.logicalGeometry.tableRect.y(),
		std::min(
			block.logicalGeometry.tableRect.width(),
			std::max(block.logicalGeometry.outer.width(), 1)),
		block.logicalGeometry.tableRect.height());
}

void RestoreLogicalBlockGeometry(LaidOutBlock *block) {
	block->outer = block->logicalGeometry.outer;
	block->headerRect = block->logicalGeometry.headerRect;
	block->bodyRect = block->logicalGeometry.bodyRect;
	block->iconRect = block->logicalGeometry.iconRect;
	block->textRect = block->logicalGeometry.textRect;
	block->labelRect = block->logicalGeometry.labelRect;
	block->subtitleRect = block->logicalGeometry.subtitleRect;
	block->actionRect = block->logicalGeometry.actionRect;
	block->markerRect = block->logicalGeometry.markerRect;
	block->contentRect = block->logicalGeometry.contentRect;
	block->formulaRect = block->logicalGeometry.formulaRect;
	block->tableRect = block->logicalGeometry.tableRect;
	block->mediaRect = block->logicalGeometry.mediaRect;
	block->thumbnailRect = block->logicalGeometry.thumbnailRect;
	block->markerCenter = block->logicalGeometry.markerCenter;
	block->visibleFormulaRect = block->formulaRect.intersected(
		block->contentRect);
	block->visibleTableRect = BaseVisibleTableRect(*block);
	block->tableScrollbarTrackRect = block->scrollScrollbarTrackRect;
	block->tableScrollbarThumbRect = QRect();
	block->scrollScrollbarThumbRect = QRect();
	block->visibleMediaRect = block->mediaRect;
	block->insideHorizontalScroll = false;
	block->horizontalScrollAncestorShift = 0;
	for (auto &row : block->tableRows) {
		row.outer = row.logicalOuter;
		for (auto &cell : row.cells) {
			cell.outer = cell.logicalOuter;
			cell.textRect = cell.logicalTextRect;
		}
	}
}

[[nodiscard]] bool ScrollOwnerMovesOwnContent(PreparedBlockKind kind) {
	switch (kind) {
	case PreparedBlockKind::Paragraph:
	case PreparedBlockKind::Thinking:
	case PreparedBlockKind::Heading:
	case PreparedBlockKind::CodeBlock:
	case PreparedBlockKind::DisplayMath:
	case PreparedBlockKind::Table:
		return true;
	case PreparedBlockKind::Rule:
	case PreparedBlockKind::List:
	case PreparedBlockKind::ListItem:
	case PreparedBlockKind::Quote:
	case PreparedBlockKind::Photo:
	case PreparedBlockKind::Video:
	case PreparedBlockKind::Audio:
	case PreparedBlockKind::Map:
	case PreparedBlockKind::Channel:
	case PreparedBlockKind::GroupedMedia:
	case PreparedBlockKind::RelatedArticle:
	case PreparedBlockKind::EmbedPost:
	case PreparedBlockKind::Placeholder:
	case PreparedBlockKind::Details:
		return false;
	}
	return false;
}

[[nodiscard]] bool ScrollOwnerMovesChildren(PreparedBlockKind kind) {
	switch (kind) {
	case PreparedBlockKind::List:
	case PreparedBlockKind::ListItem:
	case PreparedBlockKind::Quote:
	case PreparedBlockKind::EmbedPost:
	case PreparedBlockKind::Details:
		return true;
	case PreparedBlockKind::Paragraph:
	case PreparedBlockKind::Thinking:
	case PreparedBlockKind::Heading:
	case PreparedBlockKind::CodeBlock:
	case PreparedBlockKind::Rule:
	case PreparedBlockKind::DisplayMath:
	case PreparedBlockKind::Table:
	case PreparedBlockKind::Photo:
	case PreparedBlockKind::Video:
	case PreparedBlockKind::Audio:
	case PreparedBlockKind::Map:
	case PreparedBlockKind::Channel:
	case PreparedBlockKind::GroupedMedia:
	case PreparedBlockKind::RelatedArticle:
	case PreparedBlockKind::Placeholder:
		return false;
	}
	return false;
}

void ApplyTranslatedDescendantGeometry(
		LaidOutBlock *block,
		const ActiveHorizontalScrollOwnerState &state) {
	block->insideHorizontalScroll = true;
	block->horizontalScrollAncestorShift = state.shift;
	block->outer = ClipRectToViewport(
		TranslateRect(block->outer, state.shift),
		state.viewport);
	block->headerRect = ClipRectToViewport(
		TranslateRect(block->headerRect, state.shift),
		state.viewport);
	block->bodyRect = ClipRectToViewport(
		TranslateRect(block->bodyRect, state.shift),
		state.viewport);
	block->iconRect = ClipRectToViewport(
		TranslateRect(block->iconRect, state.shift),
		state.viewport);
	block->contentRect = ClipRectToViewport(
		TranslateRect(block->contentRect, state.shift),
		state.viewport);
	block->textRect = TranslateRect(block->textRect, state.shift);
	block->labelRect = TranslateRect(block->labelRect, state.shift);
	block->subtitleRect = TranslateRect(block->subtitleRect, state.shift);
	block->actionRect = TranslateRect(block->actionRect, state.shift);
	block->markerRect = ClipRectToViewport(
		TranslateRect(block->markerRect, state.shift),
		state.viewport);
	block->formulaRect = TranslateRect(block->formulaRect, state.shift);
	block->tableRect = TranslateRect(block->tableRect, state.shift);
	block->mediaRect = TranslateRect(block->mediaRect, state.shift);
	block->thumbnailRect = TranslateRect(block->thumbnailRect, state.shift);
	block->markerCenter = ClampPointToViewport(
		TranslatePoint(block->markerCenter, state.shift),
		state.viewport);
	block->visibleFormulaRect = ClipRectToViewport(
		block->formulaRect,
		state.viewport);
	block->visibleTableRect = ClipRectToViewport(
		TranslateRect(BaseVisibleTableRect(*block), state.shift),
		state.viewport);
	block->visibleMediaRect = ClipRectToViewport(
		block->mediaRect,
		state.viewport);
	for (auto &row : block->tableRows) {
		row.outer = ClipRectToViewport(
			TranslateRect(row.logicalOuter, state.shift),
			state.viewport);
		for (auto &cell : row.cells) {
			cell.outer = ClipRectToViewport(
				TranslateRect(cell.logicalOuter, state.shift),
				state.viewport);
			cell.textRect = TranslateRect(cell.logicalTextRect, state.shift);
		}
	}
}

void ApplyOwnerContentGeometry(
		LaidOutBlock *block,
		const ActiveHorizontalScrollOwnerState &state) {
	switch (block->kind) {
	case PreparedBlockKind::Paragraph:
	case PreparedBlockKind::Thinking:
	case PreparedBlockKind::Heading:
	case PreparedBlockKind::CodeBlock:
		block->textRect = TranslateRect(block->logicalGeometry.textRect, state.shift);
		break;
	case PreparedBlockKind::DisplayMath:
		block->formulaRect = TranslateRect(
			block->logicalGeometry.formulaRect,
			state.shift);
		block->textRect = TranslateRect(block->logicalGeometry.textRect, state.shift);
		block->visibleFormulaRect = ClipRectToViewport(
			block->formulaRect,
			state.viewport);
		break;
	case PreparedBlockKind::Table:
		block->visibleTableRect = block->scrollViewportRect;
		block->tableScrollbarTrackRect = block->scrollScrollbarTrackRect;
		for (auto &row : block->tableRows) {
			row.outer = ClipRectToViewport(
				TranslateRect(row.logicalOuter, state.shift),
				block->visibleTableRect);
			for (auto &cell : row.cells) {
				cell.outer = ClipRectToViewport(
					TranslateRect(cell.logicalOuter, state.shift),
					block->visibleTableRect);
				cell.textRect = TranslateRect(
					cell.logicalTextRect,
					state.shift);
			}
		}
		break;
	case PreparedBlockKind::Rule:
	case PreparedBlockKind::List:
	case PreparedBlockKind::ListItem:
	case PreparedBlockKind::Quote:
	case PreparedBlockKind::Photo:
	case PreparedBlockKind::Video:
	case PreparedBlockKind::Audio:
	case PreparedBlockKind::Map:
	case PreparedBlockKind::Channel:
	case PreparedBlockKind::GroupedMedia:
	case PreparedBlockKind::RelatedArticle:
	case PreparedBlockKind::EmbedPost:
	case PreparedBlockKind::Placeholder:
	case PreparedBlockKind::Details:
		break;
	}
}

[[nodiscard]] bool ValidBlockPath(const PreparedEditBlockPath &path) {
	return (path.index >= 0);
}

[[nodiscard]] PreparedEditBlockSource EditBlockSourceFromPath(
		PreparedEditBlockPath path) {
	return { .path = std::move(path) };
}

[[nodiscard]] PreparedEditTableRowSource EditTableRowSourceFromCell(
		const PreparedEditTableCellSource &source) {
	return {
		.block = source.block,
		.tableRowIndex = source.tableRowIndex,
	};
}

[[nodiscard]] PreparedEditHit WithLeaf(
		PreparedEditHit hit,
		const std::optional<PreparedEditLeafSource> &leaf) {
	if (leaf) {
		hit.leaf = *leaf;
	}
	return hit;
}

[[nodiscard]] PreparedEditHit EditHitFromBlockSource(
		const PreparedEditBlockSource &source,
		const std::optional<PreparedEditLeafSource> &leaf = std::nullopt) {
	if (!ValidBlockPath(source.path)) {
		return {};
	}
	auto result = PreparedEditHit();
	result.kind = PreparedEditHitKind::Block;
	result.block = source;
	return WithLeaf(std::move(result), leaf);
}

[[nodiscard]] PreparedEditHit EditHitFromListItemSource(
		const PreparedEditListItemSource &source,
		const std::optional<PreparedEditLeafSource> &leaf = std::nullopt) {
	if (!ValidBlockPath(source.block) || source.listItemIndex < 0) {
		return {};
	}
	auto result = PreparedEditHit();
	result.kind = PreparedEditHitKind::ListItem;
	result.block = EditBlockSourceFromPath(source.block);
	result.listItem = source;
	return WithLeaf(std::move(result), leaf);
}

[[nodiscard]] PreparedEditHit EditHitFromTableRowSource(
		const PreparedEditTableRowSource &source,
		const std::optional<PreparedEditLeafSource> &leaf = std::nullopt) {
	if (!ValidBlockPath(source.block) || source.tableRowIndex < 0) {
		return {};
	}
	auto result = PreparedEditHit();
	result.kind = PreparedEditHitKind::TableRow;
	result.block = EditBlockSourceFromPath(source.block);
	result.tableRow = source;
	return WithLeaf(std::move(result), leaf);
}

[[nodiscard]] PreparedEditHit EditHitFromTableCellSource(
		const PreparedEditTableCellSource &source,
		const std::optional<PreparedEditLeafSource> &leaf = std::nullopt) {
	if (!ValidBlockPath(source.block)
		|| source.tableRowIndex < 0
		|| source.tableCellIndex < 0) {
		return {};
	}
	auto result = PreparedEditHit();
	result.kind = PreparedEditHitKind::TableCell;
	result.block = EditBlockSourceFromPath(source.block);
	result.tableRow = EditTableRowSourceFromCell(source);
	result.tableCell = source;
	return WithLeaf(std::move(result), leaf);
}

[[nodiscard]] PreparedEditHit EditHitFromLeafSource(
		const PreparedEditLeafSource &source,
		bool preferLeaf) {
	if (!ValidBlockPath(source.block)) {
		return {};
	}
	switch (source.kind) {
	case PreparedEditLeafKind::ListItemText:
		return EditHitFromListItemSource(
			PreparedEditListItemSource{
				.block = source.block,
				.listItemIndex = source.listItemIndex,
			},
			source);
	case PreparedEditLeafKind::TableCellText:
		return EditHitFromTableCellSource(
			PreparedEditTableCellSource{
				.block = source.block,
				.tableRowIndex = source.tableRowIndex,
				.tableCellIndex = source.tableCellIndex,
			},
			source);
	case PreparedEditLeafKind::BlockText:
	case PreparedEditLeafKind::BlockCaption:
	case PreparedEditLeafKind::MathFormula: {
		auto result = PreparedEditHit();
		result.kind = preferLeaf
			? PreparedEditHitKind::Leaf
			: PreparedEditHitKind::Block;
		result.block = EditBlockSourceFromPath(source.block);
		result.leaf = source;
		return result;
	} break;
	}
	return {};
}

[[nodiscard]] PreparedEditHit EditFallbackHitForBlock(
		const LaidOutBlock &block);

[[nodiscard]] PreparedEditHit EditHitForBlock(
		const LaidOutBlock &block,
		QPoint point);

[[nodiscard]] PreparedEditBlockContainerPath ListItemChildContainer(
		const PreparedEditListItemSource &source) {
	auto result = source.block.container;
	result.steps.push_back({
		.kind = PreparedEditBlockContainerKind::ListItemChildren,
		.blockIndex = source.block.index,
		.listItemIndex = source.listItemIndex,
	});
	return result;
}

[[nodiscard]] bool HitHasRealBlockInContainer(
		const PreparedEditHit &hit,
		const PreparedEditBlockContainerPath &container) {
	return hit.block
		&& (hit.block->path.container.steps.size() >= container.steps.size())
		&& std::equal(
			container.steps.begin(),
			container.steps.end(),
			hit.block->path.container.steps.begin());
}

[[nodiscard]] PreparedEditHit EditHitForBlocks(
		const std::vector<LaidOutBlock> &blocks,
		QPoint point) {
	auto fallback = PreparedEditHit();
	auto fallbackDistance = std::numeric_limits<int>::max();
	for (const auto &block : blocks) {
		if (ContainsPoint(block.outer, point)) {
			return EditHitForBlock(block, point);
		}
		const auto candidate = EditFallbackHitForBlock(block);
		if (!candidate.valid()) {
			continue;
		}
		const auto distance = (point.y() < block.outer.top())
			? (block.outer.top() - point.y())
			: (point.y() > block.outer.bottom())
			? (point.y() - block.outer.bottom())
			: 0;
		if (distance < fallbackDistance) {
			fallback = candidate;
			fallbackDistance = distance;
		}
	}
	return fallback;
}

void CollectMediaBlockGeometries(
		std::vector<MarkdownArticleMediaGeometry> *out,
		const std::vector<LaidOutBlock> &blocks) {
	for (const auto &block : blocks) {
		const auto media = (block.kind == PreparedBlockKind::Photo)
			|| (block.kind == PreparedBlockKind::Video)
			|| (block.kind == PreparedBlockKind::Audio)
			|| (block.kind == PreparedBlockKind::Map)
			|| (block.kind == PreparedBlockKind::GroupedMedia);
		if (media && block.editBlock) {
			const auto grouped
				= (block.kind == PreparedBlockKind::GroupedMedia);
			auto itemRects = std::vector<QRect>();
			auto activeItemIndex = -1;
			if (grouped && block.mediaBlock) {
				itemRects = block.mediaBlock->itemRects();
				activeItemIndex = block.mediaBlock->activeItemIndex();
			}
			out->push_back({
				.block = *block.editBlock,
				.mediaRect = block.mediaRect,
				.visibleMediaRect = block.visibleMediaRect,
				.grouped = grouped,
				.itemRects = std::move(itemRects),
				.activeItemIndex = activeItemIndex,
			});
		}
		if (!block.children.empty()) {
			CollectMediaBlockGeometries(out, block.children);
		}
	}
}

[[nodiscard]] PreparedEditHit EditFallbackHitForBlock(
		const LaidOutBlock &block) {
	if (block.editListItem) {
		return EditHitFromListItemSource(*block.editListItem);
	} else if (block.editBlock) {
		return EditHitFromBlockSource(*block.editBlock);
	} else if (block.editLeaf) {
		return EditHitFromLeafSource(*block.editLeaf, false);
	}
	return {};
}

[[nodiscard]] MarkdownArticleEditControlHit EditControlHitForBlocks(
		const std::vector<LaidOutBlock> &blocks,
		QPoint point);

[[nodiscard]] QRect VisibleTextRectForBlock(const LaidOutBlock &block) {
	switch (block.kind) {
	case PreparedBlockKind::CodeBlock:
		return ClipRectToViewport(block.textRect, block.contentRect);
	case PreparedBlockKind::Details:
		return ClipRectToViewport(block.textRect, block.headerRect);
	case PreparedBlockKind::Paragraph:
	case PreparedBlockKind::Thinking:
	case PreparedBlockKind::Heading:
	case PreparedBlockKind::Rule:
	case PreparedBlockKind::DisplayMath:
	case PreparedBlockKind::Table:
	case PreparedBlockKind::Photo:
	case PreparedBlockKind::Video:
	case PreparedBlockKind::Audio:
	case PreparedBlockKind::Map:
	case PreparedBlockKind::Channel:
	case PreparedBlockKind::GroupedMedia:
	case PreparedBlockKind::RelatedArticle:
	case PreparedBlockKind::EmbedPost:
	case PreparedBlockKind::Placeholder:
	case PreparedBlockKind::List:
	case PreparedBlockKind::ListItem:
	case PreparedBlockKind::Quote:
		return ClipRectToViewport(block.textRect, block.outer);
	}
	return ClipRectToViewport(block.textRect, block.outer);
}

[[nodiscard]] PreparedEditHit EditHitForTableCell(
		const LaidOutTableCell &cell,
		QPoint point) {
	auto leaf = ContainsPoint(ClipRectToViewport(cell.textRect, cell.outer), point)
		? cell.editLeaf
		: std::optional<PreparedEditLeafSource>();
	if (cell.editCell) {
		return EditHitFromTableCellSource(*cell.editCell, leaf);
	} else if (leaf) {
		return EditHitFromLeafSource(*leaf, false);
	}
	return {};
}

[[nodiscard]] PreparedEditHit EditHitForTableBlock(
		const LaidOutBlock &block,
		QPoint point) {
	if (ContainsPoint(VisibleTextRectForBlock(block), point) && block.editLeaf) {
		if (block.editBlock) {
			return EditHitFromBlockSource(*block.editBlock, block.editLeaf);
		}
		return EditHitFromLeafSource(*block.editLeaf, false);
	}
	for (const auto &row : block.tableRows) {
		for (const auto &cell : row.cells) {
			if (ContainsPoint(cell.outer, point)) {
				if (const auto result = EditHitForTableCell(cell, point);
					result.valid()) {
					return result;
				}
			}
		}
	}
	for (const auto &row : block.tableRows) {
		if (ContainsPoint(row.outer, point)) {
			if (row.editRow) {
				return EditHitFromTableRowSource(*row.editRow);
			}
		}
	}
	return EditFallbackHitForBlock(block);
}

[[nodiscard]] PreparedEditHit EditHitForListBlock(
		const LaidOutBlock &block,
		QPoint point) {
	if (!block.children.empty()) {
		if (const auto result = EditHitForBlocks(block.children, point);
			result.valid()) {
			return result;
		}
	}
	return EditFallbackHitForBlock(block);
}

[[nodiscard]] PreparedEditHit EditHitForListItemBlock(
		const LaidOutBlock &block,
		QPoint point) {
	const auto listItemHit = EditFallbackHitForBlock(block);
	if (ContainsPoint(block.contentRect, point) && !block.children.empty()) {
		if (const auto childHit = EditHitForBlocks(block.children, point);
			childHit.valid()) {
			if (block.editListItem
				&& HitHasRealBlockInContainer(
					childHit,
					ListItemChildContainer(*block.editListItem))) {
				return childHit;
			}
			return childHit.leaf
				? WithLeaf(listItemHit, childHit.leaf)
				: listItemHit;
		}
	}
	return listItemHit;
}

[[nodiscard]] PreparedEditHit EditHitForQuoteBlock(
		const LaidOutBlock &block,
		QPoint point) {
	if (ContainsPoint(block.contentRect, point) && !block.children.empty()) {
		if (const auto childHit = EditHitForBlocks(block.children, point);
			childHit.valid()) {
			return childHit;
		}
	}
	return EditFallbackHitForBlock(block);
}

[[nodiscard]] PreparedEditHit EditHitForDetailsBlock(
		const LaidOutBlock &block,
		QPoint point) {
	if (ContainsPoint(block.headerRect, point)) {
		if (ContainsPoint(VisibleTextRectForBlock(block), point) && block.editLeaf) {
			if (block.editBlock) {
				return EditHitFromBlockSource(*block.editBlock, block.editLeaf);
			}
			return EditHitFromLeafSource(*block.editLeaf, false);
		}
		return EditFallbackHitForBlock(block);
	}
	if (ContainsPoint(block.contentRect, point) && !block.children.empty()) {
		if (const auto childHit = EditHitForBlocks(block.children, point);
			childHit.valid()) {
			return childHit;
		}
	}
	return EditFallbackHitForBlock(block);
}

[[nodiscard]] MarkdownArticleEditControlHit EditControlHitForListItemBlock(
		const LaidOutBlock &block,
		QPoint point) {
	if (block.taskState != TaskState::None
		&& block.editListItem
		&& ContainsPoint(block.markerRect, point)) {
		return {
			.kind = MarkdownArticleEditControlHitKind::TaskMarker,
			.listItem = *block.editListItem,
		};
	}
	if (!block.children.empty()) {
		return EditControlHitForBlocks(block.children, point);
	}
	return {};
}

[[nodiscard]] MarkdownArticleEditControlHit EditControlHitForDetailsBlock(
		const LaidOutBlock &block,
		QPoint point) {
	if (ContainsPoint(block.headerRect, point) && block.editBlock) {
		const auto leftWidth = std::max(
			block.textRect.left() - block.headerRect.left(),
			0);
		const auto leftToggleRect = QRect(
			block.headerRect.left(),
			block.headerRect.top(),
			leftWidth,
			block.headerRect.height());
		if (ContainsPoint(leftToggleRect, point)
			|| (!block.actionRect.isEmpty()
				&& ContainsPoint(block.actionRect, point))) {
			return {
				.kind = MarkdownArticleEditControlHitKind::DetailsToggle,
				.block = *block.editBlock,
			};
		}
		return {};
	}
	if (!block.children.empty()) {
		return EditControlHitForBlocks(block.children, point);
	}
	return {};
}

[[nodiscard]] MarkdownArticleEditControlHit EditControlHitForBlock(
		const LaidOutBlock &block,
		QPoint point) {
	switch (block.kind) {
	case PreparedBlockKind::ListItem:
		return EditControlHitForListItemBlock(block, point);
	case PreparedBlockKind::Details:
		return EditControlHitForDetailsBlock(block, point);
	default:
		if (!block.children.empty()) {
			return EditControlHitForBlocks(block.children, point);
		}
		return {};
	}
}

[[nodiscard]] MarkdownArticleEditControlHit EditControlHitForBlocks(
		const std::vector<LaidOutBlock> &blocks,
		QPoint point) {
	for (const auto &block : blocks) {
		if (ContainsPoint(block.outer, point)) {
			return EditControlHitForBlock(block, point);
		}
	}
	return {};
}

[[nodiscard]] PreparedEditHit EditHitForBlock(
		const LaidOutBlock &block,
		QPoint point) {
	switch (block.kind) {
	case PreparedBlockKind::List:
		return EditHitForListBlock(block, point);
	case PreparedBlockKind::ListItem:
		return EditHitForListItemBlock(block, point);
	case PreparedBlockKind::Quote:
		return EditHitForQuoteBlock(block, point);
	case PreparedBlockKind::Table:
		return EditHitForTableBlock(block, point);
	case PreparedBlockKind::Details:
		return EditHitForDetailsBlock(block, point);
	case PreparedBlockKind::DisplayMath:
		if (block.editLeaf) {
			return EditHitFromLeafSource(*block.editLeaf, true);
		}
		break;
	case PreparedBlockKind::Paragraph:
	case PreparedBlockKind::Thinking:
	case PreparedBlockKind::Heading:
	case PreparedBlockKind::CodeBlock:
	case PreparedBlockKind::Rule:
	case PreparedBlockKind::Photo:
	case PreparedBlockKind::Video:
	case PreparedBlockKind::Audio:
	case PreparedBlockKind::Map:
	case PreparedBlockKind::Channel:
	case PreparedBlockKind::GroupedMedia:
	case PreparedBlockKind::RelatedArticle:
	case PreparedBlockKind::EmbedPost:
	case PreparedBlockKind::Placeholder:
		if (ContainsPoint(VisibleTextRectForBlock(block), point)
			&& block.editLeaf) {
			return EditHitFromLeafSource(*block.editLeaf, true);
		}
		break;
	}
	return EditFallbackHitForBlock(block);
}

[[nodiscard]] std::optional<PreparedEditLeafSource> EditableLeafForSegment(
		const SelectableSegment &segment) {
	if (segment.cell) {
		return segment.cell->editLeaf;
	} else if (IsDisplayMathSegment(segment) && segment.block) {
		return segment.block->editLeaf;
	} else if (segment.block && (segment.leaf == &segment.block->leaf)) {
		return segment.block->editLeaf;
	}
	return std::nullopt;
}

[[nodiscard]] PreparedEditBlockContainerPath BlockChildContainer(
		const PreparedEditBlockSource &source) {
	auto result = source.path.container;
	result.steps.push_back({
		.kind = PreparedEditBlockContainerKind::BlockChildren,
		.blockIndex = source.path.index,
	});
	return result;
}

[[nodiscard]] std::optional<PreparedEditBlockContainerPath> ContainerPathForChildBlock(
		const LaidOutBlock &block) {
	if (block.editBlock && ValidBlockPath(block.editBlock->path)) {
		return block.editBlock->path.container;
	} else if (block.editListItem && ValidBlockPath(block.editListItem->block)) {
		return block.editListItem->block.container;
	} else if (block.editLeaf && ValidBlockPath(block.editLeaf->block)) {
		return block.editLeaf->block.container;
	}
	return std::nullopt;
}

[[nodiscard]] std::optional<PreparedEditBlockContainerPath> ContainerPathForBlocks(
		const std::vector<LaidOutBlock> &blocks) {
	for (const auto &block : blocks) {
		if (const auto result = ContainerPathForChildBlock(block)) {
			return result;
		}
	}
	return std::nullopt;
}

[[nodiscard]] std::optional<PreparedEditBlockContainerPath> BlockChildrenContainerPath(
		const LaidOutBlock &block) {
	if (block.editBlock && ValidBlockPath(block.editBlock->path)) {
		return BlockChildContainer(*block.editBlock);
	}
	return ContainerPathForBlocks(block.children);
}

[[nodiscard]] std::optional<PreparedEditBlockContainerPath> ListItemChildrenContainerPath(
		const LaidOutBlock &block) {
	if (block.editListItem && ValidBlockPath(block.editListItem->block)) {
		return ListItemChildContainer(*block.editListItem);
	}
	return ContainerPathForBlocks(block.children);
}

[[nodiscard]] std::optional<PreparedEditBlockPath> ListBlockPath(
		const LaidOutBlock &block) {
	if (block.editBlock && ValidBlockPath(block.editBlock->path)) {
		return block.editBlock->path;
	}
	for (const auto &child : block.children) {
		if (child.editListItem && ValidBlockPath(child.editListItem->block)) {
			return child.editListItem->block;
		}
	}
	return std::nullopt;
}

[[nodiscard]] int DistanceToRect(QPoint point, QRect rect) {
	if (rect.isEmpty()) {
		return std::numeric_limits<int>::max();
	}
	auto dx = 0;
	if (point.x() < rect.left()) {
		dx = rect.left() - point.x();
	} else if (point.x() > rect.right()) {
		dx = point.x() - rect.right();
	}
	auto dy = 0;
	if (point.y() < rect.top()) {
		dy = rect.top() - point.y();
	} else if (point.y() > rect.bottom()) {
		dy = point.y() - rect.bottom();
	}
	return dx + dy;
}

[[nodiscard]] int GapIndicatorY(
		QRect before,
		QRect after,
		QRect containerRect) {
	auto result = 0;
	if (!before.isEmpty() && !after.isEmpty()) {
		const auto top = before.y() + before.height();
		const auto bottom = after.y();
		result = (top <= bottom) ? ((top + bottom) / 2) : top;
	} else if (!before.isEmpty()) {
		result = before.y() + before.height();
	} else if (!after.isEmpty()) {
		result = after.y();
	} else if (!containerRect.isEmpty()) {
		result = containerRect.y();
	}
	if (!containerRect.isEmpty()) {
		result = std::clamp(
			result,
			containerRect.y(),
			containerRect.y() + containerRect.height());
	}
	return result;
}

[[nodiscard]] QRect GapIndicatorRect(
		QRect before,
		QRect after,
		QRect containerRect) {
	auto span = QRect();
	if (!before.isEmpty()) {
		span = before;
	}
	if (!after.isEmpty()) {
		span = span.isEmpty() ? after : span.united(after);
	}
	if (span.isEmpty()) {
		span = containerRect;
	}
	if (span.isEmpty()) {
		return QRect();
	}
	auto left = span.x();
	auto right = span.x() + span.width();
	if (!containerRect.isEmpty()) {
		const auto containerLeft = containerRect.x();
		const auto containerRight = containerRect.x() + containerRect.width();
		left = std::clamp(left, containerLeft, containerRight);
		right = std::clamp(right, containerLeft, containerRight);
		if (right <= left) {
			left = containerLeft;
			right = containerRight;
		}
	}
	if (right <= left) {
		return QRect();
	}
	return QRect(
		left,
		GapIndicatorY(before, after, containerRect),
		right - left,
		1);
}

[[nodiscard]] QRect ListItemGapSpanRect(const LaidOutBlock &block) {
	return block.contentRect.isEmpty() ? block.outer : block.contentRect;
}

[[nodiscard]] MarkdownArticleDropLocation EditDropLocationForBlock(
		const LaidOutBlock &block,
		QPoint point);

struct DropGapCandidate {
	MarkdownArticleDropLocation location;
	int distance = std::numeric_limits<int>::max();
};

[[nodiscard]] bool PreparedEditContainerHasPrefix(
		const PreparedEditBlockContainerPath &path,
		const PreparedEditBlockContainerPath &prefix) {
	if (path.steps.size() < prefix.steps.size()) {
		return false;
	}
	return std::equal(
		prefix.steps.begin(),
		prefix.steps.end(),
		path.steps.begin());
}

[[nodiscard]] bool PreparedEditIndexInRange(int index, int from, int till) {
	return (index >= from) && (index < till);
}

[[nodiscard]] bool PreparedEditPathInBlockRange(
		const PreparedEditBlockPath &path,
		const PreparedEditBlockRange &range) {
	if (path.container == range.container) {
		return PreparedEditIndexInRange(path.index, range.from, range.till);
	}
	if (!PreparedEditContainerHasPrefix(path.container, range.container)
		|| (path.container.steps.size() <= range.container.steps.size())) {
		return false;
	}
	const auto &step = path.container.steps[range.container.steps.size()];
	return PreparedEditIndexInRange(step.blockIndex, range.from, range.till);
}

[[nodiscard]] bool PreparedEditPathInListItemRange(
		const PreparedEditBlockPath &path,
		const PreparedEditListItemRange &range) {
	if (!PreparedEditContainerHasPrefix(path.container, range.block.container)
		|| (path.container.steps.size() <= range.block.container.steps.size())) {
		return false;
	}
	const auto &step = path.container.steps[range.block.container.steps.size()];
	return (step.kind == PreparedEditBlockContainerKind::ListItemChildren)
		&& (step.blockIndex == range.block.index)
		&& PreparedEditIndexInRange(step.listItemIndex, range.from, range.till);
}

[[nodiscard]] bool PreparedEditContainerNestedInSelection(
		const PreparedEditBlockContainerPath &container,
		const PreparedEditSelection &selection) {
	const auto marker = PreparedEditBlockPath{
		.container = container,
		.index = 0,
	};
	switch (selection.kind) {
	case PreparedEditSelectionKind::Blocks:
		return (container.steps.size() > selection.blocks.container.steps.size())
			&& PreparedEditPathInBlockRange(marker, selection.blocks);
	case PreparedEditSelectionKind::ListItems:
		return (container.steps.size()
			> selection.listItems.block.container.steps.size())
			&& PreparedEditPathInListItemRange(marker, selection.listItems);
	case PreparedEditSelectionKind::TableRows:
	case PreparedEditSelectionKind::TableCells:
	case PreparedEditSelectionKind::None:
		return false;
	}
	return false;
}

[[nodiscard]] bool PreparedEditBlockPathInSelection(
		const PreparedEditBlockPath &path,
		const PreparedEditSelection &selection) {
	switch (selection.kind) {
	case PreparedEditSelectionKind::Blocks:
		return PreparedEditPathInBlockRange(path, selection.blocks);
	case PreparedEditSelectionKind::ListItems:
		return PreparedEditPathInListItemRange(path, selection.listItems);
	case PreparedEditSelectionKind::TableRows:
	case PreparedEditSelectionKind::TableCells:
	case PreparedEditSelectionKind::None:
		return false;
	}
	return false;
}

[[nodiscard]] bool StructuralDropTargetSupported(
		const PreparedEditBlockDropTarget &target,
		const PreparedEditSelection &selection) {
	if ((selection.kind != PreparedEditSelectionKind::Blocks)
		|| selection.blocks.empty()) {
		return false;
	}
	if ((target.container == selection.blocks.container)
		&& (target.insertIndex >= selection.blocks.from)
		&& (target.insertIndex <= selection.blocks.till)) {
		return false;
	}
	return !PreparedEditContainerNestedInSelection(
		target.container,
		selection);
}

[[nodiscard]] bool StructuralDropTargetSupported(
		const PreparedEditListItemDropTarget &target,
		const PreparedEditSelection &selection) {
	if ((selection.kind != PreparedEditSelectionKind::ListItems)
		|| selection.listItems.empty()) {
		return false;
	}
	if ((target.block == selection.listItems.block)
		&& (target.insertIndex >= selection.listItems.from)
		&& (target.insertIndex <= selection.listItems.till)) {
		return false;
	}
	return !PreparedEditBlockPathInSelection(target.block, selection);
}

void UniteNonEmptyRect(QRect *result, QRect rect) {
	if (!result || rect.isEmpty()) {
		return;
	}
	*result = result->isEmpty() ? rect : result->united(rect);
}

void AddSelectedBlockRects(
		QRect *result,
		const std::vector<LaidOutBlock> &blocks,
		const PreparedEditBlockRange &range) {
	for (const auto &block : blocks) {
		if (block.editBlock
			&& (block.editBlock->path.container == range.container)
			&& PreparedEditIndexInRange(
				block.editBlock->path.index,
				range.from,
				range.till)) {
			UniteNonEmptyRect(result, block.outer);
		}
		AddSelectedBlockRects(result, block.children, range);
	}
}

[[nodiscard]] bool AddSelectedListItemRects(
		QRect *result,
		const std::vector<LaidOutBlock> &blocks,
		const PreparedEditListItemRange &range) {
	for (const auto &block : blocks) {
		if (block.kind == PreparedBlockKind::List) {
			if (const auto listBlock = ListBlockPath(block);
				listBlock && (*listBlock == range.block)) {
				const auto count = int(block.children.size());
				const auto from = std::clamp(range.from, 0, count);
				const auto till = std::clamp(range.till, from, count);
				for (auto i = from; i != till; ++i) {
					UniteNonEmptyRect(
						result,
						ListItemGapSpanRect(block.children[i]));
				}
				return true;
			}
		}
		if (AddSelectedListItemRects(result, block.children, range)) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] QRect StructuralSelectionRect(
		const std::vector<LaidOutBlock> &blocks,
		const PreparedEditSelection &selection) {
	auto result = QRect();
	switch (selection.kind) {
	case PreparedEditSelectionKind::Blocks:
		AddSelectedBlockRects(&result, blocks, selection.blocks);
		return result;
	case PreparedEditSelectionKind::ListItems:
		static_cast<void>(AddSelectedListItemRects(
			&result,
			blocks,
			selection.listItems));
		return result;
	case PreparedEditSelectionKind::TableRows:
	case PreparedEditSelectionKind::TableCells:
	case PreparedEditSelectionKind::None:
		return {};
	}
	return {};
}

[[nodiscard]] bool PointInsideSelectionVerticalSpan(
		QPoint point,
		const std::vector<LaidOutBlock> &blocks,
		const PreparedEditSelection &selection) {
	const auto rect = StructuralSelectionRect(blocks, selection);
	return !rect.isEmpty()
		&& (point.y() >= rect.top())
		&& (point.y() <= rect.bottom());
}

template <typename Target>
void ConsiderGapCandidate(
		DropGapCandidate *best,
		Target target,
		QRect before,
		QRect after,
		QRect containerRect,
		QPoint point,
		const PreparedEditSelection *selection = nullptr) {
	if (!best) {
		return;
	}
	if (selection && !StructuralDropTargetSupported(target, *selection)) {
		return;
	}
	const auto indicatorRect = GapIndicatorRect(before, after, containerRect);
	if (indicatorRect.isEmpty()) {
		return;
	}
	const auto distance = DistanceToRect(point, indicatorRect);
	if (distance >= best->distance) {
		return;
	}
	best->distance = distance;
	best->location.target = PreparedEditDropTarget(std::move(target));
	best->location.indicatorRect = indicatorRect;
}

void ConsiderBlockContainerGapCandidates(
		DropGapCandidate *best,
		const std::vector<LaidOutBlock> &blocks,
		QPoint point,
		std::optional<PreparedEditBlockContainerPath> container,
		QRect containerRect,
		const PreparedEditSelection *selection = nullptr) {
	if (!container) {
		return;
	}
	const auto count = int(blocks.size());
	for (auto i = 0; i != count + 1; ++i) {
		ConsiderGapCandidate(
			best,
			PreparedEditBlockDropTarget{
				.container = *container,
				.insertIndex = i,
			},
			(i > 0) ? blocks[i - 1].outer : QRect(),
			(i < count) ? blocks[i].outer : QRect(),
			containerRect,
			point,
			selection);
	}
}

void ConsiderListItemGapCandidates(
		DropGapCandidate *best,
		const std::vector<LaidOutBlock> &blocks,
		QPoint point,
		std::optional<PreparedEditBlockPath> listBlock,
		QRect containerRect,
		const PreparedEditSelection *selection = nullptr) {
	if (!(listBlock && ValidBlockPath(*listBlock))) {
		return;
	}
	const auto count = int(blocks.size());
	for (auto i = 0; i != count + 1; ++i) {
		ConsiderGapCandidate(
			best,
			PreparedEditListItemDropTarget{
				.block = *listBlock,
				.insertIndex = i,
			},
			(i > 0) ? ListItemGapSpanRect(blocks[i - 1]) : QRect(),
			(i < count) ? ListItemGapSpanRect(blocks[i]) : QRect(),
			containerRect,
			point,
			selection);
	}
}

[[nodiscard]] MarkdownArticleDropLocation EditDropLocationForBlockContainer(
		const std::vector<LaidOutBlock> &blocks,
		QPoint point,
		std::optional<PreparedEditBlockContainerPath> container,
		QRect containerRect) {
	for (const auto &block : blocks) {
		if (ContainsPoint(block.outer, point)) {
			if (const auto nested = EditDropLocationForBlock(block, point);
				nested.valid()) {
				return nested;
			}
			break;
		}
	}
	auto best = DropGapCandidate();
	ConsiderBlockContainerGapCandidates(
		&best,
		blocks,
		point,
		std::move(container),
		containerRect);
	return best.location;
}

[[nodiscard]] MarkdownArticleDropLocation EditDropLocationForListItems(
		const std::vector<LaidOutBlock> &blocks,
		QPoint point,
		std::optional<PreparedEditBlockPath> listBlock,
		QRect containerRect) {
	for (const auto &block : blocks) {
		if (ContainsPoint(block.outer, point)) {
			if (const auto nested = EditDropLocationForBlock(block, point);
				nested.valid()) {
				return nested;
			}
			break;
		}
	}
	auto best = DropGapCandidate();
	ConsiderListItemGapCandidates(
		&best,
		blocks,
		point,
		std::move(listBlock),
		containerRect);
	return best.location;
}

void ConsiderStructuralBlockDropTargets(
		DropGapCandidate *best,
		const std::vector<LaidOutBlock> &blocks,
		QPoint point,
		std::optional<PreparedEditBlockContainerPath> container,
		QRect containerRect,
		const PreparedEditSelection &selection) {
	ConsiderBlockContainerGapCandidates(
		best,
		blocks,
		point,
		std::move(container),
		containerRect,
		&selection);
	for (const auto &block : blocks) {
		switch (block.kind) {
		case PreparedBlockKind::List:
			for (const auto &child : block.children) {
				if ((child.kind == PreparedBlockKind::ListItem)
					&& ContainsPoint(child.contentRect, point)) {
					ConsiderStructuralBlockDropTargets(
						best,
						child.children,
						point,
						ListItemChildrenContainerPath(child),
						child.contentRect,
						selection);
				}
			}
			break;
		case PreparedBlockKind::ListItem:
			if (ContainsPoint(block.contentRect, point)) {
				ConsiderStructuralBlockDropTargets(
					best,
					block.children,
					point,
					ListItemChildrenContainerPath(block),
					block.contentRect,
					selection);
			}
			break;
		case PreparedBlockKind::Quote:
		case PreparedBlockKind::Details:
			if (ContainsPoint(block.contentRect, point)) {
				ConsiderStructuralBlockDropTargets(
					best,
					block.children,
					point,
					BlockChildrenContainerPath(block),
					block.contentRect,
					selection);
			}
			break;
		case PreparedBlockKind::Table:
		case PreparedBlockKind::Paragraph:
		case PreparedBlockKind::Thinking:
		case PreparedBlockKind::Heading:
		case PreparedBlockKind::CodeBlock:
		case PreparedBlockKind::Rule:
		case PreparedBlockKind::DisplayMath:
		case PreparedBlockKind::Photo:
		case PreparedBlockKind::Video:
		case PreparedBlockKind::Audio:
		case PreparedBlockKind::Map:
		case PreparedBlockKind::Channel:
		case PreparedBlockKind::GroupedMedia:
		case PreparedBlockKind::RelatedArticle:
		case PreparedBlockKind::EmbedPost:
		case PreparedBlockKind::Placeholder:
			break;
		}
	}
}

void ConsiderStructuralListItemDropTargets(
		DropGapCandidate *best,
		const std::vector<LaidOutBlock> &blocks,
		QPoint point,
		const PreparedEditBlockPath &sourceListBlock,
		const PreparedEditSelection &selection) {
	for (const auto &block : blocks) {
		switch (block.kind) {
		case PreparedBlockKind::List:
			if (const auto listBlock = ListBlockPath(block);
				listBlock && (*listBlock == sourceListBlock)) {
				ConsiderListItemGapCandidates(
					best,
					block.children,
					point,
					sourceListBlock,
					block.contentRect.isEmpty()
						? block.outer
						: block.contentRect,
					&selection);
			}
			for (const auto &child : block.children) {
				if (child.kind == PreparedBlockKind::ListItem) {
					ConsiderStructuralListItemDropTargets(
						best,
						child.children,
						point,
						sourceListBlock,
						selection);
				}
			}
			break;
		case PreparedBlockKind::ListItem:
		case PreparedBlockKind::Quote:
		case PreparedBlockKind::Details:
			ConsiderStructuralListItemDropTargets(
				best,
				block.children,
				point,
				sourceListBlock,
				selection);
			break;
		case PreparedBlockKind::Table:
		case PreparedBlockKind::Paragraph:
		case PreparedBlockKind::Thinking:
		case PreparedBlockKind::Heading:
		case PreparedBlockKind::CodeBlock:
		case PreparedBlockKind::Rule:
		case PreparedBlockKind::DisplayMath:
		case PreparedBlockKind::Photo:
		case PreparedBlockKind::Video:
		case PreparedBlockKind::Audio:
		case PreparedBlockKind::Map:
		case PreparedBlockKind::Channel:
		case PreparedBlockKind::GroupedMedia:
		case PreparedBlockKind::RelatedArticle:
		case PreparedBlockKind::EmbedPost:
		case PreparedBlockKind::Placeholder:
			break;
		}
	}
}

[[nodiscard]] MarkdownArticleDropLocation EditDropLocationForTableCell(
		const LaidOutTableCell &cell,
		QPoint point) {
	return ContainsPoint(cell.outer, point)
		? MarkdownArticleDropLocation()
		: MarkdownArticleDropLocation();
}

[[nodiscard]] MarkdownArticleDropLocation EditDropLocationForTableRow(
		const LaidOutTableRow &row,
		QPoint point) {
	for (const auto &cell : row.cells) {
		if (ContainsPoint(cell.outer, point)) {
			return EditDropLocationForTableCell(cell, point);
		}
	}
	return ContainsPoint(row.outer, point)
		? MarkdownArticleDropLocation()
		: MarkdownArticleDropLocation();
}

[[nodiscard]] MarkdownArticleDropLocation EditDropLocationForBlock(
		const LaidOutBlock &block,
		QPoint point) {
	switch (block.kind) {
	case PreparedBlockKind::List:
		return EditDropLocationForListItems(
			block.children,
			point,
			ListBlockPath(block),
			block.contentRect.isEmpty() ? block.outer : block.contentRect);
	case PreparedBlockKind::ListItem:
		if (ContainsPoint(block.contentRect, point)) {
			return EditDropLocationForBlockContainer(
				block.children,
				point,
				ListItemChildrenContainerPath(block),
				block.contentRect);
		}
		return {};
	case PreparedBlockKind::Quote:
	case PreparedBlockKind::Details:
		if (ContainsPoint(block.contentRect, point)) {
			return EditDropLocationForBlockContainer(
				block.children,
				point,
				BlockChildrenContainerPath(block),
				block.contentRect);
		}
		return {};
	case PreparedBlockKind::Table:
		for (const auto &row : block.tableRows) {
			if (ContainsPoint(row.outer, point)) {
				return EditDropLocationForTableRow(row, point);
			}
		}
		return {};
	case PreparedBlockKind::Paragraph:
	case PreparedBlockKind::Thinking:
	case PreparedBlockKind::Heading:
	case PreparedBlockKind::CodeBlock:
	case PreparedBlockKind::Rule:
	case PreparedBlockKind::DisplayMath:
	case PreparedBlockKind::Photo:
	case PreparedBlockKind::Video:
	case PreparedBlockKind::Audio:
	case PreparedBlockKind::Map:
	case PreparedBlockKind::Channel:
	case PreparedBlockKind::GroupedMedia:
	case PreparedBlockKind::RelatedArticle:
	case PreparedBlockKind::EmbedPost:
	case PreparedBlockKind::Placeholder:
		return {};
	}
	return {};
}

[[nodiscard]] bool ToggleDetailsBlock(
		std::vector<PreparedBlock> *blocks,
		const QString &anchorId) {
	if (!blocks) {
		return false;
	}
	for (auto &block : *blocks) {
		if (block.kind == PreparedBlockKind::Details
			&& block.anchorId == anchorId) {
			block.collapsed = !block.collapsed;
			return true;
		}
		if (ToggleDetailsBlock(&block.children, anchorId)) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] bool PreparedBlockHasAnchor(
		const PreparedBlock &block,
		const QString &anchorId) {
	return block.anchorId == anchorId
		|| ranges::contains(block.anchorIds, anchorId);
}

[[nodiscard]] MarkdownArticleAnchorExpansion ExpandDetailsToAnchor(
		std::vector<PreparedBlock> *blocks,
		const QString &anchorId,
		bool expandTarget) {
	if (!blocks || anchorId.isEmpty()) {
		return {};
	}
	for (auto &block : *blocks) {
		auto result = MarkdownArticleAnchorExpansion();
		if (PreparedBlockHasAnchor(block, anchorId)) {
			result.found = true;
			if (!expandTarget) {
				return result;
			}
		} else {
			result = ExpandDetailsToAnchor(
				&block.children,
				anchorId,
				expandTarget);
		}
		if (result.found) {
			if (block.kind == PreparedBlockKind::Details
				&& block.collapsed) {
				block.collapsed = false;
				result.changed = true;
			}
			return result;
		}
	}
	return {};
}

[[nodiscard]] const PreparedBlock *FindPreparedDetailsBlock(
		const std::vector<PreparedBlock> &blocks,
		const QString &anchorId) {
	for (const auto &block : blocks) {
		if (block.kind == PreparedBlockKind::Details
			&& block.anchorId == anchorId) {
			return &block;
		}
		if (const auto nested = FindPreparedDetailsBlock(
				block.children,
				anchorId)) {
			return nested;
		}
	}
	return nullptr;
}

void AppendPreparedBlocksText(
		const std::vector<PreparedBlock> &blocks,
		QString *to) {
	const auto append = [&](const QString &text) {
		if (text.isEmpty()) {
			return;
		}
		if (!to->isEmpty()) {
			to->append('\n');
		}
		to->append(text);
	};
	for (const auto &block : blocks) {
		append(block.text.text);
		for (const auto &row : block.tableRows) {
			for (const auto &cell : row.cells) {
				append(cell.text.text);
			}
		}
		AppendPreparedBlocksText(block.children, to);
	}
}

void ClearColorizedFormulaImages(std::vector<LaidOutBlock> *blocks) {
	if (!blocks) {
		return;
	}
	for (auto &block : *blocks) {
		block.colorizedFormulaImage = QImage();
		block.colorizedFormulaColor = QColor();
		block.colorizedFormulaSize = QSize();
		ClearColorizedFormulaImages(&block.children);
	}
}

void HideLeafSpoilers(Ui::Text::String *leaf) {
	if (leaf->hasSpoilers()) {
		leaf->setSpoilerRevealed(false, anim::type::instant);
	}
}

void HideTableCellSpoilers(LaidOutTableCell *cell) {
	HideLeafSpoilers(&cell->leaf);
	HideLeafSpoilers(&cell->placeholderLeaf);
}

void HideBlockSpoilers(LaidOutBlock *block) {
	HideLeafSpoilers(&block->leaf);
	HideLeafSpoilers(&block->placeholderLeaf);
	HideLeafSpoilers(&block->labelLeaf);
	HideLeafSpoilers(&block->subtitleLeaf);
	HideLeafSpoilers(&block->actionLeaf);
	HideLeafSpoilers(&block->marker);
	HideLeafSpoilers(&block->fallbackLeaf);
	for (auto &row : block->tableRows) {
		for (auto &cell : row.cells) {
			HideTableCellSpoilers(&cell);
		}
	}
	for (auto &child : block->children) {
		HideBlockSpoilers(&child);
	}
}

void HideBlocksSpoilers(std::vector<LaidOutBlock> *blocks) {
	for (auto &block : *blocks) {
		HideBlockSpoilers(&block);
	}
}

struct PreparedArticleLeafLookup {
	PreparedBlock *block = nullptr;
	PreparedTableCell *cell = nullptr;

	[[nodiscard]] bool valid() const {
		return block || cell;
	}
};

struct ConstPreparedArticleLeafLookup {
	const PreparedBlock *block = nullptr;
	const PreparedTableCell *cell = nullptr;

	[[nodiscard]] bool valid() const {
		return block || cell;
	}
};

struct LaidOutArticleLeafLookup {
	LaidOutBlock *block = nullptr;
	LaidOutTableCell *cell = nullptr;

	[[nodiscard]] bool valid() const {
		return block || cell;
	}
};

[[nodiscard]] PreparedBlock *FindPreparedArticleBlockByPath(
		std::vector<PreparedBlock> *blocks,
		const PreparedEditBlockPath &path) {
	for (auto &block : *blocks) {
		if (block.editBlock && (block.editBlock->path == path)) {
			return &block;
		}
		if (const auto nested = FindPreparedArticleBlockByPath(
				&block.children,
				path)) {
			return nested;
		}
	}
	return nullptr;
}

[[nodiscard]] const PreparedBlock *FindPreparedArticleBlockByPath(
		const std::vector<PreparedBlock> &blocks,
		const PreparedEditBlockPath &path) {
	for (const auto &block : blocks) {
		if (block.editBlock && (block.editBlock->path == path)) {
			return &block;
		}
		if (const auto nested = FindPreparedArticleBlockByPath(
				block.children,
				path)) {
			return nested;
		}
	}
	return nullptr;
}

[[nodiscard]] PreparedBlock *FindPreparedArticleLeafBlock(
		PreparedBlock *block,
		const PreparedEditLeafSource &source) {
	if (!block) {
		return nullptr;
	}
	if (block->editLeaf && (*block->editLeaf == source)) {
		return block;
	}
	for (auto &child : block->children) {
		if (const auto nested = FindPreparedArticleLeafBlock(&child, source)) {
			return nested;
		}
	}
	return nullptr;
}

[[nodiscard]] const PreparedBlock *FindPreparedArticleLeafBlock(
		const PreparedBlock *block,
		const PreparedEditLeafSource &source) {
	if (!block) {
		return nullptr;
	}
	if (block->editLeaf && (*block->editLeaf == source)) {
		return block;
	}
	for (const auto &child : block->children) {
		if (const auto nested = FindPreparedArticleLeafBlock(&child, source)) {
			return nested;
		}
	}
	return nullptr;
}

[[nodiscard]] PreparedTableCell *FindPreparedArticleLeafCell(
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

[[nodiscard]] const PreparedTableCell *FindPreparedArticleLeafCell(
		const PreparedBlock *block,
		const PreparedEditLeafSource &source) {
	if (!block) {
		return nullptr;
	}
	for (const auto &row : block->tableRows) {
		for (const auto &cell : row.cells) {
			if (cell.editLeaf && (*cell.editLeaf == source)) {
				return &cell;
			}
		}
	}
	return nullptr;
}

[[nodiscard]] PreparedArticleLeafLookup FindPreparedArticleLeaf(
		std::vector<PreparedBlock> *blocks,
		const PreparedEditLeafSource &source) {
	const auto owner = FindPreparedArticleBlockByPath(blocks, source.block);
	if (!owner) {
		return {};
	}
	switch (source.kind) {
	case PreparedEditLeafKind::TableCellText:
		return { .cell = FindPreparedArticleLeafCell(owner, source) };
	case PreparedEditLeafKind::BlockText:
	case PreparedEditLeafKind::BlockCaption:
	case PreparedEditLeafKind::ListItemText:
	case PreparedEditLeafKind::MathFormula:
		return { .block = FindPreparedArticleLeafBlock(owner, source) };
	}
	return {};
}

[[nodiscard]] ConstPreparedArticleLeafLookup FindPreparedArticleLeaf(
		const std::vector<PreparedBlock> &blocks,
		const PreparedEditLeafSource &source) {
	const auto owner = FindPreparedArticleBlockByPath(blocks, source.block);
	if (!owner) {
		return {};
	}
	switch (source.kind) {
	case PreparedEditLeafKind::TableCellText:
		return { .cell = FindPreparedArticleLeafCell(owner, source) };
	case PreparedEditLeafKind::BlockText:
	case PreparedEditLeafKind::BlockCaption:
	case PreparedEditLeafKind::ListItemText:
	case PreparedEditLeafKind::MathFormula:
		return { .block = FindPreparedArticleLeafBlock(owner, source) };
	}
	return {};
}

[[nodiscard]] LaidOutBlock *FindLaidOutArticleBlockByPath(
		std::vector<LaidOutBlock> *blocks,
		const PreparedEditBlockPath &path) {
	for (auto &block : *blocks) {
		if (block.editBlock && (block.editBlock->path == path)) {
			return &block;
		}
		if (const auto nested = FindLaidOutArticleBlockByPath(
				&block.children,
				path)) {
			return nested;
		}
	}
	return nullptr;
}

[[nodiscard]] LaidOutBlock *FindLaidOutArticleLeafBlock(
		LaidOutBlock *block,
		const PreparedEditLeafSource &source) {
	if (!block) {
		return nullptr;
	}
	if (block->editLeaf && (*block->editLeaf == source)) {
		return block;
	}
	for (auto &child : block->children) {
		if (const auto nested = FindLaidOutArticleLeafBlock(&child, source)) {
			return nested;
		}
	}
	return nullptr;
}

[[nodiscard]] LaidOutTableCell *FindLaidOutArticleLeafCell(
		LaidOutBlock *block,
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

[[nodiscard]] LaidOutArticleLeafLookup FindLaidOutArticleLeaf(
		std::vector<LaidOutBlock> *blocks,
		const PreparedEditLeafSource &source) {
	const auto owner = FindLaidOutArticleBlockByPath(blocks, source.block);
	if (!owner) {
		return {};
	}
	switch (source.kind) {
	case PreparedEditLeafKind::TableCellText:
		return { .cell = FindLaidOutArticleLeafCell(owner, source) };
	case PreparedEditLeafKind::BlockText:
	case PreparedEditLeafKind::BlockCaption:
	case PreparedEditLeafKind::ListItemText:
	case PreparedEditLeafKind::MathFormula:
		return { .block = FindLaidOutArticleLeafBlock(owner, source) };
	}
	return {};
}

void PatchPreparedArticleLeaf(
		PreparedBlock *block,
		const PreparedBlock &prepared,
		const PreparedEditLeafSource &source) {
	block->text = prepared.text;
	block->links = prepared.links;
	block->editPlaceholderText = prepared.editPlaceholderText;
	if (source.kind == PreparedEditLeafKind::MathFormula) {
		block->formulaTex = prepared.formulaTex;
		block->formulaIndex = prepared.formulaIndex;
	}
}

void PatchPreparedArticleLeaf(
		PreparedTableCell *cell,
		const PreparedTableCell &prepared) {
	cell->text = prepared.text;
	cell->links = prepared.links;
	cell->editPlaceholderText = prepared.editPlaceholderText;
}

void CollectCodeBlockHighlightKeys(
		const std::vector<PreparedBlock> &blocks,
		std::unordered_set<
			PendingHighlightKey,
			PendingHighlightKeyHasher> *keys) {
	for (const auto &block : blocks) {
		if (block.kind == PreparedBlockKind::CodeBlock
			&& !block.codeLanguage.isEmpty()) {
			keys->insert({
				.text = CodeBlockDisplayText(block.text.text),
				.language = block.codeLanguage,
			});
		}
		CollectCodeBlockHighlightKeys(block.children, keys);
	}
}

[[nodiscard]] bool ExpectsMediaBlock(const PreparedBlock &prepared) {
	switch (prepared.kind) {
	case PreparedBlockKind::Photo:
		return prepared.photo.id
			&& prepared.photo.viewerOpen
			&& prepared.photo.urlOverride.isEmpty();
	case PreparedBlockKind::Video:
		return bool(prepared.video.id);
	case PreparedBlockKind::Map:
		return bool(prepared.map.id);
	case PreparedBlockKind::Audio:
		return bool(prepared.audio.id);
	case PreparedBlockKind::GroupedMedia:
		return bool(prepared.groupedMedia.id);
	default:
		return false;
	}
}

} // namespace

PlaceholderBlockRuntime::PlaceholderBlockRuntime(Fn<void()> repaint)
: clickHandler(std::make_shared<LambdaClickHandler>([] {
}))
, loadingAnimation(
	[repaint = std::move(repaint)] {
		if (repaint) {
			repaint();
		}
	},
	st::defaultInfiniteRadialAnimation) {
}

TaskMarkerRippleRuntime::TaskMarkerRippleRuntime(Fn<void()> repaint)
: repaint(std::move(repaint)) {
}

class MarkdownArticle::Impl final : public CodeBlockSyntaxHighlightTracker {
public:
	Impl(
		const style::Markdown &st,
		std::shared_ptr<MathRenderer> renderer);

	void setRenderer(std::shared_ptr<MathRenderer> renderer);

	void setMediaBlockHost(MediaBlockHost *host);

	void setMediaPixelScale(double scale);

	void setTextRepaintCallbacks(
		Fn<void()> repaint,
		Fn<void(QRect)> repaintRect,
		Fn<bool(const ClickContext&)> spoilerLinkFilter);

	void setContent(MarkdownArticleContent content);
	void setSearchMatches(
		std::vector<MarkdownArticleSearchMatch> matches,
		int current);
	[[nodiscard]] auto searchSources() const
	-> std::vector<MarkdownArticleSearchSource>;
	void updatePreparedLeaf(
		const PreparedEditLeafSource &source,
		const MarkdownArticleContent &prepared);
	void setEditableMaxLineWidthOverride(
		const PreparedEditLeafSource &source,
		int width);

	void setEditableTextEmptyOverride(
		const PreparedEditLeafSource &source,
		bool empty);

	void setEditableHeightOverride(int editableIndex, int height);

	void setEditableHeightOverrideForSegment(int segmentIndex, int height);

	void clearEditableMaxLineWidthOverride();

	void clearEditableTextEmptyOverride();

	void clearEditableHeightOverride();

	void setTextLeafHeightOverride(int textLeafIndex, int height);

	void clearTextLeafHeightOverride();

	[[nodiscard]] int maxWidth();
	[[nodiscard]] int lastLayoutWidth() const;
	[[nodiscard]] bool hasMissingMediaBlocks() const;

	[[nodiscard]] int resizeGetHeight(int width);

	[[nodiscard]] auto countRevealLinesGeometry(int width)
	-> std::vector<MarkdownArticleRevealLine>;

	void setVisibleTopBottom(int visibleTop, int visibleBottom);

	void paint(Painter &p, const MarkdownArticlePaintContext &context);

	[[nodiscard]] MarkdownArticleHitTestResult hitTest(
		QPoint point,
		Ui::Text::StateRequest::Flags flags) const;

	[[nodiscard]] PreparedEditHit editHitTest(QPoint point) const;
	[[nodiscard]] std::vector<MarkdownArticleMediaGeometry>
		mediaBlockGeometries() const;
	void setGroupedActiveIndex(
		const PreparedEditBlockSource &source,
		int index);
	[[nodiscard]] MarkdownArticleDropLocation editDropTarget(
		QPoint point) const;
	[[nodiscard]] MarkdownArticleDropLocation editBlockDropTarget(
		QPoint point) const;
	[[nodiscard]] MarkdownArticleDropLocation editStructuralDropTarget(
		QPoint point,
		const PreparedEditSelection &selection) const;
	[[nodiscard]] MarkdownArticleEditControlHit editControlHitTest(
		QPoint point) const;

	[[nodiscard]] MarkdownArticleHorizontalScrollHit horizontalScrollHit(
		QPoint point) const;
	[[nodiscard]] bool canConsumeHorizontalScroll(
		QPoint point,
		int delta) const;
	[[nodiscard]] bool consumeHorizontalScroll(QPoint point, int delta);
	[[nodiscard]] bool beginHorizontalScroll(QPoint point, bool fromTouch);
	[[nodiscard]] bool updateHorizontalScroll(QPoint point);
	void endHorizontalScroll();

	[[nodiscard]] int anchorTop(const QString &anchorId) const;

	[[nodiscard]] auto scrollAnchorForTop(int top) const
	-> std::optional<MarkdownArticleScrollAnchor>;

	[[nodiscard]] int scrollTopForAnchor(
		const MarkdownArticleScrollAnchor &anchor) const;

	[[nodiscard]] MarkdownArticleAnchorExpansion expandDetailsToAnchor(
		const QString &anchorId);

	[[nodiscard]] MarkdownArticleAnchorExpansion expandDetailsBlock(
		const QString &anchorId);

	[[nodiscard]] bool toggleDetails(const QString &anchorId);

	[[nodiscard]] bool segmentIsText(int index) const;

	[[nodiscard]] bool segmentIsDisplayMath(int index) const;

	[[nodiscard]] bool segmentIsEditable(int index) const;

	[[nodiscard]] int segmentLength(int index) const;

	[[nodiscard]] int firstTextSegmentIndex() const;

	[[nodiscard]] int firstEditableSegmentIndex() const;

	[[nodiscard]] int textLeafIndexForSegment(int segmentIndex) const;

	[[nodiscard]] int segmentIndexForTextLeafIndex(int textLeafIndex) const;

	[[nodiscard]] int editableIndexForSegment(int segmentIndex) const;

	[[nodiscard]] int segmentIndexForEditableIndex(int editableIndex) const;

	[[nodiscard]] auto editableLeafForSegment(int segmentIndex) const
	-> std::optional<PreparedEditLeafSource>;

	[[nodiscard]] int segmentIndexForEditableLeaf(
		const PreparedEditLeafSource &source) const;

	[[nodiscard]] QRect textSegmentRect(int segmentIndex) const;
	[[nodiscard]] QRect logicalSegmentRect(int segmentIndex) const;

	[[nodiscard]] QRect segmentRect(int segmentIndex) const;
	[[nodiscard]] QRect displayMathEditRect(int segmentIndex) const;
	[[nodiscard]] QRect displayMathBlockRect(int segmentIndex) const;
	[[nodiscard]] int pullquoteAvailableTextWidthForEditableLeaf(
		const PreparedEditLeafSource &source) const;
	[[nodiscard]] bool revealSegment(int segmentIndex);

	[[nodiscard]] MarkdownArticleTextLeafStyle textLeafStyleForSegment(
		int segmentIndex) const;

	[[nodiscard]] MarkdownArticleTextLeafStyle editableStyleForSegment(
		int segmentIndex) const;

	[[nodiscard]] int selectionOffsetFromHit(
		const MarkdownArticleHitTestResult &result,
		TextSelectType selectionType) const;

	[[nodiscard]] TextSelection adjustSelection(
		int segmentIndex,
		TextSelection selection,
		TextSelectType selectionType) const;

	[[nodiscard]] bool selectionContains(
		MarkdownArticleSelection selection,
		const MarkdownArticleSelectionEndpoints *endpoints,
		const MarkdownArticleHitTestResult &result) const;

	[[nodiscard]] TextForMimeData textForContext(
		const MarkdownArticleHitTestResult &result) const;

	[[nodiscard]] TextForMimeData textForSelection(
		MarkdownArticleSelection selection,
		const MarkdownArticleSelectionEndpoints *endpoints,
		const PreparedEditSelection *structuralSelection) const;

	[[nodiscard]] std::vector<RichPage::Block> richPageSliceForSelection(
		MarkdownArticleSelection selection) const;

	[[nodiscard]] bool highlightProcessDone(
		Spellchecker::HighlightProcessId processId);

	void invalidatePaletteCache();

	void invalidateRasterCache();

	[[nodiscard]] bool hasHeavyPart() const;

	void unloadHeavyPart();

	void hideSpoilers();

	[[nodiscard]] MediaBlockHost *mediaBlockHost() const;

	void setPlaceholderLoading(PreparedPlaceholderBlockId id);
	void clearPlaceholderLoading(PreparedPlaceholderBlockId id);
	void clearAllPlaceholderLoading();
	void addTaskMarkerRipple(
		const PreparedEditListItemSource &source,
		QPoint point);
	void addPlaceholderRipple(PreparedPlaceholderBlockId id, QPoint point);
	void stopPlaceholderRipple(PreparedPlaceholderBlockId id);

	void invalidateLayout();

private:
	struct ActiveHorizontalScrollDrag {
		MarkdownArticleScrollOwnerIdentity owner;
		QPoint pressPoint;
		int startScrollLeft = 0;
		int thumbGrabOffset = 0;
		bool fromTouch = false;
	};

	[[nodiscard]] int currentDevicePixelRatio() const;

	void rebuildVisibleSegmentLookup();

	void refreshVisibleSegmentSpan();

	void clearMediaBlocks();

	void refreshMediaBlockHosts();

	void clearPlaceholderRuntimes();

	[[nodiscard]] std::shared_ptr<PlaceholderBlockRuntime>
	getOrCreatePlaceholderRuntime(PreparedPlaceholderBlockId id);

	void prunePlaceholderRuntimes();

	void requestPlaceholderRepaint(PreparedPlaceholderBlockId id);

	[[nodiscard]] auto getOrCreateTaskMarkerRippleRuntime(
		const PreparedEditListItemSource &source)
	-> std::shared_ptr<TaskMarkerRippleRuntime>;

	void requestTaskMarkerRepaint(
		const PreparedEditListItemSource &source);

	void pruneTaskMarkerRuntimes();

	[[nodiscard]] std::shared_ptr<MediaBlock> getOrCreateMediaBlock(
		const PreparedBlock &prepared);

	template <typename Factory>
	[[nodiscard]] std::shared_ptr<MediaBlock> getOrCreateMediaBlock(
		PreparedMediaBlockId id,
		Factory &&factory);

	[[nodiscard]] Spellchecker::HighlightProcessId tryHighlightSyntax(
		const QString &displayText,
		const QString &language,
		TextWithEntities &marked) override;

	[[nodiscard]] SegmentSpan candidateSegmentSpan(QPoint point) const;

	void clearPendingHighlightBlockPointers();

	void prunePendingHighlightProcessesForContent();

	void registerPendingHighlightProcess(
		const PendingHighlightKey &key,
		Spellchecker::HighlightProcessId processId);

	void registerPendingHighlightBlock(LaidOutBlock &block);

	void registerPendingHighlightBlocks(std::vector<LaidOutBlock> &blocks);

	void resetFormulaRasterCache();

	void setPlaceholderLoadingValue(
		PreparedPlaceholderBlockId id,
		bool loading);

	void invalidateLayout(bool harvestCurrentBlocks);
	void invalidateGeometry();

	[[nodiscard]] const style::Markdown &layoutStyle() const;
	[[nodiscard]] MarkdownArticleScrollOwnerIdentity scrollOwnerIdentity(
		const LaidOutBlock &block,
		const std::vector<int> &preparedPath) const;
	[[nodiscard]] MarkdownArticleHorizontalScrollLookup
	findHorizontalScrollOwner(QPoint point) const;
	[[nodiscard]] MarkdownArticleHorizontalScrollLookup
	findHorizontalScrollOwner(
		const std::vector<LaidOutBlock> &blocks,
		const std::vector<PreparedBlock> *prepared,
		QPoint point,
		std::vector<int> *preparedPath) const;
	[[nodiscard]] std::optional<MarkdownArticleHorizontalScrollLookup>
	findHorizontalScrollOwner(int segmentIndex) const;
	[[nodiscard]] std::optional<MarkdownArticleHorizontalScrollLookup>
	findHorizontalScrollOwner(
		const std::vector<LaidOutBlock> &blocks,
		const std::vector<PreparedBlock> *prepared,
		const SelectableSegment &segment,
		std::optional<MarkdownArticleHorizontalScrollLookup> owner,
		std::vector<int> *preparedPath) const;
	[[nodiscard]] LaidOutBlock *findScrollOwnerByIdentity(
		const MarkdownArticleScrollOwnerIdentity &identity);
	[[nodiscard]] LaidOutBlock *findScrollOwnerByIdentity(
		std::vector<LaidOutBlock> &blocks,
		const std::vector<PreparedBlock> *prepared,
		const MarkdownArticleScrollOwnerIdentity &identity,
		std::vector<int> *preparedPath);
	void captureScrollState();
	void captureScrollState(
		const std::vector<LaidOutBlock> &blocks,
		const std::vector<PreparedBlock> *prepared,
		std::vector<int> *preparedPath);
	void restoreScrollState();
	void restoreScrollState(
		std::vector<LaidOutBlock> &blocks,
		const std::vector<PreparedBlock> *prepared,
		std::vector<int> *preparedPath);
	void refreshScrolledGeometry(LaidOutBlock &block);
	void refreshScrolledGeometry(
		LaidOutBlock &block,
		const ActiveHorizontalScrollOwnerState *activeOwner);
	void refreshScrolledGeometry(std::vector<LaidOutBlock> &blocks);
	void refreshScrolledGeometry(
		std::vector<LaidOutBlock> &blocks,
		const ActiveHorizontalScrollOwnerState *activeOwner);
	void updateScrollbarThumb(LaidOutBlock &block);
	[[nodiscard]] bool setScrollLeft(
		LaidOutBlock &block,
		const MarkdownArticleScrollOwnerIdentity &identity,
		int left);

	void finalizeRelayout(int heightBottom);
	void relayout(int width);
	void relayoutRetained(int width);
	void retainBlocks();
	[[nodiscard]] const LaidOutBlock *pullquoteBlockForEditableLeaf(
		const PreparedEditLeafSource &source) const;

	mutable MarkdownArticleContent _content;
	style::Markdown _style;
	std::vector<RenderedFormula> _formulaRenders;
	std::shared_ptr<MathRenderer> _renderer;
	std::shared_ptr<InlineFormulaObjectCache> _inlineFormulaObjects;
	MediaBlockHost *_mediaBlockHost = nullptr;
	Fn<void()> _textRepaint;
	Fn<void(QRect)> _textRepaintRect;
	Fn<bool(const ClickContext&)> _textSpoilerLinkFilter;
	int _width = -1;
	int _laidOutWidth = 0;
	int _height = 0;
	int _layoutGeneration = 0;
	double _mediaPixelScale = 1.;
	MarkdownArticleRevealLineCountsCache _revealLineCounts;
	CachedTextLeafPool _cachedTextLeafs;
	std::vector<LaidOutBlock> _blocks;
	std::vector<LaidOutBlock> _retainedBlocks;
	MediaBlockStorage _mediaBlocks;
	int _missingMediaBlocks = 0;
	std::unordered_map<uint64, std::shared_ptr<PlaceholderBlockRuntime>>
		_placeholderRuntimes;
	TaskMarkerRippleRuntimeMap _taskMarkerRippleRuntimes;
	std::unordered_map<
		uint64,
		RelatedArticleImageState> _relatedArticleImages;
	std::unordered_map<
		PendingHighlightKey,
		Spellchecker::HighlightProcessId,
		PendingHighlightKeyHasher> _pendingHighlightProcesses;
	std::unordered_map<
		Spellchecker::HighlightProcessId,
		PendingHighlightEntry> _pendingHighlightEntries;
	std::vector<std::pair<QString, int>> _anchors;
	std::vector<SelectableSegment> _segments;
	std::vector<MarkdownArticleSearchMatch> _searchMatches;
	int _currentSearchMatch = -1;
	std::optional<LogicalVisibleRange> _visibleRange;
	SegmentSpan _visibleSegmentSpan;
	std::vector<int> _segmentTops;
	std::vector<int> _segmentBottoms;
	std::unordered_map<
		MarkdownArticleScrollOwnerIdentity,
		int,
		MarkdownArticleScrollOwnerIdentityHasher> _capturedScrollLefts;
	std::optional<ActiveHorizontalScrollDrag> _activeHorizontalScrollDrag;
	std::optional<PreparedEditLeafSource> _editableMaxLineWidthOverrideLeaf;
	int _editableMaxLineWidthOverride = 0;
	std::optional<PreparedEditLeafSource> _editableTextEmptyOverrideLeaf;
	bool _editableTextEmptyOverride = true;
	int _editableHeightOverrideIndex = -1;
	int _editableHeightOverride = 0;
	bool _blocksPainted = false;

};

MarkdownArticle::Impl::Impl(
	const style::Markdown &st,
	std::shared_ptr<MathRenderer> renderer)
: _style(st)
, _renderer(std::move(renderer))
, _inlineFormulaObjects(CreateInlineFormulaObjectCache(_renderer)) {
	_style.code.font = _style.code.font->monospace();
}

void MarkdownArticle::Impl::setRenderer(std::shared_ptr<MathRenderer> renderer) {
	_renderer = std::move(renderer);
	SetInlineFormulaObjectCacheRenderer(_inlineFormulaObjects, _renderer);
	invalidateRasterCache();
	invalidateLayout();
}

void MarkdownArticle::Impl::setMediaBlockHost(MediaBlockHost *host) {
	if (_mediaBlockHost == host) {
		return;
	}
	_mediaBlockHost = host;
	refreshMediaBlockHosts();
}

void MarkdownArticle::Impl::setMediaPixelScale(double scale) {
	if (_mediaPixelScale == scale) {
		return;
	}
	_mediaPixelScale = scale;
	invalidateLayout();
}

void MarkdownArticle::Impl::setTextRepaintCallbacks(
		Fn<void()> repaint,
		Fn<void(QRect)> repaintRect,
		Fn<bool(const ClickContext&)> spoilerLinkFilter) {
	_textRepaint = std::move(repaint);
	_textRepaintRect = std::move(repaintRect);
	_textSpoilerLinkFilter = std::move(spoilerLinkFilter);
}

void MarkdownArticle::Impl::setContent(MarkdownArticleContent content) {
	if (hasHeavyPart()) {
		unloadHeavyPart();
	}
	auto reusedMediaBlocks = MediaBlockStorage();
	const auto reuseMediaBlocks = (_content.mediaRuntime == content.mediaRuntime);
	RebuildCachedTextLeafs(
		_content.blocks.blocks,
		&_blocks,
		layoutStyle(),
		&_cachedTextLeafs);
	if (!reuseMediaBlocks) {
		PruneMediaRuntimeBoundCachedTextLeafs(&_cachedTextLeafs);
	}
	if (reuseMediaBlocks) {
		auto oldMediaBlocks = MediaBlockStorage();
		oldMediaBlocks.swap(_mediaBlocks);
		reusedMediaBlocks = ReuseMediaBlocks(
			_content.blocks.blocks,
			&oldMediaBlocks,
			content.blocks.blocks);
	} else {
		clearMediaBlocks();
	}
	clearPlaceholderRuntimes();
	_relatedArticleImages.clear();
	_content = std::move(content);
	if (reuseMediaBlocks) {
		_mediaBlocks = std::move(reusedMediaBlocks);
	}
	prunePendingHighlightProcessesForContent();
	ClearInlineFormulaObjectCache(_inlineFormulaObjects);
	resetFormulaRasterCache();
	_searchMatches.clear();
	_currentSearchMatch = -1;
	invalidateLayout(false);
}

void MarkdownArticle::Impl::setSearchMatches(
		std::vector<MarkdownArticleSearchMatch> matches,
		int current) {
	_searchMatches = std::move(matches);
	_currentSearchMatch = (current >= 0
		&& current < int(_searchMatches.size()))
		? current
		: -1;
}

auto MarkdownArticle::Impl::searchSources() const
-> std::vector<MarkdownArticleSearchSource> {
	auto result = std::vector<MarkdownArticleSearchSource>();
	result.reserve(_segments.size());
	for (const auto &segment : _segments) {
		auto source = MarkdownArticleSearchSource();
		if (segment.isTextLeaf()) {
			source.text = segment.leaf->toString();
		}
		const auto block = segment.block;
		if (block && block->kind == PreparedBlockKind::Details) {
			source.detailsAnchorId = block->anchorId;
			if (block->collapsed) {
				const auto prepared = FindPreparedDetailsBlock(
					_content.blocks.blocks,
					block->anchorId);
				if (prepared) {
					AppendPreparedBlocksText(
						prepared->children,
						&source.hiddenText);
				}
			}
		}
		result.push_back(std::move(source));
	}
	return result;
}

void MarkdownArticle::Impl::updatePreparedLeaf(
		const PreparedEditLeafSource &source,
		const MarkdownArticleContent &prepared) {
	if (!ValidBlockPath(source.block)
		|| (_content.mediaRuntime != prepared.mediaRuntime)
		|| (_content.editMode != prepared.editMode)) {
		setContent(prepared);
		return;
	}
	const auto current = FindPreparedArticleLeaf(&_content.blocks.blocks, source);
	const auto incoming = FindPreparedArticleLeaf(prepared.blocks.blocks, source);
	const auto live = FindLaidOutArticleLeaf(&_blocks, source);
	if (!current.valid() || !incoming.valid() || !live.valid()) {
		setContent(prepared);
		return;
	}

	auto codeTextChanged = false;
	auto displayMathOldIndex = -1;
	auto displayMathNewIndex = -1;

	switch (source.kind) {
	case PreparedEditLeafKind::TableCellText:
		if (!current.cell
			|| !incoming.cell
			|| !live.cell
			|| (current.cell->header != incoming.cell->header)
			|| (current.cell->alignment != incoming.cell->alignment)
			|| (current.cell->verticalAlignment
				!= incoming.cell->verticalAlignment)
			|| (current.cell->column != incoming.cell->column)
			|| (current.cell->colspan != incoming.cell->colspan)
			|| (current.cell->rowspan != incoming.cell->rowspan)) {
			setContent(prepared);
			return;
		}
		PatchPreparedArticleLeaf(current.cell, *incoming.cell);
		break;
	case PreparedEditLeafKind::BlockText:
	case PreparedEditLeafKind::BlockCaption:
	case PreparedEditLeafKind::ListItemText:
	case PreparedEditLeafKind::MathFormula:
		if (!current.block
			|| !incoming.block
			|| !live.block
			|| (current.block->kind != incoming.block->kind)
			|| (current.block->codeLanguage != incoming.block->codeLanguage)
			|| ((source.kind == PreparedEditLeafKind::MathFormula)
				&& (current.block->kind != PreparedBlockKind::DisplayMath))) {
			setContent(prepared);
			return;
		}
		codeTextChanged = (current.block->kind == PreparedBlockKind::CodeBlock)
			&& (CodeBlockDisplayText(current.block->text.text)
				!= CodeBlockDisplayText(incoming.block->text.text));
		displayMathOldIndex = current.block->formulaIndex;
		displayMathNewIndex = incoming.block->formulaIndex;
		PatchPreparedArticleLeaf(current.block, *incoming.block, source);
		break;
	}

	_content.formulas = prepared.formulas;
	_formulaRenders.resize(_content.formulas.size());
	if (source.kind == PreparedEditLeafKind::MathFormula) {
		if (displayMathOldIndex >= 0
			&& displayMathOldIndex < int(_formulaRenders.size())) {
			_formulaRenders[displayMathOldIndex] = RenderedFormula();
		}
		if (displayMathNewIndex >= 0
			&& displayMathNewIndex < int(_formulaRenders.size())) {
			_formulaRenders[displayMathNewIndex] = RenderedFormula();
		}
		if (live.block) {
			live.block->colorizedFormulaImage = QImage();
			live.block->colorizedFormulaColor = QColor();
			live.block->colorizedFormulaSize = QSize();
		}
	}

	auto context = LayoutContext();
	context.syntaxHighlightTracker = this;
	context.repaint = _textRepaint;
	context.repaintRect = _textRepaintRect;
	context.spoilerLinkFilter = _textSpoilerLinkFilter;
	if (live.block && incoming.block) {
		UpdateLaidOutLeafContent(
			live.block,
			*incoming.block,
			&_content.formulas,
			_inlineFormulaObjects.get(),
			_content.mediaRuntime,
			layoutStyle(),
			context);
	} else if (live.cell && incoming.cell) {
		UpdateLaidOutLeafContent(
			live.cell,
			*incoming.cell,
			&_content.formulas,
			_inlineFormulaObjects.get(),
			_content.mediaRuntime,
			layoutStyle(),
			source.tableRowIndex,
			source.tableCellIndex,
			context);
	} else {
		setContent(prepared);
		return;
	}

	if (codeTextChanged) {
		prunePendingHighlightProcessesForContent();
	}
	if (_width > 0) {
		invalidateGeometry();
	}
}

void MarkdownArticle::Impl::setEditableMaxLineWidthOverride(
		const PreparedEditLeafSource &source,
		int width) {
	width = std::max(width, 0);
	if (_editableMaxLineWidthOverrideLeaf
		&& (*_editableMaxLineWidthOverrideLeaf == source)
		&& (_editableMaxLineWidthOverride == width)) {
		return;
	}
	_editableMaxLineWidthOverrideLeaf = source;
	_editableMaxLineWidthOverride = width;
	invalidateGeometry();
}

void MarkdownArticle::Impl::setEditableTextEmptyOverride(
		const PreparedEditLeafSource &source,
		bool empty) {
	if (_editableTextEmptyOverrideLeaf
		&& (*_editableTextEmptyOverrideLeaf == source)
		&& (_editableTextEmptyOverride == empty)) {
		return;
	}
	_editableTextEmptyOverrideLeaf = source;
	_editableTextEmptyOverride = empty;
	invalidateGeometry();
}

void MarkdownArticle::Impl::setEditableHeightOverride(
		int editableIndex,
		int height) {
	editableIndex = std::max(editableIndex, -1);
	height = std::max(height, 0);
	if (_editableHeightOverrideIndex == editableIndex
		&& _editableHeightOverride == height) {
		return;
	}
	_editableHeightOverrideIndex = editableIndex;
	_editableHeightOverride = height;
	invalidateGeometry();
}

void MarkdownArticle::Impl::setEditableHeightOverrideForSegment(
		int segmentIndex,
		int height) {
	setEditableHeightOverride(editableIndexForSegment(segmentIndex), height);
}

void MarkdownArticle::Impl::clearEditableMaxLineWidthOverride() {
	if (!_editableMaxLineWidthOverrideLeaf
		&& (_editableMaxLineWidthOverride == 0)) {
		return;
	}
	_editableMaxLineWidthOverrideLeaf = std::nullopt;
	_editableMaxLineWidthOverride = 0;
	invalidateGeometry();
}

void MarkdownArticle::Impl::clearEditableTextEmptyOverride() {
	if (!_editableTextEmptyOverrideLeaf) {
		return;
	}
	_editableTextEmptyOverrideLeaf = std::nullopt;
	_editableTextEmptyOverride = true;
	invalidateGeometry();
}

void MarkdownArticle::Impl::clearEditableHeightOverride() {
	setEditableHeightOverride(-1, 0);
}

void MarkdownArticle::Impl::setTextLeafHeightOverride(
		int textLeafIndex,
		int height) {
	setEditableHeightOverrideForSegment(
		segmentIndexForTextLeafIndex(textLeafIndex),
		height);
}

void MarkdownArticle::Impl::clearTextLeafHeightOverride() {
	clearEditableHeightOverride();
}

int MarkdownArticle::Impl::maxWidth() {
	const auto &st = layoutStyle();
	return std::max(
		st.pageMaxWidth,
		st.pagePadding.left()
			+ st.pagePadding.right()
			+ 1);
}

int MarkdownArticle::Impl::lastLayoutWidth() const {
	return _laidOutWidth;
}

bool MarkdownArticle::Impl::hasMissingMediaBlocks() const {
	return _missingMediaBlocks > 0;
}

int MarkdownArticle::Impl::resizeGetHeight(int width) {
	width = std::max(width, 1);
	if (_width != width) {
		if (_blocks.empty()) {
			relayout(width);
		} else {
			relayoutRetained(width);
		}
	}
	return std::max(_height, 1);
}

auto MarkdownArticle::Impl::countRevealLinesGeometry(int width)
-> std::vector<MarkdownArticleRevealLine> {
	width = std::max(width, 1);
	if (_width != width) {
		if (_blocks.empty()) {
			relayout(width);
		} else {
			relayoutRetained(width);
		}
	}
	return CollectRevealLines(_blocks, layoutStyle());
}

void MarkdownArticle::Impl::setVisibleTopBottom(int visibleTop, int visibleBottom) {
	if (visibleBottom <= visibleTop) {
		_visibleRange = std::nullopt;
		_visibleSegmentSpan = {};
		return;
	}
	_visibleRange = LogicalVisibleRange{
		.top = visibleTop,
		.bottom = visibleBottom,
	};
	refreshVisibleSegmentSpan();
}

void MarkdownArticle::Impl::paint(
		Painter &p,
		const MarkdownArticlePaintContext &context) {
	const auto &st = layoutStyle();
	auto local = context;
	local.selectionState.segments = &_segments;
	local.searchState.matches = &_searchMatches;
	local.searchState.current = _currentSearchMatch;
	const auto &paintSt = local.paintMarkdownStyle(st);
	auto textPalette = paintSt.textPalette;
	auto markBg = MarkBgColorForStyle(paintSt);
	const auto ownedMarkBg = style::internal::OwnedColor(markBg);
	textPalette.markBg = ownedMarkBg.color();
	if (local.reveal) {
		if (_revealLineCounts.layoutGeneration != _layoutGeneration) {
			_revealLineCounts.layoutGeneration = _layoutGeneration;
			_revealLineCounts.counts.clear();
		}
		local.reveal->lineCounts = &_revealLineCounts;
	} else if (!_revealLineCounts.counts.empty()) {
		_revealLineCounts = {};
	}
	const auto &previousTextPalette = p.textPalette();
	p.setTextPalette(textPalette);
	PaintBlocks(
		p,
		_blocks,
		&_content.formulas,
		&_formulaRenders,
		_renderer.get(),
		currentDevicePixelRatio(),
		std::max(_width, 1),
		st,
		local);
	p.setTextPalette(previousTextPalette);
	_retainedBlocks.clear();
	_blocksPainted = true;
}

MarkdownArticleHitTestResult MarkdownArticle::Impl::hitTest(
		QPoint point,
		Ui::Text::StateRequest::Flags flags) const {
	const auto span = candidateSegmentSpan(point);
	for (auto i = span.from; i != span.till; ++i) {
		const auto &segment = _segments[i];
		if (const auto result = HitCodeBlockHeader(segment, point, flags);
			result.valid()) {
			return result;
		}
	}
	for (auto i = span.from; i != span.till; ++i) {
		const auto &segment = _segments[i];
		if (const auto result = HitTextSegment(segment, point, flags);
			result.valid()) {
			return result;
		}
	}
	for (auto i = span.from; i != span.till; ++i) {
		const auto &segment = _segments[i];
		if (const auto result = HitBlockSegment(segment, point, flags);
			result.valid()) {
			return result;
		}
	}
	if (flags & Ui::Text::StateRequest::Flag::LookupSymbol) {
		return HitSegmentFallback(_segments, span, point);
	}
	return {};
}

PreparedEditHit MarkdownArticle::Impl::editHitTest(QPoint point) const {
	return EditHitForBlocks(_blocks, point);
}

std::vector<MarkdownArticleMediaGeometry>
MarkdownArticle::Impl::mediaBlockGeometries() const {
	auto result = std::vector<MarkdownArticleMediaGeometry>();
	CollectMediaBlockGeometries(&result, _blocks);
	return result;
}

void MarkdownArticle::Impl::setGroupedActiveIndex(
		const PreparedEditBlockSource &source,
		int index) {
	const auto block = FindLaidOutArticleBlockByPath(&_blocks, source.path);
	if (block && block->mediaBlock) {
		block->mediaBlock->setActiveItemIndex(index);
	}
}

MarkdownArticleDropLocation MarkdownArticle::Impl::editDropTarget(
		QPoint point) const {
	if (const auto result = hitTest(
			point,
			Ui::Text::StateRequest::Flag::LookupSymbol);
		result.valid() && result.direct && !result.codeHeaderCopy) {
		if (const auto segment = FindSegment(&_segments, result.segmentIndex)) {
			if (const auto leaf = EditableLeafForSegment(*segment)) {
				if (leaf->kind != PreparedEditLeafKind::MathFormula) {
					auto location = MarkdownArticleDropLocation();
					location.target = PreparedEditDropTarget(
						PreparedEditTextDropTarget{
							.leaf = *leaf,
							.offset = selectionOffsetFromHit(
								result,
								TextSelectType::Letters),
						});
					return location;
				}
			}
		}
	}
	return EditDropLocationForBlockContainer(
		_blocks,
		point,
		PreparedEditBlockContainerPath(),
		QRect());
}

MarkdownArticleDropLocation MarkdownArticle::Impl::editBlockDropTarget(
		QPoint point) const {
	return EditDropLocationForBlockContainer(
		_blocks,
		point,
		PreparedEditBlockContainerPath(),
		QRect());
}

MarkdownArticleDropLocation MarkdownArticle::Impl::editStructuralDropTarget(
		QPoint point,
		const PreparedEditSelection &selection) const {
	if (PointInsideSelectionVerticalSpan(point, _blocks, selection)) {
		return {};
	}
	switch (selection.kind) {
	case PreparedEditSelectionKind::Blocks: {
		if (selection.blocks.empty()) {
			return {};
		}
		auto best = DropGapCandidate();
		ConsiderStructuralBlockDropTargets(
			&best,
			_blocks,
			point,
			PreparedEditBlockContainerPath(),
			QRect(),
			selection);
		return best.location;
	}
	case PreparedEditSelectionKind::ListItems: {
		if (selection.listItems.empty()) {
			return {};
		}
		auto best = DropGapCandidate();
		ConsiderStructuralListItemDropTargets(
			&best,
			_blocks,
			point,
			selection.listItems.block,
			selection);
		return best.location;
	}
	case PreparedEditSelectionKind::TableRows:
	case PreparedEditSelectionKind::TableCells:
	case PreparedEditSelectionKind::None:
		return {};
	}
	return {};
}

MarkdownArticleEditControlHit MarkdownArticle::Impl::editControlHitTest(
		QPoint point) const {
	return EditControlHitForBlocks(_blocks, point);
}

int MarkdownArticle::Impl::anchorTop(const QString &anchorId) const {
	for (const auto &entry : _anchors) {
		if (entry.first == anchorId) {
			return entry.second;
		}
	}
	return -1;
}

auto MarkdownArticle::Impl::scrollAnchorForTop(int top) const
-> std::optional<MarkdownArticleScrollAnchor> {
	if (_segments.empty()) {
		return std::nullopt;
	}
	const auto make = [&](const SelectableSegment &segment) {
		const auto rect = segment.outerRect;
		const auto height = std::max(rect.height(), 1);
		const auto fraction = std::clamp(
			(top - rect.top()) / double(height),
			0.,
			1.);
		return MarkdownArticleScrollAnchor{ segment.index, fraction };
	};
	const auto span = LookupVisibleSegmentSpan(
		_segmentTops,
		_segmentBottoms,
		{ top, top + 1 });
	const auto containsVertically = [&](const SelectableSegment &segment) {
		const auto rect = segment.outerRect;
		return (rect.top() <= top) && (top < rect.top() + rect.height());
	};
	auto found = (const SelectableSegment*)nullptr;
	for (auto i = span.from; i != span.till; ++i) {
		const auto &segment = _segments[i];
		if (!containsVertically(segment)) {
			continue;
		} else if (!found || (segment.isTextLeaf() && !found->isTextLeaf())) {
			found = &segment;
		}
	}
	if (found) {
		return make(*found);
	}
	for (const auto &segment : _segments) {
		if (segment.outerRect.top() >= top) {
			return MarkdownArticleScrollAnchor{ segment.index, 0. };
		}
	}
	return make(_segments.back());
}

int MarkdownArticle::Impl::scrollTopForAnchor(
		const MarkdownArticleScrollAnchor &anchor) const {
	const auto segment = FindSegment(&_segments, anchor.segmentIndex);
	if (!segment) {
		return -1;
	}
	const auto rect = segment->outerRect;
	const auto fraction = std::clamp(anchor.fraction, 0., 1.);
	return rect.top() + int(std::round(fraction * rect.height()));
}

MarkdownArticleAnchorExpansion MarkdownArticle::Impl::expandDetailsToAnchor(
		const QString &anchorId) {
	const auto result = ExpandDetailsToAnchor(
		&_content.blocks.blocks,
		anchorId,
		false);
	if (result.changed) {
		invalidateLayout();
	}
	return result;
}

MarkdownArticleAnchorExpansion MarkdownArticle::Impl::expandDetailsBlock(
		const QString &anchorId) {
	const auto result = ExpandDetailsToAnchor(
		&_content.blocks.blocks,
		anchorId,
		true);
	if (result.changed) {
		invalidateLayout();
	}
	return result;
}

bool MarkdownArticle::Impl::toggleDetails(const QString &anchorId) {
	if (!ToggleDetailsBlock(&_content.blocks.blocks, anchorId)) {
		return false;
	}
	invalidateLayout();
	return true;
}

bool MarkdownArticle::Impl::segmentIsText(int index) const {
	const auto segment = FindSegment(&_segments, index);
	return segment && segment->isTextLeaf();
}

bool MarkdownArticle::Impl::segmentIsDisplayMath(int index) const {
	const auto segment = FindSegment(&_segments, index);
	return segment && IsDisplayMathSegment(*segment);
}

bool MarkdownArticle::Impl::segmentIsEditable(int index) const {
	const auto segment = FindSegment(&_segments, index);
	return segment && IsEditableSegment(*segment);
}

int MarkdownArticle::Impl::segmentLength(int index) const {
	const auto segment = FindSegment(&_segments, index);
	return segment ? SegmentLength(*segment) : 0;
}

int MarkdownArticle::Impl::firstTextSegmentIndex() const {
	for (const auto &segment : _segments) {
		if (segment.isTextLeaf()) {
			return segment.index;
		}
	}
	return -1;
}

int MarkdownArticle::Impl::firstEditableSegmentIndex() const {
	for (const auto &segment : _segments) {
		if (IsEditableSegment(segment)) {
			return segment.index;
		}
	}
	return -1;
}

int MarkdownArticle::Impl::textLeafIndexForSegment(int segmentIndex) const {
	auto textLeafIndex = 0;
	for (const auto &segment : _segments) {
		if (!segment.isTextLeaf()) {
			continue;
		} else if (segment.index == segmentIndex) {
			return textLeafIndex;
		}
		++textLeafIndex;
	}
	return -1;
}

int MarkdownArticle::Impl::segmentIndexForTextLeafIndex(
		int textLeafIndex) const {
	if (textLeafIndex < 0) {
		return -1;
	}
	auto current = 0;
	for (const auto &segment : _segments) {
		if (!segment.isTextLeaf()) {
			continue;
		} else if (current == textLeafIndex) {
			return segment.index;
		}
		++current;
	}
	return -1;
}

int MarkdownArticle::Impl::editableIndexForSegment(int segmentIndex) const {
	auto editableIndex = 0;
	for (const auto &segment : _segments) {
		if (!IsEditableSegment(segment)) {
			continue;
		} else if (segment.index == segmentIndex) {
			return editableIndex;
		}
		++editableIndex;
	}
	return -1;
}

int MarkdownArticle::Impl::segmentIndexForEditableIndex(
		int editableIndex) const {
	if (editableIndex < 0) {
		return -1;
	}
	auto current = 0;
	for (const auto &segment : _segments) {
		if (!IsEditableSegment(segment)) {
			continue;
		} else if (current == editableIndex) {
			return segment.index;
		}
		++current;
	}
	return -1;
}

std::optional<PreparedEditLeafSource>
MarkdownArticle::Impl::editableLeafForSegment(int segmentIndex) const {
	const auto segment = FindSegment(&_segments, segmentIndex);
	return (segment && IsEditableSegment(*segment))
		? EditableLeafForSegment(*segment)
		: std::nullopt;
}

int MarkdownArticle::Impl::segmentIndexForEditableLeaf(
		const PreparedEditLeafSource &source) const {
	for (const auto &segment : _segments) {
		if (!IsEditableSegment(segment)) {
			continue;
		}
		const auto leaf = EditableLeafForSegment(segment);
		if (leaf && (*leaf == source)) {
			return segment.index;
		}
	}
	return -1;
}

QRect MarkdownArticle::Impl::textSegmentRect(int segmentIndex) const {
	const auto segment = FindSegment(&_segments, segmentIndex);
	return (segment && segment->isTextLeaf()) ? segment->textRect : QRect();
}

QRect MarkdownArticle::Impl::logicalSegmentRect(int segmentIndex) const {
	const auto segment = FindSegment(&_segments, segmentIndex);
	if (!segment) {
		return QRect();
	} else if (segment->isTextLeaf()) {
		return LogicalTextRectForSegment(*segment);
	} else if (IsDisplayMathSegment(*segment)) {
		return segment->block ? segment->block->formulaRect : QRect();
	}
	return QRect();
}

QRect MarkdownArticle::Impl::segmentRect(int segmentIndex) const {
	const auto segment = FindSegment(&_segments, segmentIndex);
	if (!segment) {
		return QRect();
	} else if (segment->isTextLeaf()) {
		return segment->textRect;
	} else if (IsDisplayMathSegment(*segment)) {
		if (!segment->outerRect.isEmpty()) {
			return segment->outerRect;
		}
		return segment->block ? segment->block->formulaRect : QRect();
	}
	return QRect();
}

QRect MarkdownArticle::Impl::displayMathEditRect(int segmentIndex) const {
	const auto segment = FindSegment(&_segments, segmentIndex);
	if (!segment || !IsDisplayMathSegment(*segment) || !segment->block) {
		return QRect();
	}
	const auto &block = *segment->block;
	if (!block.contentRect.isEmpty()) {
		return block.contentRect;
	} else if (!block.outer.isEmpty()) {
		return block.outer;
	}
	return block.formulaRect;
}

QRect MarkdownArticle::Impl::displayMathBlockRect(int segmentIndex) const {
	const auto segment = FindSegment(&_segments, segmentIndex);
	if (!segment || !IsDisplayMathSegment(*segment) || !segment->block) {
		return QRect();
	}
	const auto &block = *segment->block;
	if (!block.outer.isEmpty()) {
		return block.outer;
	}
	return displayMathEditRect(segmentIndex);
}

const LaidOutBlock *MarkdownArticle::Impl::pullquoteBlockForEditableLeaf(
		const PreparedEditLeafSource &source) const {
	const auto find = [&](const auto &self,
			const std::vector<LaidOutBlock> &blocks,
			const LaidOutBlock *activePullquote) -> const LaidOutBlock * {
		for (const auto &block : blocks) {
			const auto nextPullquote = (block.kind == PreparedBlockKind::Quote)
				&& block.pullquote
				? &block
				: activePullquote;
			if (block.editLeaf && (*block.editLeaf == source) && nextPullquote) {
				return nextPullquote;
			}
			if (const auto nested = self(self, block.children, nextPullquote)) {
				return nested;
			}
		}
		return nullptr;
	};
	return find(find, _blocks, nullptr);
}

int MarkdownArticle::Impl::pullquoteAvailableTextWidthForEditableLeaf(
		const PreparedEditLeafSource &source) const {
	const auto block = pullquoteBlockForEditableLeaf(source);
	if (block) {
		return block->textWidth;
	}
	return 0;
}

MarkdownArticleTextLeafStyle MarkdownArticle::Impl::textLeafStyleForSegment(
		int segmentIndex) const {
	const auto segment = FindSegment(&_segments, segmentIndex);
	if (!segment || !segment->isTextLeaf()) {
		return {};
	}
	const auto &st = layoutStyle();
	const auto &textStyle = TextStyleForSegment(*segment, st);
	return {
		.textStyle = &textStyle,
		.textColor = TextColorForSegment(*segment, st),
		.markBg = MarkBgColorForStyle(st),
		.lineHeight = TextLineHeight(textStyle),
		.align = segment->align,
		.italic = segment->block
			&& segment->block->pullquote
			&& !segment->block->quoteAuthor,
	};
}

MarkdownArticleTextLeafStyle MarkdownArticle::Impl::editableStyleForSegment(
		int segmentIndex) const {
	const auto segment = FindSegment(&_segments, segmentIndex);
	if (!segment) {
		return {};
	} else if (segment->isTextLeaf()) {
		return textLeafStyleForSegment(segmentIndex);
	} else if (!IsDisplayMathSegment(*segment)) {
		return {};
	}
	const auto &st = layoutStyle();
	return {
		.textStyle = &st.displayMath.fallbackStyle,
		.textColor = st.displayMath.fg,
		.markBg = MarkBgColorForStyle(st),
		.lineHeight = TextLineHeight(st.displayMath.fallbackStyle),
		.align = ::style::al_center,
	};
}

int MarkdownArticle::Impl::selectionOffsetFromHit(
		const MarkdownArticleHitTestResult &result,
		TextSelectType selectionType) const {
	const auto segment = FindSegment(&_segments, result.segmentIndex);
	if (!segment) {
		return 0;
	}
	if (result.forcedOffset >= 0) {
		return std::clamp(result.forcedOffset, 0, SegmentLength(*segment));
	}
	auto offset = int(result.state.symbol);
	if (selectionType == TextSelectType::Letters
		&& result.state.afterSymbol) {
		++offset;
	}
	return std::clamp(offset, 0, SegmentLength(*segment));
}

TextSelection MarkdownArticle::Impl::adjustSelection(
		int segmentIndex,
		TextSelection selection,
		TextSelectType selectionType) const {
	const auto segment = FindSegment(&_segments, segmentIndex);
	if (!segment || !segment->isTextLeaf()) {
		return selection;
	}
	return segment->leaf->adjustSelection(selection, selectionType);
}

bool MarkdownArticle::Impl::selectionContains(
		MarkdownArticleSelection selection,
		const MarkdownArticleSelectionEndpoints *endpoints,
		const MarkdownArticleHitTestResult &result) const {
	if (result.codeHeaderCopy) {
		return false;
	}
	const auto segment = FindSegment(&_segments, result.segmentIndex);
	if (!segment || selection.empty() || !result.valid()) {
		return false;
	}
	const auto selectionState = PaintSelectionState{
		.segments = &_segments,
		.selection = selection,
		.endpoints = endpoints,
	};
	if (segment->tableSegmentIndex >= 0
		&& TableSegmentSelected(selectionState, segment->tableSegmentIndex)) {
		return true;
	}
	if (!segment->isTextLeaf()) {
		return WholeSegmentSelected(*segment, selectionState);
	}
	const auto textSelection = TextSelectionForSegment(*segment, selectionState);
	if (!textSelection || textSelection->empty()) {
		return false;
	}
	const auto offset = selectionOffsetFromHit(result, TextSelectType::Letters);
	return (offset >= textSelection->from) && (offset < textSelection->to);
}

TextForMimeData MarkdownArticle::Impl::textForContext(
		const MarkdownArticleHitTestResult &result) const {
	if (!result.valid() || !result.direct) {
		return TextForMimeData();
	}
	const auto segment = FindSegment(&_segments, result.segmentIndex);
	return segment ? TextForSegment(*segment) : TextForMimeData();
}

TextForMimeData MarkdownArticle::Impl::textForSelection(
		MarkdownArticleSelection selection,
		const MarkdownArticleSelectionEndpoints *endpoints,
		const PreparedEditSelection *structuralSelection) const {
	return TextForSelectedSegments(
		_segments,
		selection,
		endpoints,
		structuralSelection);
}

std::vector<RichPage::Block> MarkdownArticle::Impl::richPageSliceForSelection(
		MarkdownArticleSelection selection) const {
	if (!_content.richPage) {
		return {};
	}
	return RichPageBlocksForSelectedSegments(
		*_content.richPage,
		_segments,
		selection);
}

bool MarkdownArticle::Impl::highlightProcessDone(
		Spellchecker::HighlightProcessId processId) {
	const auto i = _pendingHighlightEntries.find(processId);
	if (i == end(_pendingHighlightEntries)) {
		return false;
	}
	auto entry = std::move(i->second);
	_pendingHighlightEntries.erase(i);
	_pendingHighlightProcesses.erase(entry.key);

	auto rebuilt = false;
	for (const auto block : entry.blocks) {
		RepopulateCodeBlockLeaf(
			*block,
			&_content.formulas,
			_inlineFormulaObjects.get(),
			_content.mediaRuntime,
			layoutStyle(),
			true,
			this,
			_textRepaint,
			_textRepaintRect,
			_textSpoilerLinkFilter);
		registerPendingHighlightBlock(*block);
		rebuilt = true;
	}
	return rebuilt;
}

void MarkdownArticle::Impl::invalidatePaletteCache() {
	InvalidateInlineFormulaPaletteCache(_inlineFormulaObjects);
	ClearColorizedFormulaImages(&_blocks);
}

void MarkdownArticle::Impl::invalidateRasterCache() {
	resetFormulaRasterCache();
	InvalidateInlineFormulaRasterCache(_inlineFormulaObjects);
	ClearColorizedFormulaImages(&_blocks);
}

bool MarkdownArticle::Impl::hasHeavyPart() const {
	for (const auto &entry : _mediaBlocks) {
		const auto &block = entry.second;
		if (block && block->hasHeavyPart()) {
			return true;
		}
	}
	return false;
}

void MarkdownArticle::Impl::unloadHeavyPart() {
	for (const auto &entry : _mediaBlocks) {
		if (const auto &block = entry.second) {
			block->unloadHeavyPart();
		}
	}
}

void MarkdownArticle::Impl::hideSpoilers() {
	HideBlocksSpoilers(&_blocks);
	HideBlocksSpoilers(&_retainedBlocks);
	for (auto &entry : _cachedTextLeafs.entries) {
		HideLeafSpoilers(&entry.second.leaf);
	}
	for (const auto &entry : _mediaBlocks) {
		if (const auto &block = entry.second) {
			block->hideSpoilers();
		}
	}
}

MediaBlockHost *MarkdownArticle::Impl::mediaBlockHost() const {
	return _mediaBlockHost;
}

void MarkdownArticle::Impl::setPlaceholderLoading(
		PreparedPlaceholderBlockId id) {
	setPlaceholderLoadingValue(id, true);
}

void MarkdownArticle::Impl::clearPlaceholderLoading(
		PreparedPlaceholderBlockId id) {
	setPlaceholderLoadingValue(id, false);
}

void MarkdownArticle::Impl::clearAllPlaceholderLoading() {
	auto repaintIds = std::vector<PreparedPlaceholderBlockId>();
	repaintIds.reserve(_placeholderRuntimes.size());
	for (const auto &[value, runtime] : _placeholderRuntimes) {
		if (!runtime || !runtime->loading) {
			continue;
		}
		runtime->loading = false;
		runtime->loadingAnimation.stop(anim::type::instant);
		repaintIds.push_back({ .value = value });
	}
	for (const auto id : repaintIds) {
		requestPlaceholderRepaint(id);
	}
}

void MarkdownArticle::Impl::addTaskMarkerRipple(
		const PreparedEditListItemSource &source,
		QPoint point) {
	auto block = FindListItemBlock(&_blocks, source);
	if (!block
		|| block->taskState == TaskState::None
		|| block->markerRect.isEmpty()) {
		return;
	}
	auto runtime = block->taskMarkerRippleRuntime
		? block->taskMarkerRippleRuntime
		: getOrCreateTaskMarkerRippleRuntime(source);
	if (!runtime) {
		return;
	}
	block->taskMarkerRippleRuntime = runtime;
	auto view = Ui::CheckView(
		layoutStyle().list.taskCheck,
		block->taskState == TaskState::Checked);
	const auto mask = view.prepareRippleMask();
	const auto size = mask.size();
	if (!runtime->ripple || runtime->rippleSize != size) {
		runtime->ripple = std::make_unique<Ui::RippleAnimation>(
			st::defaultCheckbox.ripple,
			mask,
			[repaint = runtime->repaint] {
				if (repaint) {
					repaint();
				}
			});
		runtime->rippleSize = size;
	}
	point -= (block->markerRect.topLeft()
		+ st::defaultCheckbox.rippleAreaPosition);
	point.setX(std::clamp(point.x(), 0, std::max(size.width() - 1, 0)));
	point.setY(std::clamp(point.y(), 0, std::max(size.height() - 1, 0)));
	runtime->ripple->add(point);
	runtime->ripple->lastStop();
	if (runtime->repaint) {
		runtime->repaint();
	}
}

void MarkdownArticle::Impl::addPlaceholderRipple(
		PreparedPlaceholderBlockId id,
		QPoint point) {
	const auto block = FindPlaceholderBlock(&_blocks, id);
	if (!block) {
		return;
	}
	auto runtime = block->placeholderRuntime
		? block->placeholderRuntime
		: getOrCreatePlaceholderRuntime(id);
	if (!runtime) {
		return;
	}
	block->placeholderRuntime = runtime;
	const auto size = block->mediaRect.size();
	if (!runtime->ripple || runtime->rippleSize != size) {
		runtime->ripple = std::make_unique<Ui::RippleAnimation>(
			st::defaultRippleAnimation,
			Ui::RippleAnimation::RoundRectMask(
				size,
				layoutStyle().placeholder.radius),
			[=] {
				requestPlaceholderRepaint(id);
			});
		runtime->rippleSize = size;
	}
	point.setX(std::clamp(point.x(), 0, std::max(size.width() - 1, 0)));
	point.setY(std::clamp(point.y(), 0, std::max(size.height() - 1, 0)));
	runtime->ripple->add(point);
	requestPlaceholderRepaint(id);
}

void MarkdownArticle::Impl::stopPlaceholderRipple(
		PreparedPlaceholderBlockId id) {
	if (!id) {
		return;
	}
	const auto i = _placeholderRuntimes.find(id.value);
	if (i == end(_placeholderRuntimes)
		|| !i->second
		|| !i->second->ripple) {
		return;
	}
	i->second->ripple->lastStop();
	requestPlaceholderRepaint(id);
}

void MarkdownArticle::Impl::invalidateLayout() {
	invalidateLayout(true);
}

void MarkdownArticle::Impl::invalidateGeometry() {
	_width = -1;
	_laidOutWidth = 0;
	_height = 0;
	captureScrollState();
	clearPendingHighlightBlockPointers();
	_anchors.clear();
	_segments.clear();
	_visibleSegmentSpan = {};
	_segmentTops.clear();
	_segmentBottoms.clear();
}

void MarkdownArticle::Impl::invalidateLayout(bool harvestCurrentBlocks) {
	invalidateGeometry();
	if (harvestCurrentBlocks) {
		RebuildCachedTextLeafs(
			_content.blocks.blocks,
			&_blocks,
			layoutStyle(),
			&_cachedTextLeafs);
	}
	retainBlocks();
}

void MarkdownArticle::Impl::retainBlocks() {
	if (_blocks.empty()) {
		_blocksPainted = false;
		return;
	}
	if (_blocksPainted) {
		_retainedBlocks = std::move(_blocks);
	}
	_blocks.clear();
	_blocksPainted = false;
}

int MarkdownArticle::Impl::currentDevicePixelRatio() const {
	return std::max(style::DevicePixelRatio(), 1);
}

void MarkdownArticle::Impl::rebuildVisibleSegmentLookup() {
	RebuildVisibleSegmentLookup(
		_segments,
		&_segmentTops,
		&_segmentBottoms);
	refreshVisibleSegmentSpan();
}

void MarkdownArticle::Impl::refreshVisibleSegmentSpan() {
	_visibleSegmentSpan = _visibleRange
		? LookupVisibleSegmentSpan(
			_segmentTops,
			_segmentBottoms,
			*_visibleRange)
		: SegmentSpan();
}

void MarkdownArticle::Impl::clearMediaBlocks() {
	ClearMediaBlockStorage(&_mediaBlocks);
}

void MarkdownArticle::Impl::clearPlaceholderRuntimes() {
	_placeholderRuntimes.clear();
}

void MarkdownArticle::Impl::refreshMediaBlockHosts() {
	for (const auto &[id, block] : _mediaBlocks) {
		if (block) {
			block->setLayoutStyle(layoutStyle());
			block->setHost(_mediaBlockHost);
		}
	}
}

auto MarkdownArticle::Impl::getOrCreateTaskMarkerRippleRuntime(
		const PreparedEditListItemSource &source)
-> std::shared_ptr<TaskMarkerRippleRuntime> {
	if (const auto i = _taskMarkerRippleRuntimes.find(source);
		i != end(_taskMarkerRippleRuntimes)) {
		return i->second;
	}
	auto runtime = std::make_shared<TaskMarkerRippleRuntime>([=] {
		requestTaskMarkerRepaint(source);
	});
	_taskMarkerRippleRuntimes.emplace(source, runtime);
	return runtime;
}

std::shared_ptr<PlaceholderBlockRuntime>
MarkdownArticle::Impl::getOrCreatePlaceholderRuntime(
		PreparedPlaceholderBlockId id) {
	if (!id) {
		return nullptr;
	}
	if (const auto i = _placeholderRuntimes.find(id.value);
		i != end(_placeholderRuntimes)) {
		return i->second;
	}
	auto runtime = std::make_shared<PlaceholderBlockRuntime>([=] {
		requestPlaceholderRepaint(id);
	});
	_placeholderRuntimes.emplace(id.value, runtime);
	return runtime;
}

void MarkdownArticle::Impl::pruneTaskMarkerRuntimes() {
	auto live = TaskMarkerSourceSet();
	CollectTaskMarkerSources(_blocks, &live);
	for (auto i = _taskMarkerRippleRuntimes.begin();
		i != _taskMarkerRippleRuntimes.end();) {
		if (live.find(i->first) != end(live)) {
			++i;
		} else {
			i = _taskMarkerRippleRuntimes.erase(i);
		}
	}
}

void MarkdownArticle::Impl::prunePlaceholderRuntimes() {
	auto live = std::unordered_set<uint64>();
	CollectPlaceholderIds(_blocks, &live);
	for (auto i = _placeholderRuntimes.begin(); i != _placeholderRuntimes.end();) {
		if (live.find(i->first) != end(live)) {
			++i;
		} else {
			i = _placeholderRuntimes.erase(i);
		}
	}
}

void MarkdownArticle::Impl::requestTaskMarkerRepaint(
		const PreparedEditListItemSource &source) {
	if (const auto block = FindListItemBlock(_blocks, source)) {
		const auto rect = TaskMarkerRippleRect(*block, layoutStyle());
		if (_textRepaintRect && !rect.isEmpty()) {
			_textRepaintRect(rect);
		} else if (_textRepaint) {
			_textRepaint();
		}
	} else if (_textRepaint) {
		_textRepaint();
	}
}

void MarkdownArticle::Impl::requestPlaceholderRepaint(
		PreparedPlaceholderBlockId id) {
	if (const auto block = FindPlaceholderBlock(_blocks, id)) {
		if (_textRepaintRect) {
			_textRepaintRect(block->mediaRect);
		} else if (_textRepaint) {
			_textRepaint();
		}
	} else if (_textRepaint) {
		_textRepaint();
	}
}

std::shared_ptr<MediaBlock> MarkdownArticle::Impl::getOrCreateMediaBlock(
		const PreparedBlock &prepared) {
	switch (prepared.kind) {
	case PreparedBlockKind::Photo:
		return getOrCreateMediaBlock(
			prepared.photo.id,
			[=] {
				return CreatePhotoMediaBlock(
					prepared.photo,
					_content.mediaRuntime,
					layoutStyle());
			});
	case PreparedBlockKind::Video:
		return getOrCreateMediaBlock(
			prepared.video.id,
			[=] {
				return CreateVideoMediaBlock(
					prepared.video,
					_content.mediaRuntime,
					layoutStyle());
			});
	case PreparedBlockKind::Map:
		return getOrCreateMediaBlock(
			prepared.map.id,
			[=] {
				return CreateMapMediaBlock(
					prepared.map,
					_content.mediaRuntime,
					layoutStyle());
			});
	case PreparedBlockKind::Audio:
		return getOrCreateMediaBlock(
			prepared.audio.id,
			[=] {
				return CreateAudioMediaBlock(
					prepared.audio,
					_content.mediaRuntime,
					layoutStyle());
			});
	case PreparedBlockKind::Channel:
		return getOrCreateMediaBlock(
			prepared.channel.id,
			[=] {
				return CreateChannelMediaBlock(
					prepared.channel,
					_content.mediaRuntime,
					layoutStyle());
			});
	case PreparedBlockKind::GroupedMedia:
		return getOrCreateMediaBlock(
			prepared.groupedMedia.id,
			[=] {
				return CreateGroupedMediaBlock(
					prepared.groupedMedia,
					_content.mediaRuntime,
					layoutStyle());
			});
	default:
		return nullptr;
	}
}

template <typename Factory>
std::shared_ptr<MediaBlock> MarkdownArticle::Impl::getOrCreateMediaBlock(
		PreparedMediaBlockId id,
		Factory &&factory) {
	if (!id) {
		return nullptr;
	}
	if (const auto i = _mediaBlocks.find(id.value);
		i != end(_mediaBlocks)) {
		if (i->second && !i->second->alive()) {
			i->second->setHost(nullptr);
			_mediaBlocks.erase(i);
		} else {
			if (i->second) {
				i->second->setLayoutStyle(layoutStyle());
				i->second->setHost(_mediaBlockHost);
			}
			return i->second;
		}
	}
	auto block = factory();
	if (block) {
		block->setLayoutStyle(layoutStyle());
		block->setHost(_mediaBlockHost);
		_mediaBlocks.emplace(id.value, block);
	}
	return block;
}

Spellchecker::HighlightProcessId MarkdownArticle::Impl::tryHighlightSyntax(
		const QString &displayText,
		const QString &language,
		TextWithEntities &marked) {
	const auto key = PendingHighlightKey{
		.text = displayText,
		.language = language,
	};
	if (const auto i = _pendingHighlightProcesses.find(key);
		i != end(_pendingHighlightProcesses)) {
		return i->second;
	}
	const auto processId = Spellchecker::TryHighlightSyntax(marked);
	if (processId) {
		registerPendingHighlightProcess(key, processId);
	}
	return processId;
}

SegmentSpan MarkdownArticle::Impl::candidateSegmentSpan(QPoint point) const {
	if (_visibleRange
		&& (_visibleRange->top <= point.y())
		&& (point.y() < _visibleRange->bottom)) {
		return _visibleSegmentSpan.empty()
			? FullSegmentSpan(_segments)
			: _visibleSegmentSpan;
	}
	return FullSegmentSpan(_segments);
}

void MarkdownArticle::Impl::clearPendingHighlightBlockPointers() {
	for (auto &entry : _pendingHighlightEntries) {
		entry.second.blocks.clear();
	}
}

void MarkdownArticle::Impl::prunePendingHighlightProcessesForContent() {
	if (_pendingHighlightProcesses.empty()) {
		return;
	}
	auto live = std::unordered_set<
		PendingHighlightKey,
		PendingHighlightKeyHasher>();
	CollectCodeBlockHighlightKeys(_content.blocks.blocks, &live);
	for (auto i = _pendingHighlightProcesses.begin();
			i != end(_pendingHighlightProcesses);) {
		if (live.contains(i->first)) {
			++i;
			continue;
		}
		_pendingHighlightEntries.erase(i->second);
		i = _pendingHighlightProcesses.erase(i);
	}
}

void MarkdownArticle::Impl::registerPendingHighlightProcess(
		const PendingHighlightKey &key,
		Spellchecker::HighlightProcessId processId) {
	_pendingHighlightProcesses[key] = processId;
	auto &entry = _pendingHighlightEntries[processId];
	entry.key = key;
}

void MarkdownArticle::Impl::registerPendingHighlightBlock(LaidOutBlock &block) {
	if (!block.syntaxHighlightProcessId) {
		return;
	}
	if (!_pendingHighlightEntries.contains(block.syntaxHighlightProcessId)) {
		registerPendingHighlightProcess(
			PendingHighlightKeyForBlock(block),
			block.syntaxHighlightProcessId);
	}
	_pendingHighlightEntries[block.syntaxHighlightProcessId].blocks.push_back(
		&block);
}

void MarkdownArticle::Impl::registerPendingHighlightBlocks(std::vector<LaidOutBlock> &blocks) {
	for (auto &block : blocks) {
		registerPendingHighlightBlock(block);
		registerPendingHighlightBlocks(block.children);
	}
}

void MarkdownArticle::Impl::resetFormulaRasterCache() {
	_formulaRenders.clear();
	_formulaRenders.resize(_content.formulas.size());
}

void MarkdownArticle::Impl::setPlaceholderLoadingValue(
		PreparedPlaceholderBlockId id,
		bool loading) {
	if (!id) {
		return;
	}
	const auto runtime = loading
		? getOrCreatePlaceholderRuntime(id)
		: [&]() -> std::shared_ptr<PlaceholderBlockRuntime> {
			if (const auto i = _placeholderRuntimes.find(id.value);
				i != end(_placeholderRuntimes)) {
				return i->second;
			}
			return nullptr;
		}();
	if (!runtime || runtime->loading == loading) {
		return;
	}
	runtime->loading = loading;
	if (loading) {
		runtime->loadingAnimation.start();
	} else {
		runtime->loadingAnimation.stop(anim::type::instant);
	}
	requestPlaceholderRepaint(id);
}

const style::Markdown &MarkdownArticle::Impl::layoutStyle() const {
	return _style;
}

MarkdownArticleScrollOwnerIdentity MarkdownArticle::Impl::scrollOwnerIdentity(
		const LaidOutBlock &block,
		const std::vector<int> &preparedPath) const {
	if (block.editBlock && ValidBlockPath(block.editBlock->path)) {
		return { .blockPath = block.editBlock->path };
	}
	return { .preparedPath = preparedPath };
}

MarkdownArticleHorizontalScrollLookup
MarkdownArticle::Impl::findHorizontalScrollOwner(QPoint point) const {
	auto preparedPath = std::vector<int>();
	return findHorizontalScrollOwner(
		_blocks,
		&_content.blocks.blocks,
		point,
		&preparedPath);
}

MarkdownArticleHorizontalScrollLookup
MarkdownArticle::Impl::findHorizontalScrollOwner(
		const std::vector<LaidOutBlock> &blocks,
		const std::vector<PreparedBlock> *prepared,
		QPoint point,
		std::vector<int> *preparedPath) const {
	for (auto i = 0, count = int(blocks.size()); i != count; ++i) {
		const auto &block = blocks[i];
		preparedPath->push_back(i);
		const auto preparedBlock = (prepared && (i < prepared->size()))
			? &(*prepared)[i]
			: nullptr;
		if (const auto result = findHorizontalScrollOwner(
				block.children,
				preparedBlock ? &preparedBlock->children : nullptr,
				point,
				preparedPath);
			result.block) {
			return result;
		}
		if (block.horizontalScrollMax > 0
			&& !block.scrollViewportRect.isEmpty()) {
			const auto identity = scrollOwnerIdentity(block, *preparedPath);
			auto hit = MarkdownArticleHorizontalScrollHit{
				.scrollable = true,
				.overViewport = ContainsPoint(block.scrollViewportRect, point),
				.overScrollbar = ContainsPoint(
					block.scrollScrollbarTrackRect,
					point),
				.overScrollbarThumb = ContainsPoint(
					block.scrollScrollbarThumbRect,
					point),
			};
			if (hit.overViewport || hit.overScrollbar) {
				return {
					.hit = hit,
					.identity = identity,
					.block = &block,
				};
			}
		}
		preparedPath->pop_back();
	}
	return {};
}

std::optional<MarkdownArticleHorizontalScrollLookup>
MarkdownArticle::Impl::findHorizontalScrollOwner(int segmentIndex) const {
	const auto segment = FindSegment(&_segments, segmentIndex);
	if (!segment || !IsEditableSegment(*segment)) {
		return std::nullopt;
	}
	auto preparedPath = std::vector<int>();
	return findHorizontalScrollOwner(
		_blocks,
		&_content.blocks.blocks,
		*segment,
		std::nullopt,
		&preparedPath);
}

std::optional<MarkdownArticleHorizontalScrollLookup>
MarkdownArticle::Impl::findHorizontalScrollOwner(
		const std::vector<LaidOutBlock> &blocks,
		const std::vector<PreparedBlock> *prepared,
		const SelectableSegment &segment,
		std::optional<MarkdownArticleHorizontalScrollLookup> owner,
		std::vector<int> *preparedPath) const {
	for (auto i = 0, count = int(blocks.size()); i != count; ++i) {
		const auto &block = blocks[i];
		preparedPath->push_back(i);
		const auto preparedBlock = (prepared && (i < prepared->size()))
			? &(*prepared)[i]
			: nullptr;
		auto nextOwner = owner;
		if (!block.scrollViewportRect.isEmpty()) {
			nextOwner = MarkdownArticleHorizontalScrollLookup{
				.identity = scrollOwnerIdentity(block, *preparedPath),
				.block = &block,
			};
		}
		const auto matchesCell = [&] {
			if (!segment.cell) {
				return false;
			}
			for (const auto &row : block.tableRows) {
				for (const auto &cell : row.cells) {
					if (segment.cell == &cell) {
						return true;
					}
				}
			}
			return false;
		}();
		if (segment.block == &block || matchesCell) {
			return nextOwner;
		}
		if (const auto result = findHorizontalScrollOwner(
				block.children,
				preparedBlock ? &preparedBlock->children : nullptr,
				segment,
				nextOwner,
				preparedPath)) {
			return result;
		}
		preparedPath->pop_back();
	}
	return std::nullopt;
}

LaidOutBlock *MarkdownArticle::Impl::findScrollOwnerByIdentity(
		const MarkdownArticleScrollOwnerIdentity &identity) {
	auto preparedPath = std::vector<int>();
	return findScrollOwnerByIdentity(
		_blocks,
		&_content.blocks.blocks,
		identity,
		&preparedPath);
}

LaidOutBlock *MarkdownArticle::Impl::findScrollOwnerByIdentity(
		std::vector<LaidOutBlock> &blocks,
		const std::vector<PreparedBlock> *prepared,
		const MarkdownArticleScrollOwnerIdentity &identity,
		std::vector<int> *preparedPath) {
	for (auto i = 0, count = int(blocks.size()); i != count; ++i) {
		auto &block = blocks[i];
		preparedPath->push_back(i);
		const auto preparedBlock = (prepared && (i < prepared->size()))
			? &(*prepared)[i]
			: nullptr;
		if (!block.scrollViewportRect.isEmpty()) {
			const auto current = scrollOwnerIdentity(block, *preparedPath);
			if (current == identity) {
				return &block;
			}
		}
		if (const auto child = findScrollOwnerByIdentity(
				block.children,
				preparedBlock ? &preparedBlock->children : nullptr,
				identity,
				preparedPath)) {
			return child;
		}
		preparedPath->pop_back();
	}
	return nullptr;
}

void MarkdownArticle::Impl::captureScrollState() {
	auto preparedPath = std::vector<int>();
	captureScrollState(
		_blocks,
		&_content.blocks.blocks,
		&preparedPath);
}

void MarkdownArticle::Impl::captureScrollState(
		const std::vector<LaidOutBlock> &blocks,
		const std::vector<PreparedBlock> *prepared,
		std::vector<int> *preparedPath) {
	for (auto i = 0, count = int(blocks.size()); i != count; ++i) {
		const auto &block = blocks[i];
		preparedPath->push_back(i);
		const auto preparedBlock = (prepared && (i < prepared->size()))
			? &(*prepared)[i]
			: nullptr;
		if (!block.scrollViewportRect.isEmpty()) {
			const auto identity = scrollOwnerIdentity(block, *preparedPath);
			if (block.horizontalScrollLeft > 0) {
				_capturedScrollLefts[identity] = block.horizontalScrollLeft;
			} else {
				_capturedScrollLefts.erase(identity);
			}
		}
		captureScrollState(
			block.children,
			preparedBlock ? &preparedBlock->children : nullptr,
			preparedPath);
		preparedPath->pop_back();
	}
}

void MarkdownArticle::Impl::restoreScrollState() {
	auto preparedPath = std::vector<int>();
	restoreScrollState(
		_blocks,
		&_content.blocks.blocks,
		&preparedPath);
}

void MarkdownArticle::Impl::restoreScrollState(
		std::vector<LaidOutBlock> &blocks,
		const std::vector<PreparedBlock> *prepared,
		std::vector<int> *preparedPath) {
	for (auto i = 0, count = int(blocks.size()); i != count; ++i) {
		auto &block = blocks[i];
		preparedPath->push_back(i);
		const auto preparedBlock = (prepared && (i < prepared->size()))
			? &(*prepared)[i]
			: nullptr;
		if (!block.scrollViewportRect.isEmpty()) {
			const auto identity = scrollOwnerIdentity(block, *preparedPath);
			const auto j = _capturedScrollLefts.find(identity);
			block.horizontalScrollLeft = (j != end(_capturedScrollLefts))
				? std::clamp(j->second, 0, block.horizontalScrollMax)
				: 0;
		}
		restoreScrollState(
			block.children,
			preparedBlock ? &preparedBlock->children : nullptr,
			preparedPath);
		preparedPath->pop_back();
	}
}

void MarkdownArticle::Impl::refreshScrolledGeometry(
		LaidOutBlock &block,
		const ActiveHorizontalScrollOwnerState *activeOwner) {
	RestoreLogicalBlockGeometry(&block);
	if (activeOwner) {
		ApplyTranslatedDescendantGeometry(&block, *activeOwner);
	}

	std::optional<ActiveHorizontalScrollOwnerState> nextOwner;
	if (!activeOwner && !block.scrollViewportRect.isEmpty()) {
		block.horizontalScrollLeft = std::clamp(
			block.horizontalScrollLeft,
			0,
			block.horizontalScrollMax);
		if (block.horizontalScrollMax > 0) {
			const auto owner = ActiveHorizontalScrollOwnerState{
				.viewport = block.scrollViewportRect,
				.shift = -block.horizontalScrollLeft,
			};
			if (ScrollOwnerMovesOwnContent(block.kind)) {
				ApplyOwnerContentGeometry(&block, owner);
			}
			if (ScrollOwnerMovesChildren(block.kind)) {
				nextOwner = owner;
			}
		}
		updateScrollbarThumb(block);
	}
	refreshScrolledGeometry(
		block.children,
		nextOwner ? &*nextOwner : activeOwner);
}

void MarkdownArticle::Impl::refreshScrolledGeometry(LaidOutBlock &block) {
	refreshScrolledGeometry(block, nullptr);
}

void MarkdownArticle::Impl::refreshScrolledGeometry(
		std::vector<LaidOutBlock> &blocks) {
	refreshScrolledGeometry(blocks, nullptr);
}

void MarkdownArticle::Impl::refreshScrolledGeometry(
		std::vector<LaidOutBlock> &blocks,
		const ActiveHorizontalScrollOwnerState *activeOwner) {
	for (auto &block : blocks) {
		refreshScrolledGeometry(block, activeOwner);
	}
}

void MarkdownArticle::Impl::updateScrollbarThumb(LaidOutBlock &block) {
	if (block.horizontalScrollMax <= 0
		|| block.scrollScrollbarTrackRect.isEmpty()) {
		block.scrollScrollbarThumbRect = QRect();
		block.tableScrollbarThumbRect = QRect();
		return;
	}
	const auto trackWidth = block.scrollScrollbarTrackRect.width();
	if (trackWidth <= 0) {
		block.scrollScrollbarThumbRect = QRect();
		block.tableScrollbarThumbRect = QRect();
		return;
	}
	const auto viewportWidth = std::max(block.scrollViewportRect.width(), 1);
	const auto contentWidth = std::max(block.scrollLogicalContentRect.width(), 1);
	auto thumbWidth = (trackWidth * viewportWidth + (contentWidth / 2))
		/ contentWidth;
	thumbWidth = std::clamp(
		thumbWidth,
		std::min(layoutStyle().table.scrollbarMinThumbWidth, trackWidth),
		trackWidth);
	const auto available = std::max(trackWidth - thumbWidth, 0);
	const auto thumbOffset = (available > 0)
		? ((block.horizontalScrollLeft * available)
			+ (block.horizontalScrollMax / 2))
			/ block.horizontalScrollMax
		: 0;
	block.scrollScrollbarThumbRect = QRect(
		block.scrollScrollbarTrackRect.x() + thumbOffset,
		block.scrollScrollbarTrackRect.y(),
		thumbWidth,
		block.scrollScrollbarTrackRect.height());
	if (block.kind == PreparedBlockKind::Table) {
		block.tableScrollbarThumbRect = block.scrollScrollbarThumbRect;
	}
}

bool MarkdownArticle::Impl::setScrollLeft(
		LaidOutBlock &block,
		const MarkdownArticleScrollOwnerIdentity &identity,
		int left) {
	left = std::clamp(left, 0, block.horizontalScrollMax);
	if (block.horizontalScrollLeft == left) {
		return false;
	}
	block.horizontalScrollLeft = left;
	if (left > 0) {
		_capturedScrollLefts[identity] = left;
	} else {
		_capturedScrollLefts.erase(identity);
	}
	refreshScrolledGeometry(block);
	RefreshScrollableSegmentRects(block, &_segments);
	if (_textRepaintRect) {
		_textRepaintRect(block.outer);
	} else if (_textRepaint) {
		_textRepaint();
	}
	return true;
}

bool MarkdownArticle::Impl::revealSegment(int segmentIndex) {
	const auto logicalRect = logicalSegmentRect(segmentIndex);
	if (logicalRect.isEmpty()) {
		return false;
	}
	const auto lookup = findHorizontalScrollOwner(segmentIndex);
	if (!lookup || !lookup->block) {
		return false;
	}
	const auto &owner = *lookup->block;
	if (owner.scrollViewportRect.isEmpty()
		|| owner.scrollLogicalContentRect.isEmpty()) {
		return false;
	}
	const auto viewportWidth = owner.scrollViewportRect.width();
	if (viewportWidth <= 0) {
		return false;
	}
	const auto toLeft = logicalRect.x()
		+ owner.horizontalScrollLeft
		- owner.scrollLogicalContentRect.x();
	const auto left = ComputeScrollTo(
		toLeft,
		toLeft + logicalRect.width(),
		0,
		owner.horizontalScrollMax,
		owner.horizontalScrollLeft,
		viewportWidth);
	if (left == owner.horizontalScrollLeft) {
		return false;
	}
	if (const auto block = findScrollOwnerByIdentity(lookup->identity)) {
		return setScrollLeft(*block, lookup->identity, left);
	}
	return false;
}

MarkdownArticleHorizontalScrollHit MarkdownArticle::Impl::horizontalScrollHit(
		QPoint point) const {
	return findHorizontalScrollOwner(point).hit;
}

bool MarkdownArticle::Impl::canConsumeHorizontalScroll(
		QPoint point,
		int delta) const {
	if (const auto lookup = findHorizontalScrollOwner(point);
		lookup.block) {
		const auto left = std::clamp(
			lookup.block->horizontalScrollLeft - delta,
			0,
			lookup.block->horizontalScrollMax);
		return (left != lookup.block->horizontalScrollLeft);
	}
	return false;
}

bool MarkdownArticle::Impl::consumeHorizontalScroll(QPoint point, int delta) {
	if (const auto lookup = findHorizontalScrollOwner(point);
		lookup.block) {
		if (const auto block = findScrollOwnerByIdentity(lookup.identity)) {
			return setScrollLeft(
				*block,
				lookup.identity,
				block->horizontalScrollLeft - delta);
		}
	}
	return false;
}

bool MarkdownArticle::Impl::beginHorizontalScroll(
		QPoint point,
		bool fromTouch) {
	const auto lookup = findHorizontalScrollOwner(point);
	if (!lookup.block) {
		return false;
	}
	if (fromTouch) {
		if (!lookup.hit.overViewport) {
			return false;
		}
		_activeHorizontalScrollDrag = ActiveHorizontalScrollDrag{
			.owner = lookup.identity,
			.pressPoint = point,
			.startScrollLeft = lookup.block->horizontalScrollLeft,
			.fromTouch = true,
		};
		return true;
	}
	if (!lookup.hit.overScrollbar) {
		return false;
	}
	const auto &thumb = lookup.block->scrollScrollbarThumbRect;
	_activeHorizontalScrollDrag = ActiveHorizontalScrollDrag{
		.owner = lookup.identity,
		.pressPoint = point,
		.startScrollLeft = lookup.block->horizontalScrollLeft,
		.thumbGrabOffset = lookup.hit.overScrollbarThumb
			? (point.x() - thumb.x())
			: (thumb.width() / 2),
	};
	if (!lookup.hit.overScrollbarThumb) {
		(void)updateHorizontalScroll(point);
	}
	return true;
}

bool MarkdownArticle::Impl::updateHorizontalScroll(QPoint point) {
	if (!_activeHorizontalScrollDrag) {
		return false;
	}
	const auto drag = *_activeHorizontalScrollDrag;
	const auto block = findScrollOwnerByIdentity(drag.owner);
	if (!block) {
		return false;
	}
	if (drag.fromTouch) {
		return setScrollLeft(
			*block,
			drag.owner,
			drag.startScrollLeft - (point.x() - drag.pressPoint.x()));
	}
	if (block->scrollScrollbarTrackRect.isEmpty()) {
		return false;
	}
	const auto available = std::max(
		block->scrollScrollbarTrackRect.width()
			- block->scrollScrollbarThumbRect.width(),
		0);
	auto thumbLeft = point.x() - drag.thumbGrabOffset;
	thumbLeft = std::clamp(
		thumbLeft,
		block->scrollScrollbarTrackRect.x(),
		block->scrollScrollbarTrackRect.x() + available);
	const auto left = (available > 0)
		? (((thumbLeft - block->scrollScrollbarTrackRect.x())
			* block->horizontalScrollMax)
			+ (available / 2))
		/ available
		: 0;
	return setScrollLeft(*block, drag.owner, left);
}

void MarkdownArticle::Impl::endHorizontalScroll() {
	_activeHorizontalScrollDrag.reset();
}

// The laid out width is passed through the _width field, assigned by the
// callers right before the call, instead of a parameter, because GCC 15
// IPA-CP with LTO wrongly constant-folded such a parameter to 1 (the lower
// bound of the std::max(width, 1) clamps in the callers), collapsing rich
// message bubbles to the minimum width in release Linux builds.
void MarkdownArticle::Impl::finalizeRelayout(int heightBottom) {
	const auto &page = layoutStyle().pagePadding;
	++_layoutGeneration;
	restoreScrollState();
	refreshScrolledGeometry(_blocks);
	_laidOutWidth = std::min(
		_width,
		std::max(
			ArticleContentMaxRight(_blocks, layoutStyle()) + page.right(),
			page.left() + page.right() + 1));
	pruneTaskMarkerRuntimes();
	prunePlaceholderRuntimes();
	_relatedArticleImages.clear();
	StoreRelatedArticleImageStates(
		_blocks,
		&_relatedArticleImages);
	_height = heightBottom + page.bottom();
	clearPendingHighlightBlockPointers();
	_anchors.clear();
	_segments.clear();
	_visibleSegmentSpan = {};
	_segmentTops.clear();
	_segmentBottoms.clear();
	registerPendingHighlightBlocks(_blocks);
	CollectAnchors(_blocks, &_anchors);
	CollectSelectableSegments(&_blocks, &_segments);
	RefreshScrollableSegmentRects(_blocks, &_segments);
	rebuildVisibleSegmentLookup();
}

void MarkdownArticle::Impl::relayout(int width) {
	width = std::max(width, 1);
	if (_width == width) {
		return;
	}
	StoreRelatedArticleImageStates(
		_blocks,
		&_relatedArticleImages);
	invalidateGeometry();
	RebuildCachedTextLeafs(
		_content.blocks.blocks,
		&_blocks,
		layoutStyle(),
		&_cachedTextLeafs);
	retainBlocks();
	_missingMediaBlocks = 0;

	const auto &st = layoutStyle();
	const auto &page = st.pagePadding;
	const auto innerWidth = std::max(width - page.left() - page.right(), 1);
	auto context = LayoutContext{
		.articleLeft = page.left(),
		.articleWidth = innerWidth,
		.mediaPixelScale = _mediaPixelScale,
		.useArticleBands = true,
		.editMode = _content.editMode,
		.syntaxHighlightTracker = this,
		.cachedTextLeafs = &_cachedTextLeafs,
		.repaint = _textRepaint,
		.repaintRect = _textRepaintRect,
		.spoilerLinkFilter = _textSpoilerLinkFilter,
	};
	if (_editableMaxLineWidthOverrideLeaf
		&& (_editableMaxLineWidthOverride > 0)) {
		context.editableMaxLineWidthOverride
			= std::make_shared<EditableMaxLineWidthOverride>(
				EditableMaxLineWidthOverride{
					.leaf = *_editableMaxLineWidthOverrideLeaf,
					.width = _editableMaxLineWidthOverride,
				});
	}
	if (_editableTextEmptyOverrideLeaf) {
		context.editableTextEmptyOverride
			= std::make_shared<EditableTextEmptyOverride>(
				EditableTextEmptyOverride{
					.leaf = *_editableTextEmptyOverrideLeaf,
					.empty = _editableTextEmptyOverride,
				});
	}
	if (_editableHeightOverrideIndex >= 0 && _editableHeightOverride > 0) {
		context.editableHeightOverride
			= std::make_shared<EditableHeightOverride>(
				EditableHeightOverride{
					.editableIndex = _editableHeightOverrideIndex,
					.height = _editableHeightOverride,
				});
	}
	context.mediaBlockFactory = [=](const PreparedBlock &prepared) {
		auto block = getOrCreateMediaBlock(prepared);
		if (!block
			&& _content.mediaRuntime
			&& ExpectsMediaBlock(prepared)) {
			++_missingMediaBlocks;
		}
		return block;
	};
	context.placeholderRuntimeFactory = [=](PreparedPlaceholderBlockId id) {
		return getOrCreatePlaceholderRuntime(id);
	};
	context.taskMarkerRippleRuntimeFactory
		= [=](const PreparedEditListItemSource &source) {
			return getOrCreateTaskMarkerRippleRuntime(source);
		};
	const auto contextScope = LayoutContextScope(context);
	(void)contextScope;
	const auto y = LayoutBlocks(
		_content.blocks.blocks,
		&_content.formulas,
		&_formulaRenders,
		_renderer.get(),
		_inlineFormulaObjects.get(),
		_content.mediaRuntime,
		&_blocks,
		st,
		page.left(),
		page.top(),
		innerWidth,
		context);
	RestoreRelatedArticleImageStates(
		&_blocks,
		_relatedArticleImages);
	_width = width;
	finalizeRelayout(y);
}

void MarkdownArticle::Impl::relayoutRetained(int width) {
	width = std::max(width, 1);
	if (_width == width) {
		return;
	} else if (_blocks.empty()) {
		relayout(width);
		return;
	}
	captureScrollState();

	const auto &st = layoutStyle();
	const auto &page = st.pagePadding;
	const auto innerWidth = std::max(width - page.left() - page.right(), 1);
	auto context = LayoutContext{
		.articleLeft = page.left(),
		.articleWidth = innerWidth,
		.mediaPixelScale = _mediaPixelScale,
		.useArticleBands = true,
		.editMode = _content.editMode,
		.syntaxHighlightTracker = this,
		.cachedTextLeafs = &_cachedTextLeafs,
		.repaint = _textRepaint,
		.repaintRect = _textRepaintRect,
		.spoilerLinkFilter = _textSpoilerLinkFilter,
	};
	if (_editableMaxLineWidthOverrideLeaf
		&& (_editableMaxLineWidthOverride > 0)) {
		context.editableMaxLineWidthOverride
			= std::make_shared<EditableMaxLineWidthOverride>(
				EditableMaxLineWidthOverride{
					.leaf = *_editableMaxLineWidthOverrideLeaf,
					.width = _editableMaxLineWidthOverride,
				});
	}
	if (_editableTextEmptyOverrideLeaf) {
		context.editableTextEmptyOverride
			= std::make_shared<EditableTextEmptyOverride>(
				EditableTextEmptyOverride{
					.leaf = *_editableTextEmptyOverrideLeaf,
					.empty = _editableTextEmptyOverride,
				});
	}
	if (_editableHeightOverrideIndex >= 0 && _editableHeightOverride > 0) {
		context.editableHeightOverride
			= std::make_shared<EditableHeightOverride>(
				EditableHeightOverride{
					.editableIndex = _editableHeightOverrideIndex,
					.height = _editableHeightOverride,
				});
	}
	context.mediaBlockFactory = [=](const PreparedBlock &prepared) {
		return getOrCreateMediaBlock(prepared);
	};
	context.placeholderRuntimeFactory = [=](PreparedPlaceholderBlockId id) {
		return getOrCreatePlaceholderRuntime(id);
	};
	context.taskMarkerRippleRuntimeFactory
		= [=](const PreparedEditListItemSource &source) {
			return getOrCreateTaskMarkerRippleRuntime(source);
		};
	const auto contextScope = LayoutContextScope(context);
	(void)contextScope;
	const auto y = RecountLaidOutBlocks(
		_content.blocks.blocks,
		_content.formulas,
		&_blocks,
		st,
		page.left(),
		page.top(),
		innerWidth,
		context);
	if (!y) {
		relayout(width);
		return;
	}
	_width = width;
	finalizeRelayout(*y);
}

MarkdownArticle::MarkdownArticle(
	const style::Markdown &st,
	std::shared_ptr<MathRenderer> renderer)
: _impl(std::make_unique<Impl>(st, std::move(renderer))) {
}

MarkdownArticle::~MarkdownArticle() = default;
MarkdownArticle::MarkdownArticle(MarkdownArticle &&) noexcept = default;
MarkdownArticle &MarkdownArticle::operator=(MarkdownArticle &&) noexcept = default;

void MarkdownArticle::setRenderer(std::shared_ptr<MathRenderer> renderer) {
	_impl->setRenderer(std::move(renderer));
}

void MarkdownArticle::setMediaBlockHost(MediaBlockHost *host) {
	_impl->setMediaBlockHost(host);
}

void MarkdownArticle::setMediaPixelScale(double scale) {
	_impl->setMediaPixelScale(scale);
}

void MarkdownArticle::setTextRepaintCallbacks(
		Fn<void()> repaint,
		Fn<void(QRect)> repaintRect,
		Fn<bool(const ClickContext&)> spoilerLinkFilter) {
	_impl->setTextRepaintCallbacks(
		std::move(repaint),
		std::move(repaintRect),
		std::move(spoilerLinkFilter));
}

void MarkdownArticle::setContent(MarkdownArticleContent content) {
	_impl->setContent(std::move(content));
}

void MarkdownArticle::setSearchMatches(
		std::vector<MarkdownArticleSearchMatch> matches,
		int current) {
	_impl->setSearchMatches(std::move(matches), current);
}

auto MarkdownArticle::searchSources() const
-> std::vector<MarkdownArticleSearchSource> {
	return _impl->searchSources();
}

void MarkdownArticle::updatePreparedLeaf(
		const PreparedEditLeafSource &source,
		const MarkdownArticleContent &prepared) {
	_impl->updatePreparedLeaf(source, prepared);
}

void MarkdownArticle::setEditableMaxLineWidthOverride(
		const PreparedEditLeafSource &source,
		int width) {
	_impl->setEditableMaxLineWidthOverride(source, width);
}

void MarkdownArticle::setEditableTextEmptyOverride(
		const PreparedEditLeafSource &source,
		bool empty) {
	_impl->setEditableTextEmptyOverride(source, empty);
}

void MarkdownArticle::setEditableHeightOverride(
		int editableIndex,
		int height) {
	_impl->setEditableHeightOverride(editableIndex, height);
}

void MarkdownArticle::setEditableHeightOverrideForSegment(
		int segmentIndex,
		int height) {
	_impl->setEditableHeightOverrideForSegment(segmentIndex, height);
}

void MarkdownArticle::clearEditableMaxLineWidthOverride() {
	_impl->clearEditableMaxLineWidthOverride();
}

void MarkdownArticle::clearEditableTextEmptyOverride() {
	_impl->clearEditableTextEmptyOverride();
}

void MarkdownArticle::clearEditableHeightOverride() {
	_impl->clearEditableHeightOverride();
}

void MarkdownArticle::setTextLeafHeightOverride(
		int textLeafIndex,
		int height) {
	_impl->setTextLeafHeightOverride(textLeafIndex, height);
}

void MarkdownArticle::clearTextLeafHeightOverride() {
	_impl->clearTextLeafHeightOverride();
}

void MarkdownArticle::invalidateLayout() {
	_impl->invalidateLayout();
}

int MarkdownArticle::maxWidth() const {
	return const_cast<Impl*>(_impl.get())->maxWidth();
}

int MarkdownArticle::lastLayoutWidth() const {
	return _impl->lastLayoutWidth();
}

bool MarkdownArticle::hasMissingMediaBlocks() const {
	return _impl->hasMissingMediaBlocks();
}

int MarkdownArticle::resizeGetHeight(int width) {
	return _impl->resizeGetHeight(width);
}

auto MarkdownArticle::countRevealLinesGeometry(int width)
-> std::vector<MarkdownArticleRevealLine> {
	return _impl->countRevealLinesGeometry(width);
}

void MarkdownArticle::setVisibleTopBottom(int visibleTop, int visibleBottom) {
	_impl->setVisibleTopBottom(visibleTop, visibleBottom);
}

void MarkdownArticle::paint(
		Painter &p,
		const MarkdownArticlePaintContext &context) const {
	_impl->paint(p, context);
}

MarkdownArticleHitTestResult MarkdownArticle::hitTest(
		QPoint point,
		Ui::Text::StateRequest::Flags flags) const {
	return _impl->hitTest(point, flags);
}

PreparedEditHit MarkdownArticle::editHitTest(QPoint point) const {
	return _impl->editHitTest(point);
}

MarkdownArticleDropLocation MarkdownArticle::editDropTarget(
		QPoint point) const {
	return _impl->editDropTarget(point);
}

MarkdownArticleDropLocation MarkdownArticle::editBlockDropTarget(
		QPoint point) const {
	return _impl->editBlockDropTarget(point);
}

MarkdownArticleDropLocation MarkdownArticle::editStructuralDropTarget(
		QPoint point,
		const PreparedEditSelection &selection) const {
	return _impl->editStructuralDropTarget(point, selection);
}

MarkdownArticleEditControlHit MarkdownArticle::editControlHitTest(
		QPoint point) const {
	return _impl->editControlHitTest(point);
}

void MarkdownArticle::addTaskMarkerRipple(
		const PreparedEditListItemSource &source,
		QPoint point) {
	_impl->addTaskMarkerRipple(source, point);
}

MarkdownArticleHorizontalScrollHit MarkdownArticle::horizontalScrollHit(
		QPoint point) const {
	return _impl->horizontalScrollHit(point);
}

bool MarkdownArticle::canConsumeHorizontalScroll(
		QPoint point,
		int delta) const {
	return _impl->canConsumeHorizontalScroll(point, delta);
}

bool MarkdownArticle::consumeHorizontalScroll(QPoint point, int delta) {
	return _impl->consumeHorizontalScroll(point, delta);
}

bool MarkdownArticle::beginHorizontalScroll(QPoint point, bool fromTouch) {
	return _impl->beginHorizontalScroll(point, fromTouch);
}

bool MarkdownArticle::updateHorizontalScroll(QPoint point) {
	return _impl->updateHorizontalScroll(point);
}

void MarkdownArticle::endHorizontalScroll() {
	_impl->endHorizontalScroll();
}

int MarkdownArticle::anchorTop(const QString &anchorId) const {
	return _impl->anchorTop(anchorId);
}

auto MarkdownArticle::scrollAnchorForTop(int top) const
-> std::optional<MarkdownArticleScrollAnchor> {
	return _impl->scrollAnchorForTop(top);
}

int MarkdownArticle::scrollTopForAnchor(
		const MarkdownArticleScrollAnchor &anchor) const {
	return _impl->scrollTopForAnchor(anchor);
}

MarkdownArticleAnchorExpansion MarkdownArticle::expandDetailsToAnchor(
		const QString &anchorId) {
	return _impl->expandDetailsToAnchor(anchorId);
}

MarkdownArticleAnchorExpansion MarkdownArticle::expandDetailsBlock(
		const QString &anchorId) {
	return _impl->expandDetailsBlock(anchorId);
}

bool MarkdownArticle::toggleDetails(const QString &anchorId) {
	return _impl->toggleDetails(anchorId);
}

bool MarkdownArticle::segmentIsText(int index) const {
	return _impl->segmentIsText(index);
}

bool MarkdownArticle::segmentIsDisplayMath(int index) const {
	return _impl->segmentIsDisplayMath(index);
}

bool MarkdownArticle::segmentIsEditable(int index) const {
	return _impl->segmentIsEditable(index);
}

int MarkdownArticle::segmentLength(int index) const {
	return _impl->segmentLength(index);
}

int MarkdownArticle::firstTextSegmentIndex() const {
	return _impl->firstTextSegmentIndex();
}

int MarkdownArticle::firstEditableSegmentIndex() const {
	return _impl->firstEditableSegmentIndex();
}

int MarkdownArticle::textLeafIndexForSegment(int segmentIndex) const {
	return _impl->textLeafIndexForSegment(segmentIndex);
}

int MarkdownArticle::segmentIndexForTextLeafIndex(int textLeafIndex) const {
	return _impl->segmentIndexForTextLeafIndex(textLeafIndex);
}

int MarkdownArticle::editableIndexForSegment(int segmentIndex) const {
	return _impl->editableIndexForSegment(segmentIndex);
}

int MarkdownArticle::segmentIndexForEditableIndex(int editableIndex) const {
	return _impl->segmentIndexForEditableIndex(editableIndex);
}

std::optional<PreparedEditLeafSource> MarkdownArticle::editableLeafForSegment(
		int segmentIndex) const {
	return _impl->editableLeafForSegment(segmentIndex);
}

int MarkdownArticle::segmentIndexForEditableLeaf(
		const PreparedEditLeafSource &source) const {
	return _impl->segmentIndexForEditableLeaf(source);
}

QRect MarkdownArticle::textSegmentRect(int segmentIndex) const {
	return _impl->textSegmentRect(segmentIndex);
}

QRect MarkdownArticle::logicalSegmentRect(int segmentIndex) const {
	return _impl->logicalSegmentRect(segmentIndex);
}

QRect MarkdownArticle::segmentRect(int segmentIndex) const {
	return _impl->segmentRect(segmentIndex);
}

std::vector<MarkdownArticleMediaGeometry>
MarkdownArticle::mediaBlockGeometries() const {
	return _impl->mediaBlockGeometries();
}

void MarkdownArticle::setGroupedActiveIndex(
		const PreparedEditBlockSource &source,
		int index) {
	_impl->setGroupedActiveIndex(source, index);
}

QRect MarkdownArticle::displayMathEditRect(int segmentIndex) const {
	return _impl->displayMathEditRect(segmentIndex);
}

QRect MarkdownArticle::displayMathBlockRect(int segmentIndex) const {
	return _impl->displayMathBlockRect(segmentIndex);
}

int MarkdownArticle::pullquoteAvailableTextWidthForEditableLeaf(
		const PreparedEditLeafSource &source) const {
	return _impl->pullquoteAvailableTextWidthForEditableLeaf(source);
}

bool MarkdownArticle::revealSegment(int segmentIndex) {
	return _impl->revealSegment(segmentIndex);
}

MarkdownArticleTextLeafStyle MarkdownArticle::textLeafStyleForSegment(
		int segmentIndex) const {
	return _impl->textLeafStyleForSegment(segmentIndex);
}

MarkdownArticleTextLeafStyle MarkdownArticle::editableStyleForSegment(
		int segmentIndex) const {
	return _impl->editableStyleForSegment(segmentIndex);
}

int MarkdownArticle::selectionOffsetFromHit(
		const MarkdownArticleHitTestResult &result,
		TextSelectType selectionType) const {
	return _impl->selectionOffsetFromHit(result, selectionType);
}

TextSelection MarkdownArticle::adjustSelection(
		int segmentIndex,
		TextSelection selection,
		TextSelectType selectionType) const {
	return _impl->adjustSelection(segmentIndex, selection, selectionType);
}

bool MarkdownArticle::selectionContains(
		MarkdownArticleSelection selection,
		const MarkdownArticleSelectionEndpoints *endpoints,
		const MarkdownArticleHitTestResult &result) const {
	return _impl->selectionContains(selection, endpoints, result);
}

TextForMimeData MarkdownArticle::textForContext(
		const MarkdownArticleHitTestResult &result) const {
	return _impl->textForContext(result);
}

TextForMimeData MarkdownArticle::textForSelection(
		MarkdownArticleSelection selection,
		const MarkdownArticleSelectionEndpoints *endpoints,
		const PreparedEditSelection *structuralSelection) const {
	return _impl->textForSelection(
		selection,
		endpoints,
		structuralSelection);
}

std::vector<RichPage::Block> MarkdownArticle::richPageSliceForSelection(
		MarkdownArticleSelection selection) const {
	return _impl->richPageSliceForSelection(selection);
}

bool MarkdownArticle::highlightProcessDone(
		Spellchecker::HighlightProcessId processId) {
	return _impl->highlightProcessDone(processId);
}

void MarkdownArticle::invalidatePaletteCache() {
	_impl->invalidatePaletteCache();
}

void MarkdownArticle::invalidateRasterCache() {
	_impl->invalidateRasterCache();
}

bool MarkdownArticle::hasHeavyPart() const {
	return _impl && _impl->hasHeavyPart();
}

void MarkdownArticle::unloadHeavyPart() {
	_impl->unloadHeavyPart();
}

void MarkdownArticle::hideSpoilers() {
	_impl->hideSpoilers();
}

MediaBlockHost *MarkdownArticle::mediaBlockHost() const {
	return _impl->mediaBlockHost();
}

void MarkdownArticle::setPlaceholderLoading(PreparedPlaceholderBlockId id) {
	_impl->setPlaceholderLoading(id);
}

void MarkdownArticle::clearPlaceholderLoading(PreparedPlaceholderBlockId id) {
	_impl->clearPlaceholderLoading(id);
}

void MarkdownArticle::clearAllPlaceholderLoading() {
	_impl->clearAllPlaceholderLoading();
}

void MarkdownArticle::addPlaceholderRipple(
		PreparedPlaceholderBlockId id,
		QPoint point) {
	_impl->addPlaceholderRipple(id, point);
}

void MarkdownArticle::stopPlaceholderRipple(PreparedPlaceholderBlockId id) {
	_impl->stopPlaceholderRipple(id);
}

void MarkdownArticle::clearBeforeDestroy() {
    base::take(_impl);
}

} // namespace Iv::Markdown
