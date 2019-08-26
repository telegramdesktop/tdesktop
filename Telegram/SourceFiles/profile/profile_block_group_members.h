/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "profile/profile_block_peer_list.h"

namespace Ui {
class FlatLabel;
} // namespace Ui

namespace Notify {
struct PeerUpdate;
} // namespace Notify

namespace Profile {

class GroupMembersWidget : public PeerListWidget {
	Q_OBJECT

public:
	GroupMembersWidget(QWidget *parent, PeerData *peer, const style::PeerListItem &st);

	int onlineCount() const {
		return _onlineCount;
	}

	~GroupMembersWidget();

signals:
	void onlineCountUpdated(int onlineCount);

private slots:
	void onUpdateOnlineDisplay();

private:
	// Observed notifications.
	void notifyPeerUpdated(const Notify::PeerUpdate &update);

	void removePeer(PeerData *selectedPeer);
	void refreshMembers();
	void fillChatMembers(not_null<ChatData*> chat);
	void fillMegagroupMembers(not_null<ChannelData*> megagroup);
	void sortMembers();
	void updateOnlineCount();
	void checkSelfAdmin(not_null<ChatData*> chat);
	void preloadMore();

	void refreshUserOnline(UserData *user);

	struct Member : public Item {
		explicit Member(UserData *user);
		UserData *user() const;

		TimeId onlineTextTill = 0;
		TimeId onlineTill = 0;
		TimeId onlineForSort = 0;
	};
	Member *getMember(Item *item) {
		return static_cast<Member*>(item);
	}

	void updateItemStatusText(Item *item);
	not_null<Member*> computeMember(not_null<UserData*> user);
	not_null<Member*> addUser(not_null<ChatData*> chat, not_null<UserData*> user);
	not_null<Member*> addUser(not_null<ChannelData*> megagroup, not_null<UserData*> user);
	void setItemFlags(not_null<Item*> item, not_null<ChatData*> chat);
	void setItemFlags(
		not_null<Item*> item,
		not_null<ChannelData*> megagroup);
	bool addUsersToEnd(not_null<ChannelData*> megagroup);

	QMap<UserData*, Member*> _membersByUser;
	bool _sortByOnline = false;
	TimeId _now = 0;

	int _onlineCount = 0;
	TimeId _updateOnlineAt = 0;
	QTimer _updateOnlineTimer;

};

} // namespace Profile
