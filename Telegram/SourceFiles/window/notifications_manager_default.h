/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "window/notifications_manager.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"
#include "base/timer.h"
#include "base/binary_guard.h"
#include "base/object_ptr.h"

#include <QtCore/QTimer>

namespace Data {
class CloudImageView;
} // namespace Data

namespace Ui {
class IconButton;
class RoundButton;
class InputField;
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
std::unique_ptr<Manager> Create(System *system);

class Manager final : public Notifications::Manager {
public:
	Manager(System *system);
	~Manager();

	[[nodiscard]] ManagerType type() const override {
		return ManagerType::Default;
	}

	template <typename Method>
	void enumerateNotifications(Method method) {
		for (const auto &notification : _notifications) {
			method(notification);
		}
	}

private:
	friend class internal::Notification;
	friend class internal::HideAllButton;
	friend class internal::Widget;
	using Notification = internal::Notification;
	using HideAllButton = internal::HideAllButton;
	struct SessionSubscription {
		rpl::lifetime subscription;
		rpl::lifetime lifetime;
	};

	[[nodiscard]] QPixmap hiddenUserpicPlaceholder() const;

	void doUpdateAll() override;
	void doShowNotification(
		not_null<HistoryItem*> item,
		int forwardedCount) override;
	void doClearAll() override;
	void doClearAllFast() override;
	void doClearFromHistory(not_null<History*> history) override;
	void doClearFromSession(not_null<Main::Session*> session) override;
	void doClearFromItem(not_null<HistoryItem*> item) override;
	bool doSkipAudio() const override;
	bool doSkipToast() const override;
	bool doSkipFlashBounce() const override;

	void showNextFromQueue();
	void unlinkFromShown(Notification *remove);
	void startAllHiding();
	void stopAllHiding();
	void checkLastInput();

	void removeWidget(internal::Widget *remove);

	float64 demoMasterOpacity() const;
	void demoMasterOpacityCallback();

	void moveWidgets();
	void changeNotificationHeight(Notification *widget, int newHeight);
	void settingsChanged(ChangeType change);

	bool hasReplyingNotification() const;

	void subscribeToSession(not_null<Main::Session*> session);

	std::vector<std::unique_ptr<Notification>> _notifications;
	base::flat_map<
		not_null<Main::Session*>,
		SessionSubscription> _subscriptions;

	std::unique_ptr<HideAllButton> _hideAll;

	bool _positionsOutdated = false;
	base::Timer _inputCheckTimer;

	struct QueuedNotification {
		QueuedNotification(not_null<HistoryItem*> item, int forwardedCount);

		not_null<History*> history;
		not_null<PeerData*> peer;
		QString author;
		HistoryItem *item = nullptr;
		int forwardedCount = 0;
		bool fromScheduled = false;
	};
	std::deque<QueuedNotification> _queuedNotifications;

	Ui::Animations::Simple _demoMasterOpacity;
	bool _demoIsShown = false;

	mutable QPixmap _hiddenUserpicPlaceholder;

	rpl::lifetime _lifetime;

};

namespace internal {

class Widget : public Ui::RpWidget {
public:
	enum class Direction {
		Up,
		Down,
	};
	Widget(
		not_null<Manager*> manager,
		QPoint startPosition,
		int shift,
		Direction shiftDirection);

	bool isShowing() const {
		return _a_opacity.animating() && !_hiding;
	}

	void updateOpacity();
	void changeShift(int top);
	int currentShift() const {
		return _shift.current();
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

protected:
	[[nodiscard]] not_null<Manager*> manager() const {
		return _manager;
	}

private:
	void opacityAnimationCallback();
	void destroyDelayed();
	void moveByShift();
	void hideAnimated(float64 duration, const anim::transition &func);
	bool shiftAnimationCallback(crl::time now);

	const not_null<Manager*> _manager;

	bool _hiding = false;
	bool _deleted = false;
	base::binary_guard _hidingDelayed;
	Ui::Animations::Simple _a_opacity;

	QPoint _startPosition;
	Direction _direction;
	anim::value _shift;
	Ui::Animations::Basic _shiftAnimation;

};

class Background : public TWidget {
public:
	Background(QWidget *parent);

protected:
	void paintEvent(QPaintEvent *e) override;

};

class Notification final : public Widget {
public:
	Notification(
		not_null<Manager*> manager,
		not_null<History*> history,
		not_null<PeerData*> peer,
		const QString &author,
		HistoryItem *item,
		int forwardedCount,
		bool fromScheduled,
		QPoint startPosition,
		int shift,
		Direction shiftDirection);

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
	[[nodiscard]] History *maybeHistory() const {
		return _history;
	}

	// Called only by Manager.
	bool unlinkItem(HistoryItem *del);
	bool unlinkHistory(History *history = nullptr);
	bool unlinkSession(not_null<Main::Session*> session);
	bool checkLastInput(
		bool hasReplyingNotifications,
		std::optional<crl::time> lastInputTime);

protected:
	void enterEventHook(QEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	bool eventFilter(QObject *o, QEvent *e) override;

private:
	void refreshLang();
	void updateReplyGeometry();
	bool canReply() const;
	void replyResized();
	void replyCancel();

	void unlinkHistoryInManager();
	void toggleActionButtons(bool visible);
	void prepareActionsCache();
	void showReplyField();
	void sendReply();
	void changeHeight(int newHeight);
	void updateGeometry(int x, int y, int width, int height) override;
	void actionsOpacityCallback();

	[[nodiscard]] Notifications::Manager::NotificationId myId() const;

	const not_null<PeerData*> _peer;

	QPixmap _cache;

	bool _hideReplyButton = false;
	bool _actionsVisible = false;
	Ui::Animations::Simple a_actionsOpacity;
	QPixmap _buttonsCache;

	crl::time _started;

	History *_history = nullptr;
	std::shared_ptr<Data::CloudImageView> _userpicView;
	QString _author;
	HistoryItem *_item = nullptr;
	int _forwardedCount = 0;
	bool _fromScheduled = false;
	object_ptr<Ui::IconButton> _close;
	object_ptr<Ui::RoundButton> _reply;
	object_ptr<Background> _background = { nullptr };
	object_ptr<Ui::InputField> _replyArea = { nullptr };
	object_ptr<Ui::IconButton> _replySend = { nullptr };
	bool _waitingForInput = true;

	QTimer _hideTimer;

	int _replyPadding = 0;

	bool _userpicLoaded = false;

};

class HideAllButton : public Widget {
public:
	HideAllButton(
		not_null<Manager*> manager,
		QPoint startPosition,
		int shift,
		Direction shiftDirection);

	void startHiding();
	void startHidingFast();
	void stopHiding();

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
