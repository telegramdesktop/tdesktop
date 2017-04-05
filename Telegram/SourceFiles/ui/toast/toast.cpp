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
#include "ui/toast/toast.h"

#include "ui/toast/toast_manager.h"
#include "ui/toast/toast_widget.h"
#include "mainwindow.h"

namespace Ui {
namespace Toast {

Instance::Instance(const Config &config, QWidget *widgetParent, const Private &)
: _hideAtMs(getms(true) + config.durationMs) {
	_widget = std::make_unique<internal::Widget>(widgetParent, config);
	_a_opacity.start([this] { opacityAnimationCallback(); }, 0., 1., st::toastFadeInDuration);
}

void Show(QWidget *parent, const Config &config) {
	if (auto manager = internal::Manager::instance(parent)) {
		auto toast = std::make_unique<Instance>(config, parent, Instance::Private());
		manager->addToast(std::move(toast));
	}
}

void Show(const Config &config) {
	if (auto window = App::wnd()) {
		Show(window->bodyWidget(), config);
	}
}

void Show(const QString &text) {
	Config toast;
	toast.text = text;
	Show(toast);
}

void Instance::opacityAnimationCallback() {
	_widget->setShownLevel(_a_opacity.current(_hiding ? 0. : 1.));
	_widget->update();
	if (!_a_opacity.animating()) {
		if (_hiding) {
			hide();
		}
	}
}

void Instance::hideAnimated() {
	_hiding = true;
	_a_opacity.start([this] { opacityAnimationCallback(); }, 1., 0., st::toastFadeOutDuration);
}

void Instance::hide() {
	_widget->hide();
	_widget->deleteLater();
}

} // namespace Toast
} // namespace Ui
