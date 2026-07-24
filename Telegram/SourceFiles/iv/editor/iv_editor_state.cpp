/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/editor/iv_editor_state.h"
#include "iv/editor/iv_editor_text_entities.h"
#include "lang/lang_keys.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/fields/input_field.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace Iv::Editor {
namespace {

using Block = RichPage::Block;
using BlockContainerKind = State::BlockContainerKind;
using BlockContainerPath = State::BlockContainerPath;
using ApplyResult = State::ApplyResult;
using BlockKind = RichPage::BlockKind;
using BoundaryAction = State::BoundaryTarget::Action;
using BlockPath = State::BlockPath;
using FieldMode = State::FieldMode;
using InsertBlockType = State::InsertBlockType;
using InsertionAnchor = State::InsertionAnchor;
using LeafKind = State::LeafKind;
using LeafPath = State::LeafPath;
using ListStyle = State::ListStyle;
using ListItem = RichPage::ListItem;
using ListKind = RichPage::ListKind;
using NativeInstantViewLeafUpdateResult
	= Markdown::NativeInstantViewLeafUpdateResult;
using PreparedBlockContainerKind = Markdown::PreparedEditBlockContainerKind;
using PreparedEditLeafKind = Markdown::PreparedEditLeafKind;
using PreparedEditLeafSource = Markdown::PreparedEditLeafSource;
using PreparedEditListItemRange = Markdown::PreparedEditListItemRange;
using PreparedEditListItemSource = Markdown::PreparedEditListItemSource;
using PreparedEditSelectionKind = Markdown::PreparedEditSelectionKind;
using PreparedBlockContainerPath = Markdown::PreparedEditBlockContainerPath;
using PreparedBlockPath = Markdown::PreparedEditBlockPath;
using PreparedBlockContainerStep = Markdown::PreparedEditBlockContainerStep;
using PreparedEditSelection = Markdown::PreparedEditSelection;
using PreparedMutationKind = State::PreparedMutationKind;
using PreparedOrderedListType = Markdown::PreparedOrderedListType;
using ReplaceTarget = State::ReplaceTarget;
using RemovalKind = State::RemovalKind;
using RemovalTarget = State::RemovalTarget;
using RichText = RichPage::RichText;
using OrderedListData = RichPage::OrderedListData;
using TableCell = RichPage::TableCell;
using TableRow = RichPage::TableRow;
using TaskState = RichPage::TaskState;
using TextNodeDescriptor = State::TextNodeDescriptor;
using TextFormattingAction = State::TextFormattingAction;
using TextSelectionDropResult = State::TextSelectionDropResult;
using TextNodeSpan = State::TextNodeSpan;

struct TextRange {
	int offset = 0;
	int length = 0;
};

constexpr auto kMaxRichTextNodeLength = 16000;
constexpr auto kMaxCommittedFieldLength = 256 * 1024;

[[nodiscard]] std::vector<TextWithEntities> SplitFieldText(
		TextWithEntities text) {
	auto result = std::vector<TextWithEntities>();
	auto left = std::move(text);
	auto consumed = 0;
	while (!left.text.isEmpty() && consumed < kMaxCommittedFieldLength) {
		auto part = TextWithEntities();
		const auto limit = std::min(
			kMaxRichTextNodeLength,
			kMaxCommittedFieldLength - consumed);
		if (!TextUtilities::CutPart(part, left, limit)
			|| part.text.isEmpty()) {
			break;
		}
		consumed += part.text.size();
		result.push_back(std::move(part));
	}
	return result;
}

[[nodiscard]] TextWithEntities JoinText(
		TextWithEntities before,
		TextWithEntities selected,
		TextWithEntities after) {
	before.append(std::move(selected));
	before.append(std::move(after));
	return before;
}

void ExpandInsertContextToActiveLine(State::ActiveTextInsertContext &context) {
	if (!context.selected.text.isEmpty()) {
		return;
	}
	const auto lineStart = context.before.text.lastIndexOf('\n');
	const auto lineEnd = context.after.text.indexOf('\n');
	auto lineHead = Ui::Text::Mid(context.before, lineStart + 1);
	auto lineTail = (lineEnd >= 0)
		? Ui::Text::Mid(context.after, 0, lineEnd)
		: context.after;
	auto newBefore = (lineStart >= 0)
		? Ui::Text::Mid(context.before, 0, lineStart)
		: TextWithEntities();
	auto newAfter = (lineEnd >= 0)
		? Ui::Text::Mid(context.after, lineEnd + 1)
		: TextWithEntities();
	lineHead.append(std::move(lineTail));
	context.before = std::move(newBefore);
	context.selected = std::move(lineHead);
	context.after = std::move(newAfter);
}

[[nodiscard]] bool RangeInsideText(
		const QString &text,
		int offset,
		int length) {
	return (offset >= 0)
		&& (length >= 0)
		&& (offset <= text.size())
		&& ((offset + length) <= text.size());
}

[[nodiscard]] const QString *FormattingActionTag(
		TextFormattingAction action) {
	switch (action) {
	case TextFormattingAction::Bold:
		return &Ui::InputField::kTagBold;
	case TextFormattingAction::Italic:
		return &Ui::InputField::kTagItalic;
	case TextFormattingAction::Underline:
		return &Ui::InputField::kTagUnderline;
	case TextFormattingAction::StrikeOut:
		return &Ui::InputField::kTagStrikeOut;
	case TextFormattingAction::Spoiler:
		return &Ui::InputField::kTagSpoiler;
	case TextFormattingAction::PlainText:
		return nullptr;
	}
	return nullptr;
}

[[nodiscard]] QString TagWithoutInstantViewMath(QStringView tag) {
	return TextUtilities::TagWithRemoved(
		tag.toString(),
		Ui::InputField::kTagIvMath);
}

[[nodiscard]] QString TagWithAddedDroppingMath(
		const QString &tag,
		const QString &added) {
	if (added == Ui::InputField::kTagIvMath) {
		return Ui::InputField::kTagIvMath;
	}
	return TextUtilities::TagWithAdded(
		TagWithoutInstantViewMath(tag),
		added);
}

void SortTags(TextWithTags::Tags *tags) {
	std::sort(tags->begin(), tags->end(), [](const auto &a, const auto &b) {
		if (a.offset != b.offset) {
			return a.offset < b.offset;
		} else if (a.length != b.length) {
			return a.length < b.length;
		}
		return a.id < b.id;
	});
}

[[nodiscard]] bool TagContains(QStringView tags, QStringView tagId) {
	return TextUtilities::SplitTags(tags).contains(tagId);
}

[[nodiscard]] bool HasFullTextTag(
		const TextWithTags &textWithTags,
		const QString &tag) {
	if (tag.isEmpty() || textWithTags.text.isEmpty()) {
		return false;
	}
	auto ranges = std::vector<TextRange>();
	ranges.reserve(textWithTags.tags.size());
	for (const auto &existing : textWithTags.tags) {
		if (existing.length <= 0
			|| !RangeInsideText(
				textWithTags.text,
				existing.offset,
				existing.length)
			|| !TagContains(existing.id, tag)) {
			continue;
		}
		ranges.push_back({
			.offset = existing.offset,
			.length = existing.length,
		});
	}
	if (ranges.empty()) {
		return false;
	}
	std::sort(ranges.begin(), ranges.end(), [](const auto &a, const auto &b) {
		if (a.offset != b.offset) {
			return a.offset < b.offset;
		}
		return a.length < b.length;
	});
	auto coveredTill = 0;
	for (const auto &range : ranges) {
		if (range.offset > coveredTill) {
			return false;
		}
		coveredTill = std::max(coveredTill, range.offset + range.length);
		if (coveredTill >= textWithTags.text.size()) {
			return true;
		}
	}
	return (coveredTill >= textWithTags.text.size());
}

void OverlayTag(
		TextWithTags::Tags *tags,
		const TextWithTags::Tag &overlay,
		const QString &text) {
	if (overlay.id.isEmpty()
		|| overlay.length <= 0
		|| !RangeInsideText(text, overlay.offset, overlay.length)) {
		return;
	}
	const auto from = overlay.offset;
	const auto till = from + overlay.length;
	auto coveredTill = from;
	auto result = TextWithTags::Tags();
	result.reserve(tags->size() + 3);

	for (const auto &tag : *tags) {
		const auto tagFrom = tag.offset;
		const auto tagTill = tag.offset + tag.length;
		if (tagTill <= from) {
			result.push_back(tag);
			continue;
		} else if (tagFrom >= till) {
			if (coveredTill < till) {
				result.push_back({
					.offset = coveredTill,
					.length = till - coveredTill,
					.id = overlay.id,
				});
				coveredTill = till;
			}
			result.push_back(tag);
			continue;
		}
		if (tagFrom > coveredTill) {
			result.push_back({
				.offset = coveredTill,
				.length = tagFrom - coveredTill,
				.id = overlay.id,
			});
			coveredTill = tagFrom;
		}
		if (tagFrom < from) {
			result.push_back({
				.offset = tagFrom,
				.length = from - tagFrom,
				.id = tag.id,
			});
		}
		const auto middleFrom = std::max(tagFrom, from);
		const auto middleTill = std::min(tagTill, till);
		if (middleFrom < middleTill) {
			result.push_back({
				.offset = middleFrom,
				.length = middleTill - middleFrom,
				.id = TagWithAddedDroppingMath(tag.id, overlay.id),
			});
			coveredTill = middleTill;
		}
		if (tagTill > till) {
			result.push_back({
				.offset = till,
				.length = tagTill - till,
				.id = tag.id,
			});
		}
	}
	if (coveredTill < till) {
		result.push_back({
			.offset = coveredTill,
			.length = till - coveredTill,
			.id = overlay.id,
		});
	}
	SortTags(&result);
	*tags = TextUtilities::SimplifyTags(std::move(result));
}

void RemoveTagFromSelection(
		TextWithTags::Tags *tags,
		const QString &tag) {
	auto result = TextWithTags::Tags();
	result.reserve(tags->size());
	for (const auto &existing : *tags) {
		const auto updated = TextUtilities::TagWithRemoved(existing.id, tag);
		if (!updated.isEmpty()) {
			result.push_back({
				.offset = existing.offset,
				.length = existing.length,
				.id = updated,
			});
		}
	}
	*tags = std::move(result);
}

[[nodiscard]] bool SplitTextSpan(
		const TextWithEntities &text,
		int from,
		int till,
		TextWithEntities *before,
		TextWithEntities *selected,
		TextWithEntities *after) {
	if (!before || !selected || !after) {
		return false;
	}
	const auto textSize = int(text.text.size());
	from = std::clamp(from, 0, textSize);
	till = std::clamp(till, from, textSize);
	if (from >= till) {
		return false;
	}
	*before = Ui::Text::Mid(text, 0, from);
	*selected = Ui::Text::Mid(text, from, till - from);
	if (selected->text.isEmpty()) {
		return false;
	}
	*after = Ui::Text::Mid(text, till);
	return true;
}

[[nodiscard]] bool MediaBlockSupportsSpoiler(const Block &block) {
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

[[nodiscard]] bool MediaBlockHasSpoiler(const Block &block) {
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

bool SetMediaBlockSpoiler(Block *block, bool enabled) {
	if (!block || !MediaBlockSupportsSpoiler(*block)) {
		return false;
	} else if (block->kind == BlockKind::GroupedMedia) {
		auto changed = false;
		for (auto &item : block->mediaItems) {
			if ((item.kind != BlockKind::Photo)
				&& (item.kind != BlockKind::Video)
				&& (item.kind != BlockKind::Audio)
				&& (item.kind != BlockKind::Map)) {
				continue;
			}
			if (item.spoiler != enabled) {
				item.spoiler = enabled;
				changed = true;
			}
		}
		return changed;
	} else if (block->spoiler != enabled) {
		block->spoiler = enabled;
		return true;
	}
	return false;
}

[[nodiscard]] bool IsPhotoVideoBlockKind(BlockKind kind) {
	return (kind == BlockKind::Photo) || (kind == BlockKind::Video);
}

[[nodiscard]] bool IsReplaceableMediaBlockKind(BlockKind kind) {
	return IsPhotoVideoBlockKind(kind) || (kind == BlockKind::Audio);
}

[[nodiscard]] bool IsTaskList(const std::vector<ListItem> &items) {
	return std::any_of(
		items.begin(),
		items.end(),
		[](const ListItem &item) {
			return item.taskState != TaskState::None;
		});
}

[[nodiscard]] bool IsTaskList(const ClipboardListItemsData &data) {
	return data.taskList || IsTaskList(data.items);
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

[[nodiscard]] std::optional<QString> StoredOrderedListType(
		PreparedOrderedListType type,
		bool explicitDecimal = false) {
	switch (type) {
	case PreparedOrderedListType::LowerAlpha:
		return u"a"_q;
	case PreparedOrderedListType::UpperAlpha:
		return u"A"_q;
	case PreparedOrderedListType::LowerRoman:
		return u"i"_q;
	case PreparedOrderedListType::UpperRoman:
		return u"I"_q;
	case PreparedOrderedListType::Decimal:
		return explicitDecimal ? std::make_optional(u"1"_q) : std::nullopt;
	}
	return explicitDecimal ? std::make_optional(u"1"_q) : std::nullopt;
}

[[nodiscard]] int OrderedListSequenceStart(const Block &block) {
	return block.orderedList.start.value_or(
		block.orderedList.reversed ? int(block.listItems.size()) : 1);
}

[[nodiscard]] int OrderedListSequenceStep(const Block &block) {
	return block.orderedList.reversed ? -1 : 1;
}

[[nodiscard]] std::optional<int> EffectiveOrderedItemValue(
		const Block &block,
		int itemIndex) {
	if (block.kind != BlockKind::List
		|| block.listKind != ListKind::Ordered
		|| itemIndex < 0
		|| itemIndex >= int(block.listItems.size())) {
		return std::nullopt;
	}
	auto next = OrderedListSequenceStart(block);
	const auto step = OrderedListSequenceStep(block);
	for (auto i = 0; i <= itemIndex; ++i) {
		const auto value = block.listItems[i].number.value.value_or(next);
		if (i == itemIndex) {
			return value;
		}
		next = value + step;
	}
	return std::nullopt;
}

[[nodiscard]] bool ClearOrderedListRawMarkers(
		Block *block,
		int from,
		int till) {
	if (!block
		|| block->kind != BlockKind::List
		|| block->listKind != ListKind::Ordered) {
		return false;
	}
	auto changed = false;
	from = std::clamp(from, 0, int(block->listItems.size()));
	till = std::clamp(till, from, int(block->listItems.size()));
	for (auto i = from; i != till; ++i) {
		auto &item = block->listItems[i];
		if (item.number.num.has_value()) {
			item.number.num = std::nullopt;
			changed = true;
		}
	}
	return changed;
}

[[nodiscard]] bool ClearOrderedListRawMarkers(Block *block) {
	return block
		? ClearOrderedListRawMarkers(block, 0, int(block->listItems.size()))
		: false;
}

[[nodiscard]] bool ClearOrderedTaskStates(Block *block) {
	if (!block || block->kind != BlockKind::List) {
		return false;
	}
	auto changed = false;
	for (auto &item : block->listItems) {
		if (item.taskState != TaskState::None) {
			item.taskState = TaskState::None;
			changed = true;
		}
	}
	return changed;
}

[[nodiscard]] bool ClearOrderedListData(ListItem *item) {
	if (!item) {
		return false;
	}
	const auto changed = item->number.num.has_value()
		|| item->number.value.has_value()
		|| item->number.type.has_value();
	item->number = {};
	return changed;
}

[[nodiscard]] bool ResetNonOrderedListMetadata(Block *block) {
	if (!block || block->kind != BlockKind::List) {
		return false;
	}
	auto changed = (block->orderedList != OrderedListData());
	block->orderedList = {};
	for (auto &item : block->listItems) {
		changed = ClearOrderedListData(&item) || changed;
	}
	return changed;
}

void NormalizeInsertedOrderedListMetadata(Block *block) {
	if (!block) {
		return;
	}
	for (auto &child : block->blocks) {
		NormalizeInsertedOrderedListMetadata(&child);
	}
	for (auto &item : block->listItems) {
		for (auto &child : item.blocks) {
			NormalizeInsertedOrderedListMetadata(&child);
		}
	}
	if (block->kind != BlockKind::List) {
		return;
	}
	if (block->listKind != ListKind::Ordered) {
		(void)ResetNonOrderedListMetadata(block);
	}
}

void NormalizeInsertedOrderedListMetadata(std::vector<Block> *blocks) {
	if (!blocks) {
		return;
	}
	for (auto &block : *blocks) {
		NormalizeInsertedOrderedListMetadata(&block);
	}
}

[[nodiscard]] ListStyle CurrentListStyle(const Block &block) {
	return (block.listKind == ListKind::Ordered)
		? ListStyle::Ordered
		: IsTaskList(block.listItems)
		? ListStyle::Task
		: ListStyle::Bullet;
}

[[nodiscard]] PreparedOrderedListType EffectiveOrderedListType(
		const Block &block,
		const ListItem &item) {
	return item.number.type.has_value()
		? ResolvePreparedOrderedListType(item.number.type)
		: ResolvePreparedOrderedListType(block.orderedList.type);
}

[[nodiscard]] bool ListBlockMatchesClipboardData(
		const Block &block,
		const ClipboardListItemsData &data) {
	if (block.kind != BlockKind::List
		|| block.listKind != data.listKind
		|| IsTaskList(block.listItems) != IsTaskList(data)) {
		return false;
	}
	return (block.listKind != ListKind::Ordered)
		|| (block.orderedList == data.orderedList);
}

[[nodiscard]] std::optional<uint64> ReplaceTargetMediaId(const Block &block) {
	switch (block.kind) {
	case BlockKind::Photo:
		return block.photoId ? std::make_optional(block.photoId) : std::nullopt;
	case BlockKind::Video:
	case BlockKind::Audio:
		return block.documentId
			? std::make_optional(block.documentId)
			: std::nullopt;
	default:
		return std::nullopt;
	}
}

[[nodiscard]] bool BlockMatchesReplaceTarget(
		const Block &block,
		const ReplaceTarget &target) {
	const auto mediaId = ReplaceTargetMediaId(block);
	return (block.kind == target.kind)
		&& mediaId
		&& (*mediaId == target.mediaId);
}

[[nodiscard]] std::optional<uint64> ReplaceTargetMediaId(
		const RichPage::GroupedMediaItem &item) {
	switch (item.kind) {
	case BlockKind::Photo:
		return item.photoId ? std::make_optional(item.photoId) : std::nullopt;
	case BlockKind::Video:
		return item.documentId
			? std::make_optional(item.documentId)
			: std::nullopt;
	default:
		return std::nullopt;
	}
}

[[nodiscard]] bool GroupedItemMatchesReplaceTarget(
		const RichPage::GroupedMediaItem &item,
		const ReplaceTarget &target) {
	const auto mediaId = ReplaceTargetMediaId(item);
	return (item.kind == target.kind)
		&& mediaId
		&& (*mediaId == target.mediaId);
}

[[nodiscard]] std::optional<RichPage::GroupedMediaItem>
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

[[nodiscard]] std::optional<Block> PhotoVideoBlockFromGroupedItem(
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

[[nodiscard]] bool GroupingRichTextIsEmpty(const RichText &text) {
	return text.text.text.trimmed().isEmpty()
		&& text.anchorId.isEmpty()
		&& text.anchorIds.empty();
}

[[nodiscard]] bool BlockHasGroupingCaptionOrAnchor(const Block &block) {
	return !GroupingRichTextIsEmpty(block.caption)
		|| !block.anchorId.isEmpty();
}

[[nodiscard]] bool HasValidGroupingCaptionAndAnchorSource(
		const std::vector<Block> &blocks,
		int from,
		int till) {
	auto found = false;
	for (auto i = from; i != till; ++i) {
		if (!BlockHasGroupingCaptionOrAnchor(blocks[i])) {
			continue;
		} else if (found) {
			return false;
		}
		found = true;
	}
	return true;
}

[[nodiscard]] BlockContainerPath BlockChildrenContainer(BlockPath path) {
	auto result = std::move(path.container);
	result.steps.push_back({
		.kind = BlockContainerKind::BlockChildren,
		.blockIndex = path.index,
	});
	return result;
}

[[nodiscard]] BlockContainerPath ListItemChildrenContainer(
		BlockPath path,
		int itemIndex) {
	auto result = std::move(path.container);
	result.steps.push_back({
		.kind = BlockContainerKind::ListItemChildren,
		.blockIndex = path.index,
		.listItemIndex = itemIndex,
	});
	return result;
}

[[nodiscard]] bool ContainerStartsWith(
		const BlockContainerPath &container,
		const BlockContainerPath &prefix) {
	if (container.steps.size() < prefix.steps.size()) {
		return false;
	}
	for (auto i = 0, count = int(prefix.steps.size()); i != count; ++i) {
		const auto &a = container.steps[i];
		const auto &b = prefix.steps[i];
		if (a.kind != b.kind
			|| a.blockIndex != b.blockIndex
			|| a.listItemIndex != b.listItemIndex) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] PreparedBlockContainerPath ToPreparedBlockContainerPath(
		const BlockContainerPath &path) {
	auto result = PreparedBlockContainerPath();
	result.steps.reserve(path.steps.size());
	for (const auto &step : path.steps) {
		auto converted = PreparedBlockContainerStep();
		converted.blockIndex = step.blockIndex;
		converted.listItemIndex = step.listItemIndex;
		switch (step.kind) {
		case BlockContainerKind::Root:
			continue;
		case BlockContainerKind::BlockChildren:
			converted.kind = PreparedBlockContainerKind::BlockChildren;
			break;
		case BlockContainerKind::ListItemChildren:
			converted.kind = PreparedBlockContainerKind::ListItemChildren;
			break;
		}
		result.steps.push_back(converted);
	}
	return result;
}

[[nodiscard]] bool ContainerHasPrefix(
		const BlockContainerPath &path,
		const BlockContainerPath &prefix) {
	if (path.steps.size() < prefix.steps.size()) {
		return false;
	}
	return std::equal(
		prefix.steps.begin(),
		prefix.steps.end(),
		path.steps.begin());
}

[[nodiscard]] bool IndexInRange(int index, int from, int till) {
	return (index >= from) && (index < till);
}

[[nodiscard]] bool ShiftBlockContainerPathAfterRemovedBlock(
		BlockContainerPath &path,
		const BlockPath &removed) {
	if (!ContainerHasPrefix(path, removed.container)) {
		return true;
	}
	const auto size = removed.container.steps.size();
	if (path.steps.size() == size) {
		return true;
	}
	auto &step = path.steps[size];
	if (step.blockIndex == removed.index) {
		return false;
	} else if (step.blockIndex > removed.index) {
		--step.blockIndex;
	}
	return true;
}

[[nodiscard]] bool ShiftBlockPathAfterRemovedBlock(
		BlockPath &path,
		const BlockPath &removed) {
	if (path.container == removed.container) {
		if (path.index == removed.index) {
			return false;
		} else if (path.index > removed.index) {
			--path.index;
		}
		return true;
	}
	return ShiftBlockContainerPathAfterRemovedBlock(path.container, removed);
}

[[nodiscard]] bool ShiftBlockContainerPathAfterRemovedListItem(
		BlockContainerPath &path,
		const BlockPath &list,
		int removedItemIndex) {
	const auto removed = ListItemChildrenContainer(list, removedItemIndex);
	if (ContainerHasPrefix(path, removed)) {
		return false;
	}
	if (!ContainerHasPrefix(path, list.container)) {
		return true;
	}
	const auto size = list.container.steps.size();
	if (path.steps.size() <= size) {
		return true;
	}
	auto &step = path.steps[size];
	if (step.blockIndex != list.index
		|| step.kind != BlockContainerKind::ListItemChildren) {
		return true;
	}
	if (step.listItemIndex == removedItemIndex) {
		return false;
	} else if (step.listItemIndex > removedItemIndex) {
		--step.listItemIndex;
	}
	return true;
}

[[nodiscard]] bool ShiftBlockPathAfterRemovedListItem(
		BlockPath &path,
		const BlockPath &list,
		int removedItemIndex) {
	return ShiftBlockContainerPathAfterRemovedListItem(
		path.container,
		list,
		removedItemIndex);
}

[[nodiscard]] std::optional<int> BlockIndexInContainer(
		const LeafPath &leaf,
		const BlockContainerPath &container) {
	if (leaf.block.container == container) {
		return leaf.block.index;
	}
	if (!ContainerHasPrefix(leaf.block.container, container)
		|| leaf.block.container.steps.size() <= container.steps.size()) {
		return std::nullopt;
	}
	const auto &step = leaf.block.container.steps[container.steps.size()];
	return (step.kind == BlockContainerKind::BlockChildren
			|| step.kind == BlockContainerKind::ListItemChildren)
		? std::make_optional(step.blockIndex)
		: std::nullopt;
}

[[nodiscard]] std::optional<int> ListItemIndexForLeaf(
		const LeafPath &leaf,
		const BlockPath &block) {
	if (leaf.block == block && leaf.kind == LeafKind::ListItemText) {
		return leaf.listItemIndex;
	}
	if (!ContainerHasPrefix(leaf.block.container, block.container)
		|| leaf.block.container.steps.size() <= block.container.steps.size()) {
		return std::nullopt;
	}
	const auto &step = leaf.block.container.steps[block.container.steps.size()];
	return (step.kind == BlockContainerKind::ListItemChildren
			&& step.blockIndex == block.index)
		? std::make_optional(step.listItemIndex)
		: std::nullopt;
}

[[nodiscard]] std::optional<int> TableRowIndexForLeaf(
		const LeafPath &leaf,
		const BlockPath &block) {
	if (!(leaf.block == block)) {
		return std::nullopt;
	}
	if (leaf.kind == LeafKind::BlockText) {
		return -1;
	}
	return (leaf.kind == LeafKind::TableCellText)
		? std::make_optional(leaf.tableRowIndex)
		: std::nullopt;
}

[[nodiscard]] std::optional<int> TableCellIndexForLeaf(
		const LeafPath &leaf,
		const BlockPath &block,
		int rowIndex) {
	return (leaf.block == block
			&& leaf.kind == LeafKind::TableCellText
			&& leaf.tableRowIndex == rowIndex)
		? std::make_optional(leaf.tableCellIndex)
		: std::nullopt;
}

using TableGridOccupancyRow = std::vector<char>;
using TableGridOccupancy = std::vector<TableGridOccupancyRow>;

struct TableGridCellReference {
	int rowIndex = -1;
	int cellIndex = -1;
	int rowFrom = -1;
	int rowTill = -1;
	int columnFrom = -1;
	int columnTill = -1;
};

struct TableGrid {
	std::vector<TableGridCellReference> cells;
	TableGridOccupancy occupancy;
	int rowCount = 0;
	int columnCount = 0;
};

[[nodiscard]] int NormalizeTableSpan(int span) {
	return std::max(span, 1);
}

[[nodiscard]] int ClampTableRowspan(
		int rawRowspan,
		int row,
		int rowCount) {
	if ((row < 0) || (row >= rowCount) || (rowCount <= 0)) {
		return 0;
	}
	const auto remainingRows = int64(rowCount) - row;
	return int(std::min<int64>(NormalizeTableSpan(rawRowspan), remainingRows));
}

[[nodiscard]] int ClampTableColspan(
		int rawColspan,
		int column,
		int maxColumns) {
	if ((column < 0) || (column >= maxColumns) || (maxColumns <= 0)) {
		return 0;
	}
	const auto remainingColumns = int64(maxColumns) - column;
	return int(std::min<int64>(
		NormalizeTableSpan(rawColspan),
		remainingColumns));
}

[[nodiscard]] bool CanOccupyTableSlots(
		const TableGridOccupancy &occupancy,
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

[[nodiscard]] int FirstAvailableTableColumn(
		const TableGridOccupancy &occupancy,
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
		const auto effectiveColspan = ClampTableColspan(
			colspan,
			column,
			maxColumns);
		if (effectiveColspan <= 0) {
			continue;
		}
		if (CanOccupyTableSlots(
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

void MarkTableSlots(
		TableGridOccupancy *occupancy,
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

[[nodiscard]] int TableGridColumnCount(
		const TableGridOccupancy &occupancy) {
	auto result = 0;
	for (const auto &row : occupancy) {
		result = std::max(result, int(row.size()));
	}
	return result;
}

[[nodiscard]] TableGrid BuildTableGrid(
		const Block &table,
		const Markdown::MarkdownPrepareTableRenderLimits &limits) {
	auto result = TableGrid();
	result.rowCount = std::min(int(table.tableRows.size()), limits.maxRows);
	if (result.rowCount < 0) {
		result.rowCount = 0;
	}
	result.occupancy = TableGridOccupancy(result.rowCount);
	auto occupiedSlotCountSoFar = int64(0);

	for (auto rowIndex = 0; rowIndex != result.rowCount; ++rowIndex) {
		const auto &row = table.tableRows[rowIndex];
		for (auto cellIndex = 0, cellCount = int(row.cells.size());
				cellIndex != cellCount;
				++cellIndex) {
			const auto &cell = row.cells[cellIndex];
			const auto normalizedColspan = NormalizeTableSpan(cell.colspan);
			const auto rowspan = ClampTableRowspan(
				cell.rowspan,
				rowIndex,
				result.rowCount);
			if (rowspan <= 0) {
				continue;
			}
			const auto column = FirstAvailableTableColumn(
				result.occupancy,
				rowIndex,
				rowspan,
				normalizedColspan,
				limits.maxColumns);
			if (column < 0) {
				continue;
			}
			const auto colspan = ClampTableColspan(
				normalizedColspan,
				column,
				limits.maxColumns);
			if (colspan <= 0) {
				continue;
			}
			const auto occupiedSlotGrowth = int64(rowspan) * colspan;
			if (occupiedSlotGrowth > limits.maxCells
				|| (occupiedSlotCountSoFar + occupiedSlotGrowth)
					> limits.maxCells) {
				continue;
			}
			result.cells.push_back({
				.rowIndex = rowIndex,
				.cellIndex = cellIndex,
				.rowFrom = rowIndex,
				.rowTill = rowIndex + rowspan,
				.columnFrom = column,
				.columnTill = column + colspan,
			});
			MarkTableSlots(
				&result.occupancy,
				rowIndex,
				column,
				rowspan,
				colspan);
			occupiedSlotCountSoFar += occupiedSlotGrowth;
		}
	}
	result.columnCount = TableGridColumnCount(result.occupancy);
	return result;
}

template <typename Range>
[[nodiscard]] bool TableGridCellIntersectsRange(
		const TableGridCellReference &cell,
		const Range &range) {
	return (cell.rowFrom < range.rowTill)
		&& (cell.rowTill > range.rowFrom)
		&& (cell.columnFrom < range.columnTill)
		&& (cell.columnTill > range.columnFrom);
}

template <typename Range>
[[nodiscard]] bool TableGridCellContainedInRange(
		const TableGridCellReference &cell,
		const Range &range) {
	return (cell.rowFrom >= range.rowFrom)
		&& (cell.rowTill <= range.rowTill)
		&& (cell.columnFrom >= range.columnFrom)
		&& (cell.columnTill <= range.columnTill);
}

template <typename Range>
[[nodiscard]] std::vector<TableGridCellReference> SelectedTableGridCells(
		const TableGrid &grid,
		const Range &range) {
	auto result = std::vector<TableGridCellReference>();
	result.reserve(grid.cells.size());
	for (const auto &cell : grid.cells) {
		if (TableGridCellIntersectsRange(cell, range)) {
			result.push_back(cell);
		}
	}
	return result;
}

template <typename Range>
[[nodiscard]] bool TableGridRangeCovered(
		const TableGrid &grid,
		const Range &range) {
	if ((range.rowFrom < 0)
		|| (range.rowTill <= range.rowFrom)
		|| (range.columnFrom < 0)
		|| (range.columnTill <= range.columnFrom)
		|| (range.rowTill > grid.rowCount)
		|| (range.columnTill > grid.columnCount)) {
		return false;
	}
	for (auto row = range.rowFrom; row != range.rowTill; ++row) {
		if (row >= int(grid.occupancy.size())) {
			return false;
		}
		const auto &occupied = grid.occupancy[row];
		for (auto column = range.columnFrom;
				column != range.columnTill;
				++column) {
			if (column >= int(occupied.size()) || !occupied[column]) {
				return false;
			}
		}
	}
	return true;
}

template <typename Range>
[[maybe_unused]] [[nodiscard]] bool CleanTableGridUniteRange(
		const TableGrid &grid,
		const Range &range) {
	const auto selected = SelectedTableGridCells(grid, range);
	if (selected.empty()) {
		return false;
	}
	for (const auto &cell : selected) {
		if (!TableGridCellContainedInRange(cell, range)) {
			return false;
		}
	}
	return TableGridRangeCovered(grid, range);
}

template <typename Range>
[[nodiscard]] bool TableGridRangeSpansAllRows(
		const TableGrid &grid,
		const Range &range) {
	return (range.rowFrom == 0)
		&& (range.rowTill == grid.rowCount)
		&& (range.columnFrom >= 0)
		&& (range.columnTill > range.columnFrom)
		&& (range.columnTill <= grid.columnCount);
}

template <typename Range>
[[nodiscard]] bool TableGridRangeSpansAllColumns(
		const TableGrid &grid,
		const Range &range) {
	return (range.rowFrom >= 0)
		&& (range.rowTill > range.rowFrom)
		&& (range.rowTill <= grid.rowCount)
		&& (range.columnFrom == 0)
		&& (range.columnTill == grid.columnCount);
}

template <typename Range>
[[nodiscard]] bool TableGridRangeCoversFullTable(
		const TableGrid &grid,
		const Range &range) {
	return TableGridRangeSpansAllRows(grid, range)
		&& TableGridRangeSpansAllColumns(grid, range);
}

template <typename Range>
[[nodiscard]] int TableGridCellColumnIntersection(
		const TableGridCellReference &cell,
		const Range &range) {
	const auto from = std::max(cell.columnFrom, range.columnFrom);
	const auto till = std::min(cell.columnTill, range.columnTill);
	return std::max(till - from, 0);
}

[[nodiscard]] bool TableGridCellMatchesLeaf(
		const TableGridCellReference &cell,
		const LeafPath &leaf,
		const BlockPath &block) {
	const auto index = TableCellIndexForLeaf(leaf, block, cell.rowIndex);
	return index && *index == cell.cellIndex;
}

[[nodiscard]] TableCell MakeDefaultTableCell() {
	return TableCell();
}

[[nodiscard]] TableCell MakeDefaultTableCell(bool header) {
	auto result = MakeDefaultTableCell();
	result.header = header;
	return result;
}

[[nodiscard]] const TableCell *TableGridCellAt(
		const Block &table,
		const TableGrid &grid,
		int row,
		int column) {
	if (row < 0 || row >= grid.rowCount || column < 0) {
		return nullptr;
	}
	for (const auto &reference : grid.cells) {
		if (reference.rowFrom <= row
			&& reference.rowTill > row
			&& reference.columnFrom <= column
			&& reference.columnTill > column) {
			return &table.tableRows[reference.rowIndex].cells[
				reference.cellIndex];
		}
	}
	return nullptr;
}

[[nodiscard]] int IncrementTableSpan(int span) {
	const auto normalized = NormalizeTableSpan(span);
	return (normalized == std::numeric_limits<int>::max())
		? normalized
		: normalized + 1;
}

void InsertTableCellBeforeVisualColumn(
		TableRow *row,
		const TableGrid &grid,
		int rowIndex,
		int column,
		TableCell insertedCell) {
	auto insertAt = int(row->cells.size());
	for (const auto &reference : grid.cells) {
		if (reference.rowIndex == rowIndex
			&& reference.columnFrom >= column) {
			insertAt = std::min(reference.cellIndex, int(row->cells.size()));
			break;
		}
	}
	row->cells.insert(
		row->cells.begin() + insertAt,
		std::move(insertedCell));
}

[[nodiscard]] bool BlockCanOwnChildContainer(const Block &block) {
	return (block.kind == BlockKind::Quote)
		|| (block.kind == BlockKind::Details);
}

[[nodiscard]] bool BlockSupportsBlockText(const Block &block) {
	switch (block.kind) {
	case BlockKind::Heading:
	case BlockKind::Paragraph:
	case BlockKind::Footer:
	case BlockKind::Code:
	case BlockKind::Quote:
	case BlockKind::Table:
	case BlockKind::Details:
		return true;
	default:
		return false;
	}
}

[[nodiscard]] bool BlockSupportsBlockCaption(const Block &block) {
	switch (block.kind) {
	case BlockKind::Quote:
	case BlockKind::Photo:
	case BlockKind::Video:
	case BlockKind::Audio:
	case BlockKind::Map:
	case BlockKind::GroupedMedia:
		return true;
	default:
		return false;
	}
}

[[nodiscard]] bool StringIsEmpty(const QString &text) {
	return text.trimmed().isEmpty();
}

[[nodiscard]] bool RichTextHasVisibleText(const RichText &text) {
	return !StringIsEmpty(text.text.text);
}

void MergeRichTextAnchors(RichText *target, RichText source) {
	if (!target) {
		return;
	}
	if (!source.anchorId.isEmpty()) {
		if (target->anchorId.isEmpty()) {
			target->anchorId = std::move(source.anchorId);
		} else {
			target->anchorIds.push_back(std::move(source.anchorId));
		}
	}
	for (auto &anchorId : source.anchorIds) {
		if (!anchorId.isEmpty()) {
			target->anchorIds.push_back(std::move(anchorId));
		}
	}
}

[[nodiscard]] bool JoinableTextBlockKind(BlockKind kind) {
	return (kind == BlockKind::Heading)
		|| (kind == BlockKind::Paragraph)
		|| (kind == BlockKind::Footer);
}

[[nodiscard]] int AppendParagraphSeam(Block *destination, Block &&source) {
	auto updated = std::move(destination->text.text);
	const auto seamOffset = int(updated.text.size());
	updated.append(std::move(source.text.text));
	destination->text.text = std::move(updated);
	MergeRichTextAnchors(&destination->text, std::move(source.text));
	if (!source.anchorId.isEmpty()) {
		auto anchor = RichText();
		anchor.anchorId = std::move(source.anchorId);
		MergeRichTextAnchors(&destination->text, std::move(anchor));
	}
	return seamOffset;
}

[[nodiscard]] bool CanEditBlocks(const std::vector<Block> &blocks);

[[nodiscard]] bool CanEditBlock(const Block &block) {
	switch (block.kind) {
	case BlockKind::Heading:
	case BlockKind::Paragraph:
	case BlockKind::Footer:
	case BlockKind::Code:
	case BlockKind::Divider:
	case BlockKind::Anchor:
	case BlockKind::GroupedMedia:
	case BlockKind::Photo:
	case BlockKind::Video:
	case BlockKind::Audio:
	case BlockKind::Math:
	case BlockKind::Table:
	case BlockKind::Map:
		return true;
	case BlockKind::Quote:
	case BlockKind::Details:
		return CanEditBlocks(block.blocks);
	case BlockKind::List:
		return ranges::all_of(block.listItems, [](const ListItem &item) {
			return CanEditBlocks(item.blocks);
		});
	case BlockKind::Unsupported:
	case BlockKind::Thinking:
	case BlockKind::AuthorDate:
	case BlockKind::Embed:
	case BlockKind::EmbedPost:
	case BlockKind::Channel:
	case BlockKind::RelatedArticles:
		return false;
	}
	return false;
}

[[nodiscard]] bool CanEditBlocks(const std::vector<Block> &blocks) {
	return ranges::all_of(blocks, &CanEditBlock);
}

} // namespace

State::State()
: State(std::make_shared<RichPage>(), nullptr, RichMessageLimits()) {
}

State::State(
	std::shared_ptr<RichPage> richPage,
	std::shared_ptr<Markdown::MediaRuntime> mediaRuntime,
	RichMessageLimits limits)
: _richPage(richPage ? std::move(richPage) : std::make_shared<RichPage>())
, _mediaRuntime(std::move(mediaRuntime))
, _limits(std::move(limits)) {
	if (_richPage->blocks.empty()) {
		_richPage->blocks.push_back(MakeParagraphBlock());
	}
	StripEditModeWrapperEntities(_richPage->blocks);
	rebuild();
}

const RichPage &State::richPage() const {
	return *_richPage;
}

bool State::articleEmpty() const {
	return ranges::all_of(_richPage->blocks, [](const auto &block) {
		return BlockIsEmpty(block);
	});
}

const Markdown::MarkdownArticleContent &State::prepared() const {
	return _prepared;
}

auto State::tableRenderLimits() const
-> Markdown::MarkdownPrepareTableRenderLimits {
	return Markdown::PrepareTableRenderLimitsForRichMessage(_limits);
}

template <typename Result, typename Callback>
Result State::applyCheckedMutation(Result failure, Callback &&callback) {
	_lastLimitError = std::nullopt;
	auto candidate = State(
		std::make_shared<RichPage>(*_richPage),
		_mediaRuntime,
		_limits);
	candidate._activeTextOrdinal = _activeTextOrdinal;
	candidate._lastLimitError = std::nullopt;
	candidate._temporaryDownParagraph = _temporaryDownParagraph;
	const auto outcome = callback(candidate);
	if (!outcome.apply) {
		return outcome.result;
	}
	if (const auto error = ValidateRichMessage(*candidate._richPage, _limits)) {
		_lastLimitError = error;
		return failure;
	}
	commitCheckedMutation(std::move(candidate));
	return outcome.result;
}

void State::commitCheckedMutation(State state) {
	_richPage = std::move(state._richPage);
	_prepared = std::move(state._prepared);
	_textNodes = std::move(state._textNodes);
	_activeTextOrdinal = state._activeTextOrdinal;
	_lastPreparedMutationKind = (
		state._lastPreparedMutationKind == PreparedMutationKind::FullRebuild)
		? state._lastPreparedMutationKind
		: PreparedMutationKind::FullRebuild;
	_lastLimitError = std::nullopt;
	_temporaryDownParagraph = std::move(state._temporaryDownParagraph);
}

const std::vector<TextNodeDescriptor> &State::textNodes() const {
	return _textNodes;
}

State::Snapshot State::snapshot() const {
	return {
		.richPage = *_richPage,
		.activeLeaf = activeLeafPath(),
		.temporaryDownParagraph = _temporaryDownParagraph,
	};
}

void State::restoreSnapshot(Snapshot snapshot) {
	_richPage = std::make_shared<RichPage>(std::move(snapshot.richPage));
	_activeTextOrdinal = -1;
	_lastLimitError = std::nullopt;
	_temporaryDownParagraph = std::move(snapshot.temporaryDownParagraph);
	rebuild();
	if (snapshot.activeLeaf && (textNodeOrdinal(*snapshot.activeLeaf) >= 0)) {
		const auto activated = activateRebuiltLeaf(*snapshot.activeLeaf);
		Assert(activated);
	} else {
		ensureActiveTextOrdinal();
	}
}

std::optional<LeafPath> State::activeLeafPath() const {
	if (const auto descriptor = textNode(_activeTextOrdinal)) {
		return descriptor->leaf;
	}
	return std::nullopt;
}

int State::textOrdinalForLeafPath(const LeafPath &path) const {
	return textNodeOrdinal(path);
}

void State::clearTemporaryDownParagraph() {
	_temporaryDownParagraph = std::nullopt;
}

void State::clearTemporaryDownParagraphIfInvalid() {
	if (!_temporaryDownParagraph
		|| _temporaryDownParagraph->kind != LeafKind::BlockText
		|| (textNodeOrdinal(*_temporaryDownParagraph) < 0)) {
		clearTemporaryDownParagraph();
		return;
	}
	const auto owner = block(_temporaryDownParagraph->block);
	if (!owner
		|| owner->kind != BlockKind::Paragraph
		|| !BlockIsEmpty(*owner)) {
		clearTemporaryDownParagraph();
	}
}

int State::textOrdinalForLeaf(
		const Markdown::PreparedEditLeafSource &source) const {
	const auto leaf = convertLeafPath(source);
	return leaf ? textOrdinalForLeafPath(*leaf) : -1;
}

std::optional<PreparedEditLeafSource> State::preparedLeafSourceForOrdinal(
		int ordinal) const {
	const auto descriptor = textNode(ordinal);
	return descriptor ? convertPreparedLeafSource(*descriptor) : std::nullopt;
}

PreparedMutationKind State::lastPreparedMutationKind() const {
	return _lastPreparedMutationKind;
}

std::optional<PreparedEditLeafSource> State::activePreparedLeafSource() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	return descriptor ? convertPreparedLeafSource(*descriptor) : std::nullopt;
}

std::vector<TextNodeSpan> State::resolveTextSpansForPreparedLeafRange(
		const PreparedEditLeafSource &source,
		int from,
		int till) const {
	if (from < 0 || till <= from) {
		return {};
	}
	const auto firstLeaf = convertLeafPath(source);
	if (!firstLeaf) {
		return {};
	}
	const auto firstOrdinal = textOrdinalForLeafPath(*firstLeaf);
	if (firstOrdinal < 0) {
		return {};
	}
	auto result = std::vector<TextNodeSpan>();
	auto consumed = 0;
	for (auto i = firstOrdinal, count = textNodeCount()
		; i != count && consumed < till
		; ++i) {
		const auto current = richText(_textNodes[i].leaf);
		if (!current) {
			return {};
		}
		const auto length = int(current->text.text.size());
		const auto spanFrom = std::max(from - consumed, 0);
		const auto spanTo = std::min(till - consumed, length);
		if (spanFrom < spanTo) {
			result.push_back(TextNodeSpan{
				.leaf = _textNodes[i].leaf,
				.from = spanFrom,
				.till = spanTo,
			});
		}
		consumed += length;
	}
	return (consumed >= till) ? result : std::vector<TextNodeSpan>();
}

int State::textNodeCount() const {
	return int(_textNodes.size());
}

int State::activeTextOrdinal() const {
	return _activeTextOrdinal;
}

bool State::setActiveTextByOrdinal(int ordinal) {
	if (ordinal < 0 || ordinal >= textNodeCount()) {
		return false;
	}
	if (_temporaryDownParagraph
		&& !(_textNodes[ordinal].leaf == *_temporaryDownParagraph)) {
		clearTemporaryDownParagraph();
	}
	_activeTextOrdinal = ordinal;
	return true;
}

TextWithEntities State::activeText() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return TextWithEntities();
	}
	if (const auto current = richText(descriptor->leaf)) {
		return StripEditModeWrapperEntities(current->text);
	}
	if (const auto current = rawText(descriptor->leaf)) {
		return MakeText(*current);
	}
	return TextWithEntities();
}

ApplyResult State::applyActiveText(TextWithEntities text) {
	_lastLimitError = std::nullopt;
	_lastPreparedMutationKind = PreparedMutationKind::None;
	return applyActiveTextWithLocalLimit(std::move(text));
}

ApplyResult State::applyActiveTextUnchecked(TextWithEntities text) {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return ApplyResult::Failed;
	}
	if (auto current = richText(descriptor->leaf)) {
		if (current->text == text) {
			return ApplyResult::Unchanged;
		}
		current->text = std::move(text);
		if (leafMutationKeepsTextNodes(*descriptor)) {
			if (_temporaryDownParagraph
				&& (descriptor->leaf == *_temporaryDownParagraph)
				&& !RichTextIsEmpty(*current)) {
				clearTemporaryDownParagraph();
			}
			if (updatePreparedActiveLeaf(*descriptor)) {
				_lastPreparedMutationKind = PreparedMutationKind::LeafOnly;
			} else {
				rebuildPrepared();
			}
		} else {
			rebuild();
		}
		return ApplyResult::Changed;
	}
	if (auto current = rawText(descriptor->leaf)) {
		if (*current == text.text) {
			return ApplyResult::Unchanged;
		}
		*current = std::move(text.text);
		if (leafMutationKeepsTextNodes(*descriptor)) {
			if (updatePreparedActiveLeaf(*descriptor)) {
				_lastPreparedMutationKind = PreparedMutationKind::LeafOnly;
			} else {
				rebuildPrepared();
			}
		} else {
			rebuild();
		}
		return ApplyResult::Changed;
	}
	return ApplyResult::Failed;
}

ApplyResult State::applyActiveTextWithLocalLimit(TextWithEntities text) {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return ApplyResult::Failed;
	}
	auto chunks = SplitFieldText(std::move(text));
	if (chunks.size() <= 1) {
		return applyActiveTextUnchecked(chunks.empty()
			? TextWithEntities()
			: std::move(chunks.front()));
	}
	auto first = chunks.front();
	if (const auto result = applySplitParagraphText(
			*descriptor,
			std::move(chunks)); result != ApplyResult::Failed) {
		return result;
	}
	return applyActiveTextUnchecked(std::move(first));
}

FieldMode State::activeFieldMode() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	return descriptor ? descriptor->mode : FieldMode::Rich;
}

QString State::activeRawText() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return QString();
	}
	if (const auto current = rawText(descriptor->leaf)) {
		return *current;
	}
	if (const auto current = richText(descriptor->leaf)) {
		return StripEditModeWrapperEntities(current->text).text;
	}
	return QString();
}

QString State::activePlaceholderText() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return QString();
	}
	const auto owner = block(descriptor->leaf.block);
	if (!owner) {
		return QString();
	}
	switch (descriptor->leaf.kind) {
	case LeafKind::BlockText:
		switch (owner->kind) {
		case BlockKind::Quote:
			return tr::lng_article_placeholder_quote(tr::now);
		case BlockKind::Heading:
			return Markdown::HeadingLevelLabel(owner->headingLevel);
		case BlockKind::Footer:
			return tr::lng_article_insert_footer(tr::now);
		case BlockKind::Details:
			return tr::lng_article_table_header(tr::now);
		default:
			return QString();
		}
	case LeafKind::BlockCaption:
		switch (owner->kind) {
		case BlockKind::Quote:
			return tr::lng_article_placeholder_author(tr::now);
		case BlockKind::Photo:
		case BlockKind::Video:
		case BlockKind::Audio:
		case BlockKind::Map:
		case BlockKind::GroupedMedia:
			return tr::lng_photo_caption(tr::now);
		default:
			return QString();
		}
	case LeafKind::TableCellText: {
		const auto cell = tableCell(
			descriptor->leaf.block,
			descriptor->leaf.tableRowIndex,
			descriptor->leaf.tableCellIndex);
		return (cell && cell->header)
			? tr::lng_article_table_header(tr::now)
			: tr::lng_article_placeholder_cell(tr::now);
	}
	case LeafKind::MathFormula:
		return u"x^2 + y^2"_q;
	}
	return QString();
}

