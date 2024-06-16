/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_thread.h"
#include "data/notify/data_peer_notify_settings.h"
#include "base/flags.h"

class ChannelData;
enum class ChatRestriction;

namespace style {
struct ForumTopicIcon;
} // namespace style

namespace Dialogs {
class MainList;
} // namespace Dialogs

namespace Main {
class Session;
} // namespace Main

namespace HistoryView {
class SendActionPainter;
class ListMemento;
} // namespace HistoryView

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
[[nodiscard]] QImage ForumTopicGeneralIconFrame(
	int size,
	const QColor &color);
[[nodiscard]] TextWithEntities ForumTopicIconWithTitle(
	MsgId rootId,
	DocumentId iconId,
	const QString &title);

[[nodiscard]] QString ForumGeneralIconTitle();
[[nodiscard]] bool IsForumGeneralIconTitle(const QString &title);
[[nodiscard]] int32 ForumGeneralIconColor(const QColor &color);
[[nodiscard]] QColor ParseForumGeneralIconColor(int32 value);

struct TopicIconDescriptor {
	QString title;
	int32 colorId = 0;

	[[nodiscard]] bool empty() const {
		return !colorId && title.isEmpty();
	}
	explicit operator bool() const {
		return !empty();
	}
};

[[nodiscard]] QString TopicIconEmojiEntity(TopicIconDescriptor descriptor);
[[nodiscard]] TopicIconDescriptor ParseTopicIconEmojiEntity(
	QStringView entity);

class ForumTopic final : public Thread {
public:
	static constexpr auto kGeneralId = 1;

	ForumTopic(not_null<Forum*> forum, MsgId rootId);
	~ForumTopic();

	not_null<History*> owningHistory() override {
		return history();
	}

	[[nodiscard]] bool isGeneral() const {
		return (_rootId == kGeneralId);
	}

	[[nodiscard]] std::shared_ptr<RepliesList> replies() const;
	[[nodiscard]] not_null<ChannelData*> channel() const;
	[[nodiscard]] not_null<History*> history() const;
	[[nodiscard]] not_null<Forum*> forum() const;
	[[nodiscard]] rpl::producer<> destroyed() const;
	[[nodiscard]] MsgId rootId() const;
	[[nodiscard]] PeerId creatorId() const;
	[[nodiscard]] TimeId creationDate() const;

	[[nodiscard]] not_null<HistoryView::ListMemento*> listMemento();

	[[nodiscard]] bool my() const;
	[[nodiscard]] bool canEdit() const;
	[[nodiscard]] bool canToggleClosed() const;
	[[nodiscard]] bool canTogglePinned() const;
	[[nodiscard]] bool canDelete() const;

	[[nodiscard]] bool closed() const;
	void setClosed(bool closed);
	void setClosedAndSave(bool closed);

	[[nodiscard]] bool hidden() const;
	void setHidden(bool hidden);

	[[nodiscard]] bool creating() const;
	void discard();

	void setRealRootId(MsgId realId);
	void readTillEnd();
	void requestChatListMessage();

	void applyTopic(const MTPDforumTopic &data);

	TimeId adjustedChatListTimeId() const override;

	int fixedOnTopIndex() const override;
	bool shouldBeInChatList() const override;
	Dialogs::UnreadState chatListUnreadState() const override;
	Dialogs::BadgesState chatListBadgesState() const override;
	HistoryItem *chatListMessage() const override;
	bool chatListMessageKnown() const override;
	const QString &chatListName() const override;
	const QString &chatListNameSortKey() const override;
	int chatListNameVersion() const override;
	const base::flat_set<QString> &chatListNameWords() const override;
	const base::flat_set<QChar> &chatListFirstLetters() const override;

	void hasUnreadMentionChanged(bool has) override;
	void hasUnreadReactionChanged(bool has) override;

