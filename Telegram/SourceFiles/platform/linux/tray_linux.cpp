/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/tray_linux.h"

#include "base/invoke_queued.h"
#include "base/qt_signal_producer.h"
#include "base/platform/linux/base_linux_dbus_utilities.h"
#include "core/application.h"
#include "core/sandbox.h"
#include "platform/platform_specific.h"
#include "ui/ui_utility.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_controller.h"
#include "styles/style_window.h"

#include <QtCore/QCoreApplication>
#include <QtWidgets/QMenu>
#include <QtWidgets/QSystemTrayIcon>

#include <gio/gio.hpp>

namespace Platform {
namespace {

using namespace gi::repository;

[[nodiscard]] QString PanelIconName(int counter, bool muted) {
	return (counter > 0)
		? (muted
			? u"telegram-mute-panel"_q
			: u"telegram-attention-panel"_q)
		: u"telegram-panel"_q;
}

} // namespace

class IconGraphic final {
public:
	explicit IconGraphic();
	~IconGraphic();

	void updateState();
	[[nodiscard]] bool isRefreshNeeded() const;
	[[nodiscard]] QIcon trayIcon();

private:
	struct State {
		QIcon systemIcon;
		QString iconThemeName;
		bool monochrome = false;
		int32 counter = 0;
		bool muted = false;
	};

	[[nodiscard]] QIcon systemIcon() const;
	[[nodiscard]] bool isCounterNeeded(const State &state) const;
	[[nodiscard]] int counterSlice(int counter) const;
	[[nodiscard]] QSize dprSize(const QImage &image) const;

	const int _iconSizes[7];

	base::flat_map<int, QImage> _imageBack;
	QIcon _trayIcon;
	State _current;
	State _new;

};

IconGraphic::IconGraphic()
: _iconSizes{ 16, 22, 32, 48, 64, 128, 256 } {
	updateState();
}

IconGraphic::~IconGraphic() = default;

QIcon IconGraphic::systemIcon() const {
	if (_new.iconThemeName == _current.iconThemeName
		&& _new.monochrome == _current.monochrome
		&& (_new.counter > 0) == (_current.counter > 0)
		&& _new.muted == _current.muted) {
		return _current.systemIcon;
	}

	const auto candidates = {
		_new.monochrome ? PanelIconName(_new.counter, _new.muted) : QString(),
		base::IconName(),
	};

	for (const auto &candidate : candidates) {
		if (candidate.isEmpty()) {
			continue;
		}
		const auto icon = QIcon::fromTheme(candidate);
		if (icon.name() == candidate) {
			return icon;
		}
	}

	return QIcon();
}

bool IconGraphic::isCounterNeeded(const State &state) const {
	return state.systemIcon.name() != PanelIconName(
		state.counter,
		state.muted);
}

int IconGraphic::counterSlice(int counter) const {
	return (counter >= 100)
		? (100 + (counter % 10))
		: counter;
}

QSize IconGraphic::dprSize(const QImage &image) const {
	return image.size() / image.devicePixelRatio();
}

void IconGraphic::updateState() {
	_new.iconThemeName = QIcon::themeName();
	_new.monochrome = Core::App().settings().trayIconMonochrome();
	_new.counter = Core::App().unreadBadge();
	_new.muted = Core::App().unreadBadgeMuted();
	_new.systemIcon = systemIcon();
}

bool IconGraphic::isRefreshNeeded() const {
	return _trayIcon.isNull()
		|| _new.iconThemeName != _current.iconThemeName
		|| _new.systemIcon.name() != _current.systemIcon.name()
		|| (isCounterNeeded(_new)
			? _new.muted != _current.muted
				|| counterSlice(_new.counter) != counterSlice(
						_current.counter)
			: false);
}

QIcon IconGraphic::trayIcon() {
	if (!isRefreshNeeded()) {
		return _trayIcon;
	}

	const auto guard = gsl::finally([&] {
		_current = _new;
	});

	if (!isCounterNeeded(_new)) {
		_trayIcon = _new.systemIcon;
		return _trayIcon;
	}

	QIcon result;
	for (const auto iconSize : _iconSizes) {
		auto &currentImageBack = _imageBack[iconSize];
		const auto desiredSize = QSize(iconSize, iconSize);

		if (currentImageBack.isNull()
			|| _new.iconThemeName != _current.iconThemeName
			|| _new.systemIcon.name() != _current.systemIcon.name()) {
			if (!_new.systemIcon.isNull()) {
				// We can't use QIcon::actualSize here
				// since it works incorrectly with svg icon themes
				currentImageBack = _new.systemIcon
					.pixmap(desiredSize)
					.toImage();

				const auto firstAttemptSize = dprSize(currentImageBack);

				// if current icon theme is not a svg one, Qt can return
				// a pixmap that less in size even if there are a bigger one
				if (firstAttemptSize.width() < desiredSize.width()) {
					const auto availableSizes
						= _new.systemIcon.availableSizes();

					const auto biggestSize = ranges::max_element(
						availableSizes,
						std::less<>(),
						&QSize::width);

					if (biggestSize->width() > firstAttemptSize.width()) {
						currentImageBack = _new.systemIcon
							.pixmap(*biggestSize)
							.toImage();
					}
				}
			} else {
				currentImageBack = Window::Logo();
			}

			if (dprSize(currentImageBack) != desiredSize) {
				currentImageBack = currentImageBack.scaled(
					desiredSize * currentImageBack.devicePixelRatio(),
					Qt::IgnoreAspectRatio,
					Qt::SmoothTransformation);
			}
		}

		result.addPixmap(Ui::PixmapFromImage(_new.counter > 0
			? Window::WithSmallCounter(std::move(currentImageBack), {
				.size = iconSize,
				.count = _new.counter,
				.bg = _new.muted ? st::trayCounterBgMute : st::trayCounterBg,
				.fg = st::trayCounterFg,
			}) : std::move(currentImageBack)));
	}

	_trayIcon = result;
	return _trayIcon;
}

class TrayEventFilter final : public QObject {
public:
	TrayEventFilter(not_null<QObject*> parent);

