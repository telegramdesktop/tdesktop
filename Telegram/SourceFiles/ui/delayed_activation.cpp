/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/delayed_activation.h"

#include "ui/ui_utility.h"

#include <QtCore/QPointer>

namespace Ui {
namespace {

auto Paused = false;
auto Window = QPointer<QWidget>();

} // namespace

void ActivateWindowDelayed(not_null<QWidget*> widget) {
	if (Paused) {
		return;
	} else if (std::exchange(Window, widget.get())) {
		return;
	}
	crl::on_main(Window, [=] {
		if (const auto widget = base::take(Window)) {
			if (!widget->isHidden()) {
				widget->activateWindow();
			}
		}
	});
}

void PreventDelayedActivation() {
	Window = nullptr;
	Paused = true;
	PostponeCall([] {
		Paused = false;
	});
}

} // namespace Ui
