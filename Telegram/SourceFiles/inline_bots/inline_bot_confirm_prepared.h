/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class GenericBox;
} // namespace Ui

namespace InlineBots {

void PreparedPreviewBox(
	not_null<Ui::GenericBox*> box,
	not_null<HistoryItem*> item,
	Fn<void()> share);

} // namespace InlineBots
