/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
