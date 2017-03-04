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
	connect(parent, SIGNAL(resized()), this, SLOT(onParentResized()));
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
	connect(widget->parentWidget(), SIGNAL(resized(QSize)), this, SLOT(onToastWidgetParentResized()), Qt::UniqueConnection);

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

void Manager::onToastWidgetParentResized() {
	auto resizedWidget = QObject::sender();
	if (!resizedWidget) return;

	for (auto i = _toastByWidget.cbegin(), e = _toastByWidget.cend(); i != e; ++i) {
		if (i.key()->parentWidget() == resizedWidget) {
			i.key()->onParentResized();
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
