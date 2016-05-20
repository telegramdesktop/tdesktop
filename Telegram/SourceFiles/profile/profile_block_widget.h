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

class BlockWidget : public TWidget {
	Q_OBJECT

public:
	BlockWidget(QWidget *parent, PeerData *peer, const QString &title);

	// Count new height for width=newWidth and resize to it.
	void resizeToWidth(int newWidth);

	// Updates the area that is visible inside the scroll container.
	virtual void setVisibleTopBottom(int visibleTop, int visibleBottom) {
	}

protected:
	void paintEvent(QPaintEvent *e) override;
	virtual void paintContents(Painter &p) {
	}

	// Resizes content and counts natural widget height for the desired width.
	virtual int resizeGetHeight(int newWidth) = 0;

	PeerData *peer() const {
		return _peer;
	}

private:
	PeerData *_peer;
	QString _title;

};

} // namespace Profile
