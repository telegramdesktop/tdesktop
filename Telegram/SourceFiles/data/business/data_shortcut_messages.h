/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/history_item.h"
#include "base/timer.h"

class History;

namespace Main {
class Session;
} // namespace Main

namespace Data {

class Session;
struct MessagesSlice;

struct Shortcut {
	BusinessShortcutId id = 0;
	int count = 0;
	QString name;
	MsgId topMessageId = 0;

	friend inline bool operator==(
		const Shortcut &a,
		const Shortcut &b) = default;
};

struct ShortcutIdChange {
	BusinessShortcutId oldId = 0;
	BusinessShortcutId newId = 0;
};

struct Shortcuts {
	base::flat_map<BusinessShortcutId, Shortcut> list;

	friend inline bool operator==(
		const Shortcuts &a,
		const Shortcuts &b) = default;
};

[[nodiscard]] bool IsShortcutMsgId(MsgId id);

class ShortcutMessages final {
public:
	explicit ShortcutMessages(not_null<Session*> owner);
	~ShortcutMessages();

	[[nodiscard]] MsgId lookupId(not_null<const HistoryItem*> item) const;
	[[nodiscard]] int count(BusinessShortcutId shortcutId) const;
	[[nodiscard]] MsgId localMessageId(MsgId remoteId) const;

	void apply(const MTPDupdateQuickReplies &update);
	void apply(const MTPDupdateNewQuickReply &update);
	void apply(const MTPDupdateQuickReplyMessage &update);
	void apply(const MTPDupdateDeleteQuickReplyMessages &update);
	void apply(const MTPDupdateDeleteQuickReply &update);
	void apply(
		const MTPDupdateMessageID &update,
		not_null<HistoryItem*> local);

	void appendSending(not_null<HistoryItem*> item);
	void removeSending(not_null<HistoryItem*> item);

	[[nodiscard]] rpl::producer<> updates(BusinessShortcutId shortcutId);
	[[nodiscard]] Data::MessagesSlice list(BusinessShortcutId shortcutId);

	void preloadShortcuts();
	[[nodiscard]] const Shortcuts &shortcuts() const;
	[[nodiscard]] bool shortcutsLoaded() const;
	[[nodiscard]] rpl::producer<> shortcutsChanged() const;
	[[nodiscard]] rpl::producer<ShortcutIdChange> shortcutIdChanged() const;
	[[nodiscard]] BusinessShortcutId emplaceShortcut(QString name);
	[[nodiscard]] Shortcut lookupShortcut(BusinessShortcutId id) const;
	[[nodiscard]] BusinessShortcutId lookupShortcutId(
		const QString &name) const;
	void editShortcut(
		BusinessShortcutId id,
		QString name,
		Fn<void()> done,
		Fn<void(QString)> fail);
	void removeShortcut(BusinessShortcutId shortcutId);

private:
	using OwnedItem = std::unique_ptr<HistoryItem, HistoryItem::Destroyer>;
	struct List {
		std::vector<OwnedItem> items;
		base::flat_map<MsgId, not_null<HistoryItem*>> itemById;
	};
	struct Request {
		mtpRequestId requestId = 0;
		crl::time lastReceived = 0;
	};

	void request(BusinessShortcutId shortcutId);
	void parse(
		BusinessShortcutId shortcutId,
		const MTPmessages_Messages &list);
	HistoryItem *append(
		BusinessShortcutId shortcutId,
		List &list,
		const MTPMessage &message);
	void clearNotSending(BusinessShortcutId shortcutId);
	void updated(
		BusinessShortcutId shortcutId,
		const base::flat_set<not_null<HistoryItem*>> &added,
		const base::flat_set<not_null<HistoryItem*>> &clear);
	void sort(List &list);
	void remove(not_null<const HistoryItem*> item);
	[[nodiscard]] uint64 countListHash(const List &list) const;
	void clearOldRequests();
	void cancelRequest(BusinessShortcutId shortcutId);
	void updateCount(BusinessShortcutId shortcutId);

	void scheduleShortcutsReload();
	void mergeMessagesFromTo(
		BusinessShortcutId fromId,
		BusinessShortcutId toId);
	void updateShortcuts(const QVector<MTPQuickReply> &list);
	[[nodiscard]] Shortcut parseShortcut(const MTPQuickReply &reply) const;
	[[nodiscard]] Shortcuts parseShortcuts(
		const QVector<MTPQuickReply> &list) const;

	const not_null<Main::Session*> _session;
	const not_null<History*> _history;

	base::Timer _clearTimer;
	base::flat_map<BusinessShortcutId, List> _data;
	base::flat_map<BusinessShortcutId, Request> _requests;
	rpl::event_stream<BusinessShortcutId> _updates;

	Shortcuts _shortcuts;
	rpl::event_stream<> _shortcutsChanged;
	rpl::event_stream<ShortcutIdChange> _shortcutIdChanges;
	BusinessShortcutId _localShortcutId = 0;
	uint64 _shortcutsHash = 0;
	mtpRequestId _shortcutsRequestId = 0;
	bool _shortcutsLoaded = false;

	rpl::lifetime _lifetime;

};

[[nodiscard]] MTPInputQuickReplyShortcut ShortcutIdToMTP(
	not_null<Main::Session*> session,
	BusinessShortcutId id);

} // namespace Data
