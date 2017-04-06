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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "base/observer.h"

namespace style {
struct RoundButton;
} // namespace style

namespace Ui {
class FlatLabel;
class RoundButton;
class LinkButton;
} // namespace Ui

namespace Notify {
struct PeerUpdate;
} // namespace Notify

namespace Profile {

class BackButton;
class UserpicButton;
class CoverDropArea;

class CoverWidget final : public TWidget, private base::Subscriber {
	Q_OBJECT

public:
	CoverWidget(QWidget *parent, PeerData *peer);

	void showFinished();

	// Profile fixed top bar should use this flag to decide
	// if it shows "Share contact" button or not.
	// It should show it only if it is hidden in the cover.
	bool shareContactButtonShown() const;

public slots:
	void onOnlineCountUpdated(int onlineCount);

private slots:
	void onPhotoShow();
	void onPhotoUploadStatusChanged(PeerId peerId = 0);
	void onCancelPhotoUpload();

	void onSendMessage();
	void onShareContact();
	void onSetPhoto();
	void onAddMember();
	void onAddBotToGroup();
	void onJoin();
	void onViewChannel();

protected:
	void paintEvent(QPaintEvent *e) override;
	void dragEnterEvent(QDragEnterEvent *e) override;
	void dragLeaveEvent(QDragLeaveEvent *e) override;
	void dropEvent(QDropEvent *e) override;

	int resizeGetHeight(int newWidth) override;

private:
	// Observed notifications.
	void notifyPeerUpdated(const Notify::PeerUpdate &update);

	// Counts userpic button left offset for a new widget width.
	int countPhotoLeft(int newWidth) const;
	PhotoData *validatePhoto() const;

	void refreshNameGeometry(int newWidth);
	void moveAndToggleButtons(int newWidth);
	void refreshNameText();
	void refreshStatusText();

	void refreshButtons();
	void setUserButtons();
	void setChatButtons();
	void setMegagroupButtons();
	void setChannelButtons();

	void clearButtons();
	void addButton(const QString &text, const char *slot, const style::RoundButton *replacementStyle = nullptr);

	void paintDivider(Painter &p);

	bool canEditPhoto() const;
	void showSetPhotoBox(const QImage &img);
	void resizeDropArea(int newWidth);
	void dropAreaHidden(CoverDropArea *dropArea);
	bool mimeDataHasImage(const QMimeData *mimeData) const;

	PeerData *_peer;
	UserData *_peerUser;
	ChatData *_peerChat;
	ChannelData *_peerChannel;
	ChannelData *_peerMegagroup;

	object_ptr<UserpicButton> _userpicButton;
	object_ptr<CoverDropArea> _dropArea = { nullptr };

	object_ptr<Ui::FlatLabel> _name;
	object_ptr<Ui::LinkButton> _cancelPhotoUpload = { nullptr };

	QPoint _statusPosition;
	QString _statusText;
	bool _statusTextIsOnline = false;

	struct Button {
		Ui::RoundButton *widget;
		Ui::RoundButton *replacement;
	};
	QList<Button> _buttons;

	int _photoLeft = 0; // Caching countPhotoLeft() result.
	int _dividerTop = 0;

	int _onlineCount = 0;

};

} // namespace Profile
