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

namespace Data {

class Session;
class Feed;

enum class FeedUpdateFlag {
	Channels,
	ChannelPhoto,
};

struct FeedUpdate {
	not_null<Data::Feed*> feed;
	FeedUpdateFlag flag;
};

//MessagePosition FeedPositionFromMTP(const MTPFeedPosition &position); // #feed

class Feed : public Dialogs::Entry {
public:
	static constexpr auto kId = 1;
	static constexpr auto kChannelsLimit = 1000;

	Feed(FeedId id, not_null<Data::Session*> parent);

	FeedId id() const;
	void registerOne(not_null<ChannelData*> channel);
	void unregisterOne(not_null<ChannelData*> channel);

	void updateLastMessage(not_null<HistoryItem*> item);
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

	HistoryItem *lastMessage() const;
	bool lastMessageKnown() const;
	int unreadCount() const;
	bool unreadCountKnown() const;

	bool useProxyPromotion() const override;
	bool toImportant() const override;
	bool shouldBeInChatList() const override;
	int chatListUnreadCount() const override;
	bool chatListUnreadMark() const override;
	bool chatListMutedBadge() const override;
	HistoryItem *chatsListItem() const override;
	const QString &chatsListName() const override;
	const base::flat_set<QString> &chatsListNameWords() const override;
	const base::flat_set<QChar> &chatsListFirstLetters() const override;
	void changedInChatListHook(Dialogs::Mode list, bool added) override;

	void loadUserpic() override;
	void paintUserpic(
		Painter &p,
		int x,
		int y,
		int size) const override;

	const std::vector<not_null<History*>> &channels() const;
	int32 channelsHash() const;
	bool channelsLoaded() const;
	void setChannelsLoaded(bool loaded);
	void setChannels(std::vector<not_null<ChannelData*>> channels);

private:
	void indexNameParts();
	void recountLastMessage();
	void setLastMessageFromChannels();
	bool justUpdateLastMessage(not_null<HistoryItem*> item);
	void updateChatsListDate();
	void changeChannelsList(
		const std::vector<not_null<ChannelData*>> &add,
		const std::vector<not_null<ChannelData*>> &remove);

	FeedId _id = 0;
	not_null<Data::Session*> _parent;
	std::vector<not_null<History*>> _channels;
	bool _settingChannels = false;
	bool _channelsLoaded = false;

	QString _name;
	base::flat_set<QString> _nameWords;
	base::flat_set<QChar> _nameFirstLetters;
	base::optional<HistoryItem*> _lastMessage;

	rpl::variable<MessagePosition> _unreadPosition;
	base::optional<int> _unreadCount;
	rpl::event_stream<int> _unreadCountChanges;
	int _unreadMutedCount = 0;

};

} // namespace Data