ApplyResult State::applyActiveRawText(QString text) {
	_lastLimitError = std::nullopt;
	_lastPreparedMutationKind = PreparedMutationKind::None;
	return applyActiveRawTextWithLocalLimit(std::move(text));
}

ApplyResult State::applyActiveRawTextUnchecked(QString text) {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return ApplyResult::Failed;
	}
	if (auto current = rawText(descriptor->leaf)) {
		if (*current == text) {
			return ApplyResult::Unchanged;
		}
		*current = std::move(text);
		if (leafMutationKeepsTextNodes(*descriptor)) {
			if (updatePreparedActiveLeaf(*descriptor)) {
				_lastPreparedMutationKind = PreparedMutationKind::LeafOnly;
			} else {
				rebuildPrepared();
			}
		} else {
			rebuild();
		}
		return ApplyResult::Changed;
	}
	if (auto current = richText(descriptor->leaf)) {
		auto updated = MakeText(std::move(text));
		if (current->text == updated) {
			return ApplyResult::Unchanged;
		}
		current->text = std::move(updated);
		if (leafMutationKeepsTextNodes(*descriptor)) {
			if (_temporaryDownParagraph
				&& (descriptor->leaf == *_temporaryDownParagraph)
				&& !RichTextIsEmpty(*current)) {
				clearTemporaryDownParagraph();
			}
			if (updatePreparedActiveLeaf(*descriptor)) {
				_lastPreparedMutationKind = PreparedMutationKind::LeafOnly;
			} else {
				rebuildPrepared();
			}
		} else {
			rebuild();
		}
		return ApplyResult::Changed;
	}
	return ApplyResult::Failed;
}

ApplyResult State::applyActiveRawTextWithLocalLimit(QString text) {
	auto chunks = SplitFieldText(MakeText(std::move(text)));
	return applyActiveRawTextUnchecked(chunks.empty()
		? QString()
		: std::move(chunks.front().text));
}

State::ApplyResult State::applyFormattingToTextSpans(
		const std::vector<TextNodeSpan> &spans,
		TextFormattingAction action,
		std::optional<bool> enabled) {
	if (spans.empty()) {
		return ApplyResult::Unchanged;
	}
	return applyCheckedMutation(ApplyResult::Failed, [
		spans,
		action,
		enabled
	](State &candidate) {
		const auto tag = FormattingActionTag(action);
		const auto shouldEnable = enabled.value_or([&] {
			if (!tag) {
				return false;
			}
			auto any = false;
			for (const auto &span : spans) {
				const auto current = candidate.richText(span.leaf);
				if (!current) {
					continue;
				}
				auto before = TextWithEntities();
				auto selected = TextWithEntities();
				auto after = TextWithEntities();
				if (!SplitTextSpan(
						current->text,
						span.from,
						span.till,
						&before,
						&selected,
						&after)) {
					continue;
				}
				any = true;
				if (!HasFullTextTag(
						ConvertRichTextToEditorTags(std::move(selected)).text,
						*tag)) {
					return true;
				}
			}
			return any ? false : true;
		}());
		auto changed = false;
		for (const auto &span : spans) {
			const auto current = candidate.richText(span.leaf);
			if (!current) {
				continue;
			}
			auto before = TextWithEntities();
			auto selected = TextWithEntities();
			auto after = TextWithEntities();
			if (!SplitTextSpan(
					current->text,
					span.from,
					span.till,
					&before,
					&selected,
					&after)) {
				if (action != TextFormattingAction::PlainText
					|| span.leaf.kind != LeafKind::BlockText
					|| !current->text.text.isEmpty()) {
					continue;
				}
			}
			auto converted = ConvertRichTextToEditorTags(std::move(selected));
			if (action == TextFormattingAction::PlainText) {
				converted.text.tags.clear();
			} else if (tag) {
				if (shouldEnable) {
					OverlayTag(
						&converted.text.tags,
						{
							.offset = 0,
							.length = int(converted.text.text.size()),
							.id = *tag,
						},
						converted.text.text);
				} else {
					RemoveTagFromSelection(&converted.text.tags, *tag);
				}
			}
			auto demoted = false;
			if (action == TextFormattingAction::PlainText
				&& span.leaf.kind == LeafKind::BlockText
				&& before.text.isEmpty()
				&& after.text.isEmpty()) {
				if (const auto owner = candidate.block(span.leaf.block);
					owner
					&& (owner->kind == BlockKind::Heading
						|| owner->kind == BlockKind::Footer)) {
					owner->kind = BlockKind::Paragraph;
					owner->headingLevel = 0;
					demoted = true;
				}
			}
			auto updated = JoinText(
				std::move(before),
				ConvertEditorTagsToRichText(std::move(converted.text)),
				std::move(after));
			if (current->text != updated) {
				current->text = std::move(updated);
				changed = true;
			}
			if (demoted) {
				changed = true;
			}
		}
		if (!changed) {
			return CheckedMutationResult<ApplyResult>{
				.result = ApplyResult::Unchanged,
			};
		}
		candidate.rebuild();
		return CheckedMutationResult<ApplyResult>{
			.apply = true,
			.result = ApplyResult::Changed,
		};
	});
}

bool State::toggleSpoilerOnBlocks(
		const std::vector<BlockPath> &blocks,
		std::optional<bool> enabled) {
	if (blocks.empty()) {
		return false;
	}
	return applyCheckedMutation(false, [blocks, enabled](State &candidate) {
		const auto shouldEnable = enabled.value_or([&] {
			auto any = false;
			for (const auto &path : blocks) {
				const auto current = candidate.block(path);
				if (!current || !MediaBlockSupportsSpoiler(*current)) {
					continue;
				}
				any = true;
				if (!MediaBlockHasSpoiler(*current)) {
					return true;
				}
			}
			return any ? false : true;
		}());
		auto changed = false;
		for (const auto &path : blocks) {
			const auto current = candidate.block(path);
			changed |= SetMediaBlockSpoiler(current, shouldEnable);
		}
		if (!changed) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		candidate.rebuild();
		return CheckedMutationResult<bool>{
			.apply = true,
			.result = true,
		};
	});
}

bool State::toggleSpoilerOnGroupedItem(
		const BlockPath &path,
		int itemIndex,
		std::optional<bool> enabled) {
	if (itemIndex < 0) {
		return false;
	}
	return applyCheckedMutation(false, [path, itemIndex, enabled](
			State &candidate) {
		const auto current = candidate.block(path);
		if (!current
			|| current->kind != BlockKind::GroupedMedia
			|| itemIndex >= int(current->mediaItems.size())) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		auto &item = current->mediaItems[itemIndex];
		if ((item.kind != BlockKind::Photo)
			&& (item.kind != BlockKind::Video)
			&& (item.kind != BlockKind::Audio)
			&& (item.kind != BlockKind::Map)) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		const auto shouldEnable = enabled.value_or(!item.spoiler);
		if (item.spoiler == shouldEnable) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		item.spoiler = shouldEnable;
		candidate.rebuild();
		return CheckedMutationResult<bool>{
			.apply = true,
			.result = true,
		};
	});
}

std::optional<State::ReplaceTarget> State::replaceTargetForBlock(
		const BlockPath &path) const {
	const auto current = block(path);
	if (!current || !IsReplaceableMediaBlockKind(current->kind)) {
		return std::nullopt;
	}
	const auto mediaId = ReplaceTargetMediaId(*current);
	if (!mediaId) {
		return std::nullopt;
	}
	return ReplaceTarget{
		.path = path,
		.kind = current->kind,
		.mediaId = *mediaId,
	};
}

std::optional<State::ReplaceTarget> State::replaceTargetForGroupedItem(
		const BlockPath &path,
		int itemIndex) const {
	const auto current = block(path);
	if (!current
		|| current->kind != BlockKind::GroupedMedia
		|| itemIndex < 0
		|| itemIndex >= int(current->mediaItems.size())) {
		return std::nullopt;
	}
	const auto &item = current->mediaItems[itemIndex];
	if (!IsPhotoVideoBlockKind(item.kind)) {
		return std::nullopt;
	}
	const auto mediaId = ReplaceTargetMediaId(item);
	if (!mediaId) {
		return std::nullopt;
	}
	return ReplaceTarget{
		.path = path,
		.kind = item.kind,
		.mediaId = *mediaId,
		.itemIndex = itemIndex,
	};
}

bool State::replaceBlockWithPreparedBlock(
		const ReplaceTarget &target,
		Block block) {
	return applyCheckedMutation(false, [
		target,
		block = std::move(block)
	](State &candidate) mutable {
		const auto &path = target.path;
		const auto blocks = candidate.blockContainer(path.container);
		if (!blocks
			|| path.index < 0
			|| path.index >= int(blocks->size())) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		auto &current = (*blocks)[path.index];
		if (target.itemIndex >= 0) {
			if (current.kind != BlockKind::GroupedMedia
				|| target.itemIndex >= int(current.mediaItems.size())) {
				return CheckedMutationResult<bool>{ .result = false };
			}
			auto &item = current.mediaItems[target.itemIndex];
			if (!GroupedItemMatchesReplaceTarget(item, target)) {
				return CheckedMutationResult<bool>{ .result = false };
			}
			auto replacement = GroupedItemFromPhotoVideoBlock(block);
			if (!replacement) {
				return CheckedMutationResult<bool>{ .result = false };
			}
			replacement->spoiler = item.spoiler;
			item = std::move(*replacement);
			candidate.rebuild();
			return CheckedMutationResult<bool>{
				.apply = true,
				.result = true,
			};
		}
		if (!BlockMatchesReplaceTarget(current, target)
			|| !IsReplaceableMediaBlockKind(block.kind)) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		block.caption = std::move(current.caption);
		block.anchorId = std::move(current.anchorId);
		if (IsPhotoVideoBlockKind(current.kind)
			&& IsPhotoVideoBlockKind(block.kind)) {
			block.spoiler = current.spoiler;
		}
		current = std::move(block);
		candidate.rebuild();
		return CheckedMutationResult<bool>{
			.apply = true,
			.result = true,
		};
	});
}

std::optional<int> State::removeBlock(
		const BlockPath &path,
		bool forward) {
	return removeStructuralSelection(preparedSelectionForBlock(path), forward);
}

bool State::canGroupPhotoVideoBlocks(
		const PreparedEditSelection &selection) const {
	if (selection.kind != PreparedEditSelectionKind::Blocks) {
		return false;
	}
	const auto range = validateBlockRange(selection.blocks);
	if (!range || range->till - range->from < 2) {
		return false;
	}
	const auto blocks = blockContainer(range->container);
	if (!blocks) {
		return false;
	}
	for (auto i = range->from; i != range->till; ++i) {
		if (!IsPhotoVideoBlockKind((*blocks)[i].kind)) {
			return false;
		}
	}
	return HasValidGroupingCaptionAndAnchorSource(
		*blocks,
		range->from,
		range->till);
}

bool State::groupPhotoVideoBlocks(
		const PreparedEditSelection &selection,
		RichPage::GroupedMediaIntent intent) {
	return applyCheckedMutation(false, [selection, intent](State &candidate) {
		if (!candidate.canGroupPhotoVideoBlocks(selection)) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		const auto range = candidate.validateBlockRange(selection.blocks);
		if (!range) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		auto *blocks = candidate.blockContainer(range->container);
		if (!blocks) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		auto items = std::vector<RichPage::GroupedMediaItem>();
		items.reserve(range->till - range->from);
		auto captionSource = std::optional<int>();
		for (auto i = range->from; i != range->till; ++i) {
			const auto item = GroupedItemFromPhotoVideoBlock((*blocks)[i]);
			if (!item) {
				return CheckedMutationResult<bool>{ .result = false };
			}
			items.push_back(*item);
			if (!BlockHasGroupingCaptionOrAnchor((*blocks)[i])) {
				continue;
			} else if (captionSource) {
				return CheckedMutationResult<bool>{ .result = false };
			}
			captionSource = i;
		}
		auto grouped = Block();
		grouped.kind = BlockKind::GroupedMedia;
		grouped.mediaIntent = intent;
		grouped.mediaItems = std::move(items);
		if (captionSource) {
			auto &source = (*blocks)[*captionSource];
			grouped.caption = std::move(source.caption);
			grouped.anchorId = std::move(source.anchorId);
		}
		blocks->erase(
			blocks->begin() + range->from,
			blocks->begin() + range->till);
		blocks->insert(blocks->begin() + range->from, std::move(grouped));
		candidate.rebuild();
		return CheckedMutationResult<bool>{
			.apply = true,
			.result = true,
		};
	});
}

bool State::ungroupGroupedMediaBlock(const BlockPath &path) {
	return applyCheckedMutation(false, [path](State &candidate) {
		auto *blocks = candidate.blockContainer(path.container);
		if (!blocks
			|| path.index < 0
			|| path.index >= int(blocks->size())) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		auto &current = (*blocks)[path.index];
		if (current.kind != BlockKind::GroupedMedia
			|| current.mediaItems.empty()) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		auto emitted = std::vector<Block>();
		emitted.reserve(current.mediaItems.size());
		for (const auto &item : current.mediaItems) {
			auto block = PhotoVideoBlockFromGroupedItem(item);
			if (!block) {
				return CheckedMutationResult<bool>{ .result = false };
			}
			emitted.push_back(std::move(*block));
		}
		emitted.front().caption = std::move(current.caption);
		emitted.front().anchorId = std::move(current.anchorId);
		blocks->erase(blocks->begin() + path.index);
		blocks->insert(
			blocks->begin() + path.index,
			std::make_move_iterator(emitted.begin()),
			std::make_move_iterator(emitted.end()));
		candidate.rebuild();
		return CheckedMutationResult<bool>{
			.apply = true,
			.result = true,
		};
	});
}

bool State::removeGroupedItem(
		const BlockPath &path,
		int itemIndex) {
	if (itemIndex < 0) {
		return false;
	}
	return applyCheckedMutation(false, [path, itemIndex](State &candidate) {
		auto *blocks = candidate.blockContainer(path.container);
		if (!blocks
			|| path.index < 0
			|| path.index >= int(blocks->size())) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		auto &current = (*blocks)[path.index];
		if (current.kind != BlockKind::GroupedMedia
			|| itemIndex >= int(current.mediaItems.size())) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		current.mediaItems.erase(current.mediaItems.begin() + itemIndex);
		if (current.mediaItems.size() >= 2) {
			candidate.rebuild();
			return CheckedMutationResult<bool>{
				.apply = true,
				.result = true,
			};
		}
		if (current.mediaItems.empty()) {
			blocks->erase(blocks->begin() + path.index);
			candidate.rebuild();
			return CheckedMutationResult<bool>{
				.apply = true,
				.result = true,
			};
		}
		auto single = PhotoVideoBlockFromGroupedItem(current.mediaItems.front());
		if (!single) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		single->caption = std::move(current.caption);
		single->anchorId = std::move(current.anchorId);
		(*blocks)[path.index] = std::move(*single);
		candidate.rebuild();
		return CheckedMutationResult<bool>{
			.apply = true,
			.result = true,
		};
	});
}

bool State::addItemsToGroupedMedia(
		const BlockPath &path,
		int insertedCount) {
	if (insertedCount < 1) {
		return false;
	}
	return applyCheckedMutation(false, [path, insertedCount](State &candidate) {
		auto *blocks = candidate.blockContainer(path.container);
		if (!blocks
			|| path.index < 0
			|| path.index >= int(blocks->size())) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		const auto from = path.index + 1;
		const auto till = from + insertedCount;
		if (till > int(blocks->size())) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		auto &group = (*blocks)[path.index];
		if (group.kind != BlockKind::GroupedMedia) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		auto appended = std::vector<RichPage::GroupedMediaItem>();
		appended.reserve(insertedCount);
		for (auto i = from; i != till; ++i) {
			const auto item = GroupedItemFromPhotoVideoBlock((*blocks)[i]);
			if (!item) {
				return CheckedMutationResult<bool>{ .result = false };
			}
			appended.push_back(*item);
		}
		group.mediaItems.insert(
			group.mediaItems.end(),
			std::make_move_iterator(appended.begin()),
			std::make_move_iterator(appended.end()));
		blocks->erase(blocks->begin() + from, blocks->begin() + till);
		candidate.rebuild();
		return CheckedMutationResult<bool>{
			.apply = true,
			.result = true,
		};
	});
}

bool State::setGroupedMediaIntent(
		const BlockPath &path,
		RichPage::GroupedMediaIntent intent) {
	return applyCheckedMutation(false, [path, intent](State &candidate) {
		auto *current = candidate.block(path);
		if (!current || current->kind != BlockKind::GroupedMedia) {
			return CheckedMutationResult<bool>{ .result = false };
		} else if (current->mediaIntent == intent) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		current->mediaIntent = intent;
		candidate.rebuild();
		return CheckedMutationResult<bool>{
			.apply = true,
			.result = true,
		};
	});
}

ApplyResult State::applySplitParagraphText(
		const TextNodeDescriptor &descriptor,
		std::vector<TextWithEntities> chunks) {
	if (chunks.empty()) {
		return applyActiveTextUnchecked(TextWithEntities());
	}
	const auto makeParagraph = [&](TextWithEntities text) {
		auto paragraph = MakeParagraphBlock();
		paragraph.text.text = std::move(text);
		return paragraph;
	};
	const auto focus = [&](LeafPath leaf) {
		rebuild();
		if (!activateRebuiltLeaf(leaf)) {
			ensureActiveTextOrdinal();
		}
	};
	if (descriptor.leaf.kind == LeafKind::BlockText) {
		const auto path = descriptor.leaf.block;
		if (auto owner = block(path)) {
			if (owner->kind == BlockKind::Paragraph) {
				auto container = blockContainer(path.container);
				if (!container
					|| path.index < 0
					|| path.index >= int(container->size())) {
					return ApplyResult::Failed;
				}
				clearTemporaryDownParagraph();
				owner->text.text = std::move(chunks.front());
				auto blocks = std::vector<Block>();
				blocks.reserve(chunks.size() - 1);
				for (auto i = 1; i != int(chunks.size()); ++i) {
					blocks.push_back(makeParagraph(std::move(chunks[i])));
				}
				container->insert(
					container->begin() + path.index + 1,
					std::make_move_iterator(blocks.begin()),
					std::make_move_iterator(blocks.end()));
				focus(descriptor.leaf);
				return ApplyResult::Changed;
			} else if (owner->kind == BlockKind::Quote && !owner->pullquote) {
				clearTemporaryDownParagraph();
				auto firstText = std::move(owner->text);
				firstText.text = std::move(chunks.front());
				auto blocks = std::vector<Block>();
				blocks.reserve(chunks.size());
				auto first = MakeParagraphBlock();
				first.text = std::move(firstText);
				blocks.push_back(std::move(first));
				for (auto i = 1; i != int(chunks.size()); ++i) {
					blocks.push_back(makeParagraph(std::move(chunks[i])));
				}
				owner->text = RichText();
				owner->blocks.insert(
					owner->blocks.begin(),
					std::make_move_iterator(blocks.begin()),
					std::make_move_iterator(blocks.end()));
				focus({
					.kind = LeafKind::BlockText,
					.block = {
						.container = BlockChildrenContainer(path),
						.index = 0,
					},
				});
				return ApplyResult::Changed;
			}
		}
	} else if (descriptor.leaf.kind == LeafKind::ListItemText) {
		const auto path = descriptor.leaf.block;
		if (auto item = listItem(path, descriptor.leaf.listItemIndex)) {
			clearTemporaryDownParagraph();
			auto firstText = std::move(item->text);
			firstText.text = std::move(chunks.front());
			auto blocks = std::vector<Block>();
			blocks.reserve(chunks.size());
			auto first = MakeParagraphBlock();
			first.anchorId = std::move(item->anchorId);
			first.text = std::move(firstText);
			blocks.push_back(std::move(first));
			for (auto i = 1; i != int(chunks.size()); ++i) {
				blocks.push_back(makeParagraph(std::move(chunks[i])));
			}
			item->anchorId.clear();
			item->text = RichText();
			item->blocks.insert(
				item->blocks.begin(),
				std::make_move_iterator(blocks.begin()),
				std::make_move_iterator(blocks.end()));
			focus({
				.kind = LeafKind::BlockText,
				.block = {
					.container = ListItemChildrenContainer(
						path,
						descriptor.leaf.listItemIndex),
					.index = 0,
				},
			});
			return ApplyResult::Changed;
		}
	}
	return ApplyResult::Failed;
}

std::optional<RichMessageLimitError> State::lastLimitError() const {
	return _lastLimitError;
}

std::optional<QString> State::codeBlockLanguage(int ordinal) const {
	const auto descriptor = textNode(ordinal);
	if (!descriptor || descriptor->leaf.kind != LeafKind::BlockText) {
		return std::nullopt;
	}
	const auto owner = block(descriptor->leaf.block);
	return (owner && owner->kind == BlockKind::Code)
		? std::make_optional(owner->language)
		: std::nullopt;
}

bool State::setCodeBlockLanguage(int ordinal, QString language) {
	const auto descriptor = textNode(ordinal);
	if (!descriptor || descriptor->leaf.kind != LeafKind::BlockText) {
		return false;
	}
	auto owner = block(descriptor->leaf.block);
	if (!owner || owner->kind != BlockKind::Code) {
		return false;
	}
	owner->language = std::move(language).trimmed();
	rebuild();
	return true;
}

bool State::toggleTaskState(
		const Markdown::PreparedEditListItemSource &source) {
	const auto blockPath = convertBlockPath(source.block);
	if (!blockPath) {
		return false;
	}
	auto item = listItem(*blockPath, source.listItemIndex);
	if (!item || item->taskState == TaskState::None) {
		return false;
	}
	item->taskState = (item->taskState == TaskState::Unchecked)
		? TaskState::Checked
		: TaskState::Unchecked;
	rebuild();
	return true;
}

bool State::toggleDetailsOpen(
		const Markdown::PreparedEditBlockSource &source) {
	const auto path = convertBlockPath(source);
	if (!path) {
		return false;
	}
	auto owner = block(*path);
	if (!owner || owner->kind != BlockKind::Details) {
		return false;
	}
	owner->open = !owner->open;
	rebuild();
	return true;
}

State::ListSelectionInfo State::listSelectionInfo(
		const PreparedEditListItemRange &range) const {
	const auto validated = validateListItemRange(range);
	const auto owner = validated ? block(validated->block) : nullptr;
	if (!validated || !owner || owner->kind != BlockKind::List) {
		return {};
	}
	auto result = ListSelectionInfo{
		.valid = true,
		.taskList = IsTaskList(owner->listItems),
		.wholeList = (validated->from == 0)
			&& (validated->till == int(owner->listItems.size())),
		.singleItem = (validated->till == validated->from + 1),
		.reversed = (owner->listKind == ListKind::Ordered)
			&& owner->orderedList.reversed,
		.selectedItems = validated->till - validated->from,
		.listKind = owner->listKind,
	};
	if (owner->listKind != ListKind::Ordered) {
		return result;
	}
	result.allOrderedDecimal = true;
	result.allOrderedLowerAlpha = true;
	result.allOrderedUpperAlpha = true;
	result.allOrderedLowerRoman = true;
	result.allOrderedUpperRoman = true;
	for (auto i = validated->from; i != validated->till; ++i) {
		const auto type = EffectiveOrderedListType(*owner, owner->listItems[i]);
		result.allOrderedDecimal = result.allOrderedDecimal
			&& (type == PreparedOrderedListType::Decimal);
		result.allOrderedLowerAlpha = result.allOrderedLowerAlpha
			&& (type == PreparedOrderedListType::LowerAlpha);
		result.allOrderedUpperAlpha = result.allOrderedUpperAlpha
			&& (type == PreparedOrderedListType::UpperAlpha);
		result.allOrderedLowerRoman = result.allOrderedLowerRoman
			&& (type == PreparedOrderedListType::LowerRoman);
		result.allOrderedUpperRoman = result.allOrderedUpperRoman
			&& (type == PreparedOrderedListType::UpperRoman);
	}
	return result;
}

std::optional<PreparedEditListItemRange> State::listContextRangeForSelection(
		const PreparedEditSelection &selection,
		const PreparedEditListItemSource &source) const {
	if (source.listItemIndex < 0) {
		return std::nullopt;
	}
	const auto sourceBlock = convertBlockPath(source.block);
	const auto owner = sourceBlock ? block(*sourceBlock) : nullptr;
	if (!sourceBlock
		|| !owner
		|| owner->kind != BlockKind::List
		|| source.listItemIndex >= int(owner->listItems.size())) {
		return std::nullopt;
	}
	switch (selection.kind) {
	case PreparedEditSelectionKind::ListItems: {
		const auto range = validateListItemRange(selection.listItems);
		if (!range
			|| range->block != *sourceBlock
			|| source.listItemIndex < range->from
			|| source.listItemIndex >= range->till) {
			return std::nullopt;
		}
		return selection.listItems;
	}
	case PreparedEditSelectionKind::Blocks: {
		const auto range = validateBlockRange(selection.blocks);
		if (!range
			|| sourceBlock->container != range->container
			|| sourceBlock->index < range->from
			|| sourceBlock->index >= range->till) {
			return std::nullopt;
		}
		return PreparedEditListItemRange{
			.block = source.block,
			.from = 0,
			.till = int(owner->listItems.size()),
		};
	}
	case PreparedEditSelectionKind::TableRows:
	case PreparedEditSelectionKind::TableCells:
	case PreparedEditSelectionKind::None:
		return std::nullopt;
	}
	return std::nullopt;
}

