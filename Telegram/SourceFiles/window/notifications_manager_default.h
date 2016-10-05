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
#include "core/single_timer.h"

namespace Ui {
class IconButton;
} // namespace Ui

namespace Window {
namespace Notifications {
namespace Default {
namespace internal {
class Notification;
class HideAllButton;
} // namespace internal

class Manager;

void start();
Manager *manager();
void finish();

class Manager : public Notifications::Manager, private base::Subscriber {
public:
	Manager();

	template <typename Method>
	void enumerateNotifications(Method method) {
		for_const (auto notification, _notifications) {
			method(notification);
		}
	}

	~Manager();

private:
	using Notification = internal::Notification;
	friend class Notification;
	using HideAllButton = internal::HideAllButton;
	friend class HideAllButton;

	void doUpdateAll() override;
	void doShowNotification(HistoryItem *item, int forwardedCount) override;
	void doClearAll() override;
	void doClearAllFast() override;
	void doClearFromHistory(History *history) override;
	void doClearFromItem(HistoryItem *item) override;

	void showNextFromQueue();
	void unlinkFromShown(Notification *remove);
	void removeFromShown(Notification *remove);
	void removeHideAll(HideAllButton *remove);
	void startAllHiding();
	void stopAllHiding();
	void checkLastInput();

	QPoint notificationStartPosition() const;
	void moveWidgets();
	void changeNotificationHeight(Notification *widget, int newHeight);

	using Notifications = QList<Notification*>;
	Notifications _notifications;

	HideAllButton *_hideAll = nullptr;

	bool _positionsOutdated = false;
	SingleTimer _inputCheckTimer;

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

namespace internal {

class Widget : public TWidget {
public:
	Widget(QPoint position);

	bool isShowing() const {
		return _a_appearance.animating() && !_hiding;
	}
	void moveTop(int top);

	enum class AddToHeight {
		Above,
		Below,
	};
	void addToHeight(int add, AddToHeight aboveOrBelow);
	void addToTop(int add);

protected:
	void hideSlow();
	void hideFast();
	void hideStop();

	virtual void updateGeometry(int x, int y, int width, int height);

private:
	void animHide(float64 duration, anim::transition func);
	void step_appearance(float64 ms, bool timer);
	void step_movement(float64 ms, bool timer);

	bool _hiding = false;
	float64 _opacityDuration;
	anim::fvalue a_opacity;
	anim::transition a_func;
	Animation _a_appearance;

	anim::ivalue a_top;
	Animation _a_movement;

};

class Background : public TWidget {
public:
	Background(QWidget *parent);

protected:
	void paintEvent(QPaintEvent *e) override;

};

class Notification : public Widget {
	Q_OBJECT

public:
	Notification(History *history, PeerData *peer, PeerData *author, HistoryItem *item, int forwardedCount, QPoint position);

	void startHiding();
	void stopHiding();

	void updateNotifyDisplay();
	void updatePeerPhoto();

	bool isUnlinked() const {
		return !_history;
	}
	bool isReplying() const {
		return (_replyArea != nullptr) && !isUnlinked();
	}

	// Called only by Manager.
	void itemRemoved(HistoryItem *del);
	bool unlinkHistory(History *history = nullptr);
	bool checkLastInput();

	~Notification();

protected:
	void enterEvent(QEvent *e) override;
	void leaveEvent(QEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;

private slots:
	void onHideByTimer();
	void onReplyResize();
	void onReplySubmit(bool ctrlShiftEnter);
	void onReplyCancel();

private:
	void unlinkHistoryInManager();
	void toggleActionButtons(bool visible);
	void prepareActionsCache();
	void showReplyField();
	void sendReply();
	void changeHeight(int newHeight);
	void updateGeometry(int x, int y, int width, int height) override;
	void actionsOpacityCallback();

	QPixmap _cache;

	bool _actionsVisible = false;
	FloatAnimation a_actionsOpacity;
	QPixmap _buttonsCache;

#if defined Q_OS_WIN && !defined Q_OS_WINRT
	uint64 _started;
#endif // Q_OS_WIN && !Q_OS_WINRT

	History *_history;
	PeerData *_peer;
	PeerData *_author;
	HistoryItem *_item;
	int _forwardedCount;
	ChildWidget<IconedButton> _close;
	ChildWidget<BoxButton> _reply;
	ChildWidget<Background> _background = { nullptr };
	ChildWidget<InputArea> _replyArea = { nullptr };
	ChildWidget<Ui::IconButton> _replySend = { nullptr };
	bool _waitingForInput = true;

	QTimer _hideTimer;

	int _replyPadding = 0;

	bool _userpicLoaded = false;

};

class HideAllButton : public Widget {
public:
	HideAllButton(QPoint position);

	void startHiding();
	void startHidingFast();
	void stopHiding();

	~HideAllButton();

protected:
	void enterEvent(QEvent *e) override;
	void leaveEvent(QEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	bool _mouseOver = false;
	bool _mouseDown = false;

};

} // namespace internal
} // namespace Default
} // namespace Notifications
} // namespace Window
