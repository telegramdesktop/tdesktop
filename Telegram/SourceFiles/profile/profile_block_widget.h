/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/observer.h"
#include "ui/rp_widget.h"

namespace Profile {

class SectionMemento;

class BlockWidget : public Ui::RpWidget {
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
		resizeToWidth(width());
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

} // namespace Profile
