/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"
#include "dialogs/dialogs_entry.h"
#include "dialogs/ui/dialogs_message_view.h"
#include "ui/text/text.h"

#include <deque>

enum class ChatRestriction;
using ChatRestrictions = base::flags<ChatRestriction>;

namespace Main {
class Session;
} // namespace Main

namespace HistoryUnreadThings {
enum class AddType;
struct All;
class Proxy;
class ConstProxy;
} // namespace HistoryUnreadThings

namespace HistoryView {
class SendActionPainter;
} // namespace HistoryView

namespace st {
extern const int &dialogsTextWidthMin;
} // namespace st

namespace Data {

class PeerNotifySettings;

enum class ItemNotificationType {
	Message,
	Reaction,
};

struct ItemNotification {
	not_null<HistoryItem*> item;
	UserData *reactionSender = nullptr;
	ItemNotificationType type = ItemNotificationType::Message;

	friend inline auto operator<=>(
		ItemNotification a,
		ItemNotification b) = default;
};

class Thread : public Dialogs::Entry {
public:
	using Entry::Entry;
	~Thread();

	[[nodiscard]] virtual not_null<History*> owningHistory() = 0;

	[[nodiscard]] not_null<Thread*> migrateToOrMe() const;
	[[nodiscard]] not_null<const History*> owningHistory() const {
		return const_cast<Thread*>(this)->owningHistory();
	}
	[[nodiscard]] MsgId topicRootId() const;
	[[nodiscard]] not_null<PeerData*> peer() const;
	[[nodiscard]] PeerNotifySettings &notify();
	[[nodiscard]] const PeerNotifySettings &notify() const;

	void setUnreadThingsKnown();
	[[nodiscard]] HistoryUnreadThings::Proxy unreadMentions();
	[[nodiscard]] HistoryUnreadThings::ConstProxy unreadMentions() const;
	[[nodiscard]] HistoryUnreadThings::Proxy unreadReactions();
	[[nodiscard]] HistoryUnreadThings::ConstProxy unreadReactions() const;
	virtual void hasUnreadMentionChanged(bool has) = 0;
	virtual void hasUnreadReactionChanged(bool has) = 0;

	void removeNotification(not_null<HistoryItem*> item);
	void clearNotifications();
	void clearIncomingNotifications();
	[[nodiscard]] auto currentNotification() const
		-> std::optional<ItemNotification>;
	bool hasNotification() const;
	void skipNotification();
	void pushNotification(ItemNotification notification);
	void popNotification(ItemNotification notification);

	[[nodiscard]] bool muted() const {
		return (_flags & Flag::Muted);
	}
	virtual void setMuted(bool muted);

	[[nodiscard]] bool unreadMark() const {
		return (_flags & Flag::UnreadMark);
	}

	[[nodiscard]] virtual bool isServerSideUnread(
		not_null<const HistoryItem*> item) const = 0;

	[[nodiscard]] const base::flat_set<MsgId> &unreadMentionsIds() const;
	[[nodiscard]] const base::flat_set<MsgId> &unreadReactionsIds() const;

	[[nodiscard]] Ui::Text::String &cloudDraftTextCache() {
		return _cloudDraftTextCache;
	}
	[[nodiscard]] Dialogs::Ui::MessageView &lastItemDialogsView() {
		return _lastItemDialogsView;
	}

	[[nodiscard]] virtual auto sendActionPainter()
		-> not_null<HistoryView::SendActionPainter*> = 0;

	[[nodiscard]] bool hasPinnedMessages() const;
	void setHasPinnedMessages(bool has);

protected:
	void setUnreadMarkFlag(bool unread);

private:
	enum class Flag : uchar {
		UnreadMark = (1 << 0),
		Muted = (1 << 1),
		UnreadThingsKnown = (1 << 2),
		HasPinnedMessages = (1 << 3),
	};
	friend inline constexpr bool is_flag_type(Flag) { return true; }

	Ui::Text::String _cloudDraftTextCache = { st::dialogsTextWidthMin };
	Dialogs::Ui::MessageView _lastItemDialogsView;
	std::unique_ptr<HistoryUnreadThings::All> _unreadThings;
	std::deque<ItemNotification> _notifications;

	base::flags<Flag> _flags;

};

} // namespace Data
