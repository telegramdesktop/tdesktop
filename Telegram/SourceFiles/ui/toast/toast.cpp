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
#include "ui/toast/toast.h"

#include "ui/toast/toast_manager.h"
#include "ui/toast/toast_widget.h"
#include "window.h"

namespace Ui {
namespace Toast {

Instance::Instance(const Config &config, QWidget *widgetParent, const Private &)
	: _a_fade(animation(this, &Instance::step_fade))
	, _hideAtMs(getms(true) + config.durationMs) {
	_widget = MakeUnique<internal::Widget>(widgetParent, config);
	_a_fade.start();
}

void Show(const Config &config) {
	if (internal::Manager *manager = internal::Manager::instance()) {
		if (Window *window = App::wnd()) {
			auto toast = MakeUnique<Instance>(config, window, Instance::Private());
			manager->addToast(std_::move(toast));
		}
	}
}

void Instance::fadeOut() {
	_fadingOut = true;
	_a_fade.start();
}

void Instance::hide() {
	_widget->hide();
	_widget->deleteLater();
}

void Instance::step_fade(float64 ms, bool timer) {
	if (timer) {
		_widget->update();
	}
	if (_fadingOut) {
		if (ms >= st::toastFadeOutDuration) {
			hide();
		} else {
			float64 dt = ms / st::toastFadeOutDuration;
			_widget->setShownLevel(1. - dt);
		}
	} else {
		if (ms >= st::toastFadeInDuration) {
			_widget->setShownLevel(1.);
			_a_fade.stop();
		} else {
			float64 dt = ms / st::toastFadeInDuration;
			_widget->setShownLevel(dt);
		}
	}
}

} // namespace Toast
} // namespace Ui
