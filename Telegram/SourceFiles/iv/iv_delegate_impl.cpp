/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/iv_delegate_impl.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "mainwindow.h"
#include "window/main_window.h"
#include "window/window_controller.h"
#include "styles/style_window.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>
#include <QtGui/QWindow>

namespace Iv {
namespace {

[[nodiscard]] Core::WindowPosition DefaultPosition() {
	auto center = qApp->primaryScreen()->geometry().center();
	const auto moncrc = [&] {
		if (const auto active = Core::App().activeWindow()) {
			const auto widget = active->widget();
			center = widget->geometry().center();
			if (const auto screen = widget->screen()) {
				return Platform::ScreenNameChecksum(screen->name());
			}
		}
		return Core::App().settings().windowPosition().moncrc;
	}();
	return {
		.moncrc = moncrc,
		.scale = cScale(),
		.x = (center.x() - st::ivWidthDefault / 2),
		.y = (center.y() - st::ivHeightDefault / 2),
		.w = st::ivWidthDefault,
		.h = st::ivHeightDefault,
	};
}

} // namespace

void DelegateImpl::ivSetLastSourceWindow(not_null<QWidget*> window) {
	_lastSourceWindow = window;
}

QRect DelegateImpl::ivGeometry() const {
	const auto found = _lastSourceWindow
		? Core::App().findWindow(_lastSourceWindow)
		: nullptr;

	const auto saved = Core::App().settings().ivPosition();
	const auto adjusted = Core::AdjustToScale(saved, u"IV"_q);
	const auto initial = DefaultPosition();
	auto result = initial.rect();
	if (const auto window = found ? found : Core::App().activeWindow()) {
		result = window->widget()->countInitialGeometry(
			adjusted,
			initial,
			{ st::ivWidthMin, st::ivHeightMin });
	}
	return result;
}

void DelegateImpl::ivSaveGeometry(not_null<QWidget*> window) {
	if (!window->windowHandle()) {
		return;
	}
	const auto state = window->windowHandle()->windowState();
	if (state == Qt::WindowMinimized) {
		return;
	}
	const auto &savedPosition = Core::App().settings().ivPosition();
	auto realPosition = savedPosition;
	if (state == Qt::WindowMaximized) {
		realPosition.maximized = 1;
		realPosition.moncrc = 0;
		DEBUG_LOG(("IV Pos: Saving maximized position."));
	} else {
		auto r = window->geometry();
		realPosition.x = r.x();
		realPosition.y = r.y();
		realPosition.w = r.width();
		realPosition.h = r.height();
		realPosition.scale = cScale();
		realPosition.maximized = 0;
		realPosition.moncrc = 0;
		DEBUG_LOG(("IV Pos: "
			"Saving non-maximized position: %1, %2, %3, %4"
			).arg(realPosition.x
			).arg(realPosition.y
			).arg(realPosition.w
			).arg(realPosition.h));
	}
	realPosition = Window::PositionWithScreen(
		realPosition,
		window,
		{ st::ivWidthMin, st::ivHeightMin });
	if (realPosition.w >= st::ivWidthMin
		&& realPosition.h >= st::ivHeightMin
		&& realPosition != savedPosition) {
		DEBUG_LOG(("IV Pos: "
			"Writing: %1, %2, %3, %4 (scale %5%, maximized %6)")
			.arg(realPosition.x)
			.arg(realPosition.y)
			.arg(realPosition.w)
			.arg(realPosition.h)
			.arg(realPosition.scale)
			.arg(Logs::b(realPosition.maximized)));
		Core::App().settings().setIvPosition(realPosition);
		Core::App().saveSettingsDelayed();
	}
}

} // namespace Iv
