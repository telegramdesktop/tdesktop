/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "types.h"

class Application;
class Window;
class MainWidget;
class SettingsWidget;
class ApiWrap;
class Font;
class Color;
class FileUploader;

#include "history.h"

typedef QMap<HistoryItem*, bool> HistoryItemsMap;
typedef QHash<VideoData*, HistoryItemsMap> VideoItems;
typedef QHash<AudioData*, HistoryItemsMap> AudioItems;
typedef QHash<DocumentData*, HistoryItemsMap> DocumentItems;
typedef QHash<WebPageData*, HistoryItemsMap> WebPageItems;

namespace App {
	Application *app();
	Window *wnd();
	MainWidget *main();
	SettingsWidget *settings();
	bool passcoded();
	FileUploader *uploader();
	ApiWrap *api();

	void showSettings();
	void logOut();
	bool loggedOut();

	QString formatPhone(QString phone);

	inline bool isChat(const PeerId &peer) {
		return peer & 0x100000000L;
	}
	PeerId peerFromMTP(const MTPPeer &peer_id);
	PeerId peerFromChat(int32 chat_id);
	inline PeerId peerFromChat(const MTPint &chat_id) {
		return peerFromChat(chat_id.v);
	}
	PeerId peerFromUser(int32 user_id);
	inline PeerId peerFromUser(const MTPint &user_id) {
		return peerFromUser(user_id.v);
	}
	MTPpeer peerToMTP(const PeerId &peer_id);
    int32 userFromPeer(const PeerId &peer_id);
    int32 chatFromPeer(const PeerId &peer_id);

	int32 onlineForSort(int32 online, int32 now);
	int32 onlineWillChangeIn(int32 onlineOnServer, int32 nowOnServer);
	QString onlineText(UserData *user, int32 nowOnServer, bool precise = false);
	bool onlineColorUse(int32 online, int32 now);

	UserData *feedUsers(const MTPVector<MTPUser> &users); // returns last user
	ChatData *feedChats(const MTPVector<MTPChat> &chats); // returns last chat
	void feedParticipants(const MTPChatParticipants &p);
	void feedParticipantAdd(const MTPDupdateChatParticipantAdd &d);
	void feedParticipantDelete(const MTPDupdateChatParticipantDelete &d);
	void feedMsgs(const MTPVector<MTPMessage> &msgs, int msgsState = 0); // 2 - new read message, 1 - new unread message, 0 - not new message, -1 - searched message
	void feedWereRead(const QVector<MTPint> &msgsIds);
	void feedInboxRead(const PeerId &peer, int32 upTo);
	void feedOutboxRead(const PeerId &peer, int32 upTo);
	void feedWereDeleted(const QVector<MTPint> &msgsIds);
	void feedUserLinks(const MTPVector<MTPcontacts_Link> &links);
	void feedUserLink(MTPint userId, const MTPContactLink &myLink, const MTPContactLink &foreignLink);
	int32 maxMsgId();

	ImagePtr image(const MTPPhotoSize &size);

	PhotoData *feedPhoto(const MTPPhoto &photo, const PreparedPhotoThumbs &thumbs);
	PhotoData *feedPhoto(const MTPPhoto &photo, PhotoData *convert = 0);
	PhotoData *feedPhoto(const MTPDphoto &photo, PhotoData *convert = 0);
	VideoData *feedVideo(const MTPDvideo &video, VideoData *convert = 0);
	AudioData *feedAudio(const MTPDaudio &audio, AudioData *convert = 0);
	DocumentData *feedDocument(const MTPdocument &document, const QPixmap &thumb);
	DocumentData *feedDocument(const MTPdocument &document, DocumentData *convert = 0);
	DocumentData *feedDocument(const MTPDdocument &document, DocumentData *convert = 0);
	WebPageData *feedWebPage(const MTPDwebPage &webpage, WebPageData *convert = 0);
	WebPageData *feedWebPage(const MTPDwebPagePending &webpage, WebPageData *convert = 0);
	WebPageData *feedWebPage(const MTPWebPage &webpage);

	UserData *userLoaded(const PeerId &user);
	ChatData *chatLoaded(const PeerId &chat);
	PeerData *peerLoaded(const PeerId &peer);
	UserData *userLoaded(int32 user);
	ChatData *chatLoaded(int32 chat);

