/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/abstract_box.h"

#include "core/application.h"
#include "window/window_controller.h"
#include "mainwidget.h"
#include "mainwindow.h"

namespace Ui {
namespace internal {

void showBox(
		object_ptr<BoxContent> content,
		LayerOptions options,
		anim::type animated) {
	const auto window = Core::IsAppLaunched()
		? Core::App().primaryWindow()
		: nullptr;
	if (window) {
		window->show(std::move(content), options, animated);
	}
}

} // namespace internal

void hideLayer(anim::type animated) {
	const auto window = Core::IsAppLaunched()
		? Core::App().primaryWindow()
		: nullptr;
	if (window) {
		window->hideLayer(animated);
	}
}

bool isLayerShown() {
	const auto window = Core::IsAppLaunched()
		? Core::App().primaryWindow()
		: nullptr;
	return window && window->isLayerShown();
}

} // namespace Ui
