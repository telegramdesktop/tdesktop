/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class GenericBox;

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Data {
class Folder;
} // namespace Data

namespace Window {

class Controller;
class SessionController;
class SessionNavigation;

enum class PeerMenuSource {
	ChatsList,
	History,
	Profile,
};

using PeerMenuCallback = Fn<QAction*(
	const QString &text,
	Fn<void()> handler)>;

void FillPeerMenu(
	not_null<SessionController*> controller,
	not_null<PeerData*> peer,
	const PeerMenuCallback &addAction,
	PeerMenuSource source);
void FillFolderMenu(
	not_null<SessionController*> controller,
	not_null<Data::Folder*> folder,
	const PeerMenuCallback &addAction,
	PeerMenuSource source);

void PeerMenuAddMuteAction(
	not_null<PeerData*> peer,
	const PeerMenuCallback &addAction);

void PeerMenuExportChat(not_null<PeerData*> peer);
void PeerMenuDeleteContact(not_null<UserData*> user);
void PeerMenuShareContactBox(
	not_null<Window::SessionNavigation*> navigation,
	not_null<UserData*> user);
void PeerMenuAddChannelMembers(
	not_null<Window::SessionNavigation*> navigation,
	not_null<ChannelData*> channel);
//void PeerMenuUngroupFeed(not_null<Data::Feed*> feed); // #feed
void PeerMenuCreatePoll(not_null<PeerData*> peer);
void PeerMenuBlockUserBox(
	not_null<GenericBox*> box,
	not_null<Window::Controller*> window,
	not_null<UserData*> user,
	bool suggestClearChat);
void PeerMenuUnblockUserWithBotRestart(not_null<UserData*> user);

void ToggleHistoryArchived(not_null<History*> history, bool archived);
Fn<void()> ClearHistoryHandler(not_null<PeerData*> peer);
Fn<void()> DeleteAndLeaveHandler(not_null<PeerData*> peer);

QPointer<Ui::RpWidget> ShowForwardMessagesBox(
	not_null<Window::SessionNavigation*> navigation,
	MessageIdsList &&items,
	FnMut<void()> &&successCallback = nullptr);

QPointer<Ui::RpWidget> ShowSendNowMessagesBox(
	not_null<Window::SessionNavigation*> navigation,
	not_null<History*> history,
	MessageIdsList &&items,
	FnMut<void()> &&successCallback = nullptr);

} // namespace Window
