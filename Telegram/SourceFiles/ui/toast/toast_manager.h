/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/toast/toast.h"
#include "core/single_timer.h"

namespace Ui {
namespace Toast {
namespace internal {

class Widget;
class Manager : public QObject {
	Q_OBJECT

public:
	Manager(const Manager &other) = delete;
	Manager &operator=(const Manager &other) = delete;

	static Manager *instance(QWidget *parent);

	void addToast(std::unique_ptr<Instance> &&toast);

	~Manager();

protected:
	bool eventFilter(QObject *o, QEvent *e);

private slots:
	void onHideTimeout();
	void onToastWidgetDestroyed(QObject *widget);

private:
	Manager(QWidget *parent);
	void startNextHideTimer();

	SingleTimer _hideTimer;
	TimeMs _nextHide = 0;

	QMultiMap<TimeMs, Instance*> _toastByHideTime;
	QMap<Widget*, Instance*> _toastByWidget;
	QList<Instance*> _toasts;
	OrderedSet<QPointer<QWidget>> _toastParents;

};

} // namespace internal
} // namespace Toast
} // namespace Ui
