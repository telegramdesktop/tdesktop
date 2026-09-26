/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "dialogs/dialogs_key.h"

namespace Data {
class Folder;
} // namespace Data

namespace Dialogs {

class Row;
class FakeRow;

enum class SubItem : int {
	Type,
	Name,
	Warning,
	Premium,
	Verified,
	Activity,
	Muted,
	Pinned,
	Draft,
	Unread,
	Mention,
	Sender,
	Message,
	Delivery,
	Reactions,
	Time,
	Sponsored,
	Stories,
	Autodelete,
	Subscription,
	Closed,
	Forward,
	Folders,

	Count,
};

[[nodiscard]] QString RowAccessibilityName(
	not_null<const Row*> row,
	FilterId filterId);
[[nodiscard]] QString CollapsedRowAccessibilityName(
	not_null<Data::Folder*> folder);
[[nodiscard]] QString SubItemLabel(SubItem item);
[[nodiscard]] QString SubItemValue(
	not_null<const Row*> row,
	FilterId filterId,
	SubItem item);
[[nodiscard]] std::vector<SubItem> ActiveSubItems(
	not_null<const Row*> row,
	FilterId filterId);

[[nodiscard]] QString HashtagAccessibilityName(QStringView tag);
[[nodiscard]] QString PeerSearchResultAccessibilityName(
	not_null<PeerData*> peer,
	bool sponsored);
[[nodiscard]] QString SearchedMessageAccessibilityName(
	not_null<const FakeRow*> row);

} // namespace Dialogs
