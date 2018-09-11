/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_types.h"
#include "data/data_peer.h"

enum NewMessageType : char;
class Messenger;
class MainWindow;
class MainWidget;
class LocationCoords;
struct LocationData;
class HistoryItem;
class History;
class Histories;
namespace HistoryView {
class Element;
} // namespace HistoryView

using HistoryItemsMap = base::flat_set<not_null<HistoryItem*>>;
using GifItems = QHash<Media::Clip::Reader*, HistoryItem*>;

enum RoundCorners {
	SmallMaskCorners = 0x00, // for images
	LargeMaskCorners,

	BoxCorners,
	MenuCorners,
	BotKbOverCorners,
	StickerCorners,
	StickerSelectedCorners,
	SelectedOverlaySmallCorners,
	SelectedOverlayLargeCorners,
	DateCorners,
	DateSelectedCorners,
	ForwardCorners,
	MediaviewSaveCorners,
	EmojiHoverCorners,
	StickerHoverCorners,
	BotKeyboardCorners,
	PhotoSelectOverlayCorners,

	Doc1Corners,
	Doc2Corners,
	Doc3Corners,
	Doc4Corners,

	InShadowCorners, // for photos without bg
	InSelectedShadowCorners,

	MessageInCorners, // with shadow
	MessageInSelectedCorners,
	MessageOutCorners,
	MessageOutSelectedCorners,

	RoundCornersCount
};

namespace App {
	MainWindow *wnd();
	MainWidget *main();

	QString formatPhone(QString phone);

	UserData *feedUser(const MTPUser &user);
	UserData *feedUsers(const MTPVector<MTPUser> &users); // returns last user
	PeerData *feedChat(const MTPChat &chat);
	PeerData *feedChats(const MTPVector<MTPChat> &chats); // returns last chat

	void feedParticipants(const MTPChatParticipants &p, bool requestBotInfos);
	void feedParticipantAdd(const MTPDupdateChatParticipantAdd &d);
	void feedParticipantDelete(const MTPDupdateChatParticipantDelete &d);
	void feedChatAdmins(const MTPDupdateChatAdmins &d);
	void feedParticipantAdmin(const MTPDupdateChatParticipantAdmin &d);
	bool checkEntitiesAndViewsUpdate(const MTPDmessage &m); // returns true if item found and it is not detached
	void updateEditedMessage(const MTPMessage &m);
	void addSavedGif(DocumentData *doc);
	void checkSavedGif(HistoryItem *item);
	void feedMsgs(const QVector<MTPMessage> &msgs, NewMessageType type);
	void feedMsgs(const MTPVector<MTPMessage> &msgs, NewMessageType type);
	void feedInboxRead(const PeerId &peer, MsgId upTo);
	void feedOutboxRead(const PeerId &peer, MsgId upTo, TimeId when);
	void feedWereDeleted(ChannelId channelId, const QVector<MTPint> &msgsIds);
	void feedUserLink(MTPint userId, const MTPContactLink &myLink, const MTPContactLink &foreignLink);

	ImagePtr image(const MTPPhotoSize &size);

	PeerData *peer(const PeerId &id, PeerData::LoadedStatus restriction = PeerData::NotLoaded);
	inline UserData *user(const PeerId &id, PeerData::LoadedStatus restriction = PeerData::NotLoaded) {
		return asUser(peer(id, restriction));
	}
	inline ChatData *chat(const PeerId &id, PeerData::LoadedStatus restriction = PeerData::NotLoaded) {
		return asChat(peer(id, restriction));
	}
	inline ChannelData *channel(const PeerId &id, PeerData::LoadedStatus restriction = PeerData::NotLoaded) {
		return asChannel(peer(id, restriction));
	}
	inline UserData *user(UserId userId, PeerData::LoadedStatus restriction = PeerData::NotLoaded) {
		return asUser(peer(peerFromUser(userId), restriction));
	}
	inline ChatData *chat(ChatId chatId, PeerData::LoadedStatus restriction = PeerData::NotLoaded) {
		return asChat(peer(peerFromChat(chatId), restriction));
	}
	inline ChannelData *channel(ChannelId channelId, PeerData::LoadedStatus restriction = PeerData::NotLoaded) {
		return asChannel(peer(peerFromChannel(channelId), restriction));
	}
	inline PeerData *peerLoaded(const PeerId &id) {
		return peer(id, PeerData::FullLoaded);
	}
	inline UserData *userLoaded(const PeerId &id) {
		return user(id, PeerData::FullLoaded);
	}
	inline ChatData *chatLoaded(const PeerId &id) {
		return chat(id, PeerData::FullLoaded);
	}
	inline ChannelData *channelLoaded(const PeerId &id) {
		return channel(id, PeerData::FullLoaded);
	}
	inline UserData *userLoaded(UserId userId) {
		return user(userId, PeerData::FullLoaded);
	}
	inline ChatData *chatLoaded(ChatId chatId) {
		return chat(chatId, PeerData::FullLoaded);
	}
	inline ChannelData *channelLoaded(ChannelId channelId) {
		return channel(channelId, PeerData::FullLoaded);
	}
	void enumerateUsers(Fn<void(not_null<UserData*>)> action);
	void enumerateChatsChannels(
		Fn<void(not_null<PeerData*>)> action);

