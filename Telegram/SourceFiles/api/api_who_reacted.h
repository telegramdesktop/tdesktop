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
enum class WhoReadType;
} // namespace Ui

namespace Data {
struct ReactionId;
} // namespace Data

namespace Api {

enum class WhoReactedList {
	All,
	One,
};

[[nodiscard]] bool WhoReadExists(not_null<HistoryItem*> item);
[[nodiscard]] bool WhoReactedExists(
	not_null<HistoryItem*> item,
	WhoReactedList list);

struct WhoReadList {
	std::vector<PeerId> list;
	Ui::WhoReadType type = {};
};

// The context must be destroyed before the session holding this item.
[[nodiscard]] rpl::producer<Ui::WhoReadContent> WhoReacted(
	not_null<HistoryItem*> item,
	not_null<QWidget*> context, // Cache results for this lifetime.
	const style::WhoRead &st,
	std::shared_ptr<WhoReadList> whoReadIds = nullptr);
[[nodiscard]] rpl::producer<Ui::WhoReadContent> WhoReacted(
	not_null<HistoryItem*> item,
	const Data::ReactionId &reaction,
	not_null<QWidget*> context, // Cache results for this lifetime.
	const style::WhoRead &st);

} // namespace Api
