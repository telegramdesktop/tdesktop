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

TextWithEntities HistoryItemText(not_null<HistoryItem*> item);
TextWithEntities HistoryGroupText(not_null<const Data::Group*> group);