bool State::setListStyle(
		const PreparedEditListItemRange &range,
		ListStyle style) {
	const auto validated = validateListItemRange(range);
	auto owner = validated ? block(validated->block) : nullptr;
	if (!validated || !owner || owner->kind != BlockKind::List) {
		return false;
	}
	auto changed = false;
	const auto current = CurrentListStyle(*owner);
	if (current == style) {
		if (style == ListStyle::Ordered) {
			changed = ClearOrderedTaskStates(owner);
		}
		if (changed) {
			rebuild();
		}
		return true;
	}
	switch (style) {
	case ListStyle::Ordered:
		owner->listKind = ListKind::Ordered;
		owner->orderedList = {};
		changed = true;
		for (auto &item : owner->listItems) {
			if (item.taskState != TaskState::None) {
				item.taskState = TaskState::None;
			}
			item.number = {};
		}
		break;
	case ListStyle::Bullet:
		owner->listKind = ListKind::Bullet;
		changed = ResetNonOrderedListMetadata(owner) || changed;
		for (auto &item : owner->listItems) {
			if (item.taskState != TaskState::None) {
				item.taskState = TaskState::None;
				changed = true;
			}
		}
		break;
	case ListStyle::Task:
		owner->listKind = ListKind::Bullet;
		changed = ResetNonOrderedListMetadata(owner) || changed;
		for (auto &item : owner->listItems) {
			if (item.taskState == TaskState::None) {
				item.taskState = TaskState::Unchecked;
				changed = true;
			}
		}
		break;
	}
	if (changed) {
		rebuild();
	}
	return true;
}

bool State::setListOrderedType(
		const PreparedEditListItemRange &range,
		PreparedOrderedListType type) {
	const auto validated = validateListItemRange(range);
	auto owner = validated ? block(validated->block) : nullptr;
	if (!validated
		|| !owner
		|| owner->kind != BlockKind::List
		|| owner->listKind != ListKind::Ordered) {
		return false;
	}
	auto changed = false;
	const auto stored = StoredOrderedListType(type);
	if (owner->orderedList.type != stored) {
		owner->orderedList.type = stored;
		for (auto &item : owner->listItems) {
			if (!item.number.type.has_value()
				&& item.number.num.has_value()) {
				item.number.num = std::nullopt;
			}
		}
		changed = true;
	}
	if (changed) {
		rebuild();
	}
	return true;
}

bool State::setListOrderedReversed(
		const PreparedEditListItemRange &range,
		bool reversed) {
	const auto validated = validateListItemRange(range);
	auto owner = validated ? block(validated->block) : nullptr;
	if (!validated
		|| !owner
		|| owner->kind != BlockKind::List
		|| owner->listKind != ListKind::Ordered) {
		return false;
	}
	auto changed = false;
	if (owner->orderedList.reversed != reversed) {
		owner->orderedList.reversed = reversed;
		(void)ClearOrderedListRawMarkers(owner);
		changed = true;
	}
	if (changed) {
		rebuild();
	}
	return true;
}

bool State::setListItemOrderedType(
		const PreparedEditListItemRange &range,
		std::optional<PreparedOrderedListType> type) {
	const auto validated = validateListItemRange(range);
	auto owner = validated ? block(validated->block) : nullptr;
	if (!validated
		|| !owner
		|| owner->kind != BlockKind::List
		|| owner->listKind != ListKind::Ordered) {
		return false;
	}
	auto changed = false;
	const auto parentType = ResolvePreparedOrderedListType(owner->orderedList.type);
	const auto stored = (type && (*type != parentType))
		? StoredOrderedListType(*type, (*type == PreparedOrderedListType::Decimal))
		: std::optional<QString>();
	for (auto i = validated->from; i != validated->till; ++i) {
		auto &item = owner->listItems[i];
		if (item.number.type != stored) {
			item.number.type = stored;
			if (item.number.num.has_value()) {
				item.number.num = std::nullopt;
			}
			changed = true;
		}
	}
	if (changed) {
		rebuild();
	}
	return true;
}

State::TableSelectionInfo State::tableSelectionInfo(
		const Markdown::PreparedEditTableCellRange &range) const {
	const auto validated = validateTableCellRange(range);
	if (!validated) {
		return {};
	}
	const auto owner = block(validated->block);
	if (!owner || owner->kind != BlockKind::Table) {
		return {};
	}
	const auto grid = BuildTableGrid(*owner, tableRenderLimits());
	const auto selected = SelectedTableGridCells(grid, *validated);
	if (selected.empty()) {
		return {};
	}
	auto result = TableSelectionInfo{
		.valid = true,
		.allHeader = true,
		.allAlignLeft = true,
		.allAlignCenter = true,
		.allAlignRight = true,
		.allAlignTop = true,
		.allAlignMiddle = true,
		.allAlignBottom = true,
		.singleCell = (selected.size() == 1),
		.selectedRows = validated->rowTill - validated->rowFrom,
		.selectedColumns = validated->columnTill - validated->columnFrom,
		.totalRows = grid.rowCount,
		.totalColumns = grid.columnCount,
		.bordered = owner->bordered,
		.striped = owner->striped,
	};
	for (const auto &reference : selected) {
		const auto &cell = owner->tableRows[reference.rowIndex].cells[
			reference.cellIndex];
		if (!cell.header) {
			result.allHeader = false;
		}
		if (cell.alignment != RichPage::TableAlignment::Left) {
			result.allAlignLeft = false;
		}
		if (cell.alignment != RichPage::TableAlignment::Center) {
			result.allAlignCenter = false;
		}
		if (cell.alignment != RichPage::TableAlignment::Right) {
			result.allAlignRight = false;
		}
		if (cell.verticalAlignment != RichPage::TableVerticalAlignment::Top) {
			result.allAlignTop = false;
		}
		if (cell.verticalAlignment
			!= RichPage::TableVerticalAlignment::Middle) {
			result.allAlignMiddle = false;
		}
		if (cell.verticalAlignment
			!= RichPage::TableVerticalAlignment::Bottom) {
			result.allAlignBottom = false;
		}
		if (result.singleCell) {
			result.canSplitCell = (NormalizeTableSpan(cell.colspan) > 1)
				|| (NormalizeTableSpan(cell.rowspan) > 1);
		}
	}
	result.canUniteCells = !result.singleCell
		&& CleanTableGridUniteRange(grid, *validated);
	return result;
}

std::optional<Markdown::PreparedEditTableCellRange>
State::tableContextRangeForSelection(
		const Markdown::PreparedEditSelection &selection,
		const Markdown::PreparedEditTableCellSource &source) const {
	if (source.tableRowIndex < 0
		|| source.column < 0
		|| source.rowspan <= 0
		|| source.colspan <= 0) {
		return std::nullopt;
	}
	const auto sourceBlock = convertBlockPath(source.block);
	if (!sourceBlock) {
		return std::nullopt;
	}
	const auto owner = block(*sourceBlock);
	if (!owner || owner->kind != BlockKind::Table) {
		return std::nullopt;
	}
	const auto grid = BuildTableGrid(*owner, tableRenderLimits());
	if (grid.rowCount <= 0 || grid.columnCount <= 0) {
		return std::nullopt;
	}
	const auto fullTableRange = [&] {
		return Markdown::PreparedEditTableCellRange{
			.block = source.block,
			.rowFrom = 0,
			.rowTill = grid.rowCount,
			.columnFrom = 0,
			.columnTill = grid.columnCount,
		};
	};
	const auto sourceIntersects = [&](const auto &range) {
		return (source.tableRowIndex < range.rowTill)
			&& (source.tableRowIndex + source.rowspan > range.rowFrom)
			&& (source.column < range.columnTill)
			&& (source.column + source.colspan > range.columnFrom);
	};
	switch (selection.kind) {
	case PreparedEditSelectionKind::TableCells: {
		const auto range = validateTableCellRange(selection.tableCells);
		if (!range || range->block != *sourceBlock) {
			return std::nullopt;
		}
		return sourceIntersects(*range)
			? std::make_optional(selection.tableCells)
			: std::nullopt;
	}
	case PreparedEditSelectionKind::TableRows: {
		const auto range = validateTableRowRange(selection.tableRows);
		if (!range || range->block != *sourceBlock) {
			return std::nullopt;
		}
		if (source.tableRowIndex >= range->till
			|| source.tableRowIndex + source.rowspan <= range->from) {
			return std::nullopt;
		}
		return Markdown::PreparedEditTableCellRange{
			.block = source.block,
			.rowFrom = range->from,
			.rowTill = range->till,
			.columnFrom = 0,
			.columnTill = grid.columnCount,
		};
	}
	case PreparedEditSelectionKind::Blocks: {
		const auto range = validateBlockRange(selection.blocks);
		if (!range
			|| sourceBlock->container != range->container
			|| sourceBlock->index < range->from
			|| sourceBlock->index >= range->till) {
			return std::nullopt;
		}
		return fullTableRange();
	}
	case PreparedEditSelectionKind::ListItems:
	case PreparedEditSelectionKind::None:
		return std::nullopt;
	}
	return std::nullopt;
}

bool State::canRemoveStructuralSelection(
		const Markdown::PreparedEditSelection &selection) const {
	switch (selection.kind) {
	case PreparedEditSelectionKind::Blocks:
		return validateBlockRange(selection.blocks).has_value();
	case PreparedEditSelectionKind::ListItems:
		return validateListItemRange(selection.listItems).has_value();
	case PreparedEditSelectionKind::TableRows:
		return validateTableRowRange(selection.tableRows).has_value();
	case PreparedEditSelectionKind::TableCells: {
		const auto range = validateTableCellRange(selection.tableCells);
		if (!range) {
			return false;
		}
		const auto owner = block(range->block);
		if (!owner || owner->kind != BlockKind::Table) {
			return false;
		}
		return TableGridRangeSpansAllRows(
			BuildTableGrid(*owner, tableRenderLimits()),
			*range);
	}
	case PreparedEditSelectionKind::None:
		return false;
	}
	return false;
}

auto State::structuredClipboardDataForSelection(
		const Markdown::PreparedEditSelection &selection) const
-> std::optional<ClipboardData> {
	switch (selection.kind) {
	case PreparedEditSelectionKind::Blocks: {
		const auto range = validateBlockRange(selection.blocks);
		const auto blocks = range ? blockContainer(range->container) : nullptr;
		if (!range || !blocks) {
			return std::nullopt;
		}
		auto data = ClipboardBlockData();
		data.blocks = std::vector<Block>(
			blocks->begin() + range->from,
			blocks->begin() + range->till);
		return ClipboardData(std::move(data));
	}
	case PreparedEditSelectionKind::ListItems: {
		const auto range = validateListItemRange(selection.listItems);
		const auto owner = range ? block(range->block) : nullptr;
		if (!range || !owner || owner->kind != BlockKind::List) {
			return std::nullopt;
		}
		auto data = ClipboardListItemsData();
		data.listKind = owner->listKind;
		data.orderedList = owner->orderedList;
		data.items = std::vector<ListItem>(
			owner->listItems.begin() + range->from,
			owner->listItems.begin() + range->till);
		data.taskList = IsTaskList(data.items);
		return ClipboardData(std::move(data));
	}
	case PreparedEditSelectionKind::TableRows:
	case PreparedEditSelectionKind::TableCells:
	case PreparedEditSelectionKind::None:
		return std::nullopt;
	}
	return std::nullopt;
}

std::shared_ptr<const RichPage> State::richPageForTableSelection(
		const Markdown::PreparedEditSelection &selection) const {
	auto blockPath = BlockPath();
	auto rowFrom = 0;
	auto rowTill = 0;
	auto columnFrom = -1;
	auto columnTill = -1;
	if (selection.kind == PreparedEditSelectionKind::TableCells) {
		const auto range = validateTableCellRange(selection.tableCells);
		if (!range) {
			return nullptr;
		}
		blockPath = range->block;
		rowFrom = range->rowFrom;
		rowTill = range->rowTill;
		columnFrom = range->columnFrom;
		columnTill = range->columnTill;
	} else if (selection.kind == PreparedEditSelectionKind::TableRows) {
		const auto range = validateTableRowRange(selection.tableRows);
		if (!range) {
			return nullptr;
		}
		blockPath = range->block;
		rowFrom = range->from;
		rowTill = range->till;
	} else {
		return nullptr;
	}
	const auto owner = block(blockPath);
	if (!owner || owner->kind != BlockKind::Table) {
		return nullptr;
	}
	const auto grid = BuildTableGrid(*owner, tableRenderLimits());
	if (columnFrom < 0 || columnTill < 0) {
		columnFrom = 0;
		columnTill = grid.columnCount;
	}
	if (rowFrom < 0
		|| rowTill <= rowFrom
		|| columnFrom < 0
		|| columnTill <= columnFrom) {
		return nullptr;
	}
	const auto rectangle = StructuralTableCellRange{
		.rowFrom = rowFrom,
		.rowTill = rowTill,
		.columnFrom = columnFrom,
		.columnTill = columnTill,
	};
	const auto references = SelectedTableGridCells(grid, rectangle);
	if (references.empty()) {
		return nullptr;
	}

	auto page = std::make_shared<RichPage>();
	if (references.size() == 1
		&& (rowTill - rowFrom == 1)
		&& (columnTill - columnFrom == 1)) {
		const auto &reference = references.front();
		const auto &row = owner->tableRows[reference.rowIndex];
		auto paragraph = MakeParagraphBlock();
		paragraph.text = row.cells[reference.cellIndex].text;
		page->blocks.push_back(std::move(paragraph));
		return page;
	}

	auto table = Block();
	table.kind = BlockKind::Table;
	table.bordered = owner->bordered;
	table.striped = owner->striped;
	table.tableRows.resize(rowTill - rowFrom);
	for (const auto &reference : references) {
		auto cell = owner->tableRows[reference.rowIndex]
			.cells[reference.cellIndex];
		const auto clampedRowFrom = std::max(reference.rowFrom, rowFrom);
		const auto clampedRowTill = std::min(reference.rowTill, rowTill);
		const auto clampedColumnFrom = std::max(reference.columnFrom, columnFrom);
		const auto clampedColumnTill = std::min(reference.columnTill, columnTill);
		cell.rowspan = std::max(clampedRowTill - clampedRowFrom, 1);
		cell.colspan = std::max(clampedColumnTill - clampedColumnFrom, 1);
		table.tableRows[clampedRowFrom - rowFrom].cells.push_back(
			std::move(cell));
	}
	page->blocks.push_back(std::move(table));
	return page;
}

bool State::insertPreparedBlocksAfterTableSelection(
		const Markdown::PreparedEditSelection &selection,
		std::vector<RichPage::Block> blocks) {
	if (blocks.empty()) {
		return false;
	}
	auto blockPath = std::optional<Markdown::PreparedEditBlockPath>();
	if (selection.kind == PreparedEditSelectionKind::TableCells) {
		if (selection.tableCells.empty()) {
			return false;
		}
		blockPath = selection.tableCells.block;
	} else if (selection.kind == PreparedEditSelectionKind::TableRows) {
		if (selection.tableRows.empty()) {
			return false;
		}
		blockPath = selection.tableRows.block;
	} else {
		return false;
	}
	const auto path = *blockPath;
	return applyCheckedMutation(false, [
			blocks = std::move(blocks),
			path](State &candidate) mutable {
		const auto container = candidate.convertBlockContainerPath(
			path.container);
		if (!container || !candidate.blockContainer(*container)) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		candidate.normalizeInsertedBlockAnchors(blocks);
		auto insertAt = path.index + 1;
		if (!candidate.insertPreparedBlocksAtExplicitPosition(
				std::move(blocks),
				*container,
				&insertAt)) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		candidate.rebuild();
		return CheckedMutationResult<bool>{ .apply = true, .result = true };
	});
}

State::TableInPlaceApplyResult State::replaceTableSelectionCellsInPlace(
		const Markdown::PreparedEditSelection &selection,
		const RichPage &page) {
	if (page.blocks.size() != 1
		|| page.blocks.front().kind != BlockKind::Table) {
		return TableInPlaceApplyResult::StructureMismatch;
	}
	const auto source = richPageForTableSelection(selection);
	if (!source
		|| source->blocks.size() != 1
		|| source->blocks.front().kind != BlockKind::Table) {
		return TableInPlaceApplyResult::StructureMismatch;
	}
	const auto &sourceTable = source->blocks.front();
	const auto &resultTable = page.blocks.front();
	if (sourceTable.tableRows.size() != resultTable.tableRows.size()) {
		return TableInPlaceApplyResult::StructureMismatch;
	}
	auto texts = std::vector<RichText>();
	auto unchanged = true;
	for (auto row = 0, rows = int(sourceTable.tableRows.size());
			row != rows;
			++row) {
		const auto &sourceCells = sourceTable.tableRows[row].cells;
		const auto &resultCells = resultTable.tableRows[row].cells;
		if (sourceCells.size() != resultCells.size()) {
			return TableInPlaceApplyResult::StructureMismatch;
		}
		for (auto index = 0, count = int(sourceCells.size());
				index != count;
				++index) {
			const auto &sourceCell = sourceCells[index];
			const auto &resultCell = resultCells[index];
			if (NormalizeTableSpan(sourceCell.colspan)
					!= NormalizeTableSpan(resultCell.colspan)
				|| NormalizeTableSpan(sourceCell.rowspan)
					!= NormalizeTableSpan(resultCell.rowspan)) {
				return TableInPlaceApplyResult::StructureMismatch;
			}
			if (sourceCell.text != resultCell.text) {
				unchanged = false;
			}
			texts.push_back(resultCell.text);
		}
	}
	if (unchanged) {
		return TableInPlaceApplyResult::Unchanged;
	}
	auto preparedPath = Markdown::PreparedEditBlockPath();
	auto blockPath = BlockPath();
	auto rowFrom = 0;
	auto rowTill = 0;
	auto columnFrom = -1;
	auto columnTill = -1;
	if (selection.kind == PreparedEditSelectionKind::TableCells) {
		const auto range = validateTableCellRange(selection.tableCells);
		if (!range) {
			return TableInPlaceApplyResult::StructureMismatch;
		}
		preparedPath = selection.tableCells.block;
		blockPath = range->block;
		rowFrom = range->rowFrom;
		rowTill = range->rowTill;
		columnFrom = range->columnFrom;
		columnTill = range->columnTill;
	} else if (selection.kind == PreparedEditSelectionKind::TableRows) {
		const auto range = validateTableRowRange(selection.tableRows);
		if (!range) {
			return TableInPlaceApplyResult::StructureMismatch;
		}
		preparedPath = selection.tableRows.block;
		blockPath = range->block;
		rowFrom = range->from;
		rowTill = range->till;
	} else {
		return TableInPlaceApplyResult::StructureMismatch;
	}
	const auto owner = block(blockPath);
	if (!owner || owner->kind != BlockKind::Table) {
		return TableInPlaceApplyResult::StructureMismatch;
	}
	const auto grid = BuildTableGrid(*owner, tableRenderLimits());
	if (columnFrom < 0 || columnTill < 0) {
		columnFrom = 0;
		columnTill = grid.columnCount;
	}
	if (rowFrom < 0
		|| rowTill <= rowFrom
		|| columnFrom < 0
		|| columnTill <= columnFrom) {
		return TableInPlaceApplyResult::StructureMismatch;
	}
	const auto rectangle = StructuralTableCellRange{
		.rowFrom = rowFrom,
		.rowTill = rowTill,
		.columnFrom = columnFrom,
		.columnTill = columnTill,
	};
	const auto references = SelectedTableGridCells(grid, rectangle);
	if (references.size() != texts.size()) {
		return TableInPlaceApplyResult::StructureMismatch;
	}
	const auto applied = applyCheckedMutation(false, [&](State &candidate) {
		const auto path = candidate.convertBlockPath(preparedPath);
		if (!path) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		const auto table = candidate.block(*path);
		if (!table || table->kind != BlockKind::Table) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		for (auto i = 0, count = int(references.size()); i != count; ++i) {
			const auto cell = candidate.tableCell(
				*path,
				references[i].rowIndex,
				references[i].cellIndex);
			if (!cell) {
				return CheckedMutationResult<bool>{ .result = false };
			}
			cell->text = std::move(texts[i]);
		}
		candidate.rebuild();
		return CheckedMutationResult<bool>{ .apply = true, .result = true };
	});
	return applied
		? TableInPlaceApplyResult::Applied
		: TableInPlaceApplyResult::Failed;
}

bool State::addTableRow(
		const Markdown::PreparedEditTableCellRange &range,
		bool after) {
	return applyCheckedMutation(false, [range, after](State &candidate) {
		const auto applied = candidate.addTableRowUnchecked(range, after);
		return CheckedMutationResult<bool>{
			.apply = applied,
			.result = applied,
		};
	});
}

bool State::addTableRowUnchecked(
		const Markdown::PreparedEditTableCellRange &range,
		bool after) {
	const auto validated = validateTableCellRange(range);
	if (!validated) {
		return false;
	}
	auto owner = block(validated->block);
	if (!owner || owner->kind != BlockKind::Table) {
		return false;
	}
	const auto insertAt = after ? validated->rowTill : validated->rowFrom;
	const auto grid = BuildTableGrid(*owner, tableRenderLimits());
	if (insertAt < 0 || insertAt > int(owner->tableRows.size())) {
		return false;
	}
	const auto sourceRow = after
		? validated->rowTill - 1
		: validated->rowFrom;

	auto coveredColumns = std::vector<char>(grid.columnCount, false);
	for (const auto &reference : grid.cells) {
		if (reference.rowFrom >= insertAt || reference.rowTill <= insertAt) {
			continue;
		}
		auto &cell = owner->tableRows[reference.rowIndex].cells[
			reference.cellIndex];
		cell.rowspan = IncrementTableSpan(cell.rowspan);
		for (auto column = reference.columnFrom;
				column != reference.columnTill;
				++column) {
			coveredColumns[column] = true;
		}
	}

	auto row = TableRow();
	if (grid.columnCount <= 0) {
		row.cells.push_back(MakeDefaultTableCell());
	} else {
		row.cells.reserve(grid.columnCount);
		for (auto column = 0; column != grid.columnCount; ++column) {
			if (!coveredColumns[column]) {
				const auto source = TableGridCellAt(
					*owner,
					grid,
					sourceRow,
					column);
				row.cells.push_back(MakeDefaultTableCell(
					source ? source->header : false));
			}
		}
	}
	owner->tableRows.insert(
		owner->tableRows.begin() + insertAt,
		std::move(row));
	rebuild();
	return true;
}

bool State::addTableColumn(
		const Markdown::PreparedEditTableCellRange &range,
		bool after) {
	return applyCheckedMutation(false, [range, after](State &candidate) {
		const auto applied = candidate.addTableColumnUnchecked(range, after);
		return CheckedMutationResult<bool>{
			.apply = applied,
			.result = applied,
		};
	});
}

bool State::addTableColumnUnchecked(
		const Markdown::PreparedEditTableCellRange &range,
		bool after) {
	const auto validated = validateTableCellRange(range);
	if (!validated) {
		return false;
	}
	auto owner = block(validated->block);
	if (!owner || owner->kind != BlockKind::Table) {
		return false;
	}
	const auto insertAt = after
		? validated->columnTill
		: validated->columnFrom;
	const auto grid = BuildTableGrid(*owner, tableRenderLimits());
	const auto sourceColumn = after
		? validated->columnTill - 1
		: validated->columnFrom;

	auto coveredRows = std::vector<char>(owner->tableRows.size(), false);
	for (const auto &reference : grid.cells) {
		if (reference.columnFrom >= insertAt
			|| reference.columnTill <= insertAt) {
			continue;
		}
		auto &cell = owner->tableRows[reference.rowIndex].cells[
			reference.cellIndex];
		cell.colspan = IncrementTableSpan(cell.colspan);
		for (auto row = reference.rowFrom; row != reference.rowTill; ++row) {
			coveredRows[row] = true;
		}
	}

	for (auto rowIndex = 0;
			rowIndex != int(owner->tableRows.size());
			++rowIndex) {
		if (coveredRows[rowIndex]) {
			continue;
		}
		const auto source = TableGridCellAt(
			*owner,
			grid,
			rowIndex,
			sourceColumn);
		InsertTableCellBeforeVisualColumn(
			&owner->tableRows[rowIndex],
			grid,
			rowIndex,
			insertAt,
			MakeDefaultTableCell(source ? source->header : false));
	}
	rebuild();
	return true;
}

bool State::setTableHeader(
		const Markdown::PreparedEditTableCellRange &range,
		bool header) {
	const auto validated = validateTableCellRange(range);
	if (!validated) {
		return false;
	}
	auto owner = block(validated->block);
	if (!owner || owner->kind != BlockKind::Table) {
		return false;
	}
	const auto selected = SelectedTableGridCells(
		BuildTableGrid(*owner, tableRenderLimits()),
		*validated);
	if (selected.empty()) {
		return false;
	}
	auto changed = false;
	for (const auto &reference : selected) {
		auto &cell = owner->tableRows[reference.rowIndex].cells[
			reference.cellIndex];
		if (cell.header != header) {
			cell.header = header;
			changed = true;
		}
	}
	if (changed) {
		rebuild();
	}
	return true;
}

bool State::setTableAlignment(
		const Markdown::PreparedEditTableCellRange &range,
		RichPage::TableAlignment alignment) {
	const auto validated = validateTableCellRange(range);
	if (!validated) {
		return false;
	}
	auto owner = block(validated->block);
	if (!owner || owner->kind != BlockKind::Table) {
		return false;
	}
	const auto selected = SelectedTableGridCells(
		BuildTableGrid(*owner, tableRenderLimits()),
		*validated);
	if (selected.empty()) {
		return false;
	}
	auto changed = false;
	for (const auto &reference : selected) {
		auto &cell = owner->tableRows[reference.rowIndex].cells[
			reference.cellIndex];
		if (cell.alignment != alignment) {
			cell.alignment = alignment;
			changed = true;
		}
	}
	if (changed) {
		rebuild();
	}
	return true;
}

bool State::setTableVerticalAlignment(
		const Markdown::PreparedEditTableCellRange &range,
		RichPage::TableVerticalAlignment alignment) {
	const auto validated = validateTableCellRange(range);
	if (!validated) {
		return false;
	}
	auto owner = block(validated->block);
	if (!owner || owner->kind != BlockKind::Table) {
		return false;
	}
	const auto selected = SelectedTableGridCells(
		BuildTableGrid(*owner, tableRenderLimits()),
		*validated);
	if (selected.empty()) {
		return false;
	}
	auto changed = false;
	for (const auto &reference : selected) {
		auto &cell = owner->tableRows[reference.rowIndex].cells[
			reference.cellIndex];
		if (cell.verticalAlignment != alignment) {
			cell.verticalAlignment = alignment;
			changed = true;
		}
	}
	if (changed) {
		rebuild();
	}
	return true;
}

bool State::splitTableCell(
		const Markdown::PreparedEditTableCellRange &range) {
	const auto validated = validateTableCellRange(range);
	if (!validated) {
		return false;
	}
	auto owner = block(validated->block);
	if (!owner || owner->kind != BlockKind::Table) {
		return false;
	}
	const auto grid = BuildTableGrid(*owner, tableRenderLimits());
	const auto selected = SelectedTableGridCells(grid, *validated);
	if (selected.size() != 1) {
		return false;
	}
	const auto reference = selected.front();
	auto &cell = owner->tableRows[reference.rowIndex].cells[
		reference.cellIndex];
	if (cell.rowspan <= 1 && cell.colspan <= 1) {
		return false;
	}
	const auto header = cell.header;

	cell.rowspan = 1;
	cell.colspan = 1;
	for (auto rowIndex = reference.rowFrom;
			rowIndex != reference.rowTill;
			++rowIndex) {
		auto &row = owner->tableRows[rowIndex];
		for (auto column = reference.columnTill;
				column != reference.columnFrom;
				--column) {
			const auto currentColumn = column - 1;
			if (rowIndex == reference.rowFrom
				&& currentColumn == reference.columnFrom) {
				continue;
			}
			InsertTableCellBeforeVisualColumn(
				&row,
				grid,
				rowIndex,
				currentColumn,
				MakeDefaultTableCell(header));
		}
	}
	rebuild();
	return true;
}

bool State::uniteTableCells(
		const Markdown::PreparedEditTableCellRange &range) {
	const auto validated = validateTableCellRange(range);
	if (!validated) {
		return false;
	}
	auto owner = block(validated->block);
	if (!owner || owner->kind != BlockKind::Table) {
		return false;
	}
	const auto grid = BuildTableGrid(*owner, tableRenderLimits());
	const auto selected = SelectedTableGridCells(grid, *validated);
	if (selected.size() <= 1
		|| !CleanTableGridUniteRange(grid, *validated)) {
		return false;
	}
	const auto keeper = *std::min_element(
		selected.begin(),
		selected.end(),
		[](const auto &a, const auto &b) {
			if (a.rowFrom != b.rowFrom) {
				return a.rowFrom < b.rowFrom;
			} else if (a.columnFrom != b.columnFrom) {
				return a.columnFrom < b.columnFrom;
			} else if (a.rowIndex != b.rowIndex) {
				return a.rowIndex < b.rowIndex;
			}
			return a.cellIndex < b.cellIndex;
		});

	auto &keeperCell = owner->tableRows[keeper.rowIndex].cells[
		keeper.cellIndex];
	keeperCell.rowspan = validated->rowTill - validated->rowFrom;
	keeperCell.colspan = validated->columnTill - validated->columnFrom;

	auto toErase = selected;
	toErase.erase(
		std::remove_if(
			toErase.begin(),
			toErase.end(),
			[&](const TableGridCellReference &cell) {
				return cell.rowIndex == keeper.rowIndex
					&& cell.cellIndex == keeper.cellIndex;
			}),
		toErase.end());
	std::sort(
		toErase.begin(),
		toErase.end(),
		[](const auto &a, const auto &b) {
			if (a.rowIndex != b.rowIndex) {
				return a.rowIndex > b.rowIndex;
			}
			return a.cellIndex > b.cellIndex;
		});
	for (const auto &reference : toErase) {
		auto &row = owner->tableRows[reference.rowIndex];
		row.cells.erase(row.cells.begin() + reference.cellIndex);
	}
	rebuild();
	return true;
}

bool State::removeTableRows(
		const Markdown::PreparedEditTableCellRange &range) {
	const auto validated = validateTableCellRange(range);
	if (!validated) {
		return false;
	}
	const auto owner = block(validated->block);
	if (!owner || owner->kind != BlockKind::Table) {
		return false;
	}
	if (!TableGridRangeSpansAllColumns(
			BuildTableGrid(*owner, tableRenderLimits()),
			*validated)) {
		return false;
	}
	return removeStructuralSelection({
		.kind = PreparedEditSelectionKind::TableRows,
		.tableRows = {
			.block = range.block,
			.from = validated->rowFrom,
			.till = validated->rowTill,
		},
	}, true).has_value();
}

bool State::removeTableColumns(
		const Markdown::PreparedEditTableCellRange &range) {
	const auto validated = validateTableCellRange(range);
	if (!validated) {
		return false;
	}
	const auto owner = block(validated->block);
	if (!owner || owner->kind != BlockKind::Table) {
		return false;
	}
	if (!TableGridRangeSpansAllRows(
			BuildTableGrid(*owner, tableRenderLimits()),
			*validated)) {
		return false;
	}
	return removeStructuralSelection({
		.kind = PreparedEditSelectionKind::TableCells,
		.tableCells = range,
	}, true).has_value();
}

bool State::removeTable(
		const Markdown::PreparedEditTableCellRange &range) {
	const auto validated = validateTableCellRange(range);
	if (!validated) {
		return false;
	}
	const auto owner = block(validated->block);
	if (!owner || owner->kind != BlockKind::Table) {
		return false;
	}
	if (!TableGridRangeCoversFullTable(
			BuildTableGrid(*owner, tableRenderLimits()),
			*validated)) {
		return false;
	}
	return removeStructuralSelection({
		.kind = PreparedEditSelectionKind::Blocks,
		.blocks = {
			.container = range.block.container,
			.from = range.block.index,
			.till = range.block.index + 1,
		},
	}, true).has_value();
}

bool State::setTableBordered(
		const Markdown::PreparedEditTableCellRange &range,
		bool bordered) {
	const auto validated = validateTableCellRange(range);
	if (!validated) {
		return false;
	}
	auto owner = block(validated->block);
	if (!owner || owner->kind != BlockKind::Table) {
		return false;
	}
	if (owner->bordered != bordered) {
		owner->bordered = bordered;
		rebuild();
	}
	return true;
}

bool State::setTableStriped(
		const Markdown::PreparedEditTableCellRange &range,
		bool striped) {
	const auto validated = validateTableCellRange(range);
	if (!validated) {
		return false;
	}
	auto owner = block(validated->block);
	if (!owner || owner->kind != BlockKind::Table) {
		return false;
	}
	if (owner->striped != striped) {
		owner->striped = striped;
		rebuild();
	}
	return true;
}

int State::activeTextLength() const {
	return activeRawText().size();
}

std::optional<int> State::previousEditableOrdinal() const {
	return adjacentEditableOrdinal(false);
}

std::optional<int> State::nextEditableOrdinal() const {
	return adjacentEditableOrdinal(true);
}

std::vector<State::BoundaryTarget> State::boundarySteps(bool forward) const {
	auto steps = std::vector<BoundaryTarget>();
	collectBoundarySteps(
		_richPage->blocks,
		BlockContainerPath(),
		forward,
		&steps);
	return steps;
}

State::BoundaryTarget State::activeBoundaryTarget(bool forward) const {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return {};
	}
	return boundaryTargetForLeaf(
		descriptor->leaf,
		descriptor,
		forward,
		true);
}

State::BoundaryTarget State::boundaryTargetForLeaf(
		const LeafPath &leaf,
		const TextNodeDescriptor *descriptor,
		bool forward,
		bool allowRemoveDirectly) const {
	const auto ordinal = textNodeOrdinal(leaf);
	if (ordinal < 0) {
		return {};
	}
	if (!forward) {
		const auto owner = block(leaf.block);
		if (owner && owner->kind == BlockKind::Table) {
			if (leaf.kind == LeafKind::BlockText && CanEditBlock(*owner)) {
				return {
					.action = BoundaryAction::StructuralSelection,
					.structuralSelection = preparedSelectionForBlock(leaf.block),
				};
			} else if (leaf.kind == LeafKind::TableCellText
				&& leaf.tableRowIndex == 0
				&& leaf.tableCellIndex == 0) {
				const auto titleOrdinal = textNodeOrdinal(LeafPath{
					.kind = LeafKind::BlockText,
					.block = leaf.block,
				});
				if (titleOrdinal >= 0) {
					return {
						.action = BoundaryAction::Text,
						.textOrdinal = titleOrdinal,
					};
				}
			}
		}
	}
	const auto prioritizeStructuralStep = [&](const BoundaryTarget &target) {
		if (target.action != BoundaryAction::StructuralSelection) {
			return false;
		}
		switch (target.structuralSelection.kind) {
		case PreparedEditSelectionKind::Blocks:
			if (const auto range = validateBlockRange(
					target.structuralSelection.blocks)) {
				return leafWillBeRemoved(leaf, *range);
			}
			break;
		case PreparedEditSelectionKind::ListItems:
			if (const auto range = validateListItemRange(
					target.structuralSelection.listItems)) {
				return leafWillBeRemoved(leaf, *range);
			}
			break;
		default:
			break;
		}
		return false;
	};
	const auto removeDirectly = allowRemoveDirectly
		&& descriptor
		&& removalTargetIsEmpty(descriptor->removalTarget)
		&& shouldRemoveActiveOwnerDirectly(*descriptor);
	auto steps = std::vector<BoundaryTarget>();
	collectBoundarySteps(
		_richPage->blocks,
		BlockContainerPath(),
		forward,
		&steps);
	for (auto i = 0, count = int(steps.size()); i != count; ++i) {
		const auto &step = steps[i];
		if (step.action == BoundaryAction::Text
			&& step.textOrdinal == ordinal) {
			const auto next = (i + 1 < count)
				? steps[i + 1]
				: BoundaryTarget();
			if (prioritizeStructuralStep(next)) {
				return next;
			}
			if (removeDirectly) {
				return {
					.action = BoundaryAction::RemoveActiveOwner,
				};
			}
			return next;
		}
	}
	return removeDirectly
		? BoundaryTarget{
			.action = BoundaryAction::RemoveActiveOwner,
		}
		: BoundaryTarget();
}

bool State::isActiveTopLevelParagraph() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor || !descriptor->leaf.block.container.steps.empty()) {
		return false;
	}
	const auto owner = block(descriptor->leaf.block);
	return owner && owner->kind == BlockKind::Paragraph;
}

bool State::isActiveTopLevelParagraphOrHeading() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor || !descriptor->leaf.block.container.steps.empty()) {
		return false;
	}
	const auto owner = block(descriptor->leaf.block);
	return owner
		&& ((owner->kind == BlockKind::Paragraph)
			|| (owner->kind == BlockKind::Heading)
			|| (owner->kind == BlockKind::Footer));
}

bool State::hasActiveListItemSurface() const {
	return activeListItemSurface().has_value();
}

bool State::activeSurfaceAllowsSeparateLineFormula() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor || descriptor->leaf.kind == LeafKind::MathFormula) {
		return false;
	}
	if (isActiveTopLevelParagraph() || activeListItemSurface().has_value()) {
		return true;
	}
	if (descriptor->leaf.kind != LeafKind::BlockText) {
		return false;
	}
	const auto owner = block(descriptor->leaf.block);
	if (!owner) {
		return false;
	}
	if (owner->kind == BlockKind::Quote) {
		return !owner->pullquote;
	}
	if (owner->kind != BlockKind::Paragraph) {
		return false;
	}
	const auto &container = descriptor->leaf.block.container;
	if (container.steps.empty()) {
		return false;
	}
	const auto &step = container.steps.back();
	if (step.kind != BlockContainerKind::BlockChildren) {
		return false;
	}
	auto parent = container;
	parent.steps.pop_back();
	const auto quote = block({
		.container = parent,
		.index = step.blockIndex,
	});
	return quote
		&& (quote->kind == BlockKind::Quote)
		&& !quote->pullquote;
}

bool State::activeLeafUsesQuoteCaptionColor() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor || descriptor->leaf.kind != LeafKind::BlockCaption) {
		return false;
	}
	const auto owner = block(descriptor->leaf.block);
	return owner && owner->kind == BlockKind::Quote;
}

