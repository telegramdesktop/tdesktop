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

namespace Profile {

class BackButton;
class PhotoButton;

class CoverWidget final : public TWidget {
	Q_OBJECT

public:
	CoverWidget(QWidget *parent, PeerData *peer);

	// Count new height for width=newWidth and resize to it.
	void resizeToWidth(int newWidth);

public slots:
	void onBack();

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void updateStatusText();
	bool isUsingMegagroupOnlineCount() const;

	PeerData *_peer;
	UserData *_peerUser;
	ChatData *_peerChat;
	ChannelData *_peerChannel;
	ChannelData *_peerMegagroup;

	// Top bar
	ChildWidget<BackButton> _backButton;
	QList<FlatButton*> _rightActions;

	// Cover content
	ChildWidget<PhotoButton> _photoButton;

	QPoint _namePosition;
	Text _nameText;

	QPoint _statusPosition;
	QString _statusText;

	ChildWidget<BoxButton> _primaryButton = { nullptr };
	ChildWidget<BoxButton> _secondaryButton = { nullptr };

};

} // namespace Profile
