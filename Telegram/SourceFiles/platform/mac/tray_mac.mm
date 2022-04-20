/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/mac/tray_mac.h"

#include "base/qt_signal_producer.h"
#include "core/application.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "ui/ui_utility.h"
#include "styles/style_window.h"

#include <QtWidgets/QMenu>
#include <QtWidgets/QSystemTrayIcon>

namespace Platform {

namespace {

[[nodiscard]] QImage TrayIconBack(bool darkMode) {
	static const auto WithColor = [](QColor color) {
		return st::macTrayIcon.instance(color, 100);
	};
	static const auto DarkModeResult = WithColor({ 255, 255, 255 });
	static const auto LightModeResult = WithColor({ 0, 0, 0, 180 });
	auto result = darkMode ? DarkModeResult : LightModeResult;
	result.detach();
	return result;
}

void PlaceCounter(
		QImage &img,
		int size,
		int count,
		style::color bg,
		style::color color) {
	if (!count) {
		return;
	}
	const auto savedRatio = img.devicePixelRatio();
	img.setDevicePixelRatio(1.);

	{
		Painter p(&img);
		PainterHighQualityEnabler hq(p);

		const auto cnt = (count < 100)
			? QString("%1").arg(count)
			: QString("..%1").arg(count % 100, 2, 10, QChar('0'));
		const auto cntSize = cnt.size();

		p.setBrush(bg);
		p.setPen(Qt::NoPen);
		int32 fontSize, skip;
		if (size == 22) {
			skip = 1;
			fontSize = 8;
		} else {
			skip = 2;
			fontSize = 16;
		}
		style::font f(fontSize, 0, 0);
		int32 w = f->width(cnt), d, r;
		if (size == 22) {
			d = (cntSize < 2) ? 3 : 2;
			r = (cntSize < 2) ? 6 : 5;
		} else {
			d = (cntSize < 2) ? 6 : 5;
			r = (cntSize < 2) ? 9 : 11;
		}
		p.drawRoundedRect(
			QRect(
				size - w - d * 2 - skip,
				size - f->height - skip,
				w + d * 2,
				f->height),
			r,
			r);

		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.setFont(f);
		p.setPen(color);
		p.drawText(
			size - w - d - skip,
			size - f->height + f->ascent - skip,
			cnt);
	}
	img.setDevicePixelRatio(savedRatio);
}

[[nodiscard]] QIcon GenerateIconForTray(int counter, bool muted) {
	auto result = QIcon();
	auto lightMode = TrayIconBack(false);
	auto darkMode = TrayIconBack(true);
	auto lightModeActive = darkMode;
	auto darkModeActive = darkMode;
	lightModeActive.detach();
	darkModeActive.detach();
	const auto size = 22 * cIntRetinaFactor();
	const auto &bg = (muted ? st::trayCounterBgMute : st::trayCounterBg);

	const auto &fg = st::trayCounterFg;
	const auto &fgInvert = st::trayCounterFgMacInvert;
	const auto &bgInvert = st::trayCounterBgMacInvert;

	PlaceCounter(lightMode, size, counter, bg, fg);
	PlaceCounter(darkMode, size, counter, bg, muted ? fgInvert : fg);
	PlaceCounter(lightModeActive, size, counter, bgInvert, fgInvert);
	PlaceCounter(darkModeActive, size, counter, bgInvert, fgInvert);
	result.addPixmap(Ui::PixmapFromImage(
		std::move(lightMode)),
		QIcon::Normal,
		QIcon::Off);
	result.addPixmap(Ui::PixmapFromImage(
		std::move(darkMode)),
		QIcon::Normal,
		QIcon::On);
	result.addPixmap(Ui::PixmapFromImage(
		std::move(lightModeActive)),
		QIcon::Active,
		QIcon::Off);
	result.addPixmap(Ui::PixmapFromImage(
		std::move(darkModeActive)),
		QIcon::Active,
		QIcon::On);
	return result;
}

[[nodiscard]] QWidget *Parent() {
	Expects(Core::App().primaryWindow() != nullptr);
	return Core::App().primaryWindow()->widget();
}

} // namespace

Tray::Tray() {
}

void Tray::createIcon() {
	if (!_icon) {
		_icon = base::make_unique_q<QSystemTrayIcon>(Parent());
		updateIcon();
		if (Core::App().isActiveForTrayMenu()) {
			_icon->setContextMenu(_menu.get());
		} else {
			_icon->setContextMenu(nullptr);
		}
		_icon->setContextMenu(_menu.get()); // Todo.
		// attachToTrayIcon(_icon);
	} else {
		updateIcon();
	}

	_icon->show();
}

void Tray::destroyIcon() {
	_icon = nullptr;
}

void Tray::updateIcon() {
	if (_icon) {
		const auto counter = Core::App().unreadBadge();
		const auto muted = Core::App().unreadBadgeMuted();
		_icon->setIcon(GenerateIconForTray(counter, muted));
	}
}

void Tray::createMenu() {
	if (!_menu) {
		_menu = base::make_unique_q<QMenu>(Parent());
	}
}

void Tray::destroyMenu() {
	if (_menu) {
		_menu->clear();
	}
	_actionsLifetime.destroy();
}

void Tray::addAction(rpl::producer<QString> text, Fn<void()> &&callback) {
	if (!_menu) {
		return;
	}

	const auto action = _menu->addAction(QString(), std::move(callback));
	std::move(
		text
	) | rpl::start_with_next([=](const QString &text) {
		action->setText(text);
	}, _actionsLifetime);
}

void Tray::showTrayMessage() const {
}

bool Tray::hasTrayMessageSupport() const {
	return false;
}

rpl::producer<> Tray::aboutToShowRequests() const {
	return _menu
		? base::qt_signal_producer(_menu.get(), &QMenu::aboutToShow)
		: rpl::never<>();
}

rpl::producer<> Tray::showFromTrayRequests() const {
	return rpl::never<>();
}

rpl::producer<> Tray::hideToTrayRequests() const {
	return rpl::never<>();
}

rpl::producer<> Tray::iconClicks() const {
	return rpl::never<>();
}

rpl::lifetime &Tray::lifetime() {
	return _lifetime;
}

} // namespace Platform
