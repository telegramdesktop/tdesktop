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

#include "window/notifications_manager.h"
#include "core/single_timer.h"

namespace Ui {
class IconButton;
class RoundButton;
class InputArea;
} // namespace Ui

namespace Window {
namespace Notifications {
namespace Default {
namespace internal {
class Widget;
class Notification;
class HideAllButton;
} // namespace internal

class Manager;

void Start();
Manager *GetManager();
void Finish();

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
	friend class internal::Notification;
	friend class internal::HideAllButton;
	friend class internal::Widget;
	using Notification = internal::Notification;
	using HideAllButton = internal::HideAllButton;

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

	float64 demoMasterOpacity() const;
	void demoMasterOpacityCallback();

	void moveWidgets();
	void changeNotificationHeight(Notification *widget, int newHeight);
	void settingsChanged(Notify::ChangeType change);

	bool hasReplyingNotification() const;

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

	Animation _demoMasterOpacity;

};

namespace internal {

class Widget : public TWidget, protected base::Subscriber {
public:
	enum class Direction {
		Up,
		Down,
	};
	Widget(QPoint startPosition, int shift, Direction shiftDirection);

	bool isShowing() const {
		return _a_opacity.animating() && !_hiding;
	}

	void updateOpacity();
	void changeShift(int top);
	int currentShift() const {
		return a_shift.current();
	}
	void updatePosition(QPoint startPosition, Direction shiftDirection);
	void addToHeight(int add);
	void addToShift(int add);

protected:
	void hideSlow();
	void hideFast();
	void hideStop();
	QPoint computePosition(int height) const;

	virtual void updateGeometry(int x, int y, int width, int height);

private:
	void opacityAnimationCallback();
	void destroyDelayed();
	void moveByShift();
	void hideAnimated(float64 duration, const anim::transition &func);
	void step_shift(float64 ms, bool timer);

	bool _hiding = false;
	bool _deleted = false;
	Animation _a_opacity;

	QPoint _startPosition;
	Direction _direction;
	anim::value a_shift;
	BasicAnimation _a_shift;

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
	Notification(History *history, PeerData *peer, PeerData *author, HistoryItem *item, int forwardedCount, QPoint startPosition, int shift, Direction shiftDirection);

	void startHiding();
	void stopHiding();

	void updateNotifyDisplay();
	void updatePeerPhoto();

	bool isUnlinked() const {
		return !_history;
	}
	bool isReplying() const {
		return _replyArea && !isUnlinked();
	}

	// Called only by Manager.
	void itemRemoved(HistoryItem *del);
	bool unlinkHistory(History *history = nullptr);
	bool checkLastInput(bool hasReplyingNotifications);

	~Notification();

protected:
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	bool eventFilter(QObject *o, QEvent *e) override;

private slots:
	void onHideByTimer();
	void onReplyResize();
	void onReplySubmit(bool ctrlShiftEnter);
	void onReplyCancel();

private:
	bool canReply() const;

	void unlinkHistoryInManager();
	void toggleActionButtons(bool visible);
	void prepareActionsCache();
	void showReplyField();
	void sendReply();
	void changeHeight(int newHeight);
	void updateGeometry(int x, int y, int width, int height) override;
	void actionsOpacityCallback();

	QPixmap _cache;

	bool _hideReplyButton = false;
	bool _actionsVisible = false;
	Animation a_actionsOpacity;
	QPixmap _buttonsCache;

#ifdef Q_OS_WIN
	TimeMs _started;
#endif // Q_OS_WIN

	History *_history;
	PeerData *_peer;
	PeerData *_author;
	HistoryItem *_item;
	int _forwardedCount;
	object_ptr<Ui::IconButton> _close;
	object_ptr<Ui::RoundButton> _reply;
	object_ptr<Background> _background = { nullptr };
	object_ptr<Ui::InputArea> _replyArea = { nullptr };
	object_ptr<Ui::IconButton> _replySend = { nullptr };
	bool _waitingForInput = true;

	QTimer _hideTimer;

	int _replyPadding = 0;

	bool _userpicLoaded = false;

};

class HideAllButton : public Widget {
public:
	HideAllButton(QPoint startPosition, int shift, Direction shiftDirection);

	void startHiding();
	void startHidingFast();
	void stopHiding();

	~HideAllButton();

protected:
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;
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