	PeerData *peerByName(const QString &username);
	QString peerName(const PeerData *peer, bool forDialogs = false);

	LocationData *location(const LocationCoords &coords);
	void forgetMedia();

	Histories &histories();
	not_null<History*> history(const PeerId &peer);
	History *historyLoaded(const PeerId &peer);
	HistoryItem *histItemById(ChannelId channelId, MsgId itemId);
	inline not_null<History*> history(const PeerData *peer) {
		Assert(peer != nullptr);
		return history(peer->id);
	}
	inline History *historyLoaded(const PeerData *peer) {
		return peer ? historyLoaded(peer->id) : nullptr;
	}
	inline HistoryItem *histItemById(const ChannelData *channel, MsgId itemId) {
		return histItemById(channel ? peerToChannel(channel->id) : 0, itemId);
	}
	inline HistoryItem *histItemById(const FullMsgId &msgId) {
		return histItemById(msgId.channel, msgId.msg);
	}
	void historyRegItem(not_null<HistoryItem*> item);
	void historyUnregItem(not_null<HistoryItem*> item);
	void historyUpdateDependent(not_null<HistoryItem*> item);
	void historyClearMsgs();
	void historyClearItems();
	void historyRegDependency(HistoryItem *dependent, HistoryItem *dependency);
	void historyUnregDependency(HistoryItem *dependent, HistoryItem *dependency);

	void historyRegRandom(uint64 randomId, const FullMsgId &itemId);
	void historyUnregRandom(uint64 randomId);
	FullMsgId histItemByRandom(uint64 randomId);
	void historyRegSentData(uint64 randomId, const PeerId &peerId, const QString &text);
	void historyUnregSentData(uint64 randomId);
	void histSentDataByItem(uint64 randomId, PeerId &peerId, QString &text);

	void hoveredItem(HistoryView::Element *item);
	HistoryView::Element *hoveredItem();
	void pressedItem(HistoryView::Element *item);
	HistoryView::Element *pressedItem();
	void hoveredLinkItem(HistoryView::Element *item);
	HistoryView::Element *hoveredLinkItem();
	void pressedLinkItem(HistoryView::Element *item);
	HistoryView::Element *pressedLinkItem();
	void mousedItem(HistoryView::Element *item);
	HistoryView::Element *mousedItem();
	void clearMousedItems();

	const style::font &monofont();
	const QPixmap &emoji();
	const QPixmap &emojiLarge();
	const QPixmap &emojiSingle(EmojiPtr emoji, int32 fontHeight);

	void clearHistories();

	void initMedia();
	void deinitMedia();

	void checkImageCacheSize();

	bool isValidPhone(QString phone);

	enum LaunchState {
		Launched = 0,
		QuitRequested = 1,
		QuitProcessed = 2,
	};
	void quit();
	bool quitting();
	LaunchState launchState();
	void setLaunchState(LaunchState state);
	void restart();

	constexpr auto kFileSizeLimit = 1500 * 1024 * 1024; // Load files up to 1500mb
	constexpr auto kImageSizeLimit = 64 * 1024 * 1024; // Open images up to 64mb jpg/png/gif
	QImage readImage(QByteArray data, QByteArray *format = nullptr, bool opaque = true, bool *animated = nullptr);
	QImage readImage(const QString &file, QByteArray *format = nullptr, bool opaque = true, bool *animated = nullptr, QByteArray *content = 0);
	QPixmap pixmapFromImageInPlace(QImage &&image);

	void complexOverlayRect(Painter &p, QRect rect, ImageRoundRadius radius, RectParts corners);
	void complexLocationRect(Painter &p, QRect rect, ImageRoundRadius radius, RectParts corners);

	QImage *cornersMask(ImageRoundRadius radius);
	void roundRect(Painter &p, int32 x, int32 y, int32 w, int32 h, style::color bg, RoundCorners index, const style::color *shadow = nullptr, RectParts parts = RectPart::Full);
	inline void roundRect(Painter &p, const QRect &rect, style::color bg, RoundCorners index, const style::color *shadow = nullptr, RectParts parts = RectPart::Full) {
		return roundRect(p, rect.x(), rect.y(), rect.width(), rect.height(), bg, index, shadow, parts);
	}
	void roundShadow(Painter &p, int32 x, int32 y, int32 w, int32 h, style::color shadow, RoundCorners index, RectParts parts = RectPart::Full);
	inline void roundShadow(Painter &p, const QRect &rect, style::color shadow, RoundCorners index, RectParts parts = RectPart::Full) {
		return roundShadow(p, rect.x(), rect.y(), rect.width(), rect.height(), shadow, index, parts);
	}
	void roundRect(Painter &p, int32 x, int32 y, int32 w, int32 h, style::color bg, ImageRoundRadius radius, RectParts parts = RectPart::Full);
	inline void roundRect(Painter &p, const QRect &rect, style::color bg, ImageRoundRadius radius, RectParts parts = RectPart::Full) {
		return roundRect(p, rect.x(), rect.y(), rect.width(), rect.height(), bg, radius, parts);
	}

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
