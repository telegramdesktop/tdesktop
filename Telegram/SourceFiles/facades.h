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
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#pragma once

class LayeredWidget;

namespace App {

	void sendBotCommand(const QString &cmd, MsgId replyTo = 0);
	bool insertBotCommand(const QString &cmd, bool specialGif = false);
	void searchByHashtag(const QString &tag, PeerData *inPeer);
	void openPeerByName(const QString &username, bool toProfile = false, const QString &startToken = QString());
	void joinGroupByHash(const QString &hash);
	void stickersBox(const QString &name);
	void openLocalUrl(const QString &url);
	bool forward(const PeerId &peer, ForwardWhatMessages what);
	void removeDialog(History *history);
	void showSettings();

};

namespace Ui { // openssl doesn't allow me to use UI :(

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

	void historyItemResized(const HistoryItem *item, bool scrollToIt = false);
	inline void historyItemsResized() {
		historyItemResized(0);
	}
	void historyItemLayoutChanged(const HistoryItem *item);

	void automaticLoadSettingsChangedGif();

};

namespace Global {

	class Initializer {
	public:
		Initializer();
		~Initializer();
	};

#define DeclareGlobalReadOnly(Type, Name) const Type &Name();
#define DeclareGlobal(Type, Name) DeclareGlobalReadOnly(Type, Name) \
	void Set##Name(const Type &Name); \
	Type &Ref##Name();

	DeclareGlobalReadOnly(uint64, LaunchId);

};
