/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace HistoryView {

struct Selector {
	Fn<void(int, int)> move;
	Fn<void(int)> resizeToWidth;
	Fn<rpl::producer<QString>()> changes;
	Fn<rpl::producer<int>()> heightValue;
};

not_null<Selector*> CreateReactionSelector(
	not_null<QWidget*> parent,
	const base::flat_map<QString, int> &items,
	const QString &selected);

} // namespace HistoryView
