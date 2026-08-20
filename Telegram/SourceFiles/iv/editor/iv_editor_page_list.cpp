/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/editor/iv_editor_page_list.h"

#include <algorithm>

namespace Iv::Editor {
namespace {

using Block = RichPage::Block;
using BlockKind = RichPage::BlockKind;
using ListItem = RichPage::ListItem;
using ListKind = RichPage::ListKind;
using TaskState = RichPage::TaskState;
using PreparedOrderedListType = Markdown::PreparedOrderedListType;
using OrderedListData = RichPage::OrderedListData;

[[nodiscard]] int OrderedListSequenceStart(const Block &block) {
	return block.orderedList.start.value_or(
		block.orderedList.reversed ? int(block.listItems.size()) : 1);
}

[[nodiscard]] int OrderedListSequenceStep(const Block &block) {
	return block.orderedList.reversed ? -1 : 1;
}

[[nodiscard]] bool ListIsTaskList(const Block &block) {
	return ranges::any_of(block.listItems, [](const ListItem &item) {
		return (item.taskState != TaskState::None);
	});
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

} // namespace

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
		bool explicitDecimal) {
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

void DropOrderedItemNumber(ListItem *item) {
	if (item) {
		item->number = RichPage::OrderedListItemData();
	}
}

void DropOrderedItemNumbers(std::vector<ListItem> &items) {
	for (auto &item : items) {
		DropOrderedItemNumber(&item);
	}
}

void AdoptListItemMarkers(const Block &list, ListItem *item) {
	if (!item || list.kind != BlockKind::List) {
		return;
	}
	DropOrderedItemNumber(item);
	const auto task = (list.listKind != ListKind::Ordered)
		&& IsTaskList(list.listItems);
	if (!task) {
		item->taskState = TaskState::None;
	} else if (item->taskState == TaskState::None) {
		item->taskState = TaskState::Unchecked;
	}
}

[[nodiscard]] bool ListsJoinable(const Block &first, const Block &second) {
	return (first.kind == BlockKind::List)
		&& (second.kind == BlockKind::List)
		&& (first.listKind == second.listKind)
		&& (ListIsTaskList(first) == ListIsTaskList(second));
}

[[nodiscard]] bool ListsJoinSeamlessly(
		const Block &first,
		const Block &second,
		bool secondStartExplicit) {
	if (!ListsJoinable(first, second)
		|| first.listItems.empty()
		|| second.listItems.empty()) {
		return false;
	} else if (first.listKind != ListKind::Ordered) {
		return true;
	} else if (first.orderedList.reversed || second.orderedList.reversed) {
		return false;
	} else if (ResolvePreparedOrderedListType(first.orderedList.type)
		!= ResolvePreparedOrderedListType(second.orderedList.type)) {
		return false;
	}
	const auto ownNumbering = secondStartExplicit
		|| second.orderedList.start.has_value()
		|| ranges::any_of(second.listItems, [](const ListItem &item) {
			return item.number != RichPage::OrderedListItemData();
		});
	if (!ownNumbering) {
		return true;
	}
	const auto last = EffectiveOrderedItemValue(
		first,
		int(first.listItems.size()) - 1);
	const auto next = EffectiveOrderedItemValue(second, 0);
	return last && next && (*last + 1 == *next);
}

[[nodiscard]] TaskState SplitTaskState(TaskState state) {
	return (state == TaskState::Checked) ? TaskState::Unchecked : state;
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

bool ClearOrderedListRawMarkers(Block *block) {
	return block
		? ClearOrderedListRawMarkers(block, 0, int(block->listItems.size()))
		: false;
}

[[nodiscard]] bool ClearOrderedListItemTypes(Block *block) {
	if (!block
		|| block->kind != BlockKind::List
		|| block->listKind != ListKind::Ordered) {
		return false;
	}
	auto changed = false;
	for (auto &item : block->listItems) {
		if (item.number.type.has_value()) {
			item.number.type = std::nullopt;
			changed = true;
		}
	}
	return changed;
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

bool ResetNonOrderedListMetadata(Block *block) {
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
		ResetNonOrderedListMetadata(block);
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

} // namespace Iv::Editor
