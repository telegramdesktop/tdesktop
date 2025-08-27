/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "ui/chat/message_bar.h"

#include <tuple>

struct ClickHandlerContext;

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class IconButton;
class PlainShadow;
class RoundButton;
struct MessageBarContent;
} // namespace Ui

namespace HistoryView {

[[nodiscard]] rpl::producer<Ui::MessageBarContent> MessageBarContentByItemId(
	not_null<Main::Session*> session,
	FullMsgId id,
	Fn<void()> repaint);

enum class PinnedIdType;
struct PinnedId {
	FullMsgId message;
	int index = 0;
	int count = 1;

	bool operator<(const PinnedId &other) const {
		return std::tie(message, index, count)
			< std::tie(other.message, other.index, other.count);
	}
	bool operator==(const PinnedId &other) const {
		return std::tie(message, index, count)
			== std::tie(other.message, other.index, other.count);
	}
	bool operator!=(const PinnedId &other) const {
		return !(*this == other);
	}
};
[[nodiscard]] rpl::producer<Ui::MessageBarContent> PinnedBarContent(
	not_null<Main::Session*> session,
	rpl::producer<PinnedId> id,
	Fn<void()> repaint);

[[nodiscard]] rpl::producer<HistoryItem*> PinnedBarItemWithCustomButton(
	not_null<Main::Session*> session,
	rpl::producer<PinnedId> id);

[[nodiscard]] object_ptr<Ui::RoundButton> CreatePinnedBarCustomButton(
	not_null<QWidget*> parent,
	HistoryItem *item,
	Fn<ClickHandlerContext(FullMsgId)> context);

} // namespace HistoryView
