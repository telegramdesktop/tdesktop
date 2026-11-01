/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/window_palette.h"

#include "ui/style/style_core_palette.h"
#include "ui/widgets/popup_menu.h"
#include "ui/rp_widget.h"

#include <QtWidgets/QApplication>

namespace Ui {
namespace {

base::flat_map<
	const QWidget*,
	Fn<const style::palette*(bool child)>> Registry;

[[nodiscard]] auto FindRegistered(not_null<const QWidget*> widget) {
	auto w = widget->window();
	while (w) {
		const auto i = Registry.find(w);
		if (i != end(Registry)) {
			return i;
		}
		const auto parent = w->parentWidget();
		w = parent ? parent->window() : nullptr;
	}
	return end(Registry);
}

} // namespace

void SetWindowPalette(
		not_null<RpWidget*> window,
		Fn<const style::palette*(bool child)> resolve) {
	const auto key = static_cast<const QWidget*>(window.get());
	Registry[key] = std::move(resolve);
	window->lifetime().add([=] {
		Registry.remove(key);
	});
}

bool HasWindowPalettes() {
	return !Registry.empty();
}

const style::palette *WindowPaletteFor(not_null<const QWidget*> widget) {
	if (Registry.empty()) {
		return nullptr;
	}
	const auto i = FindRegistered(widget);
	return (i != end(Registry))
		? i->second(i->first != widget->window())
		: nullptr;
}

void ForceFullPaletteRepaint(not_null<QWidget*> widget) {
	const auto refresher = std::make_unique<QWidget>(widget);
	refresher->setAttribute(Qt::WA_TransparentForMouseEvents);
	refresher->setGeometry(widget->rect());
	refresher->show();
}

void RefreshWindowPaletteChild(not_null<QWidget*> child) {
	const auto scope = style::main_palette::Override(WindowPaletteFor(child));
	if (const auto menu = dynamic_cast<PopupMenu*>(child.get())) {
		menu->refreshAnimationCache();
	}
	ForceFullPaletteRepaint(child);
}

void UpdateWindowPaletteChildren(not_null<const QWidget*> window) {
	for (const auto top : QApplication::topLevelWidgets()) {
		if (top == window.get() || top->isHidden()) {
			continue;
		}
		const auto i = FindRegistered(top);
		if (i != end(Registry) && i->first == window.get()) {
			RefreshWindowPaletteChild(top);
		}
	}
}

} // namespace Ui
