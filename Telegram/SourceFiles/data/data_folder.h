/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "dialogs/dialogs_entry.h"
#include "dialogs/dialogs_indexed_list.h"
#include "data/data_messages.h"

class ChannelData;
class AuthSession;

namespace Data {

class Session;
class Folder;

enum class FolderUpdateFlag {
	List,
};

struct FolderUpdate {
	not_null<Data::Folder*> folder;
	FolderUpdateFlag flag;
};

//MessagePosition FeedPositionFromMTP(const MTPFeedPosition &position); // #feed

class Folder final : public Dialogs::Entry {
public:
	static constexpr auto kId = 1;

	Folder(not_null<Data::Session*> owner, FolderId id);
	Folder(const Folder &) = delete;
	Folder &operator=(const Folder &) = delete;

	FolderId id() const;
	void registerOne(not_null<History*> history);
	void unregisterOne(not_null<History*> history);

	not_null<Dialogs::IndexedList*> chatsList(Dialogs::Mode list);

	void applyDialog(const MTPDdialogFolder &data);
	void setUnreadCounts(int unreadNonMutedCount, int unreadMutedCount);
	//void setUnreadPosition(const MessagePosition &position); // #feed
	void unreadCountChanged(
		int unreadCountDelta,
		int mutedCountDelta);
	rpl::producer<int> unreadCountValue() const;
	//MessagePosition unreadPosition() const; // #feed
	//rpl::producer<MessagePosition> unreadPositionChanges() const; // #feed

	//void setUnreadMark(bool unread);
	//bool unreadMark() const;
	//int unreadCountForBadge() const; // unreadCount || unreadMark ? 1 : 0.

	TimeId adjustedChatListTimeId() const override;
	int unreadCount() const;
	bool unreadCountKnown() const;

	bool useProxyPromotion() const override;
	bool toImportant() const override;
	bool shouldBeInChatList() const override;
	int chatListUnreadCount() const override;
	bool chatListUnreadMark() const override;
	bool chatListMutedBadge() const override;
	HistoryItem *chatListMessage() const override;
	bool chatListMessageKnown() const override;
	void requestChatListMessage() override;
	const QString &chatListName() const override;
	const base::flat_set<QString> &chatListNameWords() const override;
	const base::flat_set<QChar> &chatListFirstLetters() const override;
	void changedInChatListHook(Dialogs::Mode list, bool added) override;

	void loadUserpic() override;
	void paintUserpic(
		Painter &p,
		int x,
		int y,
		int size) const override;

	bool chatsListLoaded() const;
	void setChatsListLoaded(bool loaded);
	//int32 chatsHash() const;
	//void setChats(std::vector<not_null<PeerData*>> chats); // #feed

private:
	void indexNameParts();
	//void changeChatsList(
	//	const std::vector<not_null<PeerData*>> &add,
	//	const std::vector<not_null<PeerData*>> &remove);

	template <typename PerformUpdate>
	void updateUnreadCounts(PerformUpdate &&performUpdate);

	FolderId _id = 0;
	Dialogs::IndexedList _chatsList;
	Dialogs::IndexedList _importantChatsList;
	bool _chatsListLoaded = false;

	QString _name;
	base::flat_set<QString> _nameWords;
	base::flat_set<QChar> _nameFirstLetters;

	//rpl::variable<MessagePosition> _unreadPosition;
	std::optional<int> _unreadCount;
	rpl::event_stream<int> _unreadCountChanges;
	int _unreadMutedCount = 0;
	//bool _unreadMark = false;

};

} // namespace Data
