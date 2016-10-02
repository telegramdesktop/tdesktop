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

#include "window/notifications_abstract_manager.h"

namespace Window {
namespace Notifications {

class DefaultManager : public AbstractManager {
public:

private:
	void create(PeerData *peer, MsgId msgId, const QString &title, const QString &subtitle, bool showUserpic, const QString &msg, bool showReplyButton) override;
	void clear(History *history, bool fast) override;

};

class Widget : public TWidget {
	Q_OBJECT

public:
	Widget(HistoryItem *item, int32 x, int32 y, int32 fwdCount);

	void step_appearance(float64 ms, bool timer);
	void animHide(float64 duration, anim::transition func);
	void startHiding();
	void stopHiding();
	void moveTo(int32 x, int32 y, int32 index = -1);

	void updateNotifyDisplay();
	void updatePeerPhoto();

	void itemRemoved(HistoryItem *del);

	int32 index() const {
		return history ? _index : -1;
	}

	void unlinkHistory(History *hist = 0);

	~Widget();

protected:
	void enterEvent(QEvent *e) override;
	void leaveEvent(QEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private slots:
	void hideByTimer();
	void checkLastInput();

private:
	void unlinkHistoryAndNotify();

#if defined Q_OS_WIN && !defined Q_OS_WINRT
	uint64 started;
#endif // Q_OS_WIN && !Q_OS_WINRT

	History *history;
	HistoryItem *item;
	int32 fwdCount;
	IconedButton close;
	QPixmap pm;
	float64 alphaDuration, posDuration;
	QTimer hideTimer, inputTimer;
	bool hiding;
	int32 _index;
	anim::fvalue a_opacity;
	anim::transition a_func;
	anim::ivalue a_y;
	Animation _a_appearance;

	ImagePtr peerPhoto;

};

} // namespace Notifications
} // namespace Window
