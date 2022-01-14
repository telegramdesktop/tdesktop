/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;

namespace style {
struct WhoRead;
} // namespace style

namespace Ui {
struct WhoReadContent;
} // namespace Ui

namespace Api {

[[nodiscard]] bool WhoReadExists(not_null<HistoryItem*> item);
[[nodiscard]] bool WhoReactedExists(not_null<HistoryItem*> item);

// The context must be destroyed before the session holding this item.
[[nodiscard]] rpl::producer<Ui::WhoReadContent> WhoReacted(
	not_null<HistoryItem*> item,
	not_null<QWidget*> context,
	const style::WhoRead &st); // Cache results for this lifetime.
[[nodiscard]] rpl::producer<Ui::WhoReadContent> WhoReacted(
	not_null<HistoryItem*> item,
	const QString &reaction,
	not_null<QWidget*> context,
	const style::WhoRead &st); // Cache results for this lifetime.

} // namespace Api