bool State::activeLeafUsesQuotePlaceholderColor() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return false;
	}
	const auto &leaf = descriptor->leaf;
	const auto owner = block(leaf.block);
	if (owner
		&& owner->kind == BlockKind::Quote
		&& (leaf.kind == LeafKind::BlockText
			|| leaf.kind == LeafKind::BlockCaption)) {
		return true;
	}
	auto container = leaf.block.container;
	while (!container.steps.empty()) {
		const auto step = container.steps.back();
		container.steps.pop_back();
		if (step.kind != BlockContainerKind::BlockChildren) {
			continue;
		}
		const auto ancestor = block({
			.container = container,
			.index = step.blockIndex,
		});
		if (ancestor
			&& ancestor->kind == BlockKind::Quote) {
			return true;
		}
	}
	return false;
}

bool State::activeBlockBodyCanEscape() const {
	return activeBlockBodyEscapeBlock().has_value();
}

bool State::shouldRemoveActiveOwnerDirectly(
		const TextNodeDescriptor &descriptor) const {
	switch (descriptor.removalTarget.kind) {
	case RemovalKind::Block: {
		const auto owner = block(descriptor.removalTarget.block);
		if (!owner) {
			return false;
		}
		switch (owner->kind) {
		case BlockKind::Heading:
		case BlockKind::Paragraph:
		case BlockKind::Footer:
		case BlockKind::Code:
		case BlockKind::Math:
			return true;
		default:
			return false;
		}
	}
	case RemovalKind::ListItem:
		if (const auto owner = listItem(
				descriptor.removalTarget.block,
				descriptor.removalTarget.listItemIndex)) {
			return owner->blocks.empty();
		}
		return false;
	case RemovalKind::TableCell:
		return false;
	}
	return false;
}

std::optional<int> State::removeActiveOwnerAndSelectAdjacent(bool forward) {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor || !removalTargetIsEmpty(descriptor->removalTarget)) {
		return std::nullopt;
	}
	const auto target = descriptor->removalTarget;
	auto first = _activeTextOrdinal;
	auto last = _activeTextOrdinal;
	while (first > 0 && _textNodes[first - 1].removalTarget == target) {
		--first;
	}
	while (last + 1 < textNodeCount()
		&& _textNodes[last + 1].removalTarget == target) {
		++last;
	}
	auto adjacent = std::optional<LeafPath>();
	if (forward) {
		for (auto i = last + 1, count = textNodeCount(); i != count; ++i) {
			if (!(_textNodes[i].removalTarget == target)) {
				adjacent = _textNodes[i].leaf;
				break;
			}
		}
	} else {
		for (auto i = first; i != 0; --i) {
			if (!(_textNodes[i - 1].removalTarget == target)) {
				adjacent = _textNodes[i - 1].leaf;
				break;
			}
		}
	}
	if (!removeTarget(target)) {
		return std::nullopt;
	}
	rebuild();
	if (adjacent && (!forward || target.kind == RemovalKind::TableCell)) {
		const auto ordinal = textNodeOrdinal(*adjacent);
		if (setActiveTextByOrdinal(ordinal)) {
			return _activeTextOrdinal;
		}
	}
	if (!textNodeCount()) {
		return std::nullopt;
	}
	const auto fallback = forward
		? std::min(first, textNodeCount() - 1)
		: std::max(std::min(first - 1, textNodeCount() - 1), 0);
	if (!setActiveTextByOrdinal(fallback)) {
		ensureActiveTextOrdinal();
	}
	return _activeTextOrdinal;
}

const TextNodeDescriptor *State::adjacentTextNode(
		int ordinal,
		bool forward) const {
	return textNode(ordinal + (forward ? 1 : -1));
}

bool State::canJoinActiveTextBlockBoundary(bool forward) const {
	const auto descriptor = textNode(_activeTextOrdinal);
	const auto adjacent = descriptor
		? adjacentTextNode(_activeTextOrdinal, forward)
		: nullptr;
	if (!descriptor
		|| !adjacent
		|| descriptor->leaf.kind != LeafKind::BlockText
		|| adjacent->leaf.kind != LeafKind::BlockText
		|| !(descriptor->leaf.block.container
			== adjacent->leaf.block.container)
		|| (adjacent->leaf.block.index
			!= descriptor->leaf.block.index + (forward ? 1 : -1))) {
		return false;
	}
	const auto activeOwner = block(descriptor->leaf.block);
	const auto adjacentOwner = block(adjacent->leaf.block);
	return activeOwner
		&& adjacentOwner
		&& JoinableTextBlockKind(activeOwner->kind)
		&& JoinableTextBlockKind(adjacentOwner->kind);
}

bool State::canJoinActiveListItemBoundary() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor
		|| (descriptor->leaf.kind == LeafKind::BlockText
			&& descriptor->leaf.block.index != 0)) {
		return false;
	}
	return activeListItemSurface().has_value();
}

bool State::joinActiveParagraphBoundaryUnchecked(
		bool forward,
		ActiveTextSelectionTarget *target) {
	if (!target || !canJoinActiveTextBlockBoundary(forward)) {
		return false;
	}
	const auto descriptor = textNode(_activeTextOrdinal);
	const auto adjacent = adjacentTextNode(_activeTextOrdinal, forward);
	const auto activeIndex = descriptor->leaf.block.index;
	const auto adjacentIndex = adjacent->leaf.block.index;
	auto *blocks = blockContainer(descriptor->leaf.block.container);
	if (!blocks
		|| activeIndex < 0
		|| adjacentIndex < 0
		|| activeIndex >= int(blocks->size())
		|| adjacentIndex >= int(blocks->size())) {
		return false;
	}
	auto &activeOwner = (*blocks)[activeIndex];
	auto &adjacentOwner = (*blocks)[adjacentIndex];
	clearTemporaryDownParagraph();
	auto destinationLeaf = forward ? descriptor->leaf : adjacent->leaf;
	const auto seamOffset = forward
		? AppendParagraphSeam(&activeOwner, std::move(adjacentOwner))
		: AppendParagraphSeam(&adjacentOwner, std::move(activeOwner));
	blocks->erase(blocks->begin() + (forward ? adjacentIndex : activeIndex));
	rebuild();
	if (!activateRebuiltLeaf(destinationLeaf)) {
		return false;
	}
	*target = {
		.leaf = destinationLeaf,
		.selectionFrom = seamOffset,
		.selectionTo = seamOffset,
	};
	return true;
}

bool State::joinActiveListItemBoundaryUnchecked(
		ActiveTextSelectionTarget *target) {
	if (!target || !canJoinActiveListItemBoundary()) {
		return false;
	}
	const auto surface = activeListItemSurface();
	if (!surface) {
		return false;
	}
	const auto owner = block(surface->path);
	const auto item = listItem(surface->path, surface->itemIndex);
	if (!owner || owner->kind != BlockKind::List || !item) {
		return false;
	}
	clearTemporaryDownParagraph();
	if (surface->itemIndex > 0) {
		const auto previousIndex = surface->itemIndex - 1;
		const auto previous = listItem(surface->path, previousIndex);
		if (!previous) {
			return false;
		}
		auto merged = takeListItemBlocksForUnwrap(previous);
		previous->anchorId = QString();
		previous->text = RichText();
		auto taken = takeListItemBlocksForUnwrap(item);
		if (taken.empty()) {
			taken.push_back(MakeParagraphBlock());
		}
		auto seamOffset = 0;
		auto destinationIndex = int(merged.size());
		if (!merged.empty()
			&& merged.back().kind == BlockKind::Paragraph
			&& taken.front().kind == BlockKind::Paragraph) {
			seamOffset = AppendParagraphSeam(
				&merged.back(),
				std::move(taken.front()));
			taken.erase(taken.begin());
			destinationIndex = int(merged.size()) - 1;
		}
		merged.insert(
			merged.end(),
			std::make_move_iterator(taken.begin()),
			std::make_move_iterator(taken.end()));
		const auto count = int(merged.size()) - destinationIndex;
		previous->blocks = std::move(merged);
		owner->listItems.erase(
			owner->listItems.begin() + surface->itemIndex);
		rebuild();
		(void)destinationTargetForInsertedBlocks(
			ListItemChildrenContainer(surface->path, previousIndex),
			destinationIndex,
			count);
		const auto destination = textNode(_activeTextOrdinal);
		if (!destination) {
			return false;
		}
		*target = {
			.leaf = destination->leaf,
			.selectionFrom = seamOffset,
			.selectionTo = seamOffset,
		};
		return true;
	}
	if (!unwrapListItemIntoParent(surface->path, 0, true)) {
		return false;
	}
	const auto destination = textNode(_activeTextOrdinal);
	if (!destination) {
		return false;
	}
	*target = {
		.leaf = destination->leaf,
		.selectionFrom = 0,
		.selectionTo = 0,
	};
	return true;
}

State::ParagraphBoundaryJoinResult State::joinActiveParagraphBoundary(
		bool forward) {
	auto failure = ParagraphBoundaryJoinResult{
		.result = ApplyResult::Failed,
	};
	return applyCheckedMutation(failure, [forward](State &candidate) {
		const auto textJoin = candidate.canJoinActiveTextBlockBoundary(
			forward);
		const auto listJoin = !textJoin
			&& !forward
			&& candidate.canJoinActiveListItemBoundary();
		if (!textJoin && !listJoin) {
			return CheckedMutationResult<ParagraphBoundaryJoinResult>{
				.result = { .result = ApplyResult::Unchanged },
			};
		}
		auto target = ActiveTextSelectionTarget();
		const auto joined = textJoin
			? candidate.joinActiveParagraphBoundaryUnchecked(
				forward,
				&target)
			: candidate.joinActiveListItemBoundaryUnchecked(&target);
		if (!joined) {
			return CheckedMutationResult<ParagraphBoundaryJoinResult>{
				.result = { .result = ApplyResult::Failed },
			};
		}
		return CheckedMutationResult<ParagraphBoundaryJoinResult>{
			.apply = true,
			.result = {
				.result = ApplyResult::Changed,
				.destinationLeaf = target.leaf,
				.selectionFrom = target.selectionFrom,
				.selectionTo = target.selectionTo,
			},
		};
	});
}

std::optional<int> State::removeStructuralSelection(
		const Markdown::PreparedEditSelection &selection,
		bool forward) {
	clearTemporaryDownParagraph();
	const auto activate = [&](const LeafPath &leaf) -> std::optional<int> {
		const auto ordinal = textNodeOrdinal(leaf);
		if (setActiveTextByOrdinal(ordinal)) {
			return _activeTextOrdinal;
		}
		return std::nullopt;
	};
	const auto finish = [&](
			auto postMutationFocus,
			std::optional<LeafPath> plannedFocus) -> std::optional<int> {
		rebuild();
		if (const auto focus = postMutationFocus()) {
			if (const auto ordinal = activate(*focus)) {
				return ordinal;
			}
		}
		if (plannedFocus) {
			if (const auto ordinal = activate(*plannedFocus)) {
				return ordinal;
			}
		}
		ensureActiveTextOrdinal();
		return (_activeTextOrdinal >= 0)
			? std::make_optional(_activeTextOrdinal)
			: std::nullopt;
	};
	const auto leafForContainerOwner = [&](
			const BlockContainerPath &container) -> std::optional<LeafPath> {
		if (container.steps.empty()) {
			return std::nullopt;
		}
		auto parent = container;
		const auto step = parent.steps.back();
		parent.steps.pop_back();
		const auto owner = BlockPath{
			.container = parent,
			.index = step.blockIndex,
		};
		if (step.kind == BlockContainerKind::BlockChildren) {
			for (const auto &descriptor : _textNodes) {
				if (leafBelongsToBlock(descriptor.leaf, owner)) {
					return descriptor.leaf;
				}
			}
		} else if (step.kind == BlockContainerKind::ListItemChildren) {
			for (const auto &descriptor : _textNodes) {
				const auto index = ListItemIndexForLeaf(
					descriptor.leaf,
					owner);
				if (index && *index == step.listItemIndex) {
					return descriptor.leaf;
				}
			}
		}
		return std::nullopt;
	};
	const auto leafNearBlockRange = [&](
			const StructuralBlockRange &range,
			bool forward) -> std::optional<LeafPath> {
		if (forward) {
			for (const auto &descriptor : _textNodes) {
				const auto index = BlockIndexInContainer(
					descriptor.leaf,
					range.container);
				if (index && *index >= range.from) {
					return descriptor.leaf;
				}
			}
		} else {
			for (auto i = textNodeCount(); i != 0; --i) {
				const auto &leaf = _textNodes[i - 1].leaf;
				const auto index = BlockIndexInContainer(leaf, range.container);
				if (index && *index < range.from) {
					return leaf;
				}
			}
		}
		return leafForContainerOwner(range.container);
	};
	const auto leafOutsideBlock = [&](
			const BlockPath &path,
			bool forward) -> std::optional<LeafPath> {
		if (forward) {
			for (const auto &descriptor : _textNodes) {
				const auto index = BlockIndexInContainer(
					descriptor.leaf,
					path.container);
				if (index && *index > path.index) {
					return descriptor.leaf;
				}
			}
		} else {
			for (auto i = textNodeCount(); i != 0; --i) {
				const auto &leaf = _textNodes[i - 1].leaf;
				const auto index = BlockIndexInContainer(leaf, path.container);
				if (index && *index < path.index) {
					return leaf;
				}
			}
		}
		return std::nullopt;
	};
	const auto leafNearListItemRange = [&](
			const StructuralListItemRange &range,
			bool forward) -> std::optional<LeafPath> {
		if (forward) {
			for (const auto &descriptor : _textNodes) {
				const auto index = ListItemIndexForLeaf(
					descriptor.leaf,
					range.block);
				if (index && *index >= range.from) {
					return descriptor.leaf;
				}
			}
		} else {
			for (auto i = textNodeCount(); i != 0; --i) {
				const auto &leaf = _textNodes[i - 1].leaf;
				const auto index = ListItemIndexForLeaf(leaf, range.block);
				if (index && *index < range.from) {
					return leaf;
				}
			}
		}
		return leafOutsideBlock(range.block, forward);
	};
	const auto tableTitleLeaf = [&](
			const BlockPath &path) -> std::optional<LeafPath> {
		auto leaf = LeafPath{
			.kind = LeafKind::BlockText,
			.block = path,
		};
		return (textNodeOrdinal(leaf) >= 0)
			? std::make_optional(leaf)
			: std::nullopt;
	};
	const auto leafNearTableRows = [&](
			const StructuralTableRowRange &range,
			bool forward) -> std::optional<LeafPath> {
		const auto cellInDirection = [&](
				bool direction) -> std::optional<LeafPath> {
			if (direction) {
				for (const auto &descriptor : _textNodes) {
					const auto &leaf = descriptor.leaf;
					if (leaf.block == range.block
						&& leaf.kind == LeafKind::TableCellText
						&& leaf.tableRowIndex >= range.from) {
						return leaf;
					}
				}
			} else {
				for (auto i = textNodeCount(); i != 0; --i) {
					const auto &leaf = _textNodes[i - 1].leaf;
					if (leaf.block == range.block
						&& leaf.kind == LeafKind::TableCellText
						&& leaf.tableRowIndex < range.from) {
						return leaf;
					}
				}
			}
			return std::nullopt;
		};
		if (const auto leaf = cellInDirection(forward)) {
			return leaf;
		}
		if (const auto leaf = cellInDirection(!forward)) {
			return leaf;
		}
		if (const auto leaf = tableTitleLeaf(range.block)) {
			return leaf;
		}
		return leafOutsideBlock(range.block, forward);
	};
	const auto leafNearTableCells = [&](
			const StructuralTableCellRange &range,
			bool forward) -> std::optional<LeafPath> {
		if (const auto leaf = firstSelectedLeaf(range)) {
			return leaf;
		}
		return adjacentLeafOutsideRange(range, forward);
	};
	const auto focusForRemovedBlock = [&](
			const BlockPath &path,
			bool forward) {
		return leafNearBlockRange(StructuralBlockRange{
			.container = path.container,
			.from = path.index,
			.till = path.index + 1,
		}, forward);
	};
	switch (selection.kind) {
	case PreparedEditSelectionKind::Blocks: {
		const auto range = validateBlockRange(selection.blocks);
		if (!range) {
			return std::nullopt;
		}
		const auto plannedFocus = plannedFocusForRange(*range, forward);
		const auto blocks = blockContainer(range->container);
		if (!blocks) {
			return std::nullopt;
		}
		blocks->erase(
			blocks->begin() + range->from,
			blocks->begin() + range->till);
		return finish([&] {
			return leafNearBlockRange(*range, forward);
		}, plannedFocus);
	}
	case PreparedEditSelectionKind::ListItems: {
		const auto range = validateListItemRange(selection.listItems);
		if (!range) {
			return std::nullopt;
		}
		const auto plannedFocus = plannedFocusForRange(*range, forward);
		const auto owner = block(range->block);
		if (!owner || owner->kind != BlockKind::List) {
			return std::nullopt;
		}
		owner->listItems.erase(
			owner->listItems.begin() + range->from,
			owner->listItems.begin() + range->till);
		if (owner->listItems.empty()) {
			const auto removed = range->block;
			if (!removeTarget({
					.kind = RemovalKind::Block,
					.block = removed,
				})) {
				return std::nullopt;
			}
			return finish([&] {
				return focusForRemovedBlock(removed, forward);
			}, plannedFocus);
		}
		return finish([&] {
			return leafNearListItemRange(*range, forward);
		}, plannedFocus);
	}
	case PreparedEditSelectionKind::TableRows: {
		const auto range = validateTableRowRange(selection.tableRows);
		if (!range) {
			return std::nullopt;
		}
		const auto plannedFocus = plannedFocusForRange(*range, forward);
		const auto owner = block(range->block);
		if (!owner || owner->kind != BlockKind::Table) {
			return std::nullopt;
		}
		owner->tableRows.erase(
			owner->tableRows.begin() + range->from,
			owner->tableRows.begin() + range->till);
		if (owner->tableRows.empty() && RichTextIsEmpty(owner->text)) {
			const auto removed = range->block;
			if (!removeTarget({
					.kind = RemovalKind::Block,
					.block = removed,
				})) {
				return std::nullopt;
			}
			return finish([&] {
				return focusForRemovedBlock(removed, forward);
			}, plannedFocus);
		}
		return finish([&] {
			return leafNearTableRows(*range, forward);
		}, plannedFocus);
	}
	case PreparedEditSelectionKind::TableCells: {
		const auto range = validateTableCellRange(selection.tableCells);
		if (!range) {
			return std::nullopt;
		}
		const auto owner = block(range->block);
		if (!owner || owner->kind != BlockKind::Table) {
			return std::nullopt;
		}
		const auto grid = BuildTableGrid(*owner, tableRenderLimits());
		if (!TableGridRangeSpansAllRows(grid, *range)) {
			return std::nullopt;
		}
		const auto plannedFocus = plannedFocusForRange(*range, forward);
		const auto selected = SelectedTableGridCells(grid, *range);
		if (selected.empty()) {
			return std::nullopt;
		}
		auto toErase = std::vector<TableGridCellReference>();
		for (const auto &reference : selected) {
			if (TableGridCellContainedInRange(reference, *range)) {
				toErase.push_back(reference);
				continue;
			}
			const auto intersection = TableGridCellColumnIntersection(
				reference,
				*range);
			if (intersection <= 0) {
				continue;
			}
			auto &cell = owner->tableRows[reference.rowIndex].cells[
				reference.cellIndex];
			cell.colspan = std::max(
				(reference.columnTill - reference.columnFrom) - intersection,
				1);
		}
		std::sort(
			toErase.begin(),
			toErase.end(),
			[](const auto &a, const auto &b) {
				if (a.rowIndex != b.rowIndex) {
					return a.rowIndex > b.rowIndex;
				}
				return a.cellIndex > b.cellIndex;
			});
		for (const auto &reference : toErase) {
			auto &row = owner->tableRows[reference.rowIndex];
			row.cells.erase(row.cells.begin() + reference.cellIndex);
		}
		if (std::all_of(
				owner->tableRows.begin(),
				owner->tableRows.end(),
				[](const TableRow &row) {
					return row.cells.empty();
				})) {
			if (RichTextIsEmpty(owner->text)) {
				const auto removed = range->block;
				if (!removeTarget({
						.kind = RemovalKind::Block,
						.block = removed,
					})) {
					return std::nullopt;
				}
				return finish([&] {
					return focusForRemovedBlock(removed, forward);
				}, plannedFocus);
			}
			auto row = TableRow();
			row.cells.push_back(MakeDefaultTableCell());
			owner->tableRows.clear();
			owner->tableRows.push_back(std::move(row));
		}
		return finish([&] {
			return leafNearTableCells(*range, forward);
		}, plannedFocus);
	}
	case PreparedEditSelectionKind::None:
		return std::nullopt;
	}
	return std::nullopt;
}

std::optional<State::BlockContainerPath> State::convertBlockContainerPath(
		const Markdown::PreparedEditBlockContainerPath &path) const {
	auto result = BlockContainerPath();
	result.steps.reserve(path.steps.size());
	const auto *blocks = &_richPage->blocks;
	for (const auto &step : path.steps) {
		if (step.blockIndex < 0 || step.blockIndex >= int(blocks->size())) {
			return std::nullopt;
		}
		const auto &parent = (*blocks)[step.blockIndex];
		auto converted = BlockContainerStep();
		converted.blockIndex = step.blockIndex;
		converted.listItemIndex = step.listItemIndex;
		switch (step.kind) {
		case PreparedBlockContainerKind::Root:
			return std::nullopt;
		case PreparedBlockContainerKind::BlockChildren:
			if (!BlockCanOwnChildContainer(parent)) {
				return std::nullopt;
			}
			converted.kind = BlockContainerKind::BlockChildren;
			blocks = &parent.blocks;
			break;
		case PreparedBlockContainerKind::ListItemChildren:
			if (parent.kind != BlockKind::List
				|| step.listItemIndex < 0
				|| step.listItemIndex >= int(parent.listItems.size())) {
				return std::nullopt;
			}
			converted.kind = BlockContainerKind::ListItemChildren;
			blocks = &parent.listItems[step.listItemIndex].blocks;
			break;
		}
		result.steps.push_back(converted);
	}
	return result;
}

std::optional<State::BlockPath> State::convertBlockPath(
		const Markdown::PreparedEditBlockPath &path) const {
	if (path.index < 0) {
		return std::nullopt;
	}
	const auto container = convertBlockContainerPath(path.container);
	if (!container) {
		return std::nullopt;
	}
	const auto blocks = blockContainer(*container);
	if (!blocks || path.index >= int(blocks->size())) {
		return std::nullopt;
	}
	return BlockPath{
		.container = *container,
		.index = path.index,
	};
}

std::optional<State::BlockPath> State::convertBlockPath(
		const Markdown::PreparedEditBlockSource &source) const {
	return convertBlockPath(source.path);
}

std::optional<State::LeafPath> State::convertLeafPath(
		const Markdown::PreparedEditLeafSource &source) const {
	const auto blockPath = convertBlockPath(source.block);
	if (!blockPath) {
		return std::nullopt;
	}
	auto result = LeafPath();
	result.block = *blockPath;
	switch (source.kind) {
	case PreparedEditLeafKind::BlockText:
		result.kind = LeafKind::BlockText;
		break;
	case PreparedEditLeafKind::BlockCaption:
		result.kind = LeafKind::BlockCaption;
		break;
	case PreparedEditLeafKind::ListItemText:
		if (source.listItemIndex < 0) {
			return std::nullopt;
		}
		result.kind = LeafKind::ListItemText;
		result.listItemIndex = source.listItemIndex;
		break;
	case PreparedEditLeafKind::TableCellText:
		if (source.tableRowIndex < 0 || source.tableCellIndex < 0) {
			return std::nullopt;
		}
		result.kind = LeafKind::TableCellText;
		result.tableRowIndex = source.tableRowIndex;
		result.tableCellIndex = source.tableCellIndex;
		break;
	case PreparedEditLeafKind::MathFormula:
		result.kind = LeafKind::MathFormula;
		break;
	}
	const auto owner = block(result.block);
	if (!owner) {
		return std::nullopt;
	}
	switch (result.kind) {
	case LeafKind::BlockText:
		if (!BlockSupportsBlockText(*owner)) {
			return std::nullopt;
		}
		break;
	case LeafKind::BlockCaption:
		if (!BlockSupportsBlockCaption(*owner)) {
			return std::nullopt;
		}
		break;
	case LeafKind::ListItemText:
		if (owner->kind != BlockKind::List
			|| !listItem(result.block, result.listItemIndex)) {
			return std::nullopt;
		}
		break;
	case LeafKind::TableCellText:
		if (owner->kind != BlockKind::Table
			|| !tableCell(
				result.block,
				result.tableRowIndex,
				result.tableCellIndex)) {
			return std::nullopt;
		}
		break;
	case LeafKind::MathFormula:
		if (owner->kind != BlockKind::Math) {
			return std::nullopt;
		}
		break;
	}
	return result;
}

std::optional<PreparedEditLeafSource> State::convertPreparedLeafSource(
		const LeafPath &path) const {
	const auto owner = block(path.block);
	if (!owner) {
		return std::nullopt;
	}
	auto result = PreparedEditLeafSource();
	result.block = {
		.container = ToPreparedBlockContainerPath(path.block.container),
		.index = path.block.index,
	};
	switch (path.kind) {
	case LeafKind::BlockText:
		if (!BlockSupportsBlockText(*owner)) {
			return std::nullopt;
		}
		result.kind = PreparedEditLeafKind::BlockText;
		break;
	case LeafKind::BlockCaption:
		if (!BlockSupportsBlockCaption(*owner)) {
			return std::nullopt;
		}
		result.kind = PreparedEditLeafKind::BlockCaption;
		break;
	case LeafKind::ListItemText:
		if (owner->kind != BlockKind::List
			|| !listItem(path.block, path.listItemIndex)) {
			return std::nullopt;
		}
		result.kind = PreparedEditLeafKind::ListItemText;
		result.listItemIndex = path.listItemIndex;
		break;
	case LeafKind::TableCellText:
		if (owner->kind != BlockKind::Table
			|| !tableCell(
				path.block,
				path.tableRowIndex,
				path.tableCellIndex)) {
			return std::nullopt;
		}
		result.kind = PreparedEditLeafKind::TableCellText;
		result.tableRowIndex = path.tableRowIndex;
		result.tableCellIndex = path.tableCellIndex;
		break;
	case LeafKind::MathFormula:
		if (owner->kind != BlockKind::Math) {
			return std::nullopt;
		}
		result.kind = PreparedEditLeafKind::MathFormula;
		break;
	}
	return result;
}

std::optional<PreparedEditLeafSource> State::convertPreparedLeafSource(
		const TextNodeDescriptor &descriptor) const {
	return convertPreparedLeafSource(descriptor.leaf);
}

std::optional<State::StructuralBlockRange> State::validateBlockRange(
		const Markdown::PreparedEditBlockRange &range) const {
	if (range.empty()) {
		return std::nullopt;
	}
	const auto container = convertBlockContainerPath(range.container);
	if (!container) {
		return std::nullopt;
	}
	const auto blocks = blockContainer(*container);
	if (!blocks
		|| range.from < 0
		|| range.till > int(blocks->size())) {
		return std::nullopt;
	}
	for (auto i = range.from; i != range.till; ++i) {
		if (!CanEditBlock((*blocks)[i])) {
			return std::nullopt;
		}
	}
	return StructuralBlockRange{
		.container = *container,
		.from = range.from,
		.till = range.till,
	};
}

std::optional<State::StructuralListItemRange> State::validateListItemRange(
		const Markdown::PreparedEditListItemRange &range) const {
	if (range.empty()) {
		return std::nullopt;
	}
	const auto path = convertBlockPath(range.block);
	if (!path) {
		return std::nullopt;
	}
	const auto owner = block(*path);
	if (!owner
		|| owner->kind != BlockKind::List
		|| range.from < 0
		|| range.till > int(owner->listItems.size())) {
		return std::nullopt;
	}
	for (auto i = range.from; i != range.till; ++i) {
		if (!CanEditBlocks(owner->listItems[i].blocks)) {
			return std::nullopt;
		}
	}
	return StructuralListItemRange{
		.block = *path,
		.from = range.from,
		.till = range.till,
	};
}

std::optional<State::StructuralTableRowRange> State::validateTableRowRange(
		const Markdown::PreparedEditTableRowRange &range) const {
	if (range.empty()) {
		return std::nullopt;
	}
	const auto path = convertBlockPath(range.block);
	if (!path) {
		return std::nullopt;
	}
	const auto owner = block(*path);
	if (!owner
		|| owner->kind != BlockKind::Table
		|| range.from < 0
		|| range.till > int(owner->tableRows.size())) {
		return std::nullopt;
	}
	return StructuralTableRowRange{
		.block = *path,
		.from = range.from,
		.till = range.till,
	};
}

std::optional<State::StructuralTableCellRange> State::validateTableCellRange(
		const Markdown::PreparedEditTableCellRange &range) const {
	if (range.empty()) {
		return std::nullopt;
	}
	const auto path = convertBlockPath(range.block);
	if (!path) {
		return std::nullopt;
	}
	const auto owner = block(*path);
	if (!owner || owner->kind != BlockKind::Table) {
		return std::nullopt;
	}
	const auto grid = BuildTableGrid(*owner, tableRenderLimits());
	if (range.rowFrom < 0
		|| range.rowTill > grid.rowCount
		|| range.columnFrom < 0
		|| range.columnTill > grid.columnCount) {
		return std::nullopt;
	}
	auto result = StructuralTableCellRange{
		.block = *path,
		.rowFrom = range.rowFrom,
		.rowTill = range.rowTill,
		.columnFrom = range.columnFrom,
		.columnTill = range.columnTill,
	};
	if (SelectedTableGridCells(grid, result).empty()) {
		return std::nullopt;
	}
	return result;
}

bool State::leafWillBeRemoved(
		const LeafPath &path,
		const StructuralBlockRange &range) const {
	const auto index = BlockIndexInContainer(path, range.container);
	return index && IndexInRange(*index, range.from, range.till);
}

bool State::leafWillBeRemoved(
		const LeafPath &path,
		const StructuralListItemRange &range) const {
	const auto index = ListItemIndexForLeaf(path, range.block);
	return index && IndexInRange(*index, range.from, range.till);
}

bool State::leafWillBeRemoved(
		const LeafPath &path,
		const StructuralTableRowRange &range) const {
	const auto index = TableRowIndexForLeaf(path, range.block);
	return index && IndexInRange(*index, range.from, range.till);
}

bool State::leafWillBeRemoved(
		const LeafPath &,
		const StructuralTableCellRange &) const {
	return false;
}

bool State::leafBelongsToBlock(
		const LeafPath &leaf,
		const BlockPath &path) const {
	const auto &owner = leaf.block;
	if (owner == path) {
		return true;
	}
	if (!ContainerHasPrefix(owner.container, path.container)) {
		return false;
	}
	if (owner.container.steps.size() <= path.container.steps.size()) {
		return false;
	}
	const auto &step = owner.container.steps[path.container.steps.size()];
	return step.blockIndex == path.index
		&& (step.kind == BlockContainerKind::BlockChildren
			|| step.kind == BlockContainerKind::ListItemChildren);
}

std::optional<State::LeafPath> State::firstSelectedLeaf(
		const StructuralTableCellRange &range) const {
	const auto owner = block(range.block);
	if (!owner || owner->kind != BlockKind::Table) {
		return std::nullopt;
	}
	const auto selected = SelectedTableGridCells(
		BuildTableGrid(*owner, tableRenderLimits()),
		range);
	for (const auto &descriptor : _textNodes) {
		const auto &leaf = descriptor.leaf;
		if (leaf.block != range.block
			|| leaf.kind != LeafKind::TableCellText) {
			continue;
		}
		for (const auto &reference : selected) {
			if (TableGridCellMatchesLeaf(reference, leaf, range.block)) {
				return leaf;
			}
		}
	}
	return std::nullopt;
}

std::optional<State::LeafPath> State::adjacentLeafOutsideRange(
		const StructuralBlockRange &range,
		bool forward) const {
	if (forward) {
		for (const auto &descriptor : _textNodes) {
			const auto index = BlockIndexInContainer(
				descriptor.leaf,
				range.container);
			if (index && *index >= range.till) {
				return descriptor.leaf;
			}
		}
	} else {
		for (auto i = textNodeCount(); i != 0; --i) {
			const auto &leaf = _textNodes[i - 1].leaf;
			const auto index = BlockIndexInContainer(leaf, range.container);
			if (index && *index < range.from) {
				return leaf;
			}
		}
	}
	return std::nullopt;
}

std::optional<State::LeafPath> State::adjacentLeafOutsideRange(
		const StructuralListItemRange &range,
		bool forward) const {
	if (forward) {
		for (const auto &descriptor : _textNodes) {
			const auto index = ListItemIndexForLeaf(
				descriptor.leaf,
				range.block);
			if (index && *index >= range.till) {
				return descriptor.leaf;
			}
		}
	} else {
		for (auto i = textNodeCount(); i != 0; --i) {
			const auto &leaf = _textNodes[i - 1].leaf;
			const auto index = ListItemIndexForLeaf(leaf, range.block);
			if (index && *index < range.from) {
				return leaf;
			}
		}
	}
	return std::nullopt;
}

std::optional<State::LeafPath> State::adjacentLeafOutsideRange(
		const StructuralTableRowRange &range,
		bool forward) const {
	if (forward) {
		for (const auto &descriptor : _textNodes) {
			const auto index = TableRowIndexForLeaf(
				descriptor.leaf,
				range.block);
			if (index && *index >= range.till) {
				return descriptor.leaf;
			}
		}
	} else {
		for (auto i = textNodeCount(); i != 0; --i) {
			const auto &leaf = _textNodes[i - 1].leaf;
			const auto index = TableRowIndexForLeaf(leaf, range.block);
			if (index && *index < range.from) {
				return leaf;
			}
		}
	}
	return std::nullopt;
}

std::optional<State::LeafPath> State::adjacentLeafOutsideRange(
		const StructuralTableCellRange &range,
		bool forward) const {
	const auto owner = block(range.block);
	if (!owner || owner->kind != BlockKind::Table) {
		return std::nullopt;
	}
	const auto grid = BuildTableGrid(*owner, tableRenderLimits());
	const auto leafIntersectsRange = [&](const LeafPath &leaf) {
		if (leaf.block != range.block
			|| leaf.kind != LeafKind::TableCellText) {
			return false;
		}
		for (const auto &reference : grid.cells) {
			if (TableGridCellMatchesLeaf(reference, leaf, range.block)) {
				return TableGridCellIntersectsRange(reference, range);
			}
		}
		return false;
	};
	auto fallback = std::optional<LeafPath>();
	auto foundIntersecting = false;
	if (forward) {
		for (const auto &descriptor : _textNodes) {
			if (descriptor.leaf.block == range.block
				&& descriptor.leaf.kind == LeafKind::TableCellText) {
				if (leafIntersectsRange(descriptor.leaf)) {
					foundIntersecting = true;
				} else if (foundIntersecting) {
					return descriptor.leaf;
				} else if (!fallback) {
					fallback = descriptor.leaf;
				}
			}
		}
	} else {
		for (auto i = textNodeCount(); i != 0; --i) {
			const auto &leaf = _textNodes[i - 1].leaf;
			if (leaf.block == range.block
				&& leaf.kind == LeafKind::TableCellText) {
				if (leafIntersectsRange(leaf)) {
					foundIntersecting = true;
				} else if (foundIntersecting) {
					return leaf;
				} else if (!fallback) {
					fallback = leaf;
				}
			}
		}
	}
	return foundIntersecting ? std::nullopt : fallback;
}

std::optional<State::LeafPath> State::fallbackFocusLeaf() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (descriptor) {
		return descriptor->leaf;
	}
	return !_textNodes.empty()
		? std::make_optional(_textNodes.front().leaf)
		: std::nullopt;
}

std::optional<State::LeafPath> State::plannedFocusForRange(
		const StructuralBlockRange &range,
		bool forward) const {
	if (const auto adjacent = adjacentLeafOutsideRange(range, forward)) {
		return adjacent;
	}
	const auto descriptor = textNode(_activeTextOrdinal);
	if (descriptor && !leafWillBeRemoved(descriptor->leaf, range)) {
		return descriptor->leaf;
	}
	for (const auto &fallback : _textNodes) {
		if (!leafWillBeRemoved(fallback.leaf, range)) {
			return fallback.leaf;
		}
	}
	return fallbackFocusLeaf();
}

std::optional<State::LeafPath> State::plannedFocusForRange(
		const StructuralListItemRange &range,
		bool forward) const {
	if (const auto adjacent = adjacentLeafOutsideRange(range, forward)) {
		return adjacent;
	}
	const auto ownerRange = StructuralBlockRange{
		.container = range.block.container,
		.from = range.block.index,
		.till = range.block.index + 1,
	};
	if (const auto adjacent = adjacentLeafOutsideRange(ownerRange, forward)) {
		return adjacent;
	}
	const auto descriptor = textNode(_activeTextOrdinal);
	if (descriptor && !leafWillBeRemoved(descriptor->leaf, range)) {
		return descriptor->leaf;
	}
	for (const auto &fallback : _textNodes) {
		if (!leafWillBeRemoved(fallback.leaf, range)) {
			return fallback.leaf;
		}
	}
	return fallbackFocusLeaf();
}

std::optional<State::LeafPath> State::plannedFocusForRange(
		const StructuralTableRowRange &range,
		bool forward) const {
	if (const auto adjacent = adjacentLeafOutsideRange(range, forward)) {
		return adjacent;
	}
	const auto ownerRange = StructuralBlockRange{
		.container = range.block.container,
		.from = range.block.index,
		.till = range.block.index + 1,
	};
	if (const auto adjacent = adjacentLeafOutsideRange(ownerRange, forward)) {
		return adjacent;
	}
	const auto descriptor = textNode(_activeTextOrdinal);
	if (descriptor && !leafWillBeRemoved(descriptor->leaf, range)) {
		return descriptor->leaf;
	}
	for (const auto &fallback : _textNodes) {
		if (!leafWillBeRemoved(fallback.leaf, range)) {
			return fallback.leaf;
		}
	}
	return fallbackFocusLeaf();
}

