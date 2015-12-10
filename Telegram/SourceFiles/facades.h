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
	void insertBotCommand(const QString &cmd);
	void searchByHashtag(const QString &tag, PeerData *inPeer);
	void openPeerByName(const QString &username, bool toProfile = false, const QString &startToken = QString());
	void joinGroupByHash(const QString &hash);
	void stickersBox(const QString &name);
	void openLocalUrl(const QString &url);
	bool forward(const PeerId &peer, ForwardWhatMessages what);
	void removeDialog(History *history);
	void showSettings();
	void showLayer(LayeredWidget *w, bool forceFast = false);
	void replaceLayer(LayeredWidget *w);
	void showLayerLast(LayeredWidget *w);

};

namespace Ui { // it doesn't allow me to use UI :(

	void showStickerPreview(DocumentData *sticker);
	void hideStickerPreview();

};

namespace Notify {

	void userIsBotChanged(UserData *user);
	void botCommandsChanged(UserData *user);
	void migrateUpdated(PeerData *peer);

};
