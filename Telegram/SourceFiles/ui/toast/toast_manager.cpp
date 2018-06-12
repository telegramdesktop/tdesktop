/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/toast/toast_manager.h"

#include "application.h"
#include "ui/toast/toast_widget.h"

namespace Ui {
namespace Toast {
namespace internal {

namespace {

NeverFreedPointer<QMap<QObject*, Manager*>> _managers;

} // namespace

Manager::Manager(QWidget *parent) : QObject(parent) {
	connect(&_hideTimer, SIGNAL(timeout()), this, SLOT(onHideTimeout()));
}

bool Manager::eventFilter(QObject *o, QEvent *e) {
	if (e->type() == QEvent::Resize) {
		for (auto i = _toastByWidget.cbegin(), e = _toastByWidget.cend(); i != e; ++i) {
			if (i.key()->parentWidget() == o) {
				i.key()->onParentResized();
			}
		}
	}
	return QObject::eventFilter(o, e);
}

Manager *Manager::instance(QWidget *parent) {
	if (!parent) {
		return nullptr;
	}

	_managers.createIfNull();
	auto i = _managers->constFind(parent);
	if (i == _managers->cend()) {
		i = _managers->insert(parent, new Manager(parent));
	}
	return i.value();
}

void Manager::addToast(std::unique_ptr<Instance> &&toast) {
	_toasts.push_back(toast.release());
	Instance *t = _toasts.back();
	Widget *widget = t->_widget.get();

	_toastByWidget.insert(widget, t);
	connect(widget, SIGNAL(destroyed(QObject*)), this, SLOT(onToastWidgetDestroyed(QObject*)));
	if (auto parent = widget->parentWidget()) {
		auto found = false;
		for (auto i = _toastParents.begin(); i != _toastParents.cend();) {
			if (*i == parent) {
				found = true;
				break;
			} else if (!*i) {
				i = _toastParents.erase(i);
			} else {
				++i;
			}
		}
		if (!found) {
			_toastParents.insert(parent);
			parent->installEventFilter(this);
		}
	}

	auto oldHideNearestMs = _toastByHideTime.isEmpty() ? 0LL : _toastByHideTime.firstKey();
	_toastByHideTime.insert(t->_hideAtMs, t);
	if (!oldHideNearestMs || _toastByHideTime.firstKey() < oldHideNearestMs) {
		startNextHideTimer();
	}
}

void Manager::onHideTimeout() {
	auto now = getms(true);
	for (auto i = _toastByHideTime.begin(); i != _toastByHideTime.cend();) {
		if (i.key() <= now) {
			auto toast = i.value();
			i = _toastByHideTime.erase(i);
			toast->hideAnimated();
		} else {
			break;
		}
	}
	startNextHideTimer();
}

void Manager::onToastWidgetDestroyed(QObject *widget) {
	auto i = _toastByWidget.find(static_cast<Widget*>(widget));
	if (i != _toastByWidget.cend()) {
		auto toast = i.value();
		_toastByWidget.erase(i);
		toast->_widget.release();

		int index = _toasts.indexOf(toast);
		if (index >= 0) {
			_toasts.removeAt(index);
			delete toast;
		}
	}
}

void Manager::startNextHideTimer() {
	if (_toastByHideTime.isEmpty()) return;

	auto ms = getms(true);
	if (ms >= _toastByHideTime.firstKey()) {
		QMetaObject::invokeMethod(this, SLOT("onHideTimeout"), Qt::QueuedConnection);
	} else {
		_hideTimer.start(_toastByHideTime.firstKey() - ms);
	}
}

Manager::~Manager() {
	_managers->remove(parent());
}

} // namespace internal
} // namespace Toast
} // namespace Ui
