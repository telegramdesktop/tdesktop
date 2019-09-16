/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/inactive_press.h"

#include "base/timer.h"
#include "base/qt_connection.h"

#include <QtCore/QPointer>

namespace Ui {
namespace {

constexpr auto kInactivePressTimeout = crl::time(200);

struct InactivePressedWidget {
	QWidget *widget = nullptr;
	base::qt_connection connection;
	base::Timer timer;
};

std::unique_ptr<InactivePressedWidget> Tracker;

} // namespace

void MarkInactivePress(not_null<QWidget*> widget, bool was) {
	if (!was) {
		if (WasInactivePress(widget)) {
			Tracker = nullptr;
		}
		return;
	}

	Tracker = std::make_unique<InactivePressedWidget>();
	Tracker->widget = widget;
	Tracker->connection = QObject::connect(widget, &QWidget::destroyed, [=] {
		Tracker->connection.release();
		Tracker = nullptr;
	});
	Tracker->timer.setCallback([=] {
		Tracker = nullptr;
	});
	Tracker->timer.callOnce(kInactivePressTimeout);
}

[[nodiscard]] bool WasInactivePress(not_null<QWidget*> widget) {
	return Tracker && (Tracker->widget == widget);
}

} // namespace Ui