std::optional<State::LeafPath> State::plannedFocusForRange(
		const StructuralTableCellRange &range,
		bool forward) const {
	if (const auto selected = firstSelectedLeaf(range)) {
		return selected;
	}
	if (const auto adjacent = adjacentLeafOutsideRange(range, forward)) {
		return adjacent;
	}
	return fallbackFocusLeaf();
}

State::InsertionAnchor State::resolveActiveInsertionTarget() const {
	auto result = InsertionAnchor{
		.container = BlockContainerPath(),
		.blockIndex = int(_richPage->blocks.size()) - 1,
	};
	if (const auto descriptor = textNode(_activeTextOrdinal)) {
		result = descriptor->insertionAnchor;
	}
	return blockContainer(result.container)
		? result
		: InsertionAnchor{
			.container = BlockContainerPath(),
			.blockIndex = int(_richPage->blocks.size()) - 1,
		};
}

std::optional<int> State::normalizeTextOnlyListItemForInsertion(
		const BlockContainerPath &container) {
	if (container.steps.empty()) {
		return std::nullopt;
	}
	const auto &step = container.steps.back();
	if (step.kind != BlockContainerKind::ListItemChildren) {
		return std::nullopt;
	}
	auto parent = container;
	parent.steps.pop_back();
	auto itemPath = BlockPath{
		.container = parent,
		.index = step.blockIndex,
	};
	auto item = listItem(itemPath, step.listItemIndex);
	if (!item || (item->anchorId.isEmpty() && RichTextIsEmpty(item->text))) {
		return std::nullopt;
	}
	auto paragraph = MakeParagraphBlock();
	paragraph.anchorId = std::move(item->anchorId);
	paragraph.text = std::move(item->text);
	item->anchorId.clear();
	item->text = RichText();
	if (!BlockIsEmpty(paragraph)) {
		clearTemporaryDownParagraph();
		item->blocks.insert(item->blocks.begin(), std::move(paragraph));
		return 0;
	}
	return -1;
}

std::optional<int> State::normalizeTextOnlyQuoteSurface(
		const BlockContainerPath &container,
		bool keepEmptyParagraph) {
	if (container.steps.empty()) {
		return std::nullopt;
	}
	const auto &step = container.steps.back();
	if (step.kind != BlockContainerKind::BlockChildren) {
		return std::nullopt;
	}
	auto parent = container;
	parent.steps.pop_back();
	auto owner = block({
		.container = parent,
		.index = step.blockIndex,
	});
	if (!owner
		|| owner->kind != BlockKind::Quote
		|| !owner->blocks.empty()) {
		return std::nullopt;
	}
	auto paragraph = MakeParagraphBlock();
	paragraph.text = std::move(owner->text);
	owner->text = RichText();
	clearTemporaryDownParagraph();
	if (keepEmptyParagraph || !BlockIsEmpty(paragraph)) {
		owner->blocks.insert(owner->blocks.begin(), std::move(paragraph));
		return 0;
	}
	return -1;
}

std::optional<int> State::normalizeTextOnlyQuoteForInsertion(
		const BlockContainerPath &container) {
	return normalizeTextOnlyQuoteSurface(container, false);
}

bool State::normalizeTextOnlyContainerForInsertion(
		const BlockContainerPath &container,
		int *insertAt) {
	if (!insertAt || *insertAt < 0) {
		return false;
	}
	if (const auto normalized = normalizeTextOnlyListItemForInsertion(
			container); normalized && (*normalized >= 0)) {
		++*insertAt;
	}
	if (const auto normalized = normalizeTextOnlyQuoteForInsertion(
			container); normalized && (*normalized >= 0)) {
		++*insertAt;
	}
	const auto blocks = blockContainer(container);
	return blocks && *insertAt <= int(blocks->size());
}

bool State::shouldReplaceActiveTextOnlyBlock(
		const TextNodeDescriptor &descriptor,
		const std::vector<Block> &blocks) const {
	if (descriptor.leaf.kind != LeafKind::BlockText
		|| descriptor.removalTarget.kind != RemovalKind::Block) {
		return false;
	}
	const auto owner = block(descriptor.removalTarget.block);
	if (!owner || !BlockIsEmpty(*owner)) {
		return false;
	}
	if (owner->kind == BlockKind::Paragraph) {
		return true;
	}
	return owner->kind == BlockKind::Heading
		&& blocks.size() == 1
		&& blocks.front().kind == BlockKind::Heading;
}

std::optional<int> State::activateRebuiltLeaf(const LeafPath &path) {
	const auto ordinal = textNodeOrdinal(path);
	if (setActiveTextByOrdinal(ordinal)) {
		return _activeTextOrdinal;
	}
	ensureActiveTextOrdinal();
	return (_activeTextOrdinal >= 0)
		? std::make_optional(_activeTextOrdinal)
		: std::nullopt;
}

std::optional<State::ParagraphTarget> State::reuseOrInsertParagraph(
		const BlockContainerPath &containerPath,
		int index) {
	const auto blocks = blockContainer(containerPath);
	if (!blocks) {
		return std::nullopt;
	}
	const auto insertAt = std::clamp(index, 0, int(blocks->size()));
	if (insertAt < int(blocks->size())
		&& (*blocks)[insertAt].kind == BlockKind::Paragraph) {
		return ParagraphTarget{
			.leaf = {
				.kind = LeafKind::BlockText,
				.block = {
					.container = containerPath,
					.index = insertAt,
				},
			},
		};
	}
	clearTemporaryDownParagraph();
	blocks->insert(blocks->begin() + insertAt, MakeParagraphBlock());
	return ParagraphTarget{
		.leaf = {
			.kind = LeafKind::BlockText,
			.block = {
				.container = containerPath,
				.index = insertAt,
			},
		},
		.inserted = true,
	};
}

auto State::resolveActiveTextInsertTarget()
-> std::optional<State::ActiveTextInsertTarget> {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return std::nullopt;
	}
	if (descriptor->leaf.kind == LeafKind::ListItemText) {
		const auto surface = normalizeActiveListItemSurface();
		if (!surface) {
			return std::nullopt;
		}
		const auto container = ListItemChildrenContainer(
			surface->path,
			surface->itemIndex);
		return ActiveTextInsertTarget{
			.leaf = {
				.kind = LeafKind::BlockText,
				.block = {
					.container = container,
					.index = 0,
				},
			},
			.anchor = {
				.container = container,
				.blockIndex = 0,
			},
		};
	}
	if (descriptor->leaf.kind == LeafKind::BlockText) {
		if (const auto owner = block(descriptor->leaf.block);
			owner
			&& owner->kind == BlockKind::Quote
			&& !owner->pullquote
			&& owner->blocks.empty()) {
			const auto container = BlockChildrenContainer(
				descriptor->leaf.block);
			if (!normalizeTextOnlyQuoteSurface(container, true)) {
				return std::nullopt;
			}
			return ActiveTextInsertTarget{
				.leaf = {
					.kind = LeafKind::BlockText,
					.block = {
						.container = container,
						.index = 0,
					},
				},
				.anchor = {
					.container = container,
					.blockIndex = 0,
				},
			};
		}
	}
	return ActiveTextInsertTarget{
		.leaf = descriptor->leaf,
		.anchor = descriptor->insertionAnchor,
	};
}

auto State::activeQuote(bool pullquote) const
-> std::optional<State::ActiveQuote> {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return std::nullopt;
	}
	const auto direct = block(descriptor->leaf.block);
	if (direct && direct->kind == BlockKind::Quote) {
		return (direct->pullquote == pullquote)
			? std::make_optional(ActiveQuote{
				.path = descriptor->leaf.block,
			})
			: std::nullopt;
	}
	auto container = descriptor->leaf.block.container;
	while (!container.steps.empty()) {
		const auto step = container.steps.back();
		container.steps.pop_back();
		if (step.kind != BlockContainerKind::BlockChildren) {
			continue;
		}
		const auto path = BlockPath{
			.container = container,
			.index = step.blockIndex,
		};
		const auto owner = block(path);
		if (!owner || owner->kind != BlockKind::Quote) {
			continue;
		}
		if (owner->pullquote != pullquote) {
			return std::nullopt;
		}
		const auto body = BlockChildrenContainer(path);
		auto lastBodyLeaf = false;
		for (auto i = textNodeCount(); i != 0; --i) {
			const auto &candidate = _textNodes[i - 1].leaf;
			if (ContainerHasPrefix(candidate.block.container, body)) {
				lastBodyLeaf = (candidate == descriptor->leaf);
				break;
			}
		}
		return ActiveQuote{
			.path = path,
			.activeLeafIsLastEditableBodyLeaf = lastBodyLeaf,
		};
	}
	return std::nullopt;
}

std::optional<LeafPath> State::leafAfterUnwrappingBlockChildren(
		const LeafPath &leaf,
		const BlockPath &wrapper) const {
	const auto body = BlockChildrenContainer(wrapper);
	if (!ContainerHasPrefix(leaf.block.container, body)) {
		return std::nullopt;
	}
	auto result = leaf;
	if (leaf.block.container == body) {
		result.block.container = wrapper.container;
		result.block.index += wrapper.index;
		return result;
	}
	auto suffix = leaf.block.container.steps;
	suffix.erase(suffix.begin(), suffix.begin() + body.steps.size());
	if (suffix.empty()) {
		return std::nullopt;
	}
	suffix.front().blockIndex += wrapper.index;
	result.block.container = wrapper.container;
	result.block.container.steps.insert(
		result.block.container.steps.end(),
		suffix.begin(),
		suffix.end());
	return result;
}

bool State::unwrapActiveCodeBlockUnchecked(
		const ActiveTextInsertContext &context,
		ActiveTextSelectionTarget *target) {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor || descriptor->leaf.kind != LeafKind::BlockText) {
		return false;
	}
	const auto owner = block(descriptor->leaf.block);
	if (!owner || owner->kind != BlockKind::Code) {
		return false;
	}
	auto *container = blockContainer(descriptor->leaf.block.container);
	const auto index = descriptor->leaf.block.index;
	if (!container || index < 0 || index >= int(container->size())) {
		return false;
	}
	auto paragraph = MakeParagraphBlock();
	paragraph.anchorId = owner->anchorId;
	paragraph.text = owner->text;
	paragraph.text.text = JoinText(
		context.before,
		context.selected,
		context.after);
	(*container)[index] = std::move(paragraph);
	clearTemporaryDownParagraph();
	rebuild();
	if (!activateRebuiltLeaf(descriptor->leaf)) {
		return false;
	}
	if (target) {
		const auto selectionFrom = int(context.before.text.size());
		*target = {
			.leaf = descriptor->leaf,
			.selectionFrom = selectionFrom,
			.selectionTo = selectionFrom + int(context.selected.text.size()),
		};
	}
	return true;
}

bool State::unwrapActiveQuoteUnchecked(
		bool pullquote,
		const ActiveTextInsertContext &context,
		ActiveTextSelectionTarget *target) {
	const auto descriptor = textNode(_activeTextOrdinal);
	const auto quote = activeQuote(pullquote);
	if (!descriptor || !quote) {
		return false;
	}
	switch (descriptor->leaf.kind) {
	case LeafKind::BlockCaption:
	case LeafKind::TableCellText:
	case LeafKind::MathFormula:
		return false;
	case LeafKind::BlockText:
	case LeafKind::ListItemText:
		break;
	}
	auto *owner = block(quote->path);
	if (!owner
		|| owner->kind != BlockKind::Quote
		|| owner->pullquote != pullquote) {
		return false;
	}
	auto *activeText = richText(descriptor->leaf);
	if (!activeText) {
		return false;
	}
	const auto selectionFrom = int(context.before.text.size());
	const auto selectionTo = selectionFrom + int(context.selected.text.size());
	if (owner->blocks.empty()) {
		if (descriptor->leaf.kind != LeafKind::BlockText
			|| descriptor->leaf.block != quote->path
			|| !RichTextIsEmpty(owner->caption)) {
			return false;
		}
		auto *container = blockContainer(quote->path.container);
		const auto index = quote->path.index;
		if (!container || index < 0 || index >= int(container->size())) {
			return false;
		}
		auto paragraph = MakeParagraphBlock();
		paragraph.anchorId = owner->anchorId;
		paragraph.text = owner->text;
		paragraph.text.text = JoinText(
			context.before,
			context.selected,
			context.after);
		(*container)[index] = std::move(paragraph);
		clearTemporaryDownParagraph();
		rebuild();
		if (!activateRebuiltLeaf(descriptor->leaf)) {
			return false;
		}
		if (target) {
			*target = {
				.leaf = descriptor->leaf,
				.selectionFrom = selectionFrom,
				.selectionTo = selectionTo,
			};
		}
		return true;
	}
	if (!owner->anchorId.isEmpty()
		|| !RichTextIsEmpty(owner->text)
		|| !RichTextIsEmpty(owner->caption)) {
		return false;
	}
	const auto destinationLeaf = leafAfterUnwrappingBlockChildren(
		descriptor->leaf,
		quote->path);
	if (!destinationLeaf) {
		return false;
	}
	auto *container = blockContainer(quote->path.container);
	const auto index = quote->path.index;
	if (!container || index < 0 || index >= int(container->size())) {
		return false;
	}
	activeText->text = JoinText(
		context.before,
		context.selected,
		context.after);
	auto blocks = std::move(owner->blocks);
	container->erase(container->begin() + index);
	container->insert(
		container->begin() + index,
		std::make_move_iterator(blocks.begin()),
		std::make_move_iterator(blocks.end()));
	clearTemporaryDownParagraph();
	rebuild();
	if (!activateRebuiltLeaf(*destinationLeaf)) {
		return false;
	}
	if (target) {
		*target = {
			.leaf = *destinationLeaf,
			.selectionFrom = selectionFrom,
			.selectionTo = selectionTo,
		};
	}
	return true;
}

bool State::convertActiveHeadingOrFooterUnchecked(
		InsertAction action,
		const ActiveTextInsertContext &context,
		ActiveTextSelectionTarget *target) {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor || descriptor->leaf.kind != LeafKind::BlockText) {
		return false;
	}
	auto *owner = block(descriptor->leaf.block);
	if (!owner) {
		return false;
	}
	const auto heading = (action.type == InsertBlockType::Heading);
	const auto required = heading ? BlockKind::Heading : BlockKind::Footer;
	if (owner->kind != required) {
		return false;
	}
	const auto level = std::clamp(action.headingLevel, 1, 6);
	if (heading && std::clamp(owner->headingLevel, 1, 6) != level) {
		owner->headingLevel = level;
	} else {
		owner->kind = BlockKind::Paragraph;
		owner->headingLevel = 0;
	}
	owner->text.text = JoinText(
		context.before,
		context.selected,
		context.after);
	clearTemporaryDownParagraph();
	rebuild();
	if (!activateRebuiltLeaf(descriptor->leaf)) {
		return false;
	}
	if (target) {
		const auto selectionFrom = int(context.before.text.size());
		*target = {
			.leaf = descriptor->leaf,
			.selectionFrom = selectionFrom,
			.selectionTo = selectionFrom + int(context.selected.text.size()),
		};
	}
	return true;
}

auto State::activeListItemSurface() const
-> std::optional<State::ActiveListItemSurface> {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return std::nullopt;
	}
	auto path = std::optional<BlockPath>();
	if (descriptor->leaf.kind == LeafKind::ListItemText) {
		path = descriptor->leaf.block;
	} else if (descriptor->leaf.kind == LeafKind::BlockText) {
		const auto owner = block(descriptor->leaf.block);
		if (!owner || owner->kind != BlockKind::Paragraph) {
			return std::nullopt;
		}
		const auto &container = descriptor->leaf.block.container;
		if (container.steps.empty()) {
			return std::nullopt;
		}
		const auto &step = container.steps.back();
		if (step.kind != BlockContainerKind::ListItemChildren) {
			return std::nullopt;
		}
		auto parent = container;
		parent.steps.pop_back();
		path = BlockPath{
			.container = parent,
			.index = step.blockIndex,
		};
	} else {
		return std::nullopt;
	}
	const auto owner = block(*path);
	if (!owner || owner->kind != BlockKind::List) {
		return std::nullopt;
	}
	const auto itemIndex = ListItemIndexForLeaf(descriptor->leaf, *path);
	if (!itemIndex) {
		return std::nullopt;
	}
	if (descriptor->leaf.kind == LeafKind::BlockText
		&& descriptor->leaf.block.container
			!= ListItemChildrenContainer(*path, *itemIndex)) {
		return std::nullopt;
	}
	return ActiveListItemSurface{
		.path = *path,
		.itemIndex = *itemIndex,
	};
}

auto State::normalizeActiveListItemSurface()
-> std::optional<State::ActiveListItemSurface> {
	const auto descriptor = textNode(_activeTextOrdinal);
	const auto surface = activeListItemSurface();
	if (!descriptor
		|| !surface
		|| descriptor->leaf.kind != LeafKind::ListItemText) {
		return surface;
	}
	const auto item = listItem(surface->path, surface->itemIndex);
	if (!item) {
		return std::nullopt;
	}
	auto paragraph = MakeParagraphBlock();
	paragraph.anchorId = std::move(item->anchorId);
	paragraph.text = std::move(item->text);
	item->anchorId.clear();
	item->text = RichText();
	clearTemporaryDownParagraph();
	item->blocks.insert(item->blocks.begin(), std::move(paragraph));
	return surface;
}

RichText *State::seedInsertedBlocks(
		std::vector<Block> &blocks,
		TextWithEntities text) {
	for (auto &block : blocks) {
		if (auto target = seedInsertedBlock(block)) {
			if (!text.text.isEmpty()) {
				auto combined = std::move(text);
				combined.append(target->text);
				target->text = std::move(combined);
			}
			return target;
		}
	}
	return nullptr;
}

RichText *State::seedInsertedBlock(Block &block) {
	switch (block.kind) {
	case BlockKind::Heading:
	case BlockKind::Paragraph:
	case BlockKind::Footer:
	case BlockKind::Code:
	case BlockKind::Table:
	case BlockKind::Details:
		return &block.text;
	case BlockKind::Quote:
		if (block.blocks.empty()) {
			return &block.text;
		}
		for (auto &child : block.blocks) {
			if (const auto result = seedInsertedBlock(child)) {
				return result;
			}
		}
		return &block.caption;
	case BlockKind::List:
		for (auto &item : block.listItems) {
			if (!RichTextIsEmpty(item.text) || item.blocks.empty()) {
				return &item.text;
			}
			for (auto &child : item.blocks) {
				if (const auto result = seedInsertedBlock(child)) {
					return result;
				}
			}
		}
		return nullptr;
	case BlockKind::Photo:
	case BlockKind::Video:
	case BlockKind::Audio:
	case BlockKind::Map:
		return &block.caption;
	default:
		return nullptr;
	}
}

bool State::appendInsertedTrailingText(
		const BlockContainerPath &container,
		int insertAt,
		int count,
		TextWithEntities text) {
	if (text.text.isEmpty()) {
		return true;
	}
	const auto blocks = blockContainer(container);
	if (!blocks
		|| insertAt < 0
		|| count < 0
		|| insertAt + count > int(blocks->size())) {
		return false;
	}
	auto &last = (*blocks)[insertAt + count - 1];
	if (last.kind == BlockKind::List) {
		const auto taskState = last.listItems.empty()
			? TaskState::None
			: last.listItems.back().taskState;
		auto item = MakeParagraphListItem(taskState);
		Assert(!item.blocks.empty());
		item.blocks.front().text.text = std::move(text);
		last.listItems.push_back(std::move(item));
		return true;
	}
	const auto paragraph = reuseOrInsertParagraph(container, insertAt + count);
	if (!paragraph) {
		return false;
	}
	const auto target = richText(paragraph->leaf);
	if (!target) {
		return false;
	}
	target->text = std::move(text);
	return true;
}

std::optional<int> State::ensureTrailingParagraphActive() {
	return applyCheckedMutation(std::optional<int>(), [](State &candidate) {
		const auto result = candidate.ensureTrailingParagraphActiveUnchecked();
		return CheckedMutationResult<std::optional<int>>{
			.apply = result.has_value(),
			.result = result,
		};
	});
}

std::optional<int> State::ensureTrailingParagraphActiveUnchecked() {
	if (_richPage->blocks.empty()
		|| _richPage->blocks.back().kind != BlockKind::Paragraph) {
		clearTemporaryDownParagraph();
		_richPage->blocks.push_back(MakeParagraphBlock());
	}
	const auto path = BlockPath{
		.container = BlockContainerPath(),
		.index = int(_richPage->blocks.size()) - 1,
	};
	rebuild();
	const auto ordinal = textNodeOrdinal({
			.kind = LeafKind::BlockText,
			.block = path,
		});
	if (!setActiveTextByOrdinal(ordinal)) {
		ensureActiveTextOrdinal();
	}
	return (_activeTextOrdinal >= 0)
		? std::make_optional(_activeTextOrdinal)
		: std::nullopt;
}

std::optional<int> State::insertLeadingParagraphActive(bool focusInserted) {
	return applyCheckedMutation(std::optional<int>(), [=](State &candidate) {
		const auto result = candidate.insertLeadingParagraphActiveUnchecked(
			focusInserted);
		return CheckedMutationResult<std::optional<int>>{
			.apply = result.has_value(),
			.result = result,
		};
	});
}

std::optional<int> State::insertLeadingParagraphActiveUnchecked(
		bool focusInserted) {
	auto restore = std::optional<LeafPath>();
	if (!focusInserted) {
		if (const auto descriptor = textNode(_activeTextOrdinal)) {
			restore = descriptor->leaf;
			// The paragraph is prepended to the top-level blocks list,
			// so the active leaf's root-level index shifts by one.
			if (restore->block.container.steps.empty()) {
				++restore->block.index;
			} else {
				++restore->block.container.steps.front().blockIndex;
			}
		}
	}
	clearTemporaryDownParagraph();
	_richPage->blocks.insert(_richPage->blocks.begin(), MakeParagraphBlock());
	rebuild();
	const auto inserted = LeafPath{
		.kind = LeafKind::BlockText,
		.block = {
			.container = BlockContainerPath(),
			.index = 0,
		},
	};
	const auto target = restore.value_or(inserted);
	if (!setActiveTextByOrdinal(textNodeOrdinal(target))) {
		ensureActiveTextOrdinal();
	}
	return (_activeTextOrdinal >= 0)
		? std::make_optional(_activeTextOrdinal)
		: std::nullopt;
}

void State::resyncAfterExternalRichPageMutation() {
	clearTemporaryDownParagraph();
	const auto activeLeaf = [&]() -> std::optional<LeafPath> {
		if (const auto descriptor = textNode(_activeTextOrdinal)) {
			return descriptor->leaf;
		}
		return std::nullopt;
	}();
	rebuild();
	if (activeLeaf && (textNodeOrdinal(*activeLeaf) >= 0)) {
		const auto activated = activateRebuiltLeaf(*activeLeaf);
		Assert(activated);
	} else {
		ensureActiveTextOrdinal();
	}
}

std::optional<int> State::moveActiveSpecialBlockDown() {
	return applyCheckedMutation(std::optional<int>(), [](State &candidate) {
		const auto result = candidate.moveActiveSpecialBlockDownUnchecked();
		return CheckedMutationResult<std::optional<int>>{
			.apply = result.has_value(),
			.result = result,
		};
	});
}

std::optional<int> State::submitActiveSingleLineField(
		const ActiveEnterContext &context) {
	return applyCheckedMutation(std::optional<int>(), [=](State &candidate) {
		const auto result = candidate.submitActiveSingleLineFieldUnchecked(
			context);
		return CheckedMutationResult<std::optional<int>>{
			.apply = result.has_value(),
			.result = result,
		};
	});
}

std::optional<int> State::escapeActiveBlockBody() {
	return applyCheckedMutation(std::optional<int>(), [](State &candidate) {
		const auto result = candidate.escapeActiveBlockBodyUnchecked();
		return CheckedMutationResult<std::optional<int>>{
			.apply = result.has_value(),
			.result = result,
		};
	});
}

State::BoundaryTarget State::removeTemporaryDownParagraphAndMove() {
	return applyCheckedMutation(BoundaryTarget(), [](State &candidate) {
		const auto result
			= candidate.removeTemporaryDownParagraphAndMoveUnchecked();
		return CheckedMutationResult<BoundaryTarget>{
			.apply = (result.action != BoundaryAction::None),
			.result = result,
		};
	});
}

std::optional<int> State::moveActiveSpecialBlockDownUnchecked() {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return std::nullopt;
	}
	auto target = std::optional<LeafPath>();
	auto trackTemporary = false;
	if (const auto quote = activeQuote(false)) {
		if (descriptor->leaf.kind == LeafKind::BlockCaption
			&& descriptor->leaf.block == quote->path) {
			if (const auto paragraph = reuseOrInsertParagraph(
					quote->path.container,
					quote->path.index + 1)) {
				target = paragraph->leaf;
				trackTemporary = paragraph->inserted;
			}
		} else if (descriptor->leaf.kind == LeafKind::BlockText
			&& descriptor->leaf.block == quote->path) {
			const auto owner = block(quote->path);
			if (owner
				&& owner->kind == BlockKind::Quote
				&& owner->blocks.empty()) {
				target = LeafPath{
					.kind = LeafKind::BlockCaption,
					.block = quote->path,
				};
			}
		} else if (quote->activeLeafIsLastEditableBodyLeaf) {
			target = LeafPath{
				.kind = LeafKind::BlockCaption,
				.block = quote->path,
			};
		}
	} else if (descriptor->leaf.kind == LeafKind::BlockText) {
		const auto owner = block(descriptor->leaf.block);
		if (owner && owner->kind == BlockKind::Code) {
			if (const auto paragraph = reuseOrInsertParagraph(
					descriptor->leaf.block.container,
					descriptor->leaf.block.index + 1)) {
				target = paragraph->leaf;
				trackTemporary = paragraph->inserted;
			}
		}
	}
	if (!target) {
		return std::nullopt;
	}
	_temporaryDownParagraph = trackTemporary
		? std::make_optional(*target)
		: std::nullopt;
	rebuild();
	return activateRebuiltLeaf(*target);
}

std::optional<int> State::submitActiveSingleLineFieldUnchecked(
		const ActiveEnterContext &context) {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return std::nullopt;
	}
	const auto leaf = descriptor->leaf;
	const auto owner = block(leaf.block);
	if (!owner) {
		return std::nullopt;
	}
	const auto activate = [&](const LeafPath &target) -> std::optional<int> {
		const auto ordinal = textNodeOrdinal(target);
		return setActiveTextByOrdinal(ordinal)
			? std::make_optional(_activeTextOrdinal)
			: std::nullopt;
	};
	const auto paragraphAfterBlock = [&]() -> std::optional<int> {
		if (const auto paragraph = reuseOrInsertParagraph(
				leaf.block.container,
				leaf.block.index + 1)) {
			rebuild();
			return activateRebuiltLeaf(paragraph->leaf);
		}
		return std::nullopt;
	};
	const auto paragraphBeforeBlock = [&]() -> std::optional<int> {
		const auto blocks = blockContainer(leaf.block.container);
		if (!blocks
			|| leaf.block.index < 0
			|| leaf.block.index >= int(blocks->size())) {
			return std::nullopt;
		}
		clearTemporaryDownParagraph();
		blocks->insert(
			blocks->begin() + leaf.block.index,
			MakeParagraphBlock());
		auto shifted = leaf;
		++shifted.block.index;
		rebuild();
		return activateRebuiltLeaf(shifted);
	};
	if (leaf.kind == LeafKind::BlockCaption) {
		switch (owner->kind) {
		case BlockKind::Quote:
		case BlockKind::Photo:
		case BlockKind::Video:
		case BlockKind::Audio:
		case BlockKind::Map:
		case BlockKind::GroupedMedia:
			if (context.position == EnterPosition::Middle) {
				return std::nullopt;
			} else if (context.position == EnterPosition::Beginning) {
				return paragraphBeforeBlock();
			}
			return paragraphAfterBlock();
		default:
			return std::nullopt;
		}
	}
	if (leaf.kind == LeafKind::BlockText) {
		if (owner->kind == BlockKind::Details
			|| owner->kind == BlockKind::Table) {
			if (context.position == EnterPosition::Middle) {
				return std::nullopt;
			} else if (context.position == EnterPosition::Beginning) {
				return paragraphBeforeBlock();
			}
		}
		if (owner->kind == BlockKind::Details) {
			const auto bodyContainer = BlockChildrenContainer(leaf.block);
			auto target = std::optional<LeafPath>();
			for (const auto &candidate : _textNodes) {
				if (ContainerStartsWith(
						candidate.leaf.block.container,
						bodyContainer)) {
					target = candidate.leaf;
					break;
				}
			}
			if (!target) {
				owner->blocks.push_back(MakeParagraphBlock());
				target = LeafPath{
					.kind = LeafKind::BlockText,
					.block = {
						.container = bodyContainer,
						.index = 0,
					},
				};
				rebuild();
				return activateRebuiltLeaf(*target);
			}
			return activate(*target);
		} else if (owner->kind == BlockKind::Table) {
			for (const auto &candidate : _textNodes) {
				if (candidate.leaf.block == leaf.block
					&& candidate.leaf.kind == LeafKind::TableCellText) {
					return activate(candidate.leaf);
				}
			}
			return paragraphAfterBlock();
		}
		return std::nullopt;
	} else if (leaf.kind == LeafKind::TableCellText) {
		const auto ordinal = textNodeOrdinal(leaf);
		for (auto i = ordinal + 1, count = textNodeCount(); i != count; ++i) {
			const auto &candidate = _textNodes[i].leaf;
			if (candidate.block == leaf.block
				&& candidate.kind == LeafKind::TableCellText) {
				return activate(candidate);
			}
		}
		return paragraphAfterBlock();
	}
	return std::nullopt;
}

std::optional<int> State::escapeActiveBlockBodyUnchecked() {
	const auto targetBlock = activeBlockBodyEscapeBlock();
	if (!targetBlock) {
		return std::nullopt;
	}
	if (const auto paragraph = reuseOrInsertParagraph(
			targetBlock->container,
			targetBlock->index + 1)) {
		rebuild();
		return activateRebuiltLeaf(paragraph->leaf);
	}
	return std::nullopt;
}

std::optional<State::BlockPath> State::activeBlockBodyEscapeBlock() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return std::nullopt;
	}
	const auto leaf = descriptor->leaf;
	auto targetBlock = std::optional<BlockPath>();
	if (const auto owner = block(leaf.block);
		owner
		&& leaf.kind == LeafKind::BlockText
		&& (owner->kind == BlockKind::Quote
			|| owner->kind == BlockKind::Code)) {
		targetBlock = leaf.block;
	} else {
		auto container = leaf.block.container;
		while (!container.steps.empty()) {
			const auto step = container.steps.back();
			container.steps.pop_back();
			if (step.kind != BlockContainerKind::BlockChildren) {
				continue;
			}
			const auto candidate = BlockPath{
				.container = container,
				.index = step.blockIndex,
			};
			const auto owner = block(candidate);
			if (owner
				&& (owner->kind == BlockKind::Quote
					|| owner->kind == BlockKind::Details)) {
				targetBlock = candidate;
				break;
			}
		}
	}
	return targetBlock;
}

auto State::captureRebuiltBoundaryTarget(
		const BoundaryTarget &target) const
-> std::optional<RebuiltBoundaryTarget> {
	switch (target.action) {
	case BoundaryAction::Text:
		if (const auto descriptor = textNode(target.textOrdinal)) {
			return RebuiltBoundaryTarget{
				.action = BoundaryAction::Text,
				.leaf = descriptor->leaf,
			};
		}
		break;
	case BoundaryAction::StructuralSelection:
		switch (target.structuralSelection.kind) {
		case PreparedEditSelectionKind::Blocks:
			if (const auto range = validateBlockRange(
					target.structuralSelection.blocks);
				range
				&& (range->till == range->from + 1)) {
				return RebuiltBoundaryTarget{
					.action = BoundaryAction::StructuralSelection,
					.block = {
						.container = range->container,
						.index = range->from,
					},
				};
			}
			break;
		case PreparedEditSelectionKind::ListItems:
			if (const auto range = validateListItemRange(
					target.structuralSelection.listItems);
				range
				&& (range->till == range->from + 1)) {
				return RebuiltBoundaryTarget{
					.action = BoundaryAction::StructuralSelection,
					.block = range->block,
					.listItemIndex = range->from,
				};
			}
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
	return std::nullopt;
}

void State::shiftRebuiltBoundaryTargetAfterRemovedBlock(
		RebuiltBoundaryTarget &target,
		const BlockPath &removed) const {
	switch (target.action) {
	case BoundaryAction::Text:
		if (!ShiftBlockPathAfterRemovedBlock(target.leaf.block, removed)) {
			target = RebuiltBoundaryTarget();
		}
		break;
	case BoundaryAction::StructuralSelection:
		if (!ShiftBlockPathAfterRemovedBlock(target.block, removed)) {
			target = RebuiltBoundaryTarget();
		}
		break;
	default:
		target = RebuiltBoundaryTarget();
		break;
	}
}

State::BoundaryTarget State::materializeBoundaryTarget(
		const RebuiltBoundaryTarget &target) const {
	switch (target.action) {
	case BoundaryAction::Text:
		if (const auto ordinal = textNodeOrdinal(target.leaf); ordinal >= 0) {
			return {
				.action = BoundaryAction::Text,
				.textOrdinal = ordinal,
			};
		}
		break;
	case BoundaryAction::StructuralSelection:
		if (target.listItemIndex >= 0) {
			const auto owner = block(target.block);
			if (owner
				&& owner->kind == BlockKind::List
				&& target.listItemIndex < int(owner->listItems.size())
				&& CanEditBlocks(owner->listItems[target.listItemIndex].blocks)) {
				return {
					.action = BoundaryAction::StructuralSelection,
					.structuralSelection = preparedSelectionForListItem(
						target.block,
						target.listItemIndex),
				};
			}
		} else if (const auto owner = block(target.block);
			owner && CanEditBlock(*owner)) {
			return {
				.action = BoundaryAction::StructuralSelection,
				.structuralSelection = preparedSelectionForBlock(target.block),
			};
		}
		break;
	default:
		break;
	}
	return {};
}

State::BoundaryTarget State::removeTemporaryDownParagraphAndMoveUnchecked() {
	const auto tracked = _temporaryDownParagraph;
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!tracked
		|| !descriptor
		|| !(descriptor->leaf == *tracked)) {
		return {};
	}
	const auto owner = block(tracked->block);
	if (!owner || owner->kind != BlockKind::Paragraph) {
		clearTemporaryDownParagraph();
		return {};
	}
	if (!BlockIsEmpty(*owner)) {
		clearTemporaryDownParagraph();
		return {};
	}
	const auto next = boundaryTargetForLeaf(
		*tracked,
		descriptor,
		true,
		false);
	if (next.action == BoundaryAction::None) {
		return {};
	}
	auto rebuiltTarget = captureRebuiltBoundaryTarget(next);
	if (!rebuiltTarget) {
		return {};
	}
	const auto removed = tracked->block;
	if (!removeTarget({
			.kind = RemovalKind::Block,
			.block = removed,
		})) {
		return {};
	}
	shiftRebuiltBoundaryTargetAfterRemovedBlock(*rebuiltTarget, removed);
	if (rebuiltTarget->action == BoundaryAction::None) {
		return {};
	}
	rebuild();
	const auto materialized = materializeBoundaryTarget(*rebuiltTarget);
	if (materialized.action == BoundaryAction::Text) {
		if (!setActiveTextByOrdinal(materialized.textOrdinal)) {
			ensureActiveTextOrdinal();
		}
	}
	return materialized;
}

std::optional<int> State::handleActiveHeadingEnter(
		const ActiveEnterContext &context) {
	return applyCheckedMutation(std::optional<int>(), [=](State &candidate) {
		const auto result = candidate.handleActiveHeadingEnterUnchecked(
			context);
		return CheckedMutationResult<std::optional<int>>{
			.apply = result.has_value(),
			.result = result,
		};
	});
}

std::optional<int> State::handleActiveHeadingEnterUnchecked(
		const ActiveEnterContext &context) {
	return handleActiveBlockEnterUnchecked(BlockKind::Heading, context);
}

std::optional<int> State::handleActiveFooterEnter(
		const ActiveEnterContext &context) {
	return applyCheckedMutation(std::optional<int>(), [=](State &candidate) {
		const auto result = candidate.handleActiveFooterEnterUnchecked(
			context);
		return CheckedMutationResult<std::optional<int>>{
			.apply = result.has_value(),
			.result = result,
		};
	});
}

std::optional<int> State::handleActiveFooterEnterUnchecked(
		const ActiveEnterContext &context) {
	return handleActiveBlockEnterUnchecked(BlockKind::Footer, context);
}

std::optional<int> State::handleActiveParagraphEnter(
		const ActiveEnterContext &context) {
	return applyCheckedMutation(std::optional<int>(), [=](State &candidate) {
		const auto result = candidate.handleActiveParagraphEnterUnchecked(
			context);
		return CheckedMutationResult<std::optional<int>>{
			.apply = result.has_value(),
			.result = result,
		};
	});
}

std::optional<int> State::handleActiveParagraphEnterUnchecked(
		const ActiveEnterContext &context) {
	return handleActiveBlockEnterUnchecked(BlockKind::Paragraph, context);
}

std::optional<int> State::handleActiveBlockEnterUnchecked(
		BlockKind kind,
		const ActiveEnterContext &context) {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor || descriptor->leaf.kind != LeafKind::BlockText) {
		return std::nullopt;
	}
	const auto path = descriptor->leaf.block;
	const auto blocks = blockContainer(path.container);
	if (!blocks
		|| path.index < 0
		|| path.index >= int(blocks->size())
		|| (*blocks)[path.index].kind != kind) {
		return std::nullopt;
	}
	return handleEnterAtBlockUnchecked(path.container, path.index, context);
}