	[[nodiscard]] rpl::producer<> contextMenuFilters() const;

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	const QString _iconObjectName;
	rpl::event_stream<> _contextMenuFilters;

};

TrayEventFilter::TrayEventFilter(not_null<QObject*> parent)
: QObject(parent)
, _iconObjectName("QSystemTrayIconSys") {
	parent->installEventFilter(this);
}

bool TrayEventFilter::eventFilter(QObject *obj, QEvent *event) {
	if (event->type() == QEvent::MouseButtonPress
		&& obj->objectName() == _iconObjectName) {
		const auto m = static_cast<QMouseEvent*>(event);
		if (m->button() == Qt::RightButton) {
			Core::Sandbox::Instance().customEnterFromEventLoop([&] {
				_contextMenuFilters.fire({});
			});
			return true;
		}
	}
	return false;
}

rpl::producer<> TrayEventFilter::contextMenuFilters() const {
	return _contextMenuFilters.events();
}

Tray::Tray() {
	auto connection = Gio::bus_get_sync(Gio::BusType::SESSION_, nullptr);
	if (connection) {
		_sniWatcher = std::make_unique<base::Platform::DBus::ServiceWatcher>(
			connection.gobj_(),
			"org.kde.StatusNotifierWatcher",
			[=](
					const std::string &service,
					const std::string &oldOwner,
					const std::string &newOwner) {
				Core::Sandbox::Instance().customEnterFromEventLoop([&] {
					if (hasIcon()) {
						destroyIcon();
						createIcon();
					}
				});
			});
	}
}

void Tray::createIcon() {
	if (!_icon) {
		LOG(("System tray available: %1").arg(Logs::b(TrayIconSupported())));

		if (!_iconGraphic) {
			_iconGraphic = std::make_unique<IconGraphic>();
		}

		const auto showXEmbed = [=] {
			_aboutToShowRequests.fire({});
			InvokeQueued(_menuXEmbed.get(), [=] {
				_menuXEmbed->popup(QCursor::pos());
			});
		};

		_icon = base::make_unique_q<QSystemTrayIcon>(nullptr);
		_icon->setIcon(_iconGraphic->trayIcon());
		_icon->setToolTip(AppName.utf16());

		using Reason = QSystemTrayIcon::ActivationReason;
		base::qt_signal_producer(
			_icon.get(),
			&QSystemTrayIcon::activated
		) | rpl::start_with_next([=](Reason reason) {
			if (reason == QSystemTrayIcon::Context) {
				showXEmbed();
			} else {
				_iconClicks.fire({});
			}
		}, _lifetime);

		_icon->setContextMenu(_menu.get());

		if (!_eventFilter) {
			_eventFilter = base::make_unique_q<TrayEventFilter>(
				QCoreApplication::instance());
			_eventFilter->contextMenuFilters(
			) | rpl::start_with_next([=] {
				showXEmbed();
			}, _lifetime);
		}
	}
	updateIcon();

	_icon->show();
}

void Tray::destroyIcon() {
	_icon = nullptr;
}

void Tray::updateIcon() {
	if (!_icon || !_iconGraphic) {
		return;
	}

	_iconGraphic->updateState();
	if (_iconGraphic->isRefreshNeeded()) {
		_icon->setIcon(_iconGraphic->trayIcon());
	}
}

void Tray::createMenu() {
	if (!_menu) {
		_menu = base::make_unique_q<QMenu>(nullptr);
	}
	if (!_menuXEmbed) {
		_menuXEmbed = base::make_unique_q<Ui::PopupMenu>(nullptr);
		_menuXEmbed->deleteOnHide(false);
	}
}

void Tray::destroyMenu() {
	_menuXEmbed = nullptr;
	if (_menu) {
		_menu->clear();
	}
	_actionsLifetime.destroy();
}

void Tray::addAction(rpl::producer<QString> text, Fn<void()> &&callback) {
	if (_menuXEmbed) {
		const auto XEAction = _menuXEmbed->addAction(QString(), callback);
		rpl::duplicate(
			text
		) | rpl::start_with_next([=](const QString &text) {
			XEAction->setText(text);
		}, _actionsLifetime);
	}

	if (_menu) {
		const auto action = _menu->addAction(QString(), std::move(callback));
		std::move(
			text
		) | rpl::start_with_next([=](const QString &text) {
			action->setText(text);
		}, _actionsLifetime);
	}
}

void Tray::showTrayMessage() const {
}

bool Tray::hasTrayMessageSupport() const {
	return false;
}

rpl::producer<> Tray::aboutToShowRequests() const {
	return rpl::merge(
		_aboutToShowRequests.events(),
		_menu
			? base::qt_signal_producer(_menu.get(), &QMenu::aboutToShow)
			: rpl::never<>() | rpl::type_erased());
}

rpl::producer<> Tray::showFromTrayRequests() const {
	return rpl::never<>();
}

rpl::producer<> Tray::hideToTrayRequests() const {
	return rpl::never<>();
}

rpl::producer<> Tray::iconClicks() const {
	return _iconClicks.events();
}

bool Tray::hasIcon() const {
	return _icon;
}

rpl::lifetime &Tray::lifetime() {
	return _lifetime;
}

Tray::~Tray() = default;

bool HasMonochromeSetting() {
	return QIcon::hasThemeIcon(
		PanelIconName(
			Core::App().unreadBadge(),
			Core::App().unreadBadgeMuted()));
}

} // namespace Platform
