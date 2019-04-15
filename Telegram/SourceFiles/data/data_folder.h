/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "dialogs/dialogs_entry.h"
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

class Folder : public Dialogs::Entry {
public:
	static constexpr auto kId = 1;

	Folder(not_null<Data::Session*> owner, FolderId id);
	Folder(const Folder &) = delete;
	Folder &operator=(const Folder &) = delete;

	Data::Session &owner() const;
	AuthSession &session() const;

	FolderId id() const;
	void registerOne(not_null<PeerData*> peer);
	void unregisterOne(not_null<PeerData*> peer);

	void updateChatListMessage(not_null<HistoryItem*> item);
	void messageRemoved(not_null<HistoryItem*> item);
	void historyCleared(not_null<History*> history);

	//void applyDialog(const MTPDdialogFeed &data); // #feed
	void setUnreadCounts(int unreadNonMutedCount, int unreadMutedCount);
	void setUnreadPosition(const MessagePosition &position);
	void unreadCountChanged(
		int unreadCountDelta,
		int mutedCountDelta);
	rpl::producer<int> unreadCountValue() const;
	MessagePosition unreadPosition() const;
	rpl::producer<MessagePosition> unreadPositionChanges() const;

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

	const std::vector<not_null<History*>> &chats() const;
	int32 chatsHash() const;
	bool chatsLoaded() const;
	void setChatsLoaded(bool loaded);
	void setChats(std::vector<not_null<PeerData*>> chats);

private:
	void indexNameParts();
	void recountChatListMessage();
	void setChatListMessageFromChannels();
	bool justUpdateChatListMessage(not_null<HistoryItem*> item);
	void updateChatListDate();
	void changeChatsList(
		const std::vector<not_null<PeerData*>> &add,
		const std::vector<not_null<PeerData*>> &remove);

	template <typename PerformUpdate>
	void updateUnreadCounts(PerformUpdate &&performUpdate);

	FolderId _id = 0;
	not_null<Data::Session*> _owner;
	std::vector<not_null<History*>> _chats;
	bool _settingChats = false;
	bool _chatsLoaded = false;

	QString _name;
	base::flat_set<QString> _nameWords;
	base::flat_set<QChar> _nameFirstLetters;
	std::optional<HistoryItem*> _chatListMessage;

	rpl::variable<MessagePosition> _unreadPosition;
	std::optional<int> _unreadCount;
	rpl::event_stream<int> _unreadCountChanges;
	int _unreadMutedCount = 0;

};

} // namespace Data
