/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#pragma once

#include "types.h"

class Application;
class Window;
class MainWidget;
class Settings;
class Font;
class Color;
class FileUploader;

#include "history.h"

typedef QMap<HistoryItem*, bool> HistoryItemsMap;
typedef QHash<VideoData*, HistoryItemsMap> VideoItems;
typedef QHash<AudioData*, HistoryItemsMap> AudioItems;
typedef QHash<DocumentData*, HistoryItemsMap> DocumentItems;

namespace App {
	Application *app();
	Window *wnd();
	MainWidget *main();
	Settings *settings();
	FileUploader *uploader();

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

	int32 onlineWillChangeIn(int32 onlineOnServer, int32 nowOnServer);
	QString onlineText(int32 onlineOnServer, int32 nowOnServer);

	void feedUsers(const MTPVector<MTPUser> &users);
	void feedChats(const MTPVector<MTPChat> &chats);
	void feedParticipants(const MTPChatParticipants &p);
	void feedParticipantAdd(const MTPDupdateChatParticipantAdd &d);
	void feedParticipantDelete(const MTPDupdateChatParticipantDelete &d);
	void feedMsgs(const MTPVector<MTPMessage> &msgs, bool newMsgs = false);
	void feedWereRead(const QVector<MTPint> &msgsIds);
	void feedWereDeleted(const QVector<MTPint> &msgsIds);
	void feedUserLinks(const MTPVector<MTPcontacts_Link> &links);
	void feedUserLink(MTPint userId, const MTPcontacts_MyLink &myLink, const MTPcontacts_ForeignLink &foreignLink);
	void feedMessageMedia(MsgId msgId, const MTPMessage &msg);
	int32 maxMsgId();

	PhotoData *feedPhoto(const MTPPhoto &photo, const PreparedPhotoThumbs &thumbs);
	PhotoData *feedPhoto(const MTPPhoto &photo, PhotoData *convert = 0);
	PhotoData *feedPhoto(const MTPDphoto &photo, PhotoData *convert = 0);
	VideoData *feedVideo(const MTPDvideo &video, VideoData *convert = 0);
	AudioData *feedAudio(const MTPDaudio &audio, AudioData *convert = 0);
	DocumentData *feedDocument(const MTPdocument &document, const QPixmap &thumb);
	DocumentData *feedDocument(const MTPdocument &document, DocumentData *convert = 0);
	DocumentData *feedDocument(const MTPDdocument &document, DocumentData *convert = 0);

	UserData *userLoaded(const PeerId &user);
	ChatData *chatLoaded(const PeerId &chat);
	PeerData *peerLoaded(const PeerId &peer);
	UserData *userLoaded(int32 user);
	ChatData *chatLoaded(int32 chat);

	PeerData *peer(const PeerId &peer);
	UserData *user(const PeerId &peer);
	UserData *user(int32 user);
	UserData *self();
	ChatData *chat(const PeerId &peer);
	ChatData *chat(int32 chat);
	QString peerName(const PeerData *peer, bool forDialogs = false);
	PhotoData *photo(const PhotoId &photo, PhotoData *convert = 0, const uint64 &access = 0, int32 user = 0, int32 date = 0, const ImagePtr &thumb = ImagePtr(), const ImagePtr &full = ImagePtr());
	void forgetPhotos();
	VideoData *video(const VideoId &video, VideoData *convert = 0, const uint64 &access = 0, int32 user = 0, int32 date = 0, int32 duration = 0, int32 w = 0, int32 h = 0, const ImagePtr &thumb = ImagePtr(), int32 dc = 0, int32 size = 0);
	void forgetVideos();
	AudioData *audio(const AudioId &audio, AudioData *convert = 0, const uint64 &access = 0, int32 user = 0, int32 date = 0, int32 duration = 0, int32 dc = 0, int32 size = 0);
	void forgetAudios();
	DocumentData *document(const DocumentId &document, DocumentData *convert = 0, const uint64 &access = 0, int32 user = 0, int32 date = 0, const QString &name = QString(), const QString &mime = QString(), const ImagePtr &thumb = ImagePtr(), int32 dc = 0, int32 size = 0);
	void forgetDocuments();

	MTPPhoto photoFromUserPhoto(MTPint userId, MTPint date, const MTPUserProfilePhoto &photo);

	Histories &histories();
	History *history(const PeerId &peer, int32 unreadCnt = 0);
	History *historyLoaded(const PeerId &peer);
	HistoryItem *histItemById(MsgId itemId);
	bool historyRegItem(HistoryItem *item);
	void historyUnregItem(HistoryItem *item);
	void historyClearMsgs();
	void historyClearItems();
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

	QPixmap &sprite();
	QPixmap &emojis();
	const QPixmap &emojiSingle(const EmojiData *emoji, int32 fontHeight);

	void initMedia();
	void deinitMedia(bool completely = true);
	void playSound();

	void writeConfig();
	void readConfig();
	void writeUserConfig();
	void readUserConfig();

	void muteHistory(History *history);
	void unmuteHistory(History *history);
	void writeAllMuted(QDataStream &stream);
	void readAllMuted(QDataStream &stream);
	void readOneMuted(QDataStream &stream);
	bool isPeerMuted(const PeerId &peer);

	void checkImageCacheSize();

	bool isValidPhone(QString phone);

	void quit();
	bool quiting();
	void setQuiting();


    QImage readImage(QByteArray data, QByteArray *format = 0);
    QImage readImage(const QString &file, QByteArray *format = 0);

	void regVideoItem(VideoData *data, HistoryItem *item);
	void unregVideoItem(VideoData *data, HistoryItem *item);
	const VideoItems &videoItems();

	void regAudioItem(AudioData *data, HistoryItem *item);
	void unregAudioItem(AudioData*data, HistoryItem *item);
	const AudioItems &audioItems();

	void regDocumentItem(DocumentData *data, HistoryItem *item);
	void unregDocumentItem(DocumentData *data, HistoryItem *item);
	const DocumentItems &documentItems();

	void setProxySettings(QNetworkAccessManager &manager);
	void setProxySettings(QTcpSocket &socket);

};
