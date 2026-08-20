/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "iv/editor/iv_editor_clipboard.h"
#include "iv/iv_rich_page.h"
#include "iv/markdown/iv_markdown_prepare.h"

#include <optional>
#include <vector>

namespace Iv::Editor {

enum class ListStyle : uchar {
	Ordered,
	Bullet,
	Task,
};

[[nodiscard]] bool IsTaskList(const std::vector<RichPage::ListItem> &items);

[[nodiscard]] bool IsTaskList(const ClipboardListItemsData &data);

[[nodiscard]] auto ResolvePreparedOrderedListType(
	const std::optional<QString> &type)
-> Markdown::PreparedOrderedListType;

[[nodiscard]] std::optional<QString> StoredOrderedListType(
	Markdown::PreparedOrderedListType type,
	bool explicitDecimal = false);

[[nodiscard]] std::optional<int> EffectiveOrderedItemValue(
	const RichPage::Block &block,
	int itemIndex);

void DropOrderedItemNumber(RichPage::ListItem *item);
void AdoptListItemMarkers(
	const RichPage::Block &list,
	RichPage::ListItem *item);

void DropOrderedItemNumbers(std::vector<RichPage::ListItem> &items);

[[nodiscard]] bool ListsJoinable(
	const RichPage::Block &first,
	const RichPage::Block &second);

[[nodiscard]] bool ListsJoinSeamlessly(
	const RichPage::Block &first,
	const RichPage::Block &second,
	bool secondStartExplicit);

[[nodiscard]] RichPage::TaskState SplitTaskState(RichPage::TaskState state);

[[nodiscard]] bool ClearOrderedListRawMarkers(
	RichPage::Block *block,
	int from,
	int till);

bool ClearOrderedListRawMarkers(RichPage::Block *block);

[[nodiscard]] bool ClearOrderedListItemTypes(RichPage::Block *block);

[[nodiscard]] bool ClearOrderedTaskStates(RichPage::Block *block);

bool ResetNonOrderedListMetadata(RichPage::Block *block);

void NormalizeInsertedOrderedListMetadata(RichPage::Block *block);

void NormalizeInsertedOrderedListMetadata(
	std::vector<RichPage::Block> *blocks);

[[nodiscard]] ListStyle CurrentListStyle(const RichPage::Block &block);

[[nodiscard]] Markdown::PreparedOrderedListType EffectiveOrderedListType(
	const RichPage::Block &block,
	const RichPage::ListItem &item);

[[nodiscard]] bool ListBlockMatchesClipboardData(
	const RichPage::Block &block,
	const ClipboardListItemsData &data);

} // namespace Iv::Editor
