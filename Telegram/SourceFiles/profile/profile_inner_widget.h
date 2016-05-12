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

class CoverWidget;
class BlockWidget;

class InnerWidget final : public TWidget {
	Q_OBJECT

public:
	InnerWidget(QWidget *parent, PeerData *peer);

	PeerData *peer() const {
		return _peer;
	}

	// Count new height for width=newWidth and resize to it.
	void resizeToWidth(int newWidth, int minHeight);

	// Sometimes height of this widget is larger than it is required
	// so that it is allowed to scroll down to the desired position.
	// When resizing with scroll moving up the additional height may be decreased.
	void decreaseAdditionalHeight(int removeHeight);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	// Resizes content and counts natural widget height for the desired width.
	int resizeGetHeight(int newWidth);

	PeerData *_peer;

	// Height that we added to the natural height so that it is allowed
	// to scroll down to the desired position.
	int _addedHeight = 0;

	ChildWidget<CoverWidget> _cover;
	QList<BlockWidget*> _blocks;

};

} // namespace Profile