std::optional<int> State::handleEnterAtBlockUnchecked(
		const BlockContainerPath &container,
		int index,
		const ActiveEnterContext &context) {
	const auto blocks = blockContainer(container);
	if (!blocks || index < 0 || index >= int(blocks->size())) {
		return std::nullopt;
	}
	if (context.position == EnterPosition::Beginning) {
		clearTemporaryDownParagraph();
		blocks->insert(blocks->begin() + index, MakeParagraphBlock());
		const auto target = LeafPath{
			.kind = LeafKind::BlockText,
			.block = {
				.container = container,
				.index = index + 1,
			},
		};
		rebuild();
		return activateRebuiltLeaf(target);
	}
	const auto insertAt = index + 1;
	if (insertAt < 0 || insertAt > int(blocks->size())) {
		return std::nullopt;
	}
	auto &owner = (*blocks)[index];
	const auto split = (context.position == EnterPosition::Middle)
		&& (context.head.text.size() + context.tail.text.size()
			== owner.text.text.text.size());
	clearTemporaryDownParagraph();
	auto paragraph = MakeParagraphBlock();
	if (split) {
		owner.text.text = context.head;
		paragraph.text.text = context.tail;
	}
	blocks->insert(blocks->begin() + insertAt, std::move(paragraph));
	const auto target = LeafPath{
		.kind = LeafKind::BlockText,
		.block = {
			.container = container,
			.index = insertAt,
		},
	};
	rebuild();
	return activateRebuiltLeaf(target);
}

std::optional<int> State::handleActiveListEnter(
		const ActiveEnterContext &context) {
	return applyCheckedMutation(std::optional<int>(), [=](State &candidate) {
		const auto result = candidate.handleActiveListEnterUnchecked(
			context);
		return CheckedMutationResult<std::optional<int>>{
			.apply = result.has_value(),
			.result = result,
		};
	});
}

std::optional<int> State::handleActiveListEnterUnchecked(
		const ActiveEnterContext &context) {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return std::nullopt;
	}
	const auto blocksForm = (descriptor->leaf.kind == LeafKind::BlockText);
	const auto paragraphIndex = blocksForm
		? descriptor->leaf.block.index
		: 0;
	const auto surface = normalizeActiveListItemSurface();
	if (!surface) {
		return std::nullopt;
	}
	const auto owner = block(surface->path);
	const auto item = listItem(surface->path, surface->itemIndex);
	if (!owner || owner->kind != BlockKind::List || !item) {
		return std::nullopt;
	}
	if (paragraphIndex < 0 || paragraphIndex >= int(item->blocks.size())) {
		return std::nullopt;
	}
	const auto itemStart = (context.position == EnterPosition::Beginning)
		&& (paragraphIndex == 0);
	const auto itemEnd = (context.position == EnterPosition::End)
		&& (paragraphIndex + 1 == int(item->blocks.size()));
	if (!itemStart && !itemEnd) {
		auto &blocks = item->blocks;
		const auto moveFrom = (context.position == EnterPosition::Beginning)
			? paragraphIndex
			: (paragraphIndex + 1);
		const auto split = (context.position == EnterPosition::Middle)
			&& (context.head.text.size() + context.tail.text.size()
				== blocks[paragraphIndex].text.text.text.size());
		clearTemporaryDownParagraph();
		auto next = ListItem();
		next.taskState = item->taskState;
		if (split) {
			blocks[paragraphIndex].text.text = context.head;
			auto paragraph = MakeParagraphBlock();
			paragraph.text.text = context.tail;
			next.blocks.push_back(std::move(paragraph));
		}
		next.blocks.insert(
			next.blocks.end(),
			std::make_move_iterator(blocks.begin() + moveFrom),
			std::make_move_iterator(blocks.end()));
		blocks.erase(blocks.begin() + moveFrom, blocks.end());
		if (next.blocks.empty()) {
			next.blocks.push_back(MakeParagraphBlock());
		}
		const auto insertedCount = int(next.blocks.size());
		owner->listItems.insert(
			owner->listItems.begin() + surface->itemIndex + 1,
			std::move(next));
		rebuild();
		focusInsertedBlocks(
			ListItemChildrenContainer(surface->path, surface->itemIndex + 1),
			0,
			insertedCount);
		return (_activeTextOrdinal >= 0)
			? std::make_optional(_activeTextOrdinal)
			: std::nullopt;
	}
	auto target = std::optional<LeafPath>();
	if (itemStart) {
		const auto first = (surface->itemIndex == 1)
			? listItem(surface->path, 0)
			: nullptr;
		const auto startEscape = first
			&& ListItemIsEmpty(*first)
			&& (first->blocks.empty()
				|| ((first->blocks.size() == 1)
					&& (first->blocks.front().kind
						== BlockKind::Paragraph)));
		clearTemporaryDownParagraph();
		if (startEscape) {
			owner->listItems.erase(owner->listItems.begin());
			if (const auto paragraph = reuseOrInsertParagraph(
					surface->path.container,
					surface->path.index)) {
				target = paragraph->leaf;
			}
		} else {
			owner->listItems.insert(
				owner->listItems.begin() + surface->itemIndex,
				MakeParagraphListItem(item->taskState));
			target = LeafPath{
				.kind = LeafKind::BlockText,
				.block = {
					.container = ListItemChildrenContainer(
						surface->path,
						surface->itemIndex + 1),
					.index = 0,
				},
			};
		}
	} else {
		const auto trailingEmpty = (surface->itemIndex + 1
				== int(owner->listItems.size()))
			&& (item->blocks.size() == 1)
			&& (item->blocks.front().kind == BlockKind::Paragraph)
			&& ListItemIsEmpty(*item);
		if (trailingEmpty) {
			clearTemporaryDownParagraph();
			owner->listItems.erase(
				owner->listItems.begin() + surface->itemIndex);
			if (owner->listItems.empty()) {
				const auto blocks = blockContainer(surface->path.container);
				if (!blocks
					|| surface->path.index < 0
						|| surface->path.index >= int(blocks->size())) {
					return std::nullopt;
				}
				clearTemporaryDownParagraph();
				blocks->erase(blocks->begin() + surface->path.index);
				if (const auto paragraph = reuseOrInsertParagraph(
						surface->path.container,
						surface->path.index)) {
					target = paragraph->leaf;
				}
			} else {
				if (const auto paragraph = reuseOrInsertParagraph(
						surface->path.container,
						surface->path.index + 1)) {
					target = paragraph->leaf;
				}
			}
		} else {
			clearTemporaryDownParagraph();
			owner->listItems.insert(
				owner->listItems.begin() + surface->itemIndex + 1,
				MakeParagraphListItem(item->taskState));
			target = LeafPath{
				.kind = LeafKind::BlockText,
				.block = {
					.container = ListItemChildrenContainer(
						surface->path,
						surface->itemIndex + 1),
					.index = 0,
				},
			};
		}
	}
	if (!target) {
		return std::nullopt;
	}
	rebuild();
	return activateRebuiltLeaf(*target);
}

std::optional<int> State::handleActiveQuoteEnter(
		const ActiveEnterContext &context) {
	return applyCheckedMutation(std::optional<int>(), [=](State &candidate) {
		const auto result = candidate.handleActiveQuoteEnterUnchecked(
			context);
		return CheckedMutationResult<std::optional<int>>{
			.apply = result.has_value(),
			.result = result,
		};
	});
}

std::optional<int> State::handleActiveQuoteEnterUnchecked(
		const ActiveEnterContext &context) {
	if (context.position == EnterPosition::End) {
		return std::nullopt;
	}
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor || descriptor->leaf.kind != LeafKind::BlockText) {
		return std::nullopt;
	}
	const auto owner = block(descriptor->leaf.block);
	if (!owner
		|| owner->kind != BlockKind::Quote
		|| owner->pullquote
		|| !owner->blocks.empty()) {
		return std::nullopt;
	}
	const auto container = BlockChildrenContainer(descriptor->leaf.block);
	if (!normalizeTextOnlyQuoteSurface(container, true)) {
		return std::nullopt;
	}
	return handleEnterAtBlockUnchecked(container, 0, context);
}

bool State::pasteClipboardListItemsAfterActive(
		const ClipboardListItemsData &data,
		std::optional<ActiveTextInsertContext> context) {
	if (data.items.empty()) {
		return false;
	}
	return applyCheckedMutation(false, [
			data,
			context = std::move(context)](State &candidate) mutable {
		auto block = Block();
		block.kind = BlockKind::List;
		block.listKind = data.listKind;
		block.orderedList = data.orderedList;
		block.listItems = data.items;
		auto blocks = std::vector<Block>();
		blocks.push_back(std::move(block));
		NormalizeInsertedOrderedListMetadata(&blocks);
		candidate.normalizeInsertedBlockAnchors(blocks);

		const auto sameList = [&] {
			const auto descriptor = candidate.textNode(
				candidate._activeTextOrdinal);
			const auto surface = candidate.activeListItemSurface();
			auto owner = surface ? candidate.block(surface->path) : nullptr;
			if (!descriptor
				|| !surface
				|| !owner
				|| !ListBlockMatchesClipboardData(*owner, data)) {
				return false;
			}
			auto insertContext = context
				? *context
				: ActiveTextInsertContext{
					.before = candidate.activeText(),
				};
			const auto itemContainer = ListItemChildrenContainer(
				surface->path,
				surface->itemIndex);
			auto activeBlockIndex = -1;
			switch (descriptor->leaf.kind) {
			case LeafKind::ListItemText:
				break;
			case LeafKind::BlockText:
				if (descriptor->leaf.block.container != itemContainer) {
					return false;
				}
				activeBlockIndex = descriptor->leaf.block.index;
				break;
			default:
				return false;
			}
			if (const auto normalized
				= candidate.normalizeTextOnlyListItemForInsertion(
					itemContainer)) {
				if (descriptor->leaf.kind == LeafKind::ListItemText) {
					activeBlockIndex = *normalized;
				} else if (*normalized >= 0) {
					++activeBlockIndex;
				}
			} else if (descriptor->leaf.kind == LeafKind::ListItemText) {
				activeBlockIndex = -1;
			}
			owner = candidate.block(surface->path);
			if (!owner
				|| owner->kind != BlockKind::List
				|| surface->itemIndex < 0
				|| surface->itemIndex >= int(owner->listItems.size())) {
				return false;
			}
			const auto item = &owner->listItems[surface->itemIndex];
			if (activeBlockIndex >= 0) {
				if (activeBlockIndex >= int(item->blocks.size())
					|| item->blocks[activeBlockIndex].kind
						!= BlockKind::Paragraph) {
					return false;
				}
			} else if (!item->blocks.empty()) {
				return false;
			}

			enum class OriginalParagraphSide {
				None,
				Leading,
				Trailing,
			};

			enum class OriginalItemSide {
				None,
				Leading,
				Trailing,
			};

			const auto makeParagraph = [](TextWithEntities text) {
				auto paragraph = State::MakeParagraphBlock();
				paragraph.text.text = std::move(text);
				return paragraph;
			};

			candidate.clearTemporaryDownParagraph();
			auto current = std::move(owner->listItems[surface->itemIndex]);
			auto insertedItems = std::move(blocks.front().listItems);
			const auto insertedCount = int(insertedItems.size());
			auto leading = ListItem();
			leading.taskState = current.taskState;
			auto trailing = ListItem();
			trailing.taskState = current.taskState;
			auto activeParagraph = Block();
			if (activeBlockIndex >= 0) {
				activeParagraph = std::move(current.blocks[activeBlockIndex]);
			}
			for (auto i = 0; i < std::max(activeBlockIndex, 0); ++i) {
				leading.blocks.push_back(std::move(current.blocks[i]));
			}
			auto originalParagraphSide = OriginalParagraphSide::None;
			if (activeBlockIndex >= 0 && !insertContext.before.text.isEmpty()) {
				activeParagraph.text.text = std::move(insertContext.before);
				leading.blocks.push_back(std::move(activeParagraph));
				originalParagraphSide = OriginalParagraphSide::Leading;
			}
			if (activeBlockIndex >= 0 && !insertContext.after.text.isEmpty()) {
				if (originalParagraphSide == OriginalParagraphSide::None) {
					activeParagraph.text.text = std::move(insertContext.after);
					trailing.blocks.push_back(std::move(activeParagraph));
					originalParagraphSide = OriginalParagraphSide::Trailing;
				} else {
					trailing.blocks.push_back(makeParagraph(
						std::move(insertContext.after)));
				}
			}
			if (activeBlockIndex >= 0) {
				for (auto i = activeBlockIndex + 1;
					i < int(current.blocks.size());
					++i) {
					trailing.blocks.push_back(std::move(current.blocks[i]));
				}
			}
			auto originalItemSide = OriginalItemSide::None;
			switch (originalParagraphSide) {
			case OriginalParagraphSide::Leading:
				originalItemSide = OriginalItemSide::Leading;
				break;
			case OriginalParagraphSide::Trailing:
				originalItemSide = OriginalItemSide::Trailing;
				break;
			case OriginalParagraphSide::None:
				break;
			}
			const auto keepLeading = !State::ListItemIsEmpty(leading);
			const auto keepTrailing = !State::ListItemIsEmpty(trailing);
			if (originalItemSide == OriginalItemSide::None) {
				if (keepLeading) {
					originalItemSide = OriginalItemSide::Leading;
				} else if (keepTrailing) {
					originalItemSide = OriginalItemSide::Trailing;
				}
			}
			if (originalItemSide == OriginalItemSide::Leading) {
				leading.number = std::move(current.number);
				leading.anchorId = std::move(current.anchorId);
				leading.text = std::move(current.text);
			} else if (originalItemSide == OriginalItemSide::Trailing) {
				trailing.number = std::move(current.number);
				trailing.anchorId = std::move(current.anchorId);
				trailing.text = std::move(current.text);
			}

			auto replacement = std::vector<ListItem>();
			replacement.reserve(
				insertedCount
				+ (keepLeading ? 1 : 0)
				+ (keepTrailing ? 1 : 0));
			if (keepLeading) {
				replacement.push_back(std::move(leading));
			}
			const auto insertedFrom = surface->itemIndex + int(keepLeading);
			replacement.insert(
				replacement.end(),
				std::make_move_iterator(insertedItems.begin()),
				std::make_move_iterator(insertedItems.end()));
			if (keepTrailing) {
				replacement.push_back(std::move(trailing));
			}
			owner->listItems.erase(owner->listItems.begin() + surface->itemIndex);
			owner->listItems.insert(
				owner->listItems.begin() + surface->itemIndex,
				std::make_move_iterator(replacement.begin()),
				std::make_move_iterator(replacement.end()));
			candidate.rebuild();
			for (auto i = 0, count = candidate.textNodeCount(); i != count; ++i) {
				const auto itemIndex = ListItemIndexForLeaf(
					candidate._textNodes[i].leaf,
					surface->path);
				if (itemIndex
					&& (*itemIndex >= insertedFrom)
					&& (*itemIndex < insertedFrom + insertedCount)
					&& candidate.setActiveTextByOrdinal(i)) {
					return true;
				}
			}
			candidate.ensureActiveTextOrdinal();
			return true;
		}();
		const auto applied = sameList
			? true
			: candidate.insertBlocksAfterActiveUnchecked(
				std::move(blocks),
				std::move(context));
		return CheckedMutationResult<bool>{
			.apply = applied,
			.result = applied,
		};
	});
}

bool State::wrapStructuralBlockSelection(
		const Markdown::PreparedEditSelection &selection,
		InsertAction action,
		BoundaryTarget *destination) {
	if (selection.kind != PreparedEditSelectionKind::Blocks) {
		return false;
	}
	const auto range = validateBlockRange(selection.blocks);
	auto payload = structuredClipboardDataForSelection(selection);
	auto *data = payload
		? std::get_if<ClipboardBlockData>(&*payload)
		: nullptr;
	if (!range || !data) {
		return false;
	}
	auto container = range->container;
	auto insertAt = range->from;
	if (!removeStructuralSelection(selection, true)) {
		return false;
	}
	if (const auto normalized = normalizeTextOnlyListItemForInsertion(
			container); normalized && (*normalized >= 0)) {
		++insertAt;
	}
	auto wrapper = makeBlock(action);
	switch (action.type) {
	case InsertBlockType::Blockquote:
	case InsertBlockType::Pullquote:
	case InsertBlockType::Details:
		wrapper.blocks = std::move(data->blocks);
		break;
	case InsertBlockType::OrderedList:
	case InsertBlockType::BulletList:
		if (wrapper.kind != BlockKind::List || wrapper.listItems.empty()) {
			return false;
		}
		wrapper.listItems.front().blocks = std::move(data->blocks);
		adoptLeadingParagraphListItemText(&wrapper.listItems.front());
		break;
	default:
		return false;
	}
	auto inserted = std::vector<Block>();
	inserted.push_back(std::move(wrapper));
	normalizeInsertedBlockAnchors(inserted);
	if (!insertPreparedBlocksAtExplicitPosition(
			std::move(inserted),
			container,
			&insertAt)) {
		return false;
	}
	rebuild();
	const auto target = destinationTargetForInsertedBlocks(
		container,
		insertAt,
		1);
	if (destination) {
		*destination = target;
	}
	return true;
}

bool State::unwrapMatchingStructuralWrapper(
		const Markdown::PreparedEditSelection &selection,
		InsertBlockType type,
		BoundaryTarget *destination) {
	if (selection.kind != PreparedEditSelectionKind::Blocks) {
		return false;
	}
	const auto range = validateBlockRange(selection.blocks);
	if (!range || range->container.steps.empty()) {
		return false;
	}
	const auto step = range->container.steps.back();
	if (step.kind != BlockContainerKind::BlockChildren) {
		return false;
	}
	auto parentContainer = range->container;
	parentContainer.steps.pop_back();
	auto *parent = blockContainer(parentContainer);
	const auto wrapperPath = BlockPath{
		.container = parentContainer,
		.index = step.blockIndex,
	};
	auto *wrapper = block(wrapperPath);
	const auto matches = [&](const Block &block) {
		switch (type) {
		case InsertBlockType::Blockquote:
			return (block.kind == BlockKind::Quote) && !block.pullquote;
		case InsertBlockType::Pullquote:
			return (block.kind == BlockKind::Quote) && block.pullquote;
		case InsertBlockType::Details:
			return (block.kind == BlockKind::Details);
		default:
			return false;
		}
	};
	if (!parent
		|| !wrapper
		|| !matches(*wrapper)
		|| (range->from != 0)
		|| (range->till != int(wrapper->blocks.size()))) {
		return false;
	}
	if (!wrapper->anchorId.isEmpty()
		|| !RichTextIsEmpty(wrapper->text)
		|| !RichTextIsEmpty(wrapper->caption)) {
		return false;
	}
	clearTemporaryDownParagraph();
	auto blocks = std::move(wrapper->blocks);
	const auto insertedCount = int(blocks.size());
	parent->erase(parent->begin() + wrapperPath.index);
	parent->insert(
		parent->begin() + wrapperPath.index,
		std::make_move_iterator(blocks.begin()),
		std::make_move_iterator(blocks.end()));
	rebuild();
	const auto target = destinationTargetForInsertedBlocks(
		parentContainer,
		wrapperPath.index,
		insertedCount);
	if (destination) {
		*destination = target;
	}
	return true;
}

std::vector<Block> State::takeListItemBlocksForUnwrap(ListItem *item) {
	auto result = std::vector<Block>();
	if (!item) {
		return result;
	}
	if (!item->anchorId.isEmpty() || !RichTextIsEmpty(item->text)) {
		auto paragraph = MakeParagraphBlock();
		paragraph.anchorId = std::move(item->anchorId);
		paragraph.text = std::move(item->text);
		result.push_back(std::move(paragraph));
	}
	result.insert(
		result.end(),
		std::make_move_iterator(item->blocks.begin()),
		std::make_move_iterator(item->blocks.end()));
	item->blocks.clear();
	return result;
}

void State::adoptLeadingParagraphListItemText(ListItem *item) const {
	// List items hold either inline text or a list of blocks, never both,
	// so adopt the paragraph text only if it is the single item block.
	if (!item
		|| (item->blocks.size() != 1)
		|| item->blocks.front().kind != BlockKind::Paragraph) {
		return;
	}
	item->text = std::move(item->blocks.front().text);
	item->anchorId = std::move(item->blocks.front().anchorId);
	item->blocks.erase(item->blocks.begin());
}

bool State::unwrapMatchingListItemWrapper(
		const Markdown::PreparedEditSelection &selection,
		InsertBlockType type,
		BoundaryTarget *destination) {
	if (selection.kind != PreparedEditSelectionKind::ListItems) {
		return false;
	}
	const auto range = validateListItemRange(selection.listItems);
	if (!range || (range->from + 1 != range->till)) {
		return false;
	}
	auto *owner = block(range->block);
	auto *parent = blockContainer(range->block.container);
	const auto matches = [&](const Block &block) {
		switch (type) {
		case InsertBlockType::OrderedList:
			return (block.kind == BlockKind::List)
				&& (block.listKind == ListKind::Ordered);
		case InsertBlockType::BulletList:
			return (block.kind == BlockKind::List)
				&& (block.listKind == ListKind::Bullet);
		default:
			return false;
		}
	};
	if (!owner || !parent || !matches(*owner) || IsTaskList(owner->listItems)) {
		return false;
	}
	clearTemporaryDownParagraph();
	return unwrapListItemIntoParent(
		range->block,
		range->from,
		false,
		destination);
}

bool State::unwrapListItemIntoParent(
		const BlockPath &listPath,
		int itemIndex,
		bool materializeEmptyItem,
		BoundaryTarget *destination) {
	auto *owner = block(listPath);
	auto *parent = blockContainer(listPath.container);
	if (!owner
		|| !parent
		|| itemIndex < 0
		|| itemIndex >= int(owner->listItems.size())
		|| listPath.index < 0
		|| listPath.index >= int(parent->size())) {
		return false;
	}
	const auto hasLeading = (itemIndex > 0);
	const auto hasTrailing = (itemIndex + 1 < int(owner->listItems.size()));
	const auto leadingStart = (owner->listKind == ListKind::Ordered
		&& hasLeading)
		? EffectiveOrderedItemValue(*owner, 0)
		: std::optional<int>();
	const auto trailingStart = (owner->listKind == ListKind::Ordered
		&& hasTrailing)
		? EffectiveOrderedItemValue(*owner, itemIndex + 1)
		: std::optional<int>();
	auto inserted = takeListItemBlocksForUnwrap(&owner->listItems[itemIndex]);
	if (materializeEmptyItem && inserted.empty()) {
		inserted.push_back(MakeParagraphBlock());
	}
	auto trailing = std::optional<Block>();
	if (hasTrailing) {
		trailing = Block();
		trailing->kind = BlockKind::List;
		trailing->listKind = owner->listKind;
		trailing->orderedList = owner->orderedList;
		if (trailingStart.has_value()) {
			trailing->orderedList.start = trailingStart;
		}
		trailing->listItems = std::vector<ListItem>(
			std::make_move_iterator(
				owner->listItems.begin() + itemIndex + 1),
			std::make_move_iterator(owner->listItems.end()));
	}
	if (hasLeading) {
		owner->listItems.erase(
			owner->listItems.begin() + itemIndex,
			owner->listItems.end());
		if (leadingStart.has_value()) {
			owner->orderedList.start = leadingStart;
		}
	} else {
		parent->erase(parent->begin() + listPath.index);
	}
	auto insertAt = listPath.index + (hasLeading ? 1 : 0);
	const auto insertedCount = int(inserted.size());
	parent->insert(
		parent->begin() + insertAt,
		std::make_move_iterator(inserted.begin()),
		std::make_move_iterator(inserted.end()));
	if (trailing.has_value()) {
		NormalizeInsertedOrderedListMetadata(&*trailing);
		parent->insert(
			parent->begin() + insertAt + insertedCount,
			std::move(*trailing));
	}
	rebuild();
	const auto target = destinationTargetForInsertedBlocks(
		listPath.container,
		insertAt,
		insertedCount);
	if (destination) {
		*destination = target;
	}
	return true;
}

bool State::replaceStructuralSelectionWithBlock(
		const Markdown::PreparedEditSelection &selection,
		InsertAction action,
		std::optional<ActiveTextInsertContext> context,
		BoundaryTarget *destination) {
	_lastLimitError = std::nullopt;
	if (destination) {
		*destination = {};
	}
	auto candidate = State(
		std::make_shared<RichPage>(*_richPage),
		_mediaRuntime,
		_limits);
	candidate._activeTextOrdinal = _activeTextOrdinal;
	candidate._lastLimitError = std::nullopt;
	candidate._temporaryDownParagraph = _temporaryDownParagraph;
	const auto commitValidatedCandidate = [&](State &&candidate) {
		const auto error = ValidateRichMessage(
			*candidate._richPage,
			_limits);
		if (error) {
			_lastLimitError = error;
			return false;
		}
		commitCheckedMutation(std::move(candidate));
		return true;
	};
	const auto structuralTextBlockConversion = [&]()
	-> std::optional<LeafPath> {
		const auto allowed = [](InsertBlockType type) {
			switch (type) {
			case InsertBlockType::Heading:
			case InsertBlockType::Code:
			case InsertBlockType::Footer:
				return true;
			default:
				return false;
			}
		};
		if (selection.kind != PreparedEditSelectionKind::Blocks
			|| !allowed(action.type)) {
			return std::nullopt;
		}
		const auto range = candidate.validateBlockRange(selection.blocks);
		if (!range || (range->from + 1 != range->till)) {
			return std::nullopt;
		}
		const auto leaf = LeafPath{
			.kind = LeafKind::BlockText,
			.block = {
				.container = range->container,
				.index = range->from,
			},
		};
		const auto owner = candidate.block(leaf.block);
		if (!owner
			|| ((owner->kind != BlockKind::Paragraph)
				&& (owner->kind != BlockKind::Heading)
				&& (owner->kind != BlockKind::Footer))
			|| (candidate.textNodeOrdinal(leaf) < 0)) {
			return std::nullopt;
		}
		return leaf;
	};
	if (const auto leaf = structuralTextBlockConversion()) {
		const auto ordinal = candidate.textNodeOrdinal(*leaf);
		const auto owner = candidate.block(leaf->block);
		if (!owner || !candidate.setActiveTextByOrdinal(ordinal)) {
			_lastLimitError = candidate._lastLimitError;
			return false;
		}
		if (!candidate.insertBlockAfterActive(action, ActiveTextInsertContext{
				.before = {},
				.selected = owner->text.text,
				.after = {},
			})) {
			_lastLimitError = candidate._lastLimitError;
			return false;
		}
		if (destination && (candidate._activeTextOrdinal >= 0)) {
			*destination = {
				.action = BoundaryTarget::Action::Text,
				.textOrdinal = candidate._activeTextOrdinal,
			};
		}
		commitCheckedMutation(std::move(candidate));
		return true;
	}
	auto target = BoundaryTarget();
	switch (action.type) {
	case InsertBlockType::Blockquote:
	case InsertBlockType::Pullquote:
	case InsertBlockType::Details:
		if (selection.kind == PreparedEditSelectionKind::TableRows
			|| selection.kind == PreparedEditSelectionKind::TableCells) {
			return false;
		}
		if (candidate.unwrapMatchingStructuralWrapper(
				selection,
				action.type,
				&target)) {
			if (!commitValidatedCandidate(std::move(candidate))) {
				return false;
			}
			if (destination) {
				*destination = target;
			}
			return true;
		}
		if (selection.kind == PreparedEditSelectionKind::Blocks) {
			if (!candidate.wrapStructuralBlockSelection(
					selection,
					action,
					&target)) {
				_lastLimitError = candidate._lastLimitError;
				return false;
			}
			if (!commitValidatedCandidate(std::move(candidate))) {
				return false;
			}
			if (destination) {
				*destination = target;
			}
			return true;
		}
		break;
	case InsertBlockType::OrderedList:
	case InsertBlockType::BulletList:
		if (selection.kind == PreparedEditSelectionKind::TableRows
			|| selection.kind == PreparedEditSelectionKind::TableCells) {
			return false;
		}
		if (candidate.unwrapMatchingListItemWrapper(
				selection,
				action.type,
				&target)) {
			if (!commitValidatedCandidate(std::move(candidate))) {
				return false;
			}
			if (destination) {
				*destination = target;
			}
			return true;
		}
		if (selection.kind == PreparedEditSelectionKind::Blocks) {
			if (!candidate.wrapStructuralBlockSelection(
					selection,
					action,
					&target)) {
				_lastLimitError = candidate._lastLimitError;
				return false;
			}
			if (!commitValidatedCandidate(std::move(candidate))) {
				return false;
			}
			if (destination) {
				*destination = target;
			}
			return true;
		}
		break;
	default:
		break;
	}
	if (!candidate.removeStructuralSelection(selection, true)) {
		_lastLimitError = candidate._lastLimitError;
		return false;
	}
	if (!candidate.insertBlockAfterActive(action, std::move(context))) {
		_lastLimitError = candidate._lastLimitError;
		return false;
	}
	if (destination && (candidate._activeTextOrdinal >= 0)) {
		*destination = {
			.action = BoundaryTarget::Action::Text,
			.textOrdinal = candidate._activeTextOrdinal,
		};
	}
	commitCheckedMutation(std::move(candidate));
	return true;
}

bool State::toggleCodeBlockForStructuralSelection(
		const Markdown::PreparedEditSelection &selection) {
	_lastLimitError = std::nullopt;
	if (selection.kind != PreparedEditSelectionKind::Blocks) {
		return false;
	}
	const auto range = validateBlockRange(selection.blocks);
	if (!range || (range->from + 1 != range->till)) {
		return false;
	}
	const auto path = BlockPath{
		.container = range->container,
		.index = range->from,
	};
	const auto owner = block(path);
	if (!owner) {
		return false;
	}
	switch (owner->kind) {
	case BlockKind::Paragraph:
		return replaceStructuralSelectionWithBlock(selection, {
			.type = InsertBlockType::Code,
		});
	case BlockKind::Code: {
		return applyCheckedMutation(false, [selection](State &candidate) {
			const auto range = candidate.validateBlockRange(selection.blocks);
			if (!range || (range->from + 1 != range->till)) {
				return CheckedMutationResult<bool>{ .result = false };
			}
			const auto path = BlockPath{
				.container = range->container,
				.index = range->from,
			};
			const auto owner = candidate.block(path);
			if (!owner || owner->kind != BlockKind::Code) {
				return CheckedMutationResult<bool>{ .result = false };
			}
			auto paragraph = MakeParagraphBlock();
			paragraph.anchorId = owner->anchorId;
			paragraph.text = owner->text;
			auto blocks = std::vector<Block>();
			blocks.push_back(std::move(paragraph));
			auto insertAt = range->from;
			if (!candidate.removeStructuralSelection(selection, true)) {
				return CheckedMutationResult<bool>{ .result = false };
			}
			const auto normalized
				= candidate.normalizeTextOnlyListItemForInsertion(
					range->container);
			if (normalized && (*normalized >= 0)) {
				++insertAt;
			}
			if (!candidate.insertPreparedBlocksAtExplicitPosition(
					std::move(blocks),
					range->container,
					&insertAt)) {
				return CheckedMutationResult<bool>{ .result = false };
			}
			candidate.rebuild();
			(void)candidate.destinationTargetForInsertedBlocks(
				range->container,
				insertAt,
				1);
			return CheckedMutationResult<bool>{
				.apply = true,
				.result = true,
			};
		});
	}
	default:
		return false;
	}
}

bool State::replaceStructuralSelectionWithPreparedBlocks(
		const Markdown::PreparedEditSelection &selection,
		std::vector<Block> blocks,
		std::optional<ActiveTextInsertContext> context) {
	_lastLimitError = std::nullopt;
	auto candidate = State(
		std::make_shared<RichPage>(*_richPage),
		_mediaRuntime,
		_limits);
	candidate._activeTextOrdinal = _activeTextOrdinal;
	candidate._lastLimitError = std::nullopt;
	candidate._temporaryDownParagraph = _temporaryDownParagraph;
	const auto blocksRange = (selection.kind == PreparedEditSelectionKind::Blocks)
		? candidate.validateBlockRange(selection.blocks)
		: std::nullopt;
	if (!candidate.removeStructuralSelection(selection, true)) {
		_lastLimitError = candidate._lastLimitError;
		return false;
	}
	const auto inserted = blocksRange
		? candidate.insertPreparedBlocksAtRemovedBlockRange(
			std::move(blocks),
			*blocksRange)
		: candidate.insertPreparedBlocksAfterActive(
			std::move(blocks),
			std::move(context));
	if (!inserted) {
		_lastLimitError = candidate._lastLimitError;
		return false;
	}
	commitCheckedMutation(std::move(candidate));
	return true;
}

bool State::replaceStructuralSelectionWithClipboardListItems(
		const Markdown::PreparedEditSelection &selection,
		const ClipboardListItemsData &data,
		std::optional<ActiveTextInsertContext> context) {
	_lastLimitError = std::nullopt;
	auto candidate = State(
		std::make_shared<RichPage>(*_richPage),
		_mediaRuntime,
		_limits);
	candidate._activeTextOrdinal = _activeTextOrdinal;
	candidate._lastLimitError = std::nullopt;
	candidate._temporaryDownParagraph = _temporaryDownParagraph;
	if (!candidate.removeStructuralSelection(selection, true)) {
		_lastLimitError = candidate._lastLimitError;
		return false;
	}
	if (!candidate.pasteClipboardListItemsAfterActive(
			data,
			std::move(context))) {
		_lastLimitError = candidate._lastLimitError;
		return false;
	}
	commitCheckedMutation(std::move(candidate));
	return true;
}

