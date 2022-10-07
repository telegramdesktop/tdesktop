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

namespace style {
struct ForumTopicIcon;
} // namespace style

namespace Dialogs {
class MainList;
} // namespace Dialogs

namespace Main {
class Session;
} // namespace Main

namespace Data {

class RepliesList;
class Session;
class Forum;

[[nodiscard]] const base::flat_map<int32, QString> &ForumTopicIcons();
[[nodiscard]] const std::vector<int32> &ForumTopicColorIds();
[[nodiscard]] const QString &ForumTopicIcon(int32 colorId);
[[nodiscard]] QString ForumTopicIconPath(const QString &name);
[[nodiscard]] QImage ForumTopicIconBackground(int32 colorId, int size);
[[nodiscard]] QImage ForumTopicIconFrame(
	int32 colorId,
	const QString &title,
	const style::ForumTopicIcon &st);

class ForumTopic final : public Dialogs::Entry {
public:
	static constexpr auto kGeneralId = 1;

	ForumTopic(not_null<History*> history, MsgId rootId);
	~ForumTopic();

	ForumTopic(const ForumTopic &) = delete;
	ForumTopic &operator=(const ForumTopic &) = delete;

	[[nodiscard]] std::shared_ptr<RepliesList> replies() const;
	[[nodiscard]] not_null<ChannelData*> channel() const;
	[[nodiscard]] not_null<History*> history() const;
	[[nodiscard]] not_null<Forum*> forum() const;
	[[nodiscard]] MsgId rootId() const;
	[[nodiscard]] bool isGeneral() const {
		return (_rootId == kGeneralId);
	}

	void setRealRootId(MsgId realId);

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
	[[nodiscard]] int32 colorId() const;
	void applyColorId(int32 colorId);
	void applyItemAdded(not_null<HistoryItem*> item);
	void applyItemRemoved(MsgId id);

	void loadUserpic() override;
	void paintUserpic(
		Painter &p,
		std::shared_ptr<Data::CloudImageView> &view,
		const Dialogs::Ui::PaintContext &context) const override;

	[[nodiscard]] int unreadCount() const;
	[[nodiscard]] bool unreadCountKnown() const;

	[[nodiscard]] int unreadCountForBadge() const; // unreadCount || unreadMark ? 1 : 0.

	void setUnreadMark(bool unread);
	[[nodiscard]] bool unreadMark() const;

	Ui::Text::String cloudDraftTextCache;
	Dialogs::Ui::MessageView lastItemDialogsView;

private:
	void indexTitleParts();
	void validateDefaultIcon() const;
	void applyTopicTopMessage(MsgId topMessageId);

	void setLastMessage(HistoryItem *item);
	void setLastServerMessage(HistoryItem *item);
	void setChatListMessage(HistoryItem *item);

	int chatListNameVersion() const override;

	[[nodiscard]] Dialogs::UnreadState unreadStateFor(
		int count,
		bool known) const;

	const not_null<History*> _history;
	const not_null<Dialogs::MainList*> _list;
	std::shared_ptr<RepliesList> _replies;
	MsgId _rootId = 0;

	QString _title;
	DocumentId _iconId = 0;
	base::flat_set<QString> _titleWords;
	base::flat_set<QChar> _titleFirstLetters;
	int _titleVersion = 0;
	int32 _colorId = 0;

	std::unique_ptr<Ui::Text::CustomEmoji> _icon;
	mutable QImage _defaultIcon; // on-demand

	std::optional<HistoryItem*> _lastMessage;
	std::optional<HistoryItem*> _lastServerMessage;
	std::optional<HistoryItem*> _chatListMessage;
	base::flat_set<FullMsgId> _requestedGroups;
	bool _unreadMark = false;

	rpl::lifetime _lifetime;

};

} // namespace Data
