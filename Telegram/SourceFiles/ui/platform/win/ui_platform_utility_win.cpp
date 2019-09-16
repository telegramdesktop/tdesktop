/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/platform/win/ui_platform_utility_win.h"

#include <QtWidgets/QApplication>

namespace Ui {
namespace Platform {

bool IsApplicationActive() {
	return QApplication::activeWindow() != nullptr;
}

void UpdateOverlayed(not_null<QWidget*> widget) {
	const auto wm = widget->testAttribute(Qt::WA_Mapped);
	const auto wv = widget->testAttribute(Qt::WA_WState_Visible);
	if (!wm) widget->setAttribute(Qt::WA_Mapped, true);
	if (!wv) widget->setAttribute(Qt::WA_WState_Visible, true);
	widget->update();
	QEvent e(QEvent::UpdateRequest);
	QGuiApplication::sendEvent(widget, &e);
	if (!wm) widget->setAttribute(Qt::WA_Mapped, false);
	if (!wv) widget->setAttribute(Qt::WA_WState_Visible, false);
}

} // namespace Platform
} // namespace Ui