State::StructuralSelectionDropResult State::moveStructuralSelectionToDropTarget(
		const PreparedEditSelection &selection,
		const Markdown::PreparedEditDropTarget &target) {
	struct BlockInsertionTarget {
		BlockContainerPath container;
		int insertIndex = -1;
	};
	struct ListInsertionTarget {
		BlockPath block;
		int insertIndex = -1;
	};
	const auto failure = StructuralSelectionDropResult{
		.result = ApplyResult::Failed,
	};
	return applyCheckedMutation(failure, [selection, target](State &candidate) {
		auto result = StructuralSelectionDropResult{
			.result = ApplyResult::Failed,
		};
		const auto payload = candidate.structuredClipboardDataForSelection(
			selection);
		if (!payload) {
			return CheckedMutationResult<StructuralSelectionDropResult>{
				.apply = false,
				.result = result,
			};
		}
		auto blockTarget = std::optional<BlockInsertionTarget>();
		auto listTarget = std::optional<ListInsertionTarget>();
		if (const auto block = std::get_if<Markdown::PreparedEditBlockDropTarget>(
				&target)) {
			const auto container = candidate.convertBlockContainerPath(
				block->container);
			if (!container
				|| !candidate.blockContainer(*container)
				|| block->insertIndex < 0) {
				return CheckedMutationResult<StructuralSelectionDropResult>{
					.apply = false,
					.result = result,
				};
			}
			blockTarget = {
				.container = *container,
				.insertIndex = block->insertIndex,
			};
		} else if (const auto list
			= std::get_if<Markdown::PreparedEditListItemDropTarget>(&target)) {
			const auto blockPath = candidate.convertBlockPath(list->block);
			const auto owner = blockPath ? candidate.block(*blockPath) : nullptr;
			if (!blockPath
				|| !owner
				|| owner->kind != BlockKind::List
				|| list->insertIndex < 0
				|| list->insertIndex > int(owner->listItems.size())) {
				return CheckedMutationResult<StructuralSelectionDropResult>{
					.apply = false,
					.result = result,
				};
			}
			listTarget = {
				.block = *blockPath,
				.insertIndex = list->insertIndex,
			};
		} else {
			return CheckedMutationResult<StructuralSelectionDropResult>{
				.apply = false,
				.result = result,
			};
		}
		switch (selection.kind) {
		case PreparedEditSelectionKind::Blocks: {
			const auto range = candidate.validateBlockRange(selection.blocks);
			if (!range || !blockTarget) {
				return CheckedMutationResult<StructuralSelectionDropResult>{
					.apply = false,
					.result = result,
				};
			}
			if (blockTarget->container == range->container) {
				if (blockTarget->insertIndex >= range->from
					&& blockTarget->insertIndex <= range->till) {
					result.result = ApplyResult::Unchanged;
					return CheckedMutationResult<StructuralSelectionDropResult>{
						.apply = false,
						.result = result,
					};
				} else if (blockTarget->insertIndex > range->till) {
					blockTarget->insertIndex -= (range->till - range->from);
				}
			}
			for (auto i = range->till; i != range->from;) {
				--i;
				const auto removed = BlockPath{
					.container = range->container,
					.index = i,
				};
				if (!ShiftBlockContainerPathAfterRemovedBlock(
						blockTarget->container,
						removed)) {
					result.result = ApplyResult::Unchanged;
					return CheckedMutationResult<StructuralSelectionDropResult>{
						.apply = false,
						.result = result,
					};
				}
			}
		} break;
		case PreparedEditSelectionKind::ListItems: {
			const auto range = candidate.validateListItemRange(
				selection.listItems);
			const auto owner = range ? candidate.block(range->block) : nullptr;
			if (!range || !owner || owner->kind != BlockKind::List) {
				return CheckedMutationResult<StructuralSelectionDropResult>{
					.apply = false,
					.result = result,
				};
			}
			const auto removesWholeList = (range->from == 0)
				&& (range->till == int(owner->listItems.size()));
			if (blockTarget) {
				if (removesWholeList
					&& blockTarget->container == range->block.container
					&& blockTarget->insertIndex >= range->block.index
					&& blockTarget->insertIndex <= range->block.index + 1) {
					result.result = ApplyResult::Unchanged;
					return CheckedMutationResult<StructuralSelectionDropResult>{
						.apply = false,
						.result = result,
					};
				}
				if (removesWholeList) {
					if (blockTarget->container == range->block.container
						&& blockTarget->insertIndex > range->block.index) {
						--blockTarget->insertIndex;
					}
					if (!ShiftBlockContainerPathAfterRemovedBlock(
							blockTarget->container,
							range->block)) {
						result.result = ApplyResult::Unchanged;
						return CheckedMutationResult<StructuralSelectionDropResult>{
							.apply = false,
							.result = result,
						};
					}
				} else {
					for (auto i = range->till; i != range->from;) {
						--i;
						if (!ShiftBlockContainerPathAfterRemovedListItem(
								blockTarget->container,
								range->block,
								i)) {
							result.result = ApplyResult::Unchanged;
							return CheckedMutationResult<StructuralSelectionDropResult>{
								.apply = false,
								.result = result,
							};
						}
					}
				}
			} else if (listTarget) {
				if (listTarget->block == range->block) {
					if (listTarget->insertIndex >= range->from
						&& listTarget->insertIndex <= range->till) {
						result.result = ApplyResult::Unchanged;
						return CheckedMutationResult<StructuralSelectionDropResult>{
							.apply = false,
							.result = result,
						};
					} else if (listTarget->insertIndex > range->till) {
						listTarget->insertIndex -= (range->till - range->from);
					}
				} else if (removesWholeList) {
					if (!ShiftBlockPathAfterRemovedBlock(
							listTarget->block,
							range->block)) {
						result.result = ApplyResult::Unchanged;
						return CheckedMutationResult<StructuralSelectionDropResult>{
							.apply = false,
							.result = result,
						};
					}
				} else {
					for (auto i = range->till; i != range->from;) {
						--i;
						if (!ShiftBlockPathAfterRemovedListItem(
								listTarget->block,
								range->block,
								i)) {
							result.result = ApplyResult::Unchanged;
							return CheckedMutationResult<StructuralSelectionDropResult>{
								.apply = false,
								.result = result,
							};
						}
					}
				}
			} else {
				return CheckedMutationResult<StructuralSelectionDropResult>{
					.apply = false,
					.result = result,
				};
			}
		} break;
		case PreparedEditSelectionKind::TableRows:
		case PreparedEditSelectionKind::TableCells:
		case PreparedEditSelectionKind::None:
			return CheckedMutationResult<StructuralSelectionDropResult>{
				.apply = false,
				.result = result,
			};
		}
		if (!candidate.removeStructuralSelection(selection, true)) {
			return CheckedMutationResult<StructuralSelectionDropResult>{
				.apply = false,
				.result = result,
			};
		}
		if (const auto blocks = std::get_if<ClipboardBlockData>(&*payload)) {
			if (!blockTarget) {
				return CheckedMutationResult<StructuralSelectionDropResult>{
					.apply = false,
					.result = result,
				};
			}
			auto inserted = blocks->blocks;
			NormalizeInsertedOrderedListMetadata(&inserted);
			candidate.normalizeInsertedBlockAnchors(inserted);
			const auto count = int(inserted.size());
			if (!candidate.insertPreparedBlocksAtExplicitPosition(
					std::move(inserted),
					blockTarget->container,
					&blockTarget->insertIndex)) {
				return CheckedMutationResult<StructuralSelectionDropResult>{
					.apply = false,
					.result = result,
				};
			}
			candidate.rebuild();
			result.result = ApplyResult::Changed;
			result.destination = candidate.destinationTargetForInsertedBlocks(
				blockTarget->container,
				blockTarget->insertIndex,
				count);
			return CheckedMutationResult<StructuralSelectionDropResult>{
				.apply = true,
				.result = result,
			};
		}
		const auto items = std::get_if<ClipboardListItemsData>(&*payload);
		if (!items) {
			return CheckedMutationResult<StructuralSelectionDropResult>{
				.apply = false,
				.result = result,
			};
		}
		auto listBlock = Block();
		listBlock.kind = BlockKind::List;
		listBlock.listKind = items->listKind;
		listBlock.orderedList = items->orderedList;
		listBlock.listItems = items->items;
		auto insertedBlocks = std::vector<Block>();
		insertedBlocks.push_back(std::move(listBlock));
		NormalizeInsertedOrderedListMetadata(&insertedBlocks);
		candidate.normalizeInsertedBlockAnchors(insertedBlocks);
		if (listTarget) {
			const auto owner = candidate.block(listTarget->block);
			if (!owner || owner->kind != BlockKind::List) {
				return CheckedMutationResult<StructuralSelectionDropResult>{
					.apply = false,
					.result = result,
				};
			}
			if (ListBlockMatchesClipboardData(*owner, *items)) {
				auto insertedItems = std::move(insertedBlocks.front().listItems);
				const auto count = int(insertedItems.size());
				if (!candidate.insertPreparedListItemsAtExplicitPosition(
						std::move(insertedItems),
						listTarget->block,
						listTarget->insertIndex)) {
					return CheckedMutationResult<StructuralSelectionDropResult>{
						.apply = false,
						.result = result,
					};
				}
				candidate.rebuild();
				result.result = ApplyResult::Changed;
				result.destination = candidate.destinationTargetForInsertedListItems(
					listTarget->block,
					listTarget->insertIndex,
					count);
				return CheckedMutationResult<StructuralSelectionDropResult>{
					.apply = true,
					.result = result,
				};
			}
			auto container = listTarget->block.container;
			auto insertAt = listTarget->block.index;
			auto trailingBlocks = std::vector<Block>();
			const auto splitLeadingStart = (owner->listKind == ListKind::Ordered
				&& listTarget->insertIndex > 0
				&& listTarget->insertIndex < int(owner->listItems.size()))
				? EffectiveOrderedItemValue(*owner, 0)
				: std::optional<int>();
			const auto splitTrailingStart = (owner->listKind == ListKind::Ordered
				&& listTarget->insertIndex > 0
				&& listTarget->insertIndex < int(owner->listItems.size()))
				? EffectiveOrderedItemValue(*owner, listTarget->insertIndex)
				: std::optional<int>();
			if (listTarget->insertIndex > 0) {
				insertAt = listTarget->block.index + 1;
				if (listTarget->insertIndex < int(owner->listItems.size())) {
					auto trailing = Block();
					trailing.kind = BlockKind::List;
					trailing.listKind = owner->listKind;
					trailing.orderedList = owner->orderedList;
					if (splitTrailingStart.has_value()) {
						trailing.orderedList.start = splitTrailingStart;
					}
					trailing.listItems = std::vector<ListItem>(
						std::make_move_iterator(
							owner->listItems.begin() + listTarget->insertIndex),
						std::make_move_iterator(owner->listItems.end()));
					owner->listItems.erase(
						owner->listItems.begin() + listTarget->insertIndex,
						owner->listItems.end());
					if (splitLeadingStart.has_value()) {
						owner->orderedList.start = splitLeadingStart;
					}
					trailingBlocks.push_back(std::move(trailing));
				}
			}
			NormalizeInsertedOrderedListMetadata(&trailingBlocks);
			if (!candidate.insertPreparedBlocksAtExplicitPosition(
					std::move(insertedBlocks),
					container,
					&insertAt)) {
				return CheckedMutationResult<StructuralSelectionDropResult>{
					.apply = false,
					.result = result,
				};
			}
			auto trailingInsertAt = insertAt + 1;
			if (!trailingBlocks.empty()
				&& !candidate.insertPreparedBlocksAtExplicitPosition(
					std::move(trailingBlocks),
					container,
					&trailingInsertAt)) {
				return CheckedMutationResult<StructuralSelectionDropResult>{
					.apply = false,
					.result = result,
				};
			}
			candidate.rebuild();
			result.result = ApplyResult::Changed;
			result.destination = candidate.destinationTargetForInsertedBlocks(
				container,
				insertAt,
				1);
			return CheckedMutationResult<StructuralSelectionDropResult>{
				.apply = true,
				.result = result,
			};
		}
		if (!blockTarget
			|| !candidate.insertPreparedBlocksAtExplicitPosition(
				std::move(insertedBlocks),
				blockTarget->container,
				&blockTarget->insertIndex)) {
			return CheckedMutationResult<StructuralSelectionDropResult>{
				.apply = false,
				.result = result,
			};
		}
		candidate.rebuild();
		result.result = ApplyResult::Changed;
		result.destination = candidate.destinationTargetForInsertedBlocks(
			blockTarget->container,
			blockTarget->insertIndex,
			1);
		return CheckedMutationResult<StructuralSelectionDropResult>{
			.apply = true,
			.result = result,
		};
	});
}

State::TextSelectionDropResult State::moveTextSelectionToDropTarget(
		const std::vector<TextNodeSpan> &source,
		const Markdown::PreparedEditDropTarget &target) {
	const auto failure = TextSelectionDropResult{
		.result = ApplyResult::Failed,
	};
	if (source.empty()) {
		return failure;
	}
	return applyCheckedMutation(failure, [source, target](State &candidate) {
		struct SourceRewrite {
			LeafPath leaf;
			TextWithEntities text;
		};

		auto result = TextSelectionDropResult{
			.result = ApplyResult::Failed,
		};
		auto moved = TextWithEntities();
		auto sourceRewrites = std::vector<SourceRewrite>();
		sourceRewrites.reserve(source.size());
		for (const auto &span : source) {
			const auto current = candidate.richText(span.leaf);
			if (!current) {
				return CheckedMutationResult<TextSelectionDropResult>{
					.apply = false,
					.result = result,
				};
			}
			auto sourceBefore = TextWithEntities();
			auto selected = TextWithEntities();
			auto sourceAfter = TextWithEntities();
			if (!SplitTextSpan(
					current->text,
					span.from,
					span.till,
					&sourceBefore,
					&selected,
					&sourceAfter)) {
				return CheckedMutationResult<TextSelectionDropResult>{
					.apply = false,
					.result = result,
				};
			}
			moved.append(std::move(selected));
			sourceRewrites.push_back(SourceRewrite{
				.leaf = span.leaf,
				.text = JoinText(
					std::move(sourceBefore),
					TextWithEntities(),
					std::move(sourceAfter)),
			});
		}
		const auto movedLength = int(moved.text.size());
		const auto applySourceRewrites = [&](std::vector<SourceRewrite> rewrites) {
			for (auto &rewrite : rewrites) {
				const auto current = candidate.richText(rewrite.leaf);
				if (!current) {
					return false;
				}
				current->text = std::move(rewrite.text);
			}
			return true;
		};
		const auto finishAtLeaf = [&](
				const LeafPath &leaf,
				int selectionFrom,
				int selectionTo) {
			candidate.rebuild();
			const auto ordinal = candidate.textOrdinalForLeafPath(leaf);
			if (ordinal >= 0) {
				(void)candidate.setActiveTextByOrdinal(ordinal);
			} else {
				candidate.ensureActiveTextOrdinal();
			}
			result.result = ApplyResult::Changed;
			result.destinationLeaf = leaf;
			result.selectionFrom = selectionFrom;
			result.selectionTo = selectionTo;
			return CheckedMutationResult<TextSelectionDropResult>{
				.apply = true,
				.result = result,
			};
		};
		if (const auto text = std::get_if<Markdown::PreparedEditTextDropTarget>(
				&target)) {
			const auto destinationLeaf = candidate.convertLeafPath(text->leaf);
			const auto destination = destinationLeaf
				? candidate.richText(*destinationLeaf)
				: nullptr;
			if (!destination
				|| (text->leaf.kind
					== Markdown::PreparedEditLeafKind::MathFormula)
				|| !RangeInsideText(destination->text.text, text->offset, 0)) {
				return CheckedMutationResult<TextSelectionDropResult>{
					.apply = false,
					.result = result,
				};
			}
			auto insertAt = text->offset;
			auto destinationRewrite = -1;
			for (auto i = 0, count = int(source.size()); i != count; ++i) {
				const auto &span = source[i];
				if (!(span.leaf == *destinationLeaf)) {
					continue;
				}
				if (text->offset >= span.from && text->offset <= span.till) {
					result.result = ApplyResult::Unchanged;
					return CheckedMutationResult<TextSelectionDropResult>{
						.apply = false,
						.result = result,
					};
				}
				if (span.from < insertAt) {
					insertAt -= std::min(span.till, insertAt) - span.from;
				}
				destinationRewrite = i;
			}
			const auto destinationText = (destinationRewrite >= 0)
				? sourceRewrites[destinationRewrite].text
				: destination->text;
			if (!RangeInsideText(destinationText.text, insertAt, 0)) {
				return CheckedMutationResult<TextSelectionDropResult>{
					.apply = false,
					.result = result,
				};
			}
			auto destinationBefore = Ui::Text::Mid(destinationText, 0, insertAt);
			auto destinationAfter = Ui::Text::Mid(destinationText, insertAt);
			auto updated = JoinText(
				std::move(destinationBefore),
				std::move(moved),
				std::move(destinationAfter));
			if (destinationRewrite >= 0) {
				sourceRewrites[destinationRewrite].text = std::move(updated);
			} else {
				destination->text = std::move(updated);
			}
			if (!applySourceRewrites(std::move(sourceRewrites))) {
				return CheckedMutationResult<TextSelectionDropResult>{
					.apply = false,
					.result = result,
				};
			}
			return finishAtLeaf(
				*destinationLeaf,
				insertAt,
				insertAt + movedLength);
		}
		const auto block = std::get_if<Markdown::PreparedEditBlockDropTarget>(
			&target);
		auto container = block
			? candidate.convertBlockContainerPath(block->container)
			: std::nullopt;
		const auto destination = container
			? candidate.blockContainer(*container)
			: nullptr;
		if (!block
			|| !destination
			|| block->insertIndex < 0) {
			return CheckedMutationResult<TextSelectionDropResult>{
				.apply = false,
				.result = result,
			};
		}
		const auto &sourceLeaf = source.front().leaf;
		const auto owner = (sourceLeaf.kind == LeafKind::BlockText)
			? candidate.block(sourceLeaf.block)
			: nullptr;
		const auto textOnly = owner && JoinableTextBlockKind(owner->kind);
		const auto removeSource = textOnly
			&& StringIsEmpty(sourceRewrites.front().text.text);
		auto insertIndex = block->insertIndex;
		if (removeSource) {
			const auto &sourcePath = sourceLeaf.block;
			if (*container == sourcePath.container) {
				if (insertIndex >= sourcePath.index
					&& insertIndex <= sourcePath.index + 1) {
					result.result = ApplyResult::Unchanged;
					return CheckedMutationResult<TextSelectionDropResult>{
						.apply = false,
						.result = result,
					};
				} else if (insertIndex > sourcePath.index) {
					--insertIndex;
				}
			}
			if (!ShiftBlockContainerPathAfterRemovedBlock(
					*container,
					sourcePath)) {
				result.result = ApplyResult::Unchanged;
				return CheckedMutationResult<TextSelectionDropResult>{
					.apply = false,
					.result = result,
				};
			}
		}
		auto inserted = MakeParagraphBlock();
		if (textOnly) {
			inserted.kind = owner->kind;
			inserted.headingLevel = owner->headingLevel;
		}
		inserted.text.text = std::move(moved);
		auto blocks = std::vector<Block>();
		blocks.push_back(std::move(inserted));
		if (!applySourceRewrites(std::move(sourceRewrites))) {
			return CheckedMutationResult<TextSelectionDropResult>{
				.apply = false,
				.result = result,
			};
		}
		if (removeSource) {
			const auto sourceBlocks = candidate.blockContainer(
				sourceLeaf.block.container);
			if (!sourceBlocks
				|| sourceLeaf.block.index < 0
				|| sourceLeaf.block.index >= int(sourceBlocks->size())) {
				return CheckedMutationResult<TextSelectionDropResult>{
					.apply = false,
					.result = result,
				};
			}
			sourceBlocks->erase(
				sourceBlocks->begin() + sourceLeaf.block.index);
		}
		if (!candidate.insertPreparedBlocksAtExplicitPosition(
				std::move(blocks),
				*container,
				&insertIndex)) {
			return CheckedMutationResult<TextSelectionDropResult>{
				.apply = false,
				.result = result,
			};
		}
		return finishAtLeaf(
			LeafPath{
				.kind = LeafKind::BlockText,
				.block = BlockPath{
					.container = *container,
					.index = insertIndex,
				},
			},
			0,
			movedLength);
	});
}

State::TextSelectionDropResult State::moveTextSelectionToDropTarget(
		const TextNodeSpan &source,
		const Markdown::PreparedEditDropTarget &target) {
	return moveTextSelectionToDropTarget(
		std::vector<TextNodeSpan>{ source },
		target);
}

void State::insertHeading1AfterActive() {
	(void)insertBlockAfterActive({
		.type = InsertBlockType::Heading,
		.headingLevel = 1,
	});
}

void State::insertBlockquoteAfterActive() {
	(void)insertBlockAfterActive({
		.type = InsertBlockType::Blockquote,
	});
}

bool State::insertBlocksAfterActiveWithContextUnchecked(
		std::vector<Block> &blocks,
		const ActiveTextInsertContext &context) {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor || descriptor->leaf.kind == LeafKind::TableCellText) {
		return false;
	}
	const auto target = resolveActiveTextInsertTarget();
	if (!target) {
		return false;
	}
	auto container = target->anchor.container;
	auto insertAt = target->anchor.blockIndex + 1;
	auto removeSource = false;
	if (target->leaf.kind == LeafKind::BlockText) {
		if (const auto owner = block(target->leaf.block);
			owner
			&& context.before.text.isEmpty()
			&& ((owner->kind == BlockKind::Paragraph)
				|| (owner->kind == BlockKind::Heading)
				|| (owner->kind == BlockKind::Footer))) {
			removeSource = true;
			container = target->leaf.block.container;
			insertAt = target->leaf.block.index;
		}
	}
	auto *destination = blockContainer(container);
	if (!destination) {
		return false;
	}
	if (removeSource) {
		if (insertAt < 0 || insertAt >= int(destination->size())) {
			return false;
		}
	} else {
		const auto current = richText(target->leaf);
		if (!current) {
			return false;
		}
		current->text = context.before;
	}
	(void)seedInsertedBlocks(blocks, context.selected);
	if (removeSource) {
		destination->erase(destination->begin() + insertAt);
	}
	const auto count = int(blocks.size());
	if (insertAt < 0 || insertAt > int(destination->size())) {
		return false;
	}
	destination->insert(
		destination->begin() + insertAt,
		std::make_move_iterator(blocks.begin()),
		std::make_move_iterator(blocks.end()));
	if (!appendInsertedTrailingText(
			container,
			insertAt,
			count,
			context.after)) {
		return false;
	}
	rebuild();
	focusInsertedBlocks(container, insertAt, count);
	return true;
}

bool State::BlockConversionExpandsToActiveLine(InsertBlockType type) {
	switch (type) {
	case InsertBlockType::Heading:
	case InsertBlockType::Blockquote:
	case InsertBlockType::Pullquote:
	case InsertBlockType::Code:
	case InsertBlockType::Footer:
		return true;
	default:
		return false;
	}
}

State::ActiveTextBlockActionResult State::applyActiveTextBlockAction(
		InsertAction action,
		ActiveTextInsertContext context) {
	return applyCheckedMutation(ActiveTextBlockActionResult{
		.result = ApplyResult::Failed,
	}, [action, context = std::move(context)](State &candidate) mutable {
		ActiveTextSelectionTarget target;
		ActiveTextBlockActionResult result{
			.result = ApplyResult::Failed,
		};
		const auto changed = [&] {
			result = {
				.result = ApplyResult::Changed,
				.destinationLeaf = target.leaf,
				.selectionFrom = target.selectionFrom,
				.selectionTo = target.selectionTo,
			};
			return CheckedMutationResult<ActiveTextBlockActionResult>{
				.apply = true,
				.result = result,
			};
		};
		if (action.type == InsertBlockType::Blockquote
			|| action.type == InsertBlockType::Pullquote) {
			const auto pullquote = (action.type == InsertBlockType::Pullquote);
			if (candidate.activeQuote(pullquote)) {
				if (candidate.unwrapActiveQuoteUnchecked(
						pullquote,
						context,
						&target)) {
					return changed();
				}
				return CheckedMutationResult<ActiveTextBlockActionResult>{
					.result = result,
				};
			}
		}
		if (action.type == InsertBlockType::Code
			&& candidate.unwrapActiveCodeBlockUnchecked(context, &target)) {
			return changed();
		}
		if ((action.type == InsertBlockType::Heading
				|| action.type == InsertBlockType::Footer)
			&& candidate.convertActiveHeadingOrFooterUnchecked(
				action,
				context,
				&target)) {
			return changed();
		}
		const auto hadSelection = !context.selected.text.isEmpty();
		const auto beforeSize = int(context.before.text.size());
		const auto lineStart = hadSelection
			? -1
			: int(context.before.text.lastIndexOf('\n'));
		ExpandInsertContextToActiveLine(context);
		const auto cursorInLine = hadSelection
			? 0
			: (beforeSize - (lineStart + 1));
		auto blocks = std::vector<Block>();
		blocks.push_back(candidate.makeBlock(action));
		const auto applied = candidate.insertBlocksAfterActiveUnchecked(
			std::move(blocks),
			context);
		if (!applied) {
			return CheckedMutationResult<ActiveTextBlockActionResult>{
				.result = result,
			};
		}
		const auto descriptor = candidate.textNode(candidate._activeTextOrdinal);
		if (!descriptor) {
			return CheckedMutationResult<ActiveTextBlockActionResult>{
				.result = result,
			};
		}
		return CheckedMutationResult<ActiveTextBlockActionResult>{
			.apply = true,
			.result = {
				.result = ApplyResult::Changed,
				.destinationLeaf = descriptor->leaf,
				.selectionFrom = (hadSelection ? 0 : cursorInLine),
				.selectionTo = (hadSelection
					? int(context.selected.text.size())
					: cursorInLine),
			},
		};
	});
}

State::ActiveTextBlockActionResult State::replaceActiveTextSelectionWithText(
		TextWithEntities text,
		ActiveTextInsertContext context) {
	return applyCheckedMutation(ActiveTextBlockActionResult{
		.result = ApplyResult::Failed,
	}, [text = std::move(text), context = std::move(context)](
			State &candidate) mutable {
		const auto failed = [&] {
			return CheckedMutationResult<ActiveTextBlockActionResult>{
				.result = ActiveTextBlockActionResult{
					.result = ApplyResult::Failed,
				},
			};
		};
		if (text.text.isEmpty()) {
			return failed();
		}
		const auto descriptor = candidate.textNode(
			candidate._activeTextOrdinal);
		if (!descriptor || (descriptor->leaf.kind == LeafKind::MathFormula)) {
			return failed();
		}
		const auto selectionFrom = int(context.before.text.size());
		const auto selectionTo = selectionFrom + int(text.text.size());
		const auto applied = candidate.applyActiveTextUnchecked(JoinText(
			std::move(context.before),
			std::move(text),
			std::move(context.after)));
		if (applied == ApplyResult::Failed) {
			return failed();
		}
		const auto updated = candidate.textNode(candidate._activeTextOrdinal);
		return CheckedMutationResult<ActiveTextBlockActionResult>{
			.apply = true,
			.result = {
				.result = ApplyResult::Changed,
				.destinationLeaf = (updated
					? updated->leaf
					: descriptor->leaf),
				.selectionFrom = selectionFrom,
				.selectionTo = selectionTo,
			},
		};
	});
}

bool State::insertBlockAfterActive(
		InsertAction action,
		std::optional<ActiveTextInsertContext> context) {
	return applyCheckedMutation(false, [action, context = std::move(context)](
			State &candidate) mutable {
		if (context && BlockConversionExpandsToActiveLine(action.type)) {
			ExpandInsertContextToActiveLine(*context);
		}
		auto blocks = std::vector<Block>();
		blocks.push_back(candidate.makeBlock(action));
		if (action.type == InsertBlockType::Divider) {
			// A divider has no editable content, so insert a paragraph
			// together with it: the selected text piece (if any) seeds into
			// the paragraph and focus lands there, keeping an editable spot
			// below the divider while the block above stays editable too.
			blocks.push_back(MakeParagraphBlock());
			if (context) {
				// Treat it as a paragraph split with a divider in between:
				// everything from the cursor on moves into the paragraph
				// below the divider instead of a separate trailing one.
				context->selected = JoinText(
					std::move(context->selected),
					std::move(context->after),
					{});
				context->after = {};
			}
		}
		const auto applied = candidate.insertBlocksAfterActiveUnchecked(
			std::move(blocks),
			std::move(context));
		return CheckedMutationResult<bool>{
			.apply = applied,
			.result = applied,
		};
	});
}

bool State::insertPreparedBlockAfterActive(Block block) {
	auto blocks = std::vector<Block>();
	blocks.push_back(std::move(block));
	return insertPreparedBlocksAfterActive(std::move(blocks));
}

bool State::insertPreparedBlocksAfterActive(
		std::vector<Block> blocks,
		std::optional<ActiveTextInsertContext> context) {
	return applyCheckedMutation(false, [
			blocks = std::move(blocks),
			context = std::move(context)](State &candidate) mutable {
		const auto applied = candidate.insertBlocksAfterActiveUnchecked(
			std::move(blocks),
			std::move(context));
		return CheckedMutationResult<bool>{
			.apply = applied,
			.result = applied,
		};
	});
}

State::DisplayMathEditResult State::editActiveDisplayMath(
		QString source,
		bool separateLine) {
	auto failure = DisplayMathEditResult{
		.result = ApplyResult::Failed,
	};
	return applyCheckedMutation(failure, [
			source = std::move(source),
			separateLine](State &candidate) mutable {
		const auto descriptor = candidate.textNode(candidate._activeTextOrdinal);
		if (!descriptor || descriptor->leaf.kind != LeafKind::MathFormula) {
			return CheckedMutationResult<DisplayMathEditResult>{
				.result = {
					.result = ApplyResult::Failed,
				},
			};
		}
		const auto leaf = descriptor->leaf;
		auto *blocks = candidate.blockContainer(leaf.block.container);
		if (!blocks
			|| leaf.block.index < 0
			|| leaf.block.index >= int(blocks->size())) {
			return CheckedMutationResult<DisplayMathEditResult>{
				.result = {
					.result = ApplyResult::Failed,
				},
			};
		}
		auto &math = (*blocks)[leaf.block.index];
		if (math.kind != BlockKind::Math) {
			return CheckedMutationResult<DisplayMathEditResult>{
				.result = {
					.result = ApplyResult::Failed,
				},
			};
		}
		if (separateLine) {
			if (math.formula == source) {
				return CheckedMutationResult<DisplayMathEditResult>{
					.result = {
						.result = ApplyResult::Unchanged,
					},
				};
			}
			math.formula = std::move(source);
			candidate.rebuild();
			if (!candidate.activateRebuiltLeaf(leaf)) {
				return CheckedMutationResult<DisplayMathEditResult>{
					.result = {
						.result = ApplyResult::Failed,
					},
				};
			}
			return CheckedMutationResult<DisplayMathEditResult>{
				.apply = true,
				.result = {
					.result = ApplyResult::Changed,
				},
			};
		}
		auto inlineMath = FormulaSourceToRichText(std::move(source));
		const auto mathIndex = leaf.block.index;
		const auto previousIndex = mathIndex - 1;
		const auto nextIndex = mathIndex + 1;
		const auto paragraphAt = [&](int index) -> Block* {
			return (index >= 0
				&& index < int(blocks->size())
				&& (*blocks)[index].kind == BlockKind::Paragraph)
				? &(*blocks)[index]
				: nullptr;
		};
		auto *previous = paragraphAt(previousIndex);
		auto *next = paragraphAt(nextIndex);
		const auto previousHasText = previous
			&& RichTextHasVisibleText(previous->text);
		const auto nextHasText = next
			&& RichTextHasVisibleText(next->text);
		enum class Target {
			Previous,
			Next,
			Replace,
		};
		auto target = Target::Replace;
		auto removePrevious = false;
		auto removeNext = false;
		if (previousHasText) {
			target = Target::Previous;
			removeNext = (next != nullptr);
		} else if (nextHasText) {
			target = Target::Next;
			removePrevious = (previous != nullptr);
		} else if (previous) {
			target = Target::Previous;
			removeNext = (next != nullptr);
		} else if (next) {
			target = Target::Next;
		}
		auto inlineLeaf = LeafPath{
			.kind = LeafKind::BlockText,
			.block = {
				.container = leaf.block.container,
				.index = mathIndex,
			},
		};
		auto selectionFrom = 0;
		auto selectionTo = 0;
		switch (target) {
		case Target::Previous: {
			auto updated = std::move(previous->text.text);
			if (previousHasText) {
				updated.append(' ');
			}
			selectionFrom = int(updated.text.size());
			updated.append(std::move(inlineMath));
			selectionTo = int(updated.text.size());
			if (next) {
				if (nextHasText) {
					updated.append(' ');
				}
				updated.append(next->text.text);
			}
			previous->text.text = std::move(updated);
			if (next) {
				MergeRichTextAnchors(&previous->text, std::move(next->text));
			}
			if (removeNext) {
				blocks->erase(blocks->begin() + nextIndex);
			}
			blocks->erase(blocks->begin() + mathIndex);
			inlineLeaf.block.index = previousIndex;
		} break;
		case Target::Next: {
			auto updated = TextWithEntities();
			auto nextText = std::move(next->text.text);
			updated.append(std::move(inlineMath));
			selectionFrom = 0;
			selectionTo = updated.text.size();
			if (nextHasText) {
				updated.append(' ');
			}
			updated.append(std::move(nextText));
			next->text.text = std::move(updated);
			if (previous && removePrevious) {
				MergeRichTextAnchors(&next->text, std::move(previous->text));
			}
			blocks->erase(blocks->begin() + mathIndex);
			inlineLeaf.block.index = mathIndex;
			if (removePrevious) {
				blocks->erase(blocks->begin() + previousIndex);
				inlineLeaf.block.index = previousIndex;
			}
		} break;
		case Target::Replace: {
			auto paragraph = MakeParagraphBlock();
			paragraph.text.text = std::move(inlineMath);
			selectionTo = paragraph.text.text.text.size();
			(*blocks)[mathIndex] = std::move(paragraph);
		} break;
		}
		candidate.rebuild();
		if (!candidate.activateRebuiltLeaf(inlineLeaf)) {
			return CheckedMutationResult<DisplayMathEditResult>{
				.result = {
					.result = ApplyResult::Failed,
				},
			};
		}
		return CheckedMutationResult<DisplayMathEditResult>{
			.apply = true,
			.result = {
				.result = ApplyResult::Changed,
				.inlineLeaf = inlineLeaf,
				.selectionFrom = selectionFrom,
				.selectionTo = selectionTo,
			},
		};
	});
}

bool State::insertBlocksAfterActiveUnchecked(
		std::vector<Block> blocks,
		std::optional<ActiveTextInsertContext> context) {
	if (blocks.empty()) {
		return false;
	}
	clearTemporaryDownParagraph();
	NormalizeInsertedOrderedListMetadata(&blocks);
	normalizeInsertedBlockAnchors(blocks);
	if (context) {
		if (insertBlocksAfterActiveWithContextUnchecked(blocks, *context)) {
			return true;
		}
		if (applyActiveTextUnchecked(JoinText(
				context->before,
				context->selected,
				context->after)) == ApplyResult::Failed) {
			return false;
		}
	}
	const auto descriptor = textNode(_activeTextOrdinal);
	if (descriptor && shouldReplaceActiveTextOnlyBlock(*descriptor, blocks)) {
		const auto path = descriptor->removalTarget.block;
		const auto container = blockContainer(path.container);
		if (container
			&& path.index >= 0
			&& path.index < int(container->size())) {
			const auto insertAt = path.index;
			const auto count = int(blocks.size());
			container->erase(container->begin() + insertAt);
			container->insert(
				container->begin() + insertAt,
				std::make_move_iterator(blocks.begin()),
				std::make_move_iterator(blocks.end()));
			rebuild();
			focusInsertedBlocks(path.container, insertAt, count);
			return true;
		}
	}
	auto anchor = resolveActiveInsertionTarget();
	if (const auto normalized = normalizeTextOnlyListItemForInsertion(
			anchor.container)) {
		anchor.blockIndex = *normalized;
	} else if (const auto normalized = normalizeTextOnlyQuoteForInsertion(
			anchor.container)) {
		anchor.blockIndex = *normalized;
	}
	auto *container = blockContainer(anchor.container);
	if (!container) {
		anchor = InsertionAnchor{
			.container = BlockContainerPath(),
			.blockIndex = int(_richPage->blocks.size()) - 1,
		};
		container = &_richPage->blocks;
	}
	const auto insertAt = std::clamp(
		anchor.blockIndex + 1,
		0,
		int(container->size()));
	const auto count = int(blocks.size());
	container->insert(
		container->begin() + insertAt,
		std::make_move_iterator(blocks.begin()),
		std::make_move_iterator(blocks.end()));
	rebuild();
	focusInsertedBlocks(anchor.container, insertAt, count);
	return true;
}

bool State::insertPreparedBlocksAtExplicitPosition(
		std::vector<Block> blocks,
		const BlockContainerPath &container,
		int *insertAt) {
	if (!normalizeTextOnlyContainerForInsertion(container, insertAt)) {
		return false;
	}
	auto *destination = blockContainer(container);
	if (!destination || *insertAt > int(destination->size())) {
		return false;
	}
	NormalizeInsertedOrderedListMetadata(&blocks);
	destination->insert(
		destination->begin() + *insertAt,
		std::make_move_iterator(blocks.begin()),
		std::make_move_iterator(blocks.end()));
	return true;
}

bool State::insertPreparedBlocksAtRemovedBlockRange(
		std::vector<Block> blocks,
		const StructuralBlockRange &range) {
	if (blocks.empty()) {
		return false;
	}
	return applyCheckedMutation(false, [
			blocks = std::move(blocks),
			range](State &candidate) mutable {
		candidate.normalizeInsertedBlockAnchors(blocks);
		auto insertAt = range.from;
		const auto count = int(blocks.size());
		if (!candidate.insertPreparedBlocksAtExplicitPosition(
				std::move(blocks),
				range.container,
				&insertAt)) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		candidate.rebuild();
		candidate.focusInsertedBlocks(range.container, insertAt, count);
		return CheckedMutationResult<bool>{ .apply = true, .result = true };
	});
}

bool State::insertPreparedBlocksAtDropTarget(
		std::vector<Block> blocks,
		const Markdown::PreparedEditBlockDropTarget &target) {
	if (blocks.empty() || target.insertIndex < 0) {
		return false;
	}
	return applyCheckedMutation(false, [
			blocks = std::move(blocks),
			target](State &candidate) mutable {
		const auto container = candidate.convertBlockContainerPath(
			target.container);
		if (!container || !candidate.blockContainer(*container)) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		candidate.normalizeInsertedBlockAnchors(blocks);
		auto insertAt = target.insertIndex;
		if (!candidate.insertPreparedBlocksAtExplicitPosition(
				std::move(blocks),
				*container,
				&insertAt)) {
			return CheckedMutationResult<bool>{ .result = false };
		}
		candidate.rebuild();
		return CheckedMutationResult<bool>{ .apply = true, .result = true };
	});
}

bool State::insertPreparedListItemsAtExplicitPosition(
		std::vector<ListItem> items,
		const BlockPath &path,
		int insertAt) {
	auto *owner = block(path);
	if (!owner
		|| owner->kind != BlockKind::List
		|| insertAt < 0
		|| insertAt > int(owner->listItems.size())) {
		return false;
	}
	owner->listItems.insert(
		owner->listItems.begin() + insertAt,
		std::make_move_iterator(items.begin()),
		std::make_move_iterator(items.end()));
	return true;
}

std::vector<Block> *State::blockContainer(const BlockContainerPath &path) {
	auto *blocks = &_richPage->blocks;
	for (const auto &step : path.steps) {
		if (step.blockIndex < 0 || step.blockIndex >= int(blocks->size())) {
			return nullptr;
		}
		auto &parent = (*blocks)[step.blockIndex];
		if (step.kind == BlockContainerKind::BlockChildren) {
			blocks = &parent.blocks;
		} else if (step.kind == BlockContainerKind::ListItemChildren) {
			if (step.listItemIndex < 0
				|| step.listItemIndex >= int(parent.listItems.size())) {
				return nullptr;
			}
			blocks = &parent.listItems[step.listItemIndex].blocks;
		} else {
			return nullptr;
		}
	}
	return blocks;
}

const std::vector<Block> *State::blockContainer(
		const BlockContainerPath &path) const {
	const auto *blocks = &_richPage->blocks;
	for (const auto &step : path.steps) {
		if (step.blockIndex < 0 || step.blockIndex >= int(blocks->size())) {
			return nullptr;
		}
		const auto &parent = (*blocks)[step.blockIndex];
		if (step.kind == BlockContainerKind::BlockChildren) {
			blocks = &parent.blocks;
		} else if (step.kind == BlockContainerKind::ListItemChildren) {
			if (step.listItemIndex < 0
				|| step.listItemIndex >= int(parent.listItems.size())) {
				return nullptr;
			}
			blocks = &parent.listItems[step.listItemIndex].blocks;
		} else {
			return nullptr;
		}
	}
	return blocks;
}

Block *State::block(const BlockPath &path) {
	const auto blocks = blockContainer(path.container);
	if (!blocks || path.index < 0 || path.index >= int(blocks->size())) {
		return nullptr;
	}
	return &(*blocks)[path.index];
}

const Block *State::block(const BlockPath &path) const {
	const auto blocks = blockContainer(path.container);
	if (!blocks || path.index < 0 || path.index >= int(blocks->size())) {
		return nullptr;
	}
	return &(*blocks)[path.index];
}

ListItem *State::listItem(const BlockPath &blockPath, int itemIndex) {
	const auto list = block(blockPath);
	if (!list
		|| itemIndex < 0
		|| itemIndex >= int(list->listItems.size())) {
		return nullptr;
	}
	return &list->listItems[itemIndex];
}

const ListItem *State::listItem(
		const BlockPath &blockPath,
		int itemIndex) const {
	const auto list = block(blockPath);
	if (!list
		|| itemIndex < 0
		|| itemIndex >= int(list->listItems.size())) {
		return nullptr;
	}
	return &list->listItems[itemIndex];
}

TableCell *State::tableCell(
		const BlockPath &blockPath,
		int rowIndex,
		int cellIndex) {
	const auto table = block(blockPath);
	if (!table
		|| rowIndex < 0
		|| rowIndex >= int(table->tableRows.size())) {
		return nullptr;
	}
	auto &row = table->tableRows[rowIndex];
	if (cellIndex < 0 || cellIndex >= int(row.cells.size())) {
		return nullptr;
	}
	return &row.cells[cellIndex];
}

