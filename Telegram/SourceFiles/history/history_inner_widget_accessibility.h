/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class History;

namespace HistoryView {

class Element;

enum class MessageSubItem : int {
	Seen,
	Sender,
	ViaBot,
	Reply,
	Forward,
	MediaType,
	Download,
	Played,
	Artist,
	Title,
	Filename,
	Duration,
	Dimensions,
	FileSize,
	Message,
	Delivery,
	Edited,
	Time,
	Reactions,
	Views,
	Signature,
	Pinned,
	WebSite,
	WebTitle,
	WebDescription,
	PollQuestion,
	PollOptions,
	PollStatus,
	ContactName,
	ContactPhone,
	Location,
	StickerEmoji,
	GameTitle,
	GameDescription,
	InvoiceTitle,
	InvoiceAmount,
	Spoiler,
	Dice,
	Giveaway,
	Gift,
	TodoTitle,
	TodoItems,
	Factcheck,
	ForwardDate,
	ForwardAuthor,
	PaidReactions,

	Count,
};

[[nodiscard]] QString MessageAccessibilityName(
	not_null<const Element*> view,
	not_null<History*> history);
[[nodiscard]] QString UnreadBarAccessibilityName(
	not_null<const Element*> barElement);
[[nodiscard]] QString MessageSubItemLabel(MessageSubItem item);
[[nodiscard]] QString MessageSubItemValue(
	not_null<const Element*> view,
	not_null<History*> history,
	MessageSubItem item);
[[nodiscard]] std::vector<MessageSubItem> ActiveMessageSubItems(
	not_null<const Element*> view,
	not_null<History*> history);

} // namespace HistoryView
