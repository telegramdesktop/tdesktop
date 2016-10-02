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

#include "window/notifications_manager.h"

namespace Window {
namespace Notifications {
namespace Default {

class Manager;
class Widget;

void start();
Manager *manager();
void finish();

class Manager : public Notifications::Manager, private base::Subscriber {
public:
	Manager();

	void showNextFromQueue();
	void removeFromShown(Widget *remove);
	void startAllHiding();
	void stopAllHiding();

	template <typename Method>
	void enumerateWidgets(Method method) {
		for_const (auto widget, _widgets) {
			method(widget);
		}
	}

	~Manager();

private:
	void doUpdateAll() override;
	void doShowNotification(HistoryItem *item, int forwardedCount) override;
	void doClearAll() override;
	void doClearAllFast() override;
	void doClearFromHistory(History *history) override;
	void doClearFromItem(HistoryItem *item) override;

	using Widgets = QList<Widget*>;
	Widgets _widgets;

	struct QueuedNotification {
		QueuedNotification(HistoryItem *item, int forwardedCount)
		: history(item->history())
		, peer(history->peer)
		, author((item->hasFromName() && !item->isPost()) ? item->author() : nullptr)
		, item((forwardedCount > 1) ? nullptr : item)
		, forwardedCount(forwardedCount) {
		}

		History *history;
		PeerData *peer;
		PeerData *author;
		HistoryItem *item;
		int forwardedCount;
	};
	using QueuedNotifications = QList<QueuedNotification>;
	QueuedNotifications _queuedNotifications;

};

class Widget : public TWidget {
	Q_OBJECT

public:
	Widget(History *history, PeerData *peer, PeerData *author, HistoryItem *item, int forwardedCount, int x, int y);

	void step_appearance(float64 ms, bool timer);
	void animHide(float64 duration, anim::transition func);
	void startHiding();
	void stopHiding();
	void moveTo(int x, int y, int index = -1);

	void updateNotifyDisplay();
	void updatePeerPhoto();

	void itemRemoved(HistoryItem *del);

	int index() const {
		return _history ? _index : -1;
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
	uint64 _started;
#endif // Q_OS_WIN && !Q_OS_WINRT

	History *_history;
	PeerData *_peer;
	PeerData *_author;
	HistoryItem *_item;
	int _forwardedCount;
	IconedButton _close;
	QPixmap _cache;
	float64 _alphaDuration, _posDuration;
	QTimer _hideTimer, _inputTimer;
	bool _hiding = false;
	int _index = 0;
	anim::fvalue a_opacity;
	anim::transition a_func;
	anim::ivalue a_y;
	Animation _a_appearance;

	ImagePtr peerPhoto;

};

} // namespace Default
} // namespace Notifications
} // namespace Window
