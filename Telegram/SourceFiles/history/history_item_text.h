/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;

namespace Data {
struct Group;
} // namespace Data

struct HistorySelectedTextEntry {
	not_null<HistoryItem*> item;
	const Data::Group *group = nullptr;
};

TextForMimeData HistoryItemText(not_null<HistoryItem*> item);
TextForMimeData HistoryGroupText(not_null<const Data::Group*> group);
TextForMimeData HistorySelectedItemsText(
	const std::vector<HistorySelectedTextEntry> &entries,
	bool richContext);
