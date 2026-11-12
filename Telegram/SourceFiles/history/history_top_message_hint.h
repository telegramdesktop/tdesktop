/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class History;
class HistoryItem;

class HistoryTopMessageHint final {
public:
	explicit HistoryTopMessageHint(not_null<History*> history);

	[[nodiscard]] bool apply(MsgId id, TimeId date);
	[[nodiscard]] bool active(MsgId id) const;
	void loaded(MsgId id, const MTPMessage *message);
	void clear();

	void lastMessageSet(HistoryItem *item, bool replaced);
	void itemRemoved(not_null<HistoryItem*> item);
	void unknownMessageDeleted(MsgId id);
	void clearedUpTill(MsgId availableMinId);

	[[nodiscard]] TimeId date() const;
	[[nodiscard]] HistoryItem *item() const;
	[[nodiscard]] bool unread(not_null<const HistoryItem*> item) const;

private:
	[[nodiscard]] bool eligible() const;
	[[nodiscard]] bool unavailable(MsgId id) const;
	[[nodiscard]] bool hasRegularMessageAfter(MsgId id) const;
	[[nodiscard]] HistoryItem *heldTarget() const;
	void createPreview(const MTPMessage &message);
	void previewRemoved(not_null<const HistoryItem*> item);
	void forgetPreview();

	const not_null<History*> _history;
	rpl::lifetime _previewLifetime;
	MsgId _id = 0;
	MsgId _previewId = 0;
	TimeId _date = 0;

};
