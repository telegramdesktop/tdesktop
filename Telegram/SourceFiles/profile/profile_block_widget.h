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

namespace Profile {

class SectionMemento;

class BlockWidget : public TWidget, protected base::Subscriber {
	Q_OBJECT

public:
	BlockWidget(QWidget *parent, PeerData *peer, const QString &title);

	virtual void showFinished() {
	}

	virtual void saveState(not_null<SectionMemento*> memento) {
	}
	virtual void restoreState(not_null<SectionMemento*> memento) {
	}

protected:
	void paintEvent(QPaintEvent *e) override;
	virtual void paintContents(Painter &p) {
	}

	// Where does the block content start (after the title).
	int contentTop() const;

	// Resizes content and counts natural widget height for the desired width.
	int resizeGetHeight(int newWidth) override = 0;

	void contentSizeUpdated() {
		auto oldHeight = height();
		resizeToWidth(width());
		emit heightUpdated();
	}

	PeerData *peer() const {
		return _peer;
	}

	bool emptyTitle() const {
		return _title.isEmpty();
	}

private:
	void paintTitle(Painter &p);

	PeerData *_peer;
	QString _title;

};

int defaultOutlineButtonLeft();

} // namespace Profile
