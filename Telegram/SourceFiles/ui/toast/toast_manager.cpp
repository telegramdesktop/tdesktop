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
#include "stdafx.h"
#include "ui/toast/toast_manager.h"

#include "ui/toast/toast_widget.h"
#include "window.h"

namespace Ui {
namespace Toast {
namespace internal {

Manager *Manager::_instance = nullptr;

Manager::Manager(QObject *parent) : QObject(parent) {
	t_assert(_instance == nullptr);
	_instance = this;

	connect(&_hideTimer, SIGNAL(timeout()), this, SLOT(onHideTimeout()));
	connect(parent, SIGNAL(resized()), this, SLOT(onParentResized()));
}

Manager *Manager::instance() {
	if (!_instance) {
		if (Window *w = App::wnd()) {
			_instance = new Manager(w);
		}
	}
	return _instance;
}

void Manager::addToast(UniquePointer<Instance> &&toast) {
	_toasts.push_back(toast.release());
	Instance *t = _toasts.back();
	Widget *widget = t->_widget.data();

	_toastByWidget.insert(widget, t);
	connect(widget, SIGNAL(destroyed(QObject*)), this, SLOT(onToastWidgetDestroyed(QObject*)));
	connect(widget->parentWidget(), SIGNAL(resized(QSize)), this, SLOT(onToastWidgetParentResized()), Qt::UniqueConnection);

	uint64 oldHideNearestMs = _toastByHideTime.isEmpty() ? 0 : _toastByHideTime.firstKey();
	_toastByHideTime.insert(t->_hideAtMs, t);
	if (!oldHideNearestMs || _toastByHideTime.firstKey() < oldHideNearestMs) {
		startNextHideTimer();
	}
}

void Manager::onHideTimeout() {
	uint64 now = getms(true);
	for (auto i = _toastByHideTime.begin(); i != _toastByHideTime.cend();) {
		if (i.key() <= now) {
			Instance *toast = i.value();
			i = _toastByHideTime.erase(i);
			toast->fadeOut();
		} else {
			break;
		}
	}
	startNextHideTimer();
}

void Manager::onToastWidgetDestroyed(QObject *widget) {
	auto i = _toastByWidget.find(static_cast<Widget*>(widget));
	if (i != _toastByWidget.cend()) {
		Instance *toast = i.value();
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
	QObject *resizedWidget = QObject::sender();
	if (!resizedWidget) return;

	for (auto i = _toastByWidget.cbegin(), e = _toastByWidget.cend(); i != e; ++i) {
		if (i.key()->parentWidget() == resizedWidget) {
			i.key()->onParentResized();
		}
	}
}

void Manager::startNextHideTimer() {
	if (_toastByHideTime.isEmpty()) return;

	uint64 ms = getms(true);
	if (ms >= _toastByHideTime.firstKey()) {
		QMetaObject::invokeMethod(this, SLOT("onHideTimeout"), Qt::QueuedConnection);
	} else {
		_hideTimer.start(_toastByHideTime.firstKey() - ms);
	}
}

Manager::~Manager() {
	_instance = nullptr;
}

} // namespace internal
} // namespace Toast
} // namespace Ui
