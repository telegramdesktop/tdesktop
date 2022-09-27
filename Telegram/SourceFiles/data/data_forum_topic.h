/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "dialogs/dialogs_entry.h"
#include "dialogs/ui/dialogs_message_view.h"

class ChannelData;

namespace Dialogs {
class MainList;
} // namespace Dialogs

namespace Main {
class Session;
} // namespace Main

namespace Data {

class Session;

class ForumTopic final : public Dialogs::Entry {
public:
	static constexpr auto kGeneralId = 1;

	ForumTopic(not_null<History*> forum, MsgId rootId);

	ForumTopic(const ForumTopic &) = delete;
	ForumTopic &operator=(const ForumTopic &) = delete;

	[[nodiscard]] not_null<ChannelData*> channel() const;
	[[nodiscard]] not_null<History*> forum() const;
	[[nodiscard]] MsgId rootId() const;
	[[nodiscard]] bool isGeneral() const {
		return (_rootId == kGeneralId);
	}

	void applyTopic(const MTPForumTopic &topic);

	TimeId adjustedChatListTimeId() const override;

	int fixedOnTopIndex() const override;
	bool shouldBeInChatList() const override;
	int chatListUnreadCount() const override;
	bool chatListUnreadMark() const override;
	bool chatListMutedBadge() const override;
	Dialogs::UnreadState chatListUnreadState() const override;
	HistoryItem *chatListMessage() const override;
	bool chatListMessageKnown() const override;
	void requestChatListMessage() override;
	const QString &chatListName() const override;
	const QString &chatListNameSortKey() const override;
	const base::flat_set<QString> &chatListNameWords() const override;
	const base::flat_set<QChar> &chatListFirstLetters() const override;

	[[nodiscard]] HistoryItem *lastMessage() const;
	[[nodiscard]] HistoryItem *lastServerMessage() const;
	[[nodiscard]] bool lastMessageKnown() const;
	[[nodiscard]] bool lastServerMessageKnown() const;

	[[nodiscard]] QString title() const;
	void applyTitle(const QString &title);
	[[nodiscard]] DocumentId iconId() const;
	void applyIconId(DocumentId iconId);
	void applyItemAdded(not_null<HistoryItem*> item);
	void applyItemRemoved(MsgId id);

	void loadUserpic() override;
	void paintUserpic(
		Painter &p,
		std::shared_ptr<Data::CloudImageView> &view,
		int x,
		int y,
		int size) const override;

	[[nodiscard]] int unreadCount() const;
	[[nodiscard]] bool unreadCountKnown() const;

	[[nodiscard]] int unreadCountForBadge() const; // unreadCount || unreadMark ? 1 : 0.

	void setUnreadCount(int newUnreadCount);
	void setUnreadMark(bool unread);
	[[nodiscard]] bool unreadMark() const;

	Ui::Text::String cloudDraftTextCache;
	Dialogs::Ui::MessageView lastItemDialogsView;

private:
	void indexTitleParts();
	void applyTopicTopMessage(MsgId topMessageId);
	void applyTopicFields(
		int unreadCount,
		MsgId maxInboxRead,
		MsgId maxOutboxRead);

	void setLastMessage(HistoryItem *item);
	void setLastServerMessage(HistoryItem *item);
	void setChatListMessage(HistoryItem *item);

	void setInboxReadTill(MsgId upTo);
	void setOutboxReadTill(MsgId upTo);

	int chatListNameVersion() const override;

	const not_null<History*> _forum;
	const not_null<Dialogs::MainList*> _list;
	const MsgId _rootId = 0;

	QString _title;
	DocumentId _iconId = 0;
	base::flat_set<QString> _titleWords;
	base::flat_set<QChar> _titleFirstLetters;
	int _titleVersion = 0;

	std::optional<MsgId> _inboxReadBefore;
	std::optional<MsgId> _outboxReadBefore;
	std::optional<int> _unreadCount;
	std::optional<HistoryItem*> _lastMessage;
	std::optional<HistoryItem*> _lastServerMessage;
	std::optional<HistoryItem*> _chatListMessage;
	bool _unreadMark = false;

	rpl::lifetime _lifetime;

};

} // namespace Data