	[[nodiscard]] HistoryItem *lastMessage() const;
	[[nodiscard]] HistoryItem *lastServerMessage() const;
	[[nodiscard]] bool lastMessageKnown() const;
	[[nodiscard]] bool lastServerMessageKnown() const;
	[[nodiscard]] MsgId lastKnownServerMessageId() const;

	[[nodiscard]] QString title() const;
	[[nodiscard]] TextWithEntities titleWithIcon() const;
	[[nodiscard]] int titleVersion() const;
	void applyTitle(const QString &title);
	[[nodiscard]] DocumentId iconId() const;
	void applyIconId(DocumentId iconId);
	[[nodiscard]] int32 colorId() const;
	void applyColorId(int32 colorId);
	void applyCreator(PeerId creatorId);
	void applyCreationDate(TimeId date);
	void applyIsMy(bool my);
	void applyItemAdded(not_null<HistoryItem*> item);
	void applyItemRemoved(MsgId id);
	void maybeSetLastMessage(not_null<HistoryItem*> item);

	[[nodiscard]] PeerNotifySettings &notify() {
		return _notify;
	}
	[[nodiscard]] const PeerNotifySettings &notify() const {
		return _notify;
	}

	void chatListPreloadData() override;
	void paintUserpic(
		Painter &p,
		Ui::PeerUserpicView &view,
		const Dialogs::Ui::PaintContext &context) const override;
	void clearUserpicLoops();

	[[nodiscard]] bool isServerSideUnread(
		not_null<const HistoryItem*> item) const override;

	void setMuted(bool muted) override;

	[[nodiscard]] auto sendActionPainter()
		->not_null<HistoryView::SendActionPainter*> override;

private:
	enum class Flag : uchar {
		Closed = (1 << 0),
		Hidden = (1 << 1),
		My = (1 << 2),
		HasPinnedMessages = (1 << 3),
		GeneralIconActive = (1 << 4),
		GeneralIconSelected = (1 << 5),
		ResolveChatListMessage = (1 << 6),
	};
	friend inline constexpr bool is_flag_type(Flag) { return true; }
	using Flags = base::flags<Flag>;

	void indexTitleParts();
	void validateDefaultIcon() const;
	void validateGeneralIcon(const Dialogs::Ui::PaintContext &context) const;
	void applyTopicTopMessage(MsgId topMessageId);
	void growLastKnownServerMessageId(MsgId id);
	void invalidateTitleWithIcon();

	void setLastMessage(HistoryItem *item);
	void setLastServerMessage(HistoryItem *item);
	void setChatListMessage(HistoryItem *item);
	void allowChatListMessageResolve();
	void resolveChatListMessageGroup();

	void subscribeToUnreadChanges();
	[[nodiscard]] Dialogs::UnreadState unreadStateFor(
		int count,
		bool known) const;

	const not_null<Forum*> _forum;
	const not_null<Dialogs::MainList*> _list;
	std::shared_ptr<RepliesList> _replies;
	std::unique_ptr<HistoryView::ListMemento> _listMemento;
	std::shared_ptr<HistoryView::SendActionPainter> _sendActionPainter;
	MsgId _rootId = 0;
	MsgId _lastKnownServerMessageId = 0;

	PeerNotifySettings _notify;

	QString _title;
	DocumentId _iconId = 0;
	base::flat_set<QString> _titleWords;
	base::flat_set<QChar> _titleFirstLetters;
	PeerId _creatorId = 0;
	TimeId _creationDate = 0;
	int _titleVersion = 0;
	int32 _colorId = 0;
	mutable Flags _flags;

	std::unique_ptr<Ui::Text::CustomEmoji> _icon;
	mutable QImage _defaultIcon; // on-demand

	std::optional<HistoryItem*> _lastMessage;
	std::optional<HistoryItem*> _lastServerMessage;
	std::optional<HistoryItem*> _chatListMessage;
	base::flat_set<FullMsgId> _requestedGroups;

	rpl::lifetime _lifetime;

};

} // namespace Data