	PeerData *peer(const PeerId &peer);
	UserData *user(const PeerId &peer);
	UserData *user(int32 user);
	UserData *self();
	UserData *userByName(const QString &username);
	ChatData *chat(const PeerId &peer);
	ChatData *chat(int32 chat);
	QString peerName(const PeerData *peer, bool forDialogs = false);
	PhotoData *photo(const PhotoId &photo, PhotoData *convert = 0, const uint64 &access = 0, int32 user = 0, int32 date = 0, const ImagePtr &thumb = ImagePtr(), const ImagePtr &medium = ImagePtr(), const ImagePtr &full = ImagePtr());
	VideoData *video(const VideoId &video, VideoData *convert = 0, const uint64 &access = 0, int32 user = 0, int32 date = 0, int32 duration = 0, int32 w = 0, int32 h = 0, const ImagePtr &thumb = ImagePtr(), int32 dc = 0, int32 size = 0);
	AudioData *audio(const AudioId &audio, AudioData *convert = 0, const uint64 &access = 0, int32 user = 0, int32 date = 0, const QString &mime = QString(), int32 duration = 0, int32 dc = 0, int32 size = 0);
	DocumentData *document(const DocumentId &document, DocumentData *convert = 0, const uint64 &access = 0, int32 date = 0, const QVector<MTPDocumentAttribute> &attributes = QVector<MTPDocumentAttribute>(), const QString &mime = QString(), const ImagePtr &thumb = ImagePtr(), int32 dc = 0, int32 size = 0);
	WebPageData *webPage(const WebPageId &webPage, WebPageData *convert = 0, const QString &type = QString(), const QString &url = QString(), const QString &displayUrl = QString(), const QString &siteName = QString(), const QString &title = QString(), const QString &description = QString(), PhotoData *photo = 0, int32 duration = 0, const QString &author = QString(), int32 pendingTill = -2);
	ImageLinkData *imageLink(const QString &imageLink, ImageLinkType type = InvalidImageLink, const QString &url = QString());
	void forgetMedia();

	MTPPhoto photoFromUserPhoto(MTPint userId, MTPint date, const MTPUserProfilePhoto &photo);

	Histories &histories();
	History *history(const PeerId &peer, int32 unreadCnt = 0, int32 maxInboxRead = 0);
	History *historyLoaded(const PeerId &peer);
	HistoryItem *histItemById(MsgId itemId);
	HistoryItem *historyRegItem(HistoryItem *item);
	void historyItemDetached(HistoryItem *item);
	void historyUnregItem(HistoryItem *item);
	void historyClearMsgs();
	void historyClearItems();
	void historyRegReply(HistoryReply *reply, HistoryItem *to);
	void historyUnregReply(HistoryReply *reply, HistoryItem *to);
//	void deleteHistory(const PeerId &peer);

	void historyRegRandom(uint64 randomId, MsgId itemId);
	void historyUnregRandom(uint64 randomId);
	MsgId histItemByRandom(uint64 randomId);

	void hoveredItem(HistoryItem *item);
	HistoryItem *hoveredItem();
	void pressedItem(HistoryItem *item);
	HistoryItem *pressedItem();
	void hoveredLinkItem(HistoryItem *item);
	HistoryItem *hoveredLinkItem();
	void pressedLinkItem(HistoryItem *item);
	HistoryItem *pressedLinkItem();
	void contextItem(HistoryItem *item);
	HistoryItem *contextItem();
	void mousedItem(HistoryItem *item);
	HistoryItem *mousedItem();

	const QPixmap &sprite();
	const QPixmap &emojis();
	const QPixmap &emojiSingle(const EmojiData *emoji, int32 fontHeight);

	void initMedia();
	void deinitMedia(bool completely = true);
	void playSound();

	void checkImageCacheSize();

	bool isValidPhone(QString phone);

	void quit();
	bool quiting();
	void setQuiting();

    QImage readImage(QByteArray data, QByteArray *format = 0, bool opaque = true, bool *animated = 0);
	QImage readImage(const QString &file, QByteArray *format = 0, bool opaque = true, bool *animated = 0, QByteArray *content = 0);

	void regVideoItem(VideoData *data, HistoryItem *item);
	void unregVideoItem(VideoData *data, HistoryItem *item);
	const VideoItems &videoItems();

	void regAudioItem(AudioData *data, HistoryItem *item);
	void unregAudioItem(AudioData*data, HistoryItem *item);
	const AudioItems &audioItems();

	void regDocumentItem(DocumentData *data, HistoryItem *item);
	void unregDocumentItem(DocumentData *data, HistoryItem *item);
	const DocumentItems &documentItems();

	void regWebPageItem(WebPageData *data, HistoryItem *item);
	void unregWebPageItem(WebPageData *data, HistoryItem *item);
	const WebPageItems &webPageItems();

	void regMuted(PeerData *peer, int32 changeIn);
	void unregMuted(PeerData *peer);
	void updateMuted();

	void setProxySettings(QNetworkAccessManager &manager);
	void setProxySettings(QTcpSocket &socket);

	void searchByHashtag(const QString &tag);
	void openUserByName(const QString &username, bool toProfile = false);
	void joinGroupByHash(const QString &hash);
	void openLocalUrl(const QString &url);

	void initBackground(int32 id = DefaultChatBackground, const QImage &p = QImage(), bool nowrite = false);

	style::color msgServiceBG();
	style::color historyScrollBarColor();
	style::color historyScrollBgColor();
	style::color historyScrollBarOverColor();
	style::color historyScrollBgOverColor();
	style::color introPointHoverColor();

	struct WallPaper {
		WallPaper(int32 id, ImagePtr thumb, ImagePtr full) : id(id), thumb(thumb), full(full) {
		}
		int32 id;
		ImagePtr thumb;
		ImagePtr full;
	};
	typedef QList<WallPaper> WallPapers;
	DeclareSetting(WallPapers, ServerBackgrounds);

};
