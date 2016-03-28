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

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

class LayeredWidget;

namespace App {

	void sendBotCommand(const QString &cmd, MsgId replyTo = 0);
	bool insertBotCommand(const QString &cmd, bool specialGif = false);
	void activateBotCommand(const HistoryMessageReplyMarkup::Button &button, MsgId replyTo = 0);
	void searchByHashtag(const QString &tag, PeerData *inPeer);
	void openPeerByName(const QString &username, MsgId msgId = ShowAtUnreadMsgId, const QString &startToken = QString());
	void joinGroupByHash(const QString &hash);
	void stickersBox(const QString &name);
	void openLocalUrl(const QString &url);
	bool forward(const PeerId &peer, ForwardWhatMessages what);
	void removeDialog(History *history);
	void showSettings();

	void activateTextLink(TextLinkPtr link, Qt::MouseButton button);

};

namespace Ui {

	void showStickerPreview(DocumentData *sticker);
	void hideStickerPreview();

	void showLayer(LayeredWidget *box, ShowLayerOptions options = CloseOtherLayers);
	void hideLayer(bool fast = false);
	bool isLayerShown();
	bool isMediaViewShown();
	bool isInlineItemBeingChosen();

	void repaintHistoryItem(const HistoryItem *item);
	void repaintInlineItem(const LayoutInlineItem *layout);
	bool isInlineItemVisible(const LayoutInlineItem *reader);
	void autoplayMediaInlineAsync(const FullMsgId &msgId);

	void showPeerHistory(const PeerId &peer, MsgId msgId, bool back = false);
	inline void showPeerHistory(const PeerData *peer, MsgId msgId, bool back = false) {
		showPeerHistory(peer->id, msgId, back);
	}
	inline void showPeerHistory(const History *history, MsgId msgId, bool back = false) {
		showPeerHistory(history->peer->id, msgId, back);
	}
	inline void showPeerHistoryAtItem(const HistoryItem *item) {
		showPeerHistory(item->history()->peer->id, item->id);
	}
	void showPeerHistoryAsync(const PeerId &peer, MsgId msgId);
	inline void showChatsList() {
		showPeerHistory(PeerId(0), 0);
	}
	inline void showChatsListAsync() {
		showPeerHistoryAsync(PeerId(0), 0);
	}

	bool hideWindowNoQuit();

};

enum ClipStopperType {
	ClipStopperMediaview,
	ClipStopperSavedGifsPanel,
};

namespace Notify {

	void userIsBotChanged(UserData *user);
	void userIsContactChanged(UserData *user, bool fromThisApp = false);
	void botCommandsChanged(UserData *user);

	void inlineBotRequesting(bool requesting);

	void migrateUpdated(PeerData *peer);

	void clipStopperHidden(ClipStopperType type);

	void historyItemLayoutChanged(const HistoryItem *item);

	void automaticLoadSettingsChangedGif();

	// handle pending resize() / paint() on history items
	void handlePendingHistoryUpdate();

};

#define DeclareReadOnlyVar(Type, Name) const Type &Name();
#define DeclareRefVar(Type, Name) DeclareReadOnlyVar(Type, Name) \
	Type &Ref##Name();
#define DeclareVar(Type, Name) DeclareRefVar(Type, Name) \
	void Set##Name(const Type &Name);

namespace Sandbox {

	bool CheckBetaVersionDir();
	void WorkingDirReady();

	void start();
	void finish();

	uint64 UserTag();

	DeclareReadOnlyVar(QString, LangSystemISO);
	DeclareReadOnlyVar(int32, LangSystem);
	DeclareVar(QByteArray, LastCrashDump);
	DeclareVar(ConnectionProxy, PreLaunchProxy);

}

namespace Adaptive {
	enum Layout {
		OneColumnLayout,
		NormalLayout,
		WideLayout,
	};
};

namespace DebugLogging {
	enum Flags {
		FileLoaderFlag = 0x00000001,
	};
}

namespace Stickers {
	static const uint64 DefaultSetId = 0; // for backward compatibility
	static const uint64 CustomSetId = 0xFFFFFFFFFFFFFFFFULL, RecentSetId = 0xFFFFFFFFFFFFFFFEULL;
	static const uint64 NoneSetId = 0xFFFFFFFFFFFFFFFDULL; // for emoji/stickers panel
	struct Set {
		Set(uint64 id, uint64 access, const QString &title, const QString &shortName, int32 count, int32 hash, MTPDstickerSet::Flags flags) : id(id), access(access), title(title), shortName(shortName), count(count), hash(hash), flags(flags) {
		}
		uint64 id, access;
		QString title, shortName;
		int32 count, hash;
		MTPDstickerSet::Flags flags;
		StickerPack stickers;
		StickersByEmojiMap emoji;
	};
	typedef QMap<uint64, Set> Sets;
	typedef QList<uint64> Order;
}

namespace Global {

	bool started();
	void start();
	void finish();

	DeclareReadOnlyVar(uint64, LaunchId);
	DeclareRefVar(SingleDelayedCall, HandleHistoryUpdate);

	DeclareVar(Adaptive::Layout, AdaptiveLayout);
	DeclareVar(bool, AdaptiveForWide);

	DeclareVar(int32, DebugLoggingFlags);

	// config
	DeclareVar(int32, ChatSizeMax);
	DeclareVar(int32, MegagroupSizeMax);
	DeclareVar(int32, ForwardedCountMax);
	DeclareVar(int32, OnlineUpdatePeriod);
	DeclareVar(int32, OfflineBlurTimeout);
	DeclareVar(int32, OfflineIdleTimeout);
	DeclareVar(int32, OnlineFocusTimeout); // not from config
	DeclareVar(int32, OnlineCloudTimeout);
	DeclareVar(int32, NotifyCloudDelay);
	DeclareVar(int32, NotifyDefaultDelay);
	DeclareVar(int32, ChatBigSize);
	DeclareVar(int32, PushChatPeriod);
	DeclareVar(int32, PushChatLimit);
	DeclareVar(int32, SavedGifsLimit);
	DeclareVar(int32, EditTimeLimit);

	typedef QMap<PeerId, MsgId> HiddenPinnedMessagesMap;
	DeclareVar(HiddenPinnedMessagesMap, HiddenPinnedMessages);

	typedef OrderedSet<HistoryItem*> PendingItemsMap;
	DeclareRefVar(PendingItemsMap, PendingRepaintItems);

	DeclareVar(Stickers::Sets, StickerSets);
	DeclareVar(Stickers::Order, StickerSetsOrder);
	DeclareVar(uint64, LastStickersUpdate);

	DeclareVar(MTP::DcOptions, DcOptions);

	typedef QMap<uint64, QPixmap> CircleMasksMap;
	DeclareRefVar(CircleMasksMap, CircleMasks);

};

namespace Adaptive {
	inline bool OneColumn() {
		return Global::AdaptiveLayout() == OneColumnLayout;
	}
	inline bool Normal() {
		return Global::AdaptiveLayout() == NormalLayout;
	}
	inline bool Wide() {
		return Global::AdaptiveForWide() && (Global::AdaptiveLayout() == WideLayout);
	}
}

namespace DebugLogging {
	inline bool FileLoader() {
		return (Global::DebugLoggingFlags() | FileLoaderFlag) != 0;
	}
}
