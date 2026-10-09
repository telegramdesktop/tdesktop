/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/window_palette.h"

#include "ui/rp_widget.h"

namespace Ui {
namespace {

base::flat_map<const QWidget*, Fn<const style::palette*()>> Registry;

} // namespace

void SetWindowPalette(
		not_null<RpWidget*> window,
		Fn<const style::palette*()> resolve) {
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
	auto w = widget->window();
	while (w) {
		const auto i = Registry.find(w);
		if (i != end(Registry)) {
			return i->second();
		}
		const auto parent = w->parentWidget();
		w = parent ? parent->window() : nullptr;
	}
	return nullptr;
}

} // namespace Ui
