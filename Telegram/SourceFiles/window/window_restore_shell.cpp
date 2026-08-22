/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_restore_shell.h"

#include "base/event_filter.h"
#include "lang/lang_keys.h"
#include "platform/platform_main_window.h"
#include "settings.h"
#include "ui/effects/radial_animation.h"
#include "ui/painter.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/rp_window.h"
#include "window/main_window.h"
#include "styles/style_window.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>
#include <QtGui/QWindow>

namespace Window {

RestoreShell::RestoreShell(
	const QString &title,
	Core::WindowPosition position)
: _window(base::make_unique_q<Ui::RpWindow>()) {
	_window->setTitle(title.isEmpty() ? u"Telegram"_q : title);
	_window->setMinimumSize(
		QSize(st::windowShellMinWidth, st::windowShellMinHeight));
	setupBody();
	applyPosition(position);
	base::install_event_filter(_window.get(), [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::Close) {
			_closeRequests.fire({});
		}
		return base::EventFilterResult::Continue;
	});
	_window->show();
}

RestoreShell::~RestoreShell() = default;

void RestoreShell::setupBody() {
	const auto body = _window->body();
	_content = Ui::CreateChild<Ui::RpWidget>(body.get());
	body->sizeValue() | rpl::on_next([=](QSize size) {
		_content->setGeometry(QRect(QPoint(), size));
	}, _content->lifetime());
	_loading = std::make_unique<Ui::InfiniteRadialAnimation>(
		[=] { _content->update(); },
		st::windowShellLoading);
	_content->paintRequest() | rpl::on_next([=](QRect clip) {
		auto p = QPainter(_content);
		p.fillRect(clip, st::windowBg);
		if (_loading) {
			const auto size = st::windowShellLoading.size;
			const auto position = QPoint(
				(_content->width() - size.width()) / 2,
				(_content->height() - size.height()) / 2);
			_loading->draw(p, position, size, _content->width());
		}
	}, _content->lifetime());
	_content->show();
	_loading->start();
}

void RestoreShell::showUnavailable() {
	if (_unavailable) {
		return;
	}
	_unavailable = true;
	_loading = nullptr;
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		_content,
		tr::lng_restore_windows_unavailable(tr::now),
		st::windowShellUnavailableLabel);
	_content->sizeValue() | rpl::on_next([=](QSize size) {
		const auto padding = st::windowShellUnavailablePadding;
		const auto available = size.width() - padding.left() - padding.right();
		if (available <= 0) {
			return;
		}
		label->resizeToWidth(std::min(label->naturalWidth(), available));
		label->move(
			(size.width() - label->width()) / 2,
			(size.height() - label->height()) / 2);
	}, label->lifetime());
	label->show();
	_content->update();
}

void RestoreShell::close() {
	_window->close();
}

void RestoreShell::activate() {
	_window->raise();
	_window->activateWindow();
}

bool RestoreShell::isActiveWindow() const {
	return _window->isActiveWindow();
}

Core::WindowPosition RestoreShell::countPositionForSave() const {
	auto result = Core::WindowPosition{ .scale = cScale() };
	const auto handle = _window->windowHandle();
	const auto state = handle ? handle->windowState() : Qt::WindowNoState;
	const auto screen = _window->screen();
	const auto screenGeometry = screen ? screen->geometry() : QRect();
	if (screen) {
		result.moncrc = Platform::ScreenNameChecksum(screen->name());
	}
	if (state == Qt::WindowMaximized || state == Qt::WindowFullScreen) {
		result.maximized = 1;
		const auto normal = _window->normalGeometry().marginsRemoved(
			_window->frameMargins());
		result.x = normal.x() - screenGeometry.x();
		result.y = normal.y() - screenGeometry.y();
		result.w = normal.width();
		result.h = normal.height();
		return result;
	}
	const auto body = _window->body();
	const auto rect = QRect(
		body->mapToGlobal(QPoint()),
		body->size());
	result.x = rect.x() - screenGeometry.x();
	result.y = rect.y() - screenGeometry.y();
	result.w = rect.width();
	result.h = rect.height();
	return result;
}

rpl::producer<> RestoreShell::closeRequests() const {
	return _closeRequests.events();
}

rpl::lifetime &RestoreShell::lifetime() {
	return _lifetime;
}

void RestoreShell::applyPosition(Core::WindowPosition position) {
	const auto primary = QGuiApplication::primaryScreen();
	const auto available = primary
		? primary->availableGeometry()
		: QRect(0, 0, st::windowDefaultWidth, st::windowDefaultHeight);
	const auto initial = Core::WindowPosition{
		.x = (available.x()
			+ std::max((available.width() - st::windowDefaultWidth) / 2, 0)),
		.y = (available.y()
			+ std::max(
				(available.height() - st::windowDefaultHeight) / 2,
				0)),
		.w = st::windowDefaultWidth,
		.h = st::windowDefaultHeight,
	};
	const auto geometry = CountInitialGeometry(
		_window.get(),
		Core::AdjustToScale(position, u"Shell"_q),
		initial,
		QSize(st::windowShellMinWidth, st::windowShellMinHeight),
		u"Shell"_q);
	_window->setGeometry(geometry);
	if (position.maximized) {
		_window->setWindowState(Qt::WindowMaximized);
	}
}

} // namespace Window
