/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/abstract_box.h"

#include "mainwidget.h"
#include "mainwindow.h"

namespace Ui {
namespace internal {

void showBox(
		object_ptr<BoxContent> content,
		LayerOptions options,
		anim::type animated) {
	if (auto w = App::wnd()) {
		w->ui_showBox(std::move(content), options, animated);
	}
}

} // namespace internal

void hideLayer(anim::type animated) {
	if (auto w = App::wnd()) {
		w->ui_showBox(
			{ nullptr },
			LayerOption::CloseOther,
			animated);
	}
}

bool isLayerShown() {
	if (auto w = App::wnd()) return w->ui_isLayerShown();
	return false;
}

} // namespace Ui