const TableCell *State::tableCell(
		const BlockPath &blockPath,
		int rowIndex,
		int cellIndex) const {
	const auto table = block(blockPath);
	if (!table
		|| rowIndex < 0
		|| rowIndex >= int(table->tableRows.size())) {
		return nullptr;
	}
	const auto &row = table->tableRows[rowIndex];
	if (cellIndex < 0 || cellIndex >= int(row.cells.size())) {
		return nullptr;
	}
	return &row.cells[cellIndex];
}

RichText *State::richText(const LeafPath &path) {
	switch (path.kind) {
	case LeafKind::BlockText:
		if (const auto owner = block(path.block)) {
			return &owner->text;
		}
		return nullptr;
	case LeafKind::BlockCaption:
		if (const auto owner = block(path.block)) {
			return &owner->caption;
		}
		return nullptr;
	case LeafKind::ListItemText:
		if (const auto item = listItem(path.block, path.listItemIndex)) {
			return &item->text;
		}
		return nullptr;
	case LeafKind::TableCellText:
		if (const auto cell = tableCell(
				path.block,
				path.tableRowIndex,
				path.tableCellIndex)) {
			return &cell->text;
		}
		return nullptr;
	case LeafKind::MathFormula:
		return nullptr;
	}
	return nullptr;
}

const RichText *State::richText(const LeafPath &path) const {
	switch (path.kind) {
	case LeafKind::BlockText:
		if (const auto owner = block(path.block)) {
			return &owner->text;
		}
		return nullptr;
	case LeafKind::BlockCaption:
		if (const auto owner = block(path.block)) {
			return &owner->caption;
		}
		return nullptr;
	case LeafKind::ListItemText:
		if (const auto item = listItem(path.block, path.listItemIndex)) {
			return &item->text;
		}
		return nullptr;
	case LeafKind::TableCellText:
		if (const auto cell = tableCell(
				path.block,
				path.tableRowIndex,
				path.tableCellIndex)) {
			return &cell->text;
		}
		return nullptr;
	case LeafKind::MathFormula:
		return nullptr;
	}
	return nullptr;
}

QString *State::rawText(const LeafPath &path) {
	if (path.kind != LeafKind::MathFormula) {
		return nullptr;
	}
	const auto owner = block(path.block);
	return owner ? &owner->formula : nullptr;
}

const QString *State::rawText(const LeafPath &path) const {
	if (path.kind != LeafKind::MathFormula) {
		return nullptr;
	}
	const auto owner = block(path.block);
	return owner ? &owner->formula : nullptr;
}

const TextNodeDescriptor *State::textNode(int ordinal) const {
	return (ordinal >= 0 && ordinal < textNodeCount())
		? &_textNodes[ordinal]
		: nullptr;
}

int State::textNodeOrdinal(const LeafPath &path) const {
	for (auto i = 0, count = textNodeCount(); i != count; ++i) {
		if (_textNodes[i].leaf == path) {
			return i;
		}
	}
	return -1;
}

bool State::leafMutationKeepsTextNodes(
		const TextNodeDescriptor &descriptor) const {
	switch (descriptor.leaf.kind) {
	case LeafKind::BlockText:
		if (const auto owner = block(descriptor.leaf.block)) {
			switch (owner->kind) {
			case BlockKind::Heading:
			case BlockKind::Paragraph:
			case BlockKind::Footer:
			case BlockKind::Code:
			case BlockKind::Details:
				return true;
			case BlockKind::Quote:
				return owner->blocks.empty();
			case BlockKind::Table:
				return !owner->text.text.text.isEmpty();
			default:
				return false;
			}
		}
		return false;
	case LeafKind::BlockCaption:
	case LeafKind::TableCellText:
	case LeafKind::MathFormula:
		return true;
	case LeafKind::ListItemText:
		if (const auto item = listItem(
				descriptor.leaf.block,
				descriptor.leaf.listItemIndex)) {
			return !RichTextIsEmpty(item->text) || item->blocks.empty();
		}
		return false;
	}
	return false;
}

bool State::updatePreparedActiveLeaf(
		const TextNodeDescriptor &descriptor) {
	if (DetermineRichPageRtl(*_richPage) != _richPage->rtl) {
		return false;
	}
	const auto source = convertPreparedLeafSource(descriptor);
	if (!source) {
		return false;
	}
	return (Markdown::UpdatePreparedNativeInstantViewLeaf(
		&_prepared,
		*_richPage,
		*source,
		tableRenderLimits()) == NativeInstantViewLeafUpdateResult::Updated);
}

void State::rebuild() {
	_lastPreparedMutationKind = PreparedMutationKind::FullRebuild;
	rebuildTextNodes();
	ensureEditableNodes();
	ensureActiveTextOrdinal();
	clearTemporaryDownParagraphIfInvalid();
	rebuildPrepared();
}

void State::rebuildPrepared() {
	_lastPreparedMutationKind = PreparedMutationKind::FullRebuild;
	_richPage->rtl = DetermineRichPageRtl(*_richPage);
	_prepared = Markdown::TryPrepareNativeInstantView({
		.richPage = _richPage,
		.mediaRuntime = _mediaRuntime,
		.tableRenderLimits = tableRenderLimits(),
		.editMode = true,
	}).content;
}

void State::rebuildTextNodes() {
	_textNodes.clear();
	_textNodes.reserve(_richPage->blocks.size() * 3);
	rebuildTextNodes(_richPage->blocks, BlockContainerPath());
}

void State::rebuildTextNodes(
		const std::vector<Block> &blocks,
		const BlockContainerPath &container) {
	for (auto i = 0, count = int(blocks.size()); i != count; ++i) {
		const auto path = BlockPath{
			.container = container,
			.index = i,
		};
		const auto &block = blocks[i];
		switch (block.kind) {
		case BlockKind::Heading:
		case BlockKind::Paragraph:
		case BlockKind::Footer:
		case BlockKind::Code:
			appendBlockTextNode(path, LeafKind::BlockText);
			break;
		case BlockKind::Quote:
			if (block.blocks.empty()) {
				appendBlockTextNode(
					path,
					LeafKind::BlockText,
					FieldMode::Rich,
					!block.pullquote
						? std::make_optional(InsertionAnchor{
							.container = BlockChildrenContainer(path),
							.blockIndex = -1,
						})
						: std::nullopt);
			}
			rebuildTextNodes(block.blocks, BlockChildrenContainer(path));
			appendBlockTextNode(path, LeafKind::BlockCaption);
			break;
		case BlockKind::List:
			for (auto j = 0, itemCount = int(block.listItems.size());
					j != itemCount;
					++j) {
				const auto &item = block.listItems[j];
				if (!RichTextIsEmpty(item.text) || item.blocks.empty()) {
					appendListItemTextNode(path, j);
				}
				if (!item.blocks.empty()) {
					rebuildTextNodes(
						item.blocks,
						ListItemChildrenContainer(path, j));
				}
			}
			break;
		case BlockKind::Photo:
		case BlockKind::Video:
		case BlockKind::Audio:
		case BlockKind::Map:
		case BlockKind::GroupedMedia:
			appendBlockTextNode(path, LeafKind::BlockCaption);
			break;
		case BlockKind::Math:
			appendBlockTextNode(path, LeafKind::MathFormula, FieldMode::Raw);
			break;
		case BlockKind::Table:
			appendBlockTextNode(path, LeafKind::BlockText);
			for (auto j = 0, rowCount = int(block.tableRows.size());
					j != rowCount;
					++j) {
				const auto &row = block.tableRows[j];
				for (auto k = 0, cellCount = int(row.cells.size());
						k != cellCount;
						++k) {
					appendTableCellTextNode(path, j, k);
				}
			}
			break;
		case BlockKind::Details:
			appendBlockTextNode(
				path,
				LeafKind::BlockText,
				FieldMode::Rich,
				InsertionAnchor{
					.container = BlockChildrenContainer(path),
					.blockIndex = -1,
				});
			rebuildTextNodes(block.blocks, BlockChildrenContainer(path));
			break;
		default:
			rebuildTextNodes(block.blocks, BlockChildrenContainer(path));
			break;
		}
	}
}

void State::appendBlockTextNode(
		const BlockPath &path,
		LeafKind kind,
		FieldMode mode,
		std::optional<InsertionAnchor> insertionAnchor) {
	_textNodes.push_back({
		.leaf = {
			.kind = kind,
			.block = path,
		},
		.insertionAnchor = insertionAnchor.value_or(InsertionAnchor{
			.container = path.container,
			.blockIndex = path.index,
		}),
		.removalTarget = {
			.kind = RemovalKind::Block,
			.block = path,
		},
		.mode = mode,
	});
}

void State::appendListItemTextNode(const BlockPath &path, int itemIndex) {
	_textNodes.push_back({
		.leaf = {
			.kind = LeafKind::ListItemText,
			.block = path,
			.listItemIndex = itemIndex,
		},
		.insertionAnchor = {
			.container = ListItemChildrenContainer(path, itemIndex),
			.blockIndex = -1,
		},
		.removalTarget = {
			.kind = RemovalKind::ListItem,
			.block = path,
			.listItemIndex = itemIndex,
		},
		.mode = FieldMode::Rich,
	});
}

void State::appendTableCellTextNode(
		const BlockPath &path,
		int rowIndex,
		int cellIndex) {
	_textNodes.push_back({
		.leaf = {
			.kind = LeafKind::TableCellText,
			.block = path,
			.tableRowIndex = rowIndex,
			.tableCellIndex = cellIndex,
		},
		.insertionAnchor = {
			.container = path.container,
			.blockIndex = path.index,
		},
		.removalTarget = {
			.kind = RemovalKind::TableCell,
			.block = path,
			.tableRowIndex = rowIndex,
			.tableCellIndex = cellIndex,
		},
		.mode = FieldMode::Rich,
	});
}

void State::ensureActiveTextOrdinal() {
	if (_textNodes.empty()) {
		_activeTextOrdinal = -1;
	} else if (_activeTextOrdinal < 0 || _activeTextOrdinal >= textNodeCount()) {
		_activeTextOrdinal = 0;
	}
}

void State::ensureEditableNodes() {
	if (!_textNodes.empty()) {
		return;
	}
	_richPage->blocks.push_back(MakeParagraphBlock());
	rebuildTextNodes();
}

void State::focusInsertedBlocks(
		const BlockContainerPath &container,
		int from,
		int count) {
	for (auto blockIndex = from; blockIndex != from + count; ++blockIndex) {
		const auto path = BlockPath{
			.container = container,
			.index = blockIndex,
		};
		for (auto i = 0, textCount = textNodeCount(); i != textCount; ++i) {
			if (descriptorBelongsToBlock(_textNodes[i], path)
				&& setActiveTextByOrdinal(i)) {
				return;
			}
		}
	}
	ensureActiveTextOrdinal();
}

State::BoundaryTarget State::destinationTargetForInsertedBlocks(
		const BlockContainerPath &container,
		int from,
		int count) {
	focusInsertedBlocks(container, from, count);
	if (const auto descriptor = textNode(_activeTextOrdinal)) {
		for (auto blockIndex = from; blockIndex != from + count; ++blockIndex) {
			const auto path = BlockPath{
				.container = container,
				.index = blockIndex,
			};
			if (descriptorBelongsToBlock(*descriptor, path)) {
				return {
					.action = BoundaryTarget::Action::Text,
					.textOrdinal = _activeTextOrdinal,
				};
			}
		}
	}
	for (auto blockIndex = from; blockIndex != from + count; ++blockIndex) {
		const auto path = BlockPath{
			.container = container,
			.index = blockIndex,
		};
		if (const auto owner = block(path); owner && CanEditBlock(*owner)) {
			return {
				.action = BoundaryTarget::Action::StructuralSelection,
				.structuralSelection = preparedSelectionForBlock(path),
			};
		}
	}
	return {};
}

State::BoundaryTarget State::destinationTargetForInsertedListItems(
		const BlockPath &path,
		int from,
		int count) {
	for (auto i = 0, textCount = textNodeCount(); i != textCount; ++i) {
		const auto itemIndex = ListItemIndexForLeaf(_textNodes[i].leaf, path);
		if (!itemIndex
			|| *itemIndex < from
			|| *itemIndex >= from + count
			|| !setActiveTextByOrdinal(i)) {
			continue;
		}
		return {
			.action = BoundaryTarget::Action::Text,
			.textOrdinal = _activeTextOrdinal,
		};
	}
	const auto owner = block(path);
	if (owner
		&& owner->kind == BlockKind::List
		&& from >= 0
		&& from < int(owner->listItems.size())
		&& CanEditBlocks(owner->listItems[from].blocks)) {
		ensureActiveTextOrdinal();
		return {
			.action = BoundaryTarget::Action::StructuralSelection,
			.structuralSelection = preparedSelectionForListItem(path, from),
		};
	}
	ensureActiveTextOrdinal();
	return (_activeTextOrdinal >= 0)
		? BoundaryTarget{
			.action = BoundaryTarget::Action::Text,
			.textOrdinal = _activeTextOrdinal,
		}
		: BoundaryTarget();
}

std::optional<int> State::adjacentEditableOrdinal(bool forward) const {
	if (_activeTextOrdinal < 0) {
		return std::nullopt;
	}
	const auto ordinal = _activeTextOrdinal + (forward ? 1 : -1);
	return (ordinal >= 0 && ordinal < textNodeCount())
		? std::make_optional(ordinal)
		: std::nullopt;
}

std::optional<int> State::firstTableCellOrdinalFromActiveTitle() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return std::nullopt;
	}
	const auto leaf = descriptor->leaf;
	if (leaf.kind != LeafKind::BlockText) {
		return std::nullopt;
	}
	const auto owner = block(leaf.block);
	if (!owner || owner->kind != BlockKind::Table) {
		return std::nullopt;
	}
	for (auto i = 0, count = textNodeCount(); i != count; ++i) {
		const auto &candidate = _textNodes[i].leaf;
		if (candidate.block == leaf.block
			&& candidate.kind == LeafKind::TableCellText) {
			return i;
		}
	}
	return std::nullopt;
}

std::optional<int> State::adjacentRowTableCellOrdinal(bool down) const {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return std::nullopt;
	}
	const auto leaf = descriptor->leaf;
	if (leaf.kind != LeafKind::TableCellText) {
		return std::nullopt;
	}
	const auto owner = block(leaf.block);
	if (!owner || owner->kind != BlockKind::Table) {
		return std::nullopt;
	}
	const auto grid = BuildTableGrid(*owner, tableRenderLimits());
	const auto active = [&]() -> const TableGridCellReference* {
		for (const auto &candidate : grid.cells) {
			if (candidate.rowIndex == leaf.tableRowIndex
				&& candidate.cellIndex == leaf.tableCellIndex) {
				return &candidate;
			}
		}
		return nullptr;
	}();
	if (!active) {
		return std::nullopt;
	}
	const auto column = active->columnFrom;
	const auto step = down ? 1 : -1;
	for (auto targetRow = down ? active->rowTill : (active->rowFrom - 1);
			targetRow >= 0 && targetRow < grid.rowCount;
			targetRow += step) {
		auto best = (const TableGridCellReference*)nullptr;
		auto bestDistance = std::numeric_limits<int>::max();
		for (const auto &candidate : grid.cells) {
			if (candidate.rowFrom > targetRow
				|| candidate.rowTill <= targetRow) {
				continue;
			}
			const auto distance = (column < candidate.columnFrom)
				? (candidate.columnFrom - column)
				: (column >= candidate.columnTill)
				? (column - candidate.columnTill + 1)
				: 0;
			if (distance < bestDistance) {
				bestDistance = distance;
				best = &candidate;
			}
		}
		if (best) {
			const auto ordinal = textNodeOrdinal({
				.kind = LeafKind::TableCellText,
				.block = leaf.block,
				.tableRowIndex = best->rowIndex,
				.tableCellIndex = best->cellIndex,
			});
			return (ordinal >= 0)
				? std::make_optional(ordinal)
				: std::nullopt;
		}
	}
	return std::nullopt;
}

std::optional<int> State::tableTitleOrdinalFromActiveCell() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return std::nullopt;
	}
	const auto leaf = descriptor->leaf;
	if (leaf.kind != LeafKind::TableCellText) {
		return std::nullopt;
	}
	const auto owner = block(leaf.block);
	if (!owner || owner->kind != BlockKind::Table) {
		return std::nullopt;
	}
	const auto ordinal = textNodeOrdinal({
		.kind = LeafKind::BlockText,
		.block = leaf.block,
	});
	return (ordinal >= 0) ? std::make_optional(ordinal) : std::nullopt;
}

std::optional<int> State::ordinalAfterActiveTable() const {
	const auto descriptor = textNode(_activeTextOrdinal);
	if (!descriptor) {
		return std::nullopt;
	}
	const auto leaf = descriptor->leaf;
	if (leaf.kind != LeafKind::TableCellText) {
		return std::nullopt;
	}
	const auto owner = block(leaf.block);
	if (!owner || owner->kind != BlockKind::Table) {
		return std::nullopt;
	}
	for (auto i = _activeTextOrdinal + 1, count = textNodeCount();
			i != count;
			++i) {
		const auto &candidate = _textNodes[i].leaf;
		if (candidate.block != leaf.block
			|| candidate.kind != LeafKind::TableCellText) {
			return i;
		}
	}
	return std::nullopt;
}

void State::collectBoundarySteps(
		const std::vector<Block> &blocks,
		const BlockContainerPath &container,
		bool forward,
		std::vector<BoundaryTarget> *steps) const {
	const auto collectBlock = [&](int index) {
		const auto path = BlockPath{
			.container = container,
			.index = index,
		};
		const auto &block = blocks[index];
		switch (block.kind) {
		case BlockKind::Heading:
		case BlockKind::Paragraph:
		case BlockKind::Footer:
		case BlockKind::Code:
			appendBoundaryTextStep({
				.kind = LeafKind::BlockText,
				.block = path,
			}, steps);
			break;
		case BlockKind::Quote:
			if (forward) {
				if (block.blocks.empty()) {
					appendBoundaryTextStep({
						.kind = LeafKind::BlockText,
						.block = path,
					}, steps);
				}
				collectBoundarySteps(
					block.blocks,
					BlockChildrenContainer(path),
					forward,
					steps);
				appendBoundaryTextStep({
					.kind = LeafKind::BlockCaption,
					.block = path,
				}, steps);
			} else {
				appendBoundaryTextStep({
					.kind = LeafKind::BlockCaption,
					.block = path,
				}, steps);
				collectBoundarySteps(
					block.blocks,
					BlockChildrenContainer(path),
					forward,
					steps);
				if (block.blocks.empty()) {
					appendBoundaryTextStep({
						.kind = LeafKind::BlockText,
						.block = path,
					}, steps);
				}
			}
			appendBoundaryBlockStep(path, steps);
			break;
		case BlockKind::List:
			if (forward) {
				for (auto j = 0, itemCount = int(block.listItems.size());
						j != itemCount;
						++j) {
					const auto &item = block.listItems[j];
					if (!RichTextIsEmpty(item.text) || item.blocks.empty()) {
						appendBoundaryTextStep({
							.kind = LeafKind::ListItemText,
							.block = path,
							.listItemIndex = j,
						}, steps);
					}
					if (!item.blocks.empty()) {
						collectBoundarySteps(
							item.blocks,
							ListItemChildrenContainer(path, j),
							forward,
							steps);
						appendBoundaryListItemStep(path, j, steps);
					}
				}
			} else {
				for (auto j = int(block.listItems.size()); j != 0; --j) {
					const auto itemIndex = j - 1;
					const auto &item = block.listItems[itemIndex];
					if (!item.blocks.empty()) {
						collectBoundarySteps(
							item.blocks,
							ListItemChildrenContainer(path, itemIndex),
							forward,
							steps);
					}
					if (!RichTextIsEmpty(item.text) || item.blocks.empty()) {
						appendBoundaryTextStep({
							.kind = LeafKind::ListItemText,
							.block = path,
							.listItemIndex = itemIndex,
						}, steps);
					}
					if (!item.blocks.empty()) {
						appendBoundaryListItemStep(path, itemIndex, steps);
					}
				}
			}
			break;
		case BlockKind::Photo:
		case BlockKind::Video:
		case BlockKind::Audio:
		case BlockKind::Map:
		case BlockKind::GroupedMedia:
			appendBoundaryTextStep({
				.kind = LeafKind::BlockCaption,
				.block = path,
			}, steps);
			appendBoundaryBlockStep(path, steps);
			break;
		case BlockKind::Math:
			appendBoundaryTextStep({
				.kind = LeafKind::MathFormula,
				.block = path,
			}, steps);
			break;
		case BlockKind::Table:
			if (forward) {
				if (!block.text.text.text.isEmpty()) {
					appendBoundaryTextStep({
						.kind = LeafKind::BlockText,
						.block = path,
					}, steps);
				}
				for (auto j = 0, rowCount = int(block.tableRows.size());
						j != rowCount;
						++j) {
					const auto &row = block.tableRows[j];
					for (auto k = 0, cellCount = int(row.cells.size());
							k != cellCount;
							++k) {
						appendBoundaryTextStep({
							.kind = LeafKind::TableCellText,
							.block = path,
							.tableRowIndex = j,
							.tableCellIndex = k,
						}, steps);
					}
				}
			} else {
				for (auto j = int(block.tableRows.size()); j != 0; --j) {
					const auto rowIndex = j - 1;
					const auto &row = block.tableRows[rowIndex];
					for (auto k = int(row.cells.size()); k != 0; --k) {
						appendBoundaryTextStep({
							.kind = LeafKind::TableCellText,
							.block = path,
							.tableRowIndex = rowIndex,
							.tableCellIndex = k - 1,
						}, steps);
					}
				}
				if (!block.text.text.text.isEmpty()) {
					appendBoundaryTextStep({
						.kind = LeafKind::BlockText,
						.block = path,
					}, steps);
				}
			}
			appendBoundaryBlockStep(path, steps);
			break;
		case BlockKind::Details:
			if (forward) {
				appendBoundaryTextStep({
					.kind = LeafKind::BlockText,
					.block = path,
				}, steps);
				collectBoundarySteps(
					block.blocks,
					BlockChildrenContainer(path),
					forward,
					steps);
			} else {
				collectBoundarySteps(
					block.blocks,
					BlockChildrenContainer(path),
					forward,
					steps);
				appendBoundaryTextStep({
					.kind = LeafKind::BlockText,
					.block = path,
				}, steps);
			}
			appendBoundaryBlockStep(path, steps);
			break;
		default: {
			const auto before = steps->size();
			if (!block.blocks.empty()) {
				collectBoundarySteps(
					block.blocks,
					BlockChildrenContainer(path),
					forward,
					steps);
			}
			if (steps->size() == before) {
				appendBoundaryBlockStep(path, steps);
			}
		} break;
		}
	};
	if (forward) {
		for (auto i = 0, count = int(blocks.size()); i != count; ++i) {
			collectBlock(i);
		}
	} else {
		for (auto i = int(blocks.size()); i != 0; --i) {
			collectBlock(i - 1);
		}
	}
}

void State::appendBoundaryTextStep(
		LeafPath leaf,
		std::vector<BoundaryTarget> *steps) const {
	const auto ordinal = textNodeOrdinal(leaf);
	if (ordinal >= 0) {
		steps->push_back({
			.action = BoundaryAction::Text,
			.textOrdinal = ordinal,
		});
	}
}

void State::appendBoundaryBlockStep(
		const BlockPath &path,
		std::vector<BoundaryTarget> *steps) const {
	const auto owner = block(path);
	if (owner && CanEditBlock(*owner)) {
		steps->push_back({
			.action = BoundaryAction::StructuralSelection,
			.structuralSelection = preparedSelectionForBlock(path),
		});
	}
}

void State::appendBoundaryListItemStep(
		const BlockPath &path,
		int itemIndex,
		std::vector<BoundaryTarget> *steps) const {
	const auto owner = block(path);
	if (!owner
		|| owner->kind != BlockKind::List
		|| itemIndex < 0
		|| itemIndex >= int(owner->listItems.size())
		|| !CanEditBlocks(owner->listItems[itemIndex].blocks)) {
		return;
	}
	steps->push_back({
		.action = BoundaryAction::StructuralSelection,
		.structuralSelection = preparedSelectionForListItem(path, itemIndex),
	});
}

PreparedEditSelection State::preparedSelectionForBlock(
		const BlockPath &path) const {
	return {
		.kind = PreparedEditSelectionKind::Blocks,
		.blocks = {
			.container = ToPreparedBlockContainerPath(path.container),
			.from = path.index,
			.till = path.index + 1,
		},
	};
}

PreparedEditSelection State::preparedSelectionForListItem(
		const BlockPath &path,
		int itemIndex) const {
	return {
		.kind = PreparedEditSelectionKind::ListItems,
		.listItems = {
			.block = PreparedBlockPath{
				.container = ToPreparedBlockContainerPath(path.container),
				.index = path.index,
			},
			.from = itemIndex,
			.till = itemIndex + 1,
		},
	};
}

bool State::descriptorBelongsToBlock(
		const TextNodeDescriptor &descriptor,
		const BlockPath &path) const {
	return leafBelongsToBlock(descriptor.leaf, path);
}

bool State::removalTargetIsEmpty(const RemovalTarget &target) const {
	switch (target.kind) {
	case RemovalKind::Block:
		if (const auto owner = block(target.block)) {
			return BlockIsEmpty(*owner);
		}
		return false;
	case RemovalKind::ListItem:
		if (const auto item = listItem(target.block, target.listItemIndex)) {
			return ListItemIsEmpty(*item);
		}
		return false;
	case RemovalKind::TableCell:
		if (const auto cell = tableCell(
				target.block,
				target.tableRowIndex,
				target.tableCellIndex)) {
			return RichTextIsEmpty(cell->text);
		}
		return false;
	}
	return false;
}

bool State::removeTarget(const RemovalTarget &target) {
	clearTemporaryDownParagraph();
	switch (target.kind) {
	case RemovalKind::Block: {
		const auto blocks = blockContainer(target.block.container);
		if (!blocks
			|| target.block.index < 0
			|| target.block.index >= int(blocks->size())) {
			return false;
		}
		blocks->erase(blocks->begin() + target.block.index);
		return true;
	}
	case RemovalKind::ListItem: {
		const auto owner = block(target.block);
		if (!owner
			|| target.listItemIndex < 0
			|| target.listItemIndex >= int(owner->listItems.size())) {
			return false;
		}
		owner->listItems.erase(owner->listItems.begin() + target.listItemIndex);
		return true;
	}
	case RemovalKind::TableCell: {
		const auto cell = tableCell(
			target.block,
			target.tableRowIndex,
			target.tableCellIndex);
		if (!cell) {
			return false;
		}
		cell->text = RichText();
		return true;
	}
	}
	return false;
}

void State::normalizeInsertedBlockAnchors(std::vector<Block> &blocks) {
	for (auto &block : blocks) {
		normalizeInsertedBlockAnchors(blocks, block);
	}
}

void State::normalizeInsertedBlockAnchors(
		std::vector<Block> &root,
		Block &block) {
	const auto normalize = [&](QString &id) {
		if (id.isEmpty()) {
			return;
		}
		const auto base = id;
		id.clear();
		auto candidate = base;
		for (auto suffix = 2;
			anchorIdExists(candidate) || anchorIdExists(root, candidate);
			++suffix) {
			candidate = base + u"-%1"_q.arg(suffix);
		}
		id = std::move(candidate);
	};
	normalize(block.anchorId);
	normalizeInsertedRichTextAnchors(root, block.text);
	normalizeInsertedRichTextAnchors(root, block.caption);
	for (auto &child : block.blocks) {
		normalizeInsertedBlockAnchors(root, child);
	}
	for (auto &item : block.listItems) {
		normalize(item.anchorId);
		normalizeInsertedRichTextAnchors(root, item.text);
		for (auto &child : item.blocks) {
			normalizeInsertedBlockAnchors(root, child);
		}
	}
	for (auto &row : block.tableRows) {
		for (auto &cell : row.cells) {
			normalizeInsertedRichTextAnchors(root, cell.text);
		}
	}
}

void State::normalizeInsertedRichTextAnchors(
		std::vector<Block> &root,
		RichText &text) {
	const auto normalize = [&](QString &id) {
		if (id.isEmpty()) {
			return;
		}
		const auto base = id;
		id.clear();
		auto candidate = base;
		for (auto suffix = 2;
			anchorIdExists(candidate) || anchorIdExists(root, candidate);
			++suffix) {
			candidate = base + u"-%1"_q.arg(suffix);
		}
		id = std::move(candidate);
	};
	normalize(text.anchorId);
	for (auto &id : text.anchorIds) {
		normalize(id);
	}
}

bool State::anchorIdExists(const QString &id) const {
	return anchorIdExists(_richPage->blocks, id);
}

bool State::anchorIdExists(
		const std::vector<Block> &blocks,
		const QString &id) const {
	for (const auto &block : blocks) {
		if (block.anchorId == id
			|| block.text.anchorId == id
			|| block.caption.anchorId == id
			|| ranges::contains(block.text.anchorIds, id)
			|| ranges::contains(block.caption.anchorIds, id)) {
			return true;
		}
		if (anchorIdExists(block.blocks, id)) {
			return true;
		}
		for (const auto &item : block.listItems) {
			if (item.anchorId == id
				|| item.text.anchorId == id
				|| ranges::contains(item.text.anchorIds, id)) {
				return true;
			}
			if (anchorIdExists(item.blocks, id)) {
				return true;
			}
		}
		for (const auto &row : block.tableRows) {
			for (const auto &cell : row.cells) {
				if (cell.text.anchorId == id
					|| ranges::contains(cell.text.anchorIds, id)) {
					return true;
				}
			}
		}
	}
	return false;
}

QString State::nextAnchorId() const {
	for (auto i = 1;; ++i) {
		auto result = u"anchor-%1"_q.arg(i);
		if (!anchorIdExists(result)) {
			return result;
		}
	}
}

Block State::makeBlock(InsertAction action) const {
	switch (action.type) {
	case InsertBlockType::Heading:
		return MakeHeadingBlock(action.headingLevel);
	case InsertBlockType::Blockquote:
		return MakeQuoteBlock(false);
	case InsertBlockType::Code:
		return MakeCodeBlock();
	case InsertBlockType::Math:
		return MakeMathBlock();
	case InsertBlockType::Footer:
		return MakeFooterBlock();
	case InsertBlockType::Divider:
		return MakeDividerBlock();
	case InsertBlockType::Anchor:
		return MakeAnchorBlock(nextAnchorId());
	case InsertBlockType::OrderedList:
		return MakeListBlock(ListKind::Ordered);
	case InsertBlockType::BulletList:
		return MakeListBlock(ListKind::Bullet);
	case InsertBlockType::TaskList:
		return MakeListBlock(ListKind::Bullet, TaskState::Unchecked);
	case InsertBlockType::Pullquote:
		return MakeQuoteBlock(true);
	case InsertBlockType::Photo:
		return MakeMediaBlock(BlockKind::Photo);
	case InsertBlockType::Video:
		return MakeMediaBlock(BlockKind::Video);
	case InsertBlockType::Audio:
		return MakeMediaBlock(BlockKind::Audio);
	case InsertBlockType::Details:
		return MakeDetailsBlock();
	case InsertBlockType::Table:
		return MakeTableBlock();
	case InsertBlockType::Map:
		return MakeMapBlock(action.latitude, action.longitude);
	}
	return MakeParagraphBlock();
}

TextWithEntities State::MakeText(QString text) {
	auto result = TextWithEntities();
	result.text = std::move(text);
	return result;
}

Block State::MakeParagraphBlock() {
	auto block = Block();
	block.kind = BlockKind::Paragraph;
	return block;
}

Block State::MakeFooterBlock() {
	auto block = Block();
	block.kind = BlockKind::Footer;
	return block;
}

Block State::MakeHeadingBlock(int level) {
	auto block = Block();
	block.kind = BlockKind::Heading;
	block.headingLevel = std::clamp(level, 1, 6);
	return block;
}

Block State::MakeQuoteBlock(bool pullquote) {
	auto block = Block();
	block.kind = BlockKind::Quote;
	block.pullquote = pullquote;
	return block;
}

Block State::MakeCodeBlock() {
	auto block = Block();
	block.kind = BlockKind::Code;
	return block;
}

Block State::MakeMathBlock() {
	auto block = Block();
	block.kind = BlockKind::Math;
	return block;
}

Block State::MakeDividerBlock() {
	auto block = Block();
	block.kind = BlockKind::Divider;
	return block;
}

Block State::MakeAnchorBlock(QString anchorId) {
	auto block = Block();
	block.kind = BlockKind::Anchor;
	block.anchorId = std::move(anchorId);
	return block;
}

Block State::MakeListBlock(ListKind kind, TaskState taskState) {
	auto block = Block();
	block.kind = BlockKind::List;
	block.listKind = kind;
	auto item = ListItem();
	item.taskState = taskState;
	block.listItems.push_back(std::move(item));
	if (kind != ListKind::Ordered) {
		block.orderedList = {};
	}
	return block;
}

ListItem State::MakeParagraphListItem(TaskState taskState) {
	auto item = ListItem();
	item.taskState = taskState;
	item.number = {};
	item.blocks.push_back(MakeParagraphBlock());
	return item;
}

Block State::MakeDetailsBlock() {
	auto block = Block();
	block.kind = BlockKind::Details;
	block.blocks.push_back(MakeParagraphBlock());
	return block;
}

Block State::MakeTableBlock() {
	auto block = Block();
	block.kind = BlockKind::Table;
	block.bordered = true;
	block.tableRows.reserve(3);
	for (auto rowIndex = 0; rowIndex != 3; ++rowIndex) {
		auto row = RichPage::TableRow();
		row.cells.reserve(3);
		for (auto cellIndex = 0; cellIndex != 3; ++cellIndex) {
			auto cell = TableCell();
			cell.header = (rowIndex == 0);
			row.cells.push_back(std::move(cell));
		}
		block.tableRows.push_back(std::move(row));
	}
	return block;
}

Block State::MakeMediaBlock(BlockKind kind) {
	auto block = Block();
	block.kind = kind;
	return block;
}

Block State::MakeMapBlock(double latitude, double longitude) {
	auto block = Block();
	block.kind = BlockKind::Map;
	block.latitude = latitude;
	block.longitude = longitude;
	return block;
}

bool State::RichTextIsEmpty(const RichText &text) {
	return StringIsEmpty(text.text.text)
		&& text.anchorId.isEmpty()
		&& text.anchorIds.empty();
}

bool State::ListItemIsEmpty(const ListItem &item) {
	if (!RichTextIsEmpty(item.text) || !item.anchorId.isEmpty()) {
		return false;
	}
	for (const auto &block : item.blocks) {
		if (!BlockIsEmpty(block)) {
			return false;
		}
	}
	return true;
}

bool State::BlockIsEmpty(const Block &block) {
	if (!RichTextIsEmpty(block.text)
		|| !RichTextIsEmpty(block.caption)
		|| !StringIsEmpty(block.formula)
		|| !block.language.isEmpty()
		|| !block.anchorId.isEmpty()
		|| !block.url.isEmpty()
		|| !block.html.isEmpty()
		|| !block.author.isEmpty()
		|| !block.username.isEmpty()
		|| !block.channelTitle.isEmpty()
		|| !block.audioTitle.isEmpty()
		|| !block.audioPerformer.isEmpty()
		|| !block.audioFileName.isEmpty()
		|| block.photo
		|| block.document
		|| block.peer
		|| block.photoId
		|| block.documentId
		|| block.channelId
		|| block.accessHash
		|| block.latitude != 0.
		|| block.longitude != 0.
		|| !block.mediaItems.empty()) {
		return false;
	}
	for (const auto &child : block.blocks) {
		if (!BlockIsEmpty(child)) {
			return false;
		}
	}
	for (const auto &item : block.listItems) {
		if (!ListItemIsEmpty(item)) {
			return false;
		}
	}
	for (const auto &row : block.tableRows) {
		for (const auto &cell : row.cells) {
			if (!RichTextIsEmpty(cell.text)) {
				return false;
			}
		}
	}
	return true;
}

bool State::StripWrapperEntityInEditMode(EntityType type) {
	switch (type) {
	case EntityType::Url:
	case EntityType::Email:
	case EntityType::Hashtag:
	case EntityType::Cashtag:
	case EntityType::Mention:
	case EntityType::BotCommand:
	case EntityType::Phone:
	case EntityType::BankCard:
		return true;
	default:
		return false;
	}
}

TextWithEntities State::StripEditModeWrapperEntities(TextWithEntities text) {
	auto filtered = EntitiesInText();
	filtered.reserve(text.entities.size());
	for (const auto &entity : text.entities) {
		if (!StripWrapperEntityInEditMode(entity.type())) {
			filtered.push_back(entity);
		}
	}
	text.entities = std::move(filtered);
	return text;
}

void State::StripEditModeWrapperEntities(RichPage::RichText &text) {
	const auto strip = ranges::any_of(
		text.text.entities,
		[](const EntityInText &entity) {
			return StripWrapperEntityInEditMode(entity.type());
		});
	if (strip) {
		text.text = StripEditModeWrapperEntities(std::move(text.text));
	}
}

void State::StripEditModeWrapperEntities(
		std::vector<RichPage::Block> &blocks) {
	for (auto &block : blocks) {
		StripEditModeWrapperEntities(block.text);
		StripEditModeWrapperEntities(block.caption);
		StripEditModeWrapperEntities(block.blocks);
		for (auto &item : block.listItems) {
			StripEditModeWrapperEntities(item.text);
			StripEditModeWrapperEntities(item.blocks);
		}
		for (auto &row : block.tableRows) {
			for (auto &cell : row.cells) {
				StripEditModeWrapperEntities(cell.text);
			}
		}
	}
}

bool CanEditRichPage(const RichPage &page) {
	return CanEditBlocks(page.blocks);
}

bool CanEditRichPage(const std::shared_ptr<const RichPage> &page) {
	return page && CanEditRichPage(*page);
}

} // namespace Iv::Editor
