/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Data {
class Feed;
} // namespace Data

namespace Window {

class Controller;

enum class PeerMenuSource {
	ChatsList,
	History,
	Profile,
};

using PeerMenuCallback = Fn<QAction*(
	const QString &text,
	Fn<void()> handler)>;

void FillPeerMenu(
	not_null<Controller*> controller,
	not_null<PeerData*> peer,
	const PeerMenuCallback &addAction,
	PeerMenuSource source);
void FillFeedMenu(
	not_null<Controller*> controller,
	not_null<Data::Feed*> feed,
	const PeerMenuCallback &addAction,
	PeerMenuSource source);

void PeerMenuAddMuteAction(
	not_null<PeerData*> peer,
	const PeerMenuCallback &addAction);

void PeerMenuExportChat(not_null<PeerData*> peer);
void PeerMenuDeleteContact(not_null<UserData*> user);
void PeerMenuShareContactBox(not_null<UserData*> user);
void PeerMenuAddContact(not_null<UserData*> user);
void PeerMenuAddChannelMembers(not_null<ChannelData*> channel);
//void PeerMenuUngroupFeed(not_null<Data::Feed*> feed); // #feed

//void ToggleChannelGrouping(not_null<ChannelData*> channel, bool group); // #feed
Fn<void()> ClearHistoryHandler(not_null<PeerData*> peer);
Fn<void()> DeleteAndLeaveHandler(not_null<PeerData*> peer);

QPointer<Ui::RpWidget> ShowForwardMessagesBox(
	MessageIdsList &&items,
	FnMut<void()> &&successCallback = nullptr);

} // namespace Window
