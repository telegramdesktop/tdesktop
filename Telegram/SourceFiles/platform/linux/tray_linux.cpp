/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/tray_linux.h"

#include "base/invoke_queued.h"
#include "base/qt_signal_producer.h"
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

namespace Platform {

namespace {

[[nodiscard]] QWidget *Parent() {
	Expects(Core::App().primaryWindow() != nullptr);
	return Core::App().primaryWindow()->widget();
}

} // namespace

class IconGraphic final {
public:
	explicit IconGraphic();
	~IconGraphic();

	[[nodiscard]] bool isRefreshNeeded(
		int counter,
		bool muted,
		const QString &iconThemeName) const;
	[[nodiscard]] QIcon trayIcon(int counter, bool muted);

private:
	[[nodiscard]] QString panelIconName(int counter, bool muted) const;
	[[nodiscard]] QString trayIconName(int counter, bool muted) const;
	[[nodiscard]] int counterSlice(int counter) const;
	void updateIconRegenerationNeeded(
		const QIcon &icon,
		int counter,
		bool muted,
		const QString &iconThemeName);
	[[nodiscard]] QSize dprSize(const QImage &image) const;

	const QString _panelTrayIconName;
	const QString _mutePanelTrayIconName;
	const QString _attentionPanelTrayIconName;

	const int _iconSizes[5];

	bool _muted = true;
	int32 _count = 0;
	base::flat_map<int, QImage> _imageBack;
	QIcon _trayIcon;
	QString _themeName;
	QString _name;

};

IconGraphic::IconGraphic()
: _panelTrayIconName("telegram-panel")
, _mutePanelTrayIconName("telegram-mute-panel")
, _attentionPanelTrayIconName("telegram-attention-panel")
, _iconSizes{ 16, 22, 24, 32, 48 } {
}

IconGraphic::~IconGraphic() = default;

QString IconGraphic::panelIconName(int counter, bool muted) const {
	return (counter > 0)
		? (muted
			? _mutePanelTrayIconName
			: _attentionPanelTrayIconName)
		: _panelTrayIconName;
}

QString IconGraphic::trayIconName(int counter, bool muted) const {
	const auto iconName = base::IconName();
	const auto panelName = panelIconName(counter, muted);

	if (QIcon::hasThemeIcon(panelName)) {
		return panelName;
	} else if (QIcon::hasThemeIcon(iconName)) {
		return iconName;
	}

	return QString();
}


int IconGraphic::counterSlice(int counter) const {
	return (counter >= 1000)
		? (1000 + (counter % 100))
		: counter;
}

bool IconGraphic::isRefreshNeeded(
		int counter,
		bool muted,
		const QString &iconThemeName) const {
	const auto iconName = trayIconName(counter, muted);

	return _trayIcon.isNull()
		|| iconThemeName != _themeName
		|| iconName != _name
		|| muted != _muted
		|| counterSlice(counter) != _count;
}

void IconGraphic::updateIconRegenerationNeeded(
		const QIcon &icon,
		int counter,
		bool muted,
		const QString &iconThemeName) {
	const auto iconName = trayIconName(counter, muted);

	_trayIcon = icon;
	_muted = muted;
	_count = counterSlice(counter);
	_themeName = iconThemeName;
	_name = iconName;
}

QSize IconGraphic::dprSize(const QImage &image) const {
	return image.size() / image.devicePixelRatio();
}

QIcon IconGraphic::trayIcon(int counter, bool muted) {
	const auto iconThemeName = QIcon::themeName();

	if (!isRefreshNeeded(counter, muted, iconThemeName)) {
		return _trayIcon;
	}

	const auto iconName = trayIconName(counter, muted);

	if (iconName == panelIconName(counter, muted)) {
		const auto result = QIcon::fromTheme(iconName);
		updateIconRegenerationNeeded(result, counter, muted, iconThemeName);
		return result;
	}

	QIcon result;
	QIcon systemIcon;

	for (const auto iconSize : _iconSizes) {
		auto &currentImageBack = _imageBack[iconSize];
		const auto desiredSize = QSize(iconSize, iconSize);

		if (currentImageBack.isNull()
			|| iconThemeName != _themeName
			|| iconName != _name) {
			if (!iconName.isEmpty()) {
				if (systemIcon.isNull()) {
					systemIcon = QIcon::fromTheme(iconName);
				}

				// We can't use QIcon::actualSize here
				// since it works incorrectly with svg icon themes
				currentImageBack = systemIcon
					.pixmap(desiredSize)
					.toImage();

				const auto firstAttemptSize = dprSize(currentImageBack);

				// if current icon theme is not a svg one, Qt can return
				// a pixmap that less in size even if there are a bigger one
				if (firstAttemptSize.width() < desiredSize.width()) {
					const auto availableSizes = systemIcon.availableSizes();

					const auto biggestSize = ranges::max_element(
						availableSizes,
						std::less<>(),
						&QSize::width);

					if (biggestSize->width() > firstAttemptSize.width()) {
						currentImageBack = systemIcon
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

		auto iconImage = currentImageBack;

		if (counter > 0) {
			const auto &bg = muted
				? st::trayCounterBgMute
				: st::trayCounterBg;
			const auto &fg = st::trayCounterFg;
			if (iconSize >= 22) {
				const auto layerSize = (iconSize >= 48)
					? 32
					: (iconSize >= 36)
					? 24
					: (iconSize >= 32)
					? 20
					: 16;
				const auto layer = Window::GenerateCounterLayer({
					.size = layerSize,
					.count = counter,
					.bg = bg,
					.fg = fg,
				});

				QPainter p(&iconImage);
				p.drawImage(
					iconImage.width() - layer.width() - 1,
					iconImage.height() - layer.height() - 1,
					layer);
			} else {
				iconImage = Window::WithSmallCounter(std::move(iconImage), {
					.size = 16,
					.count = counter,
					.bg = bg,
					.fg = fg,
				});
			}
		}

		result.addPixmap(Ui::PixmapFromImage(std::move(iconImage)));
	}

	updateIconRegenerationNeeded(result, counter, muted, iconThemeName);

	return result;
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
	LOG(("System tray available: %1").arg(Logs::b(TrayIconSupported())));
}

void Tray::createIcon() {
	if (!_icon) {
		if (!_iconGraphic) {
			_iconGraphic = std::make_unique<IconGraphic>();
		}

		const auto showXEmbed = [=] {
			_aboutToShowRequests.fire({});
			InvokeQueued(_menuXEmbed.get(), [=] {
				_menuXEmbed->popup(QCursor::pos());
			});
		};

		_icon = base::make_unique_q<QSystemTrayIcon>(Parent());
		_icon->setIcon(_iconGraphic->trayIcon(
			Core::App().unreadBadge(),
			Core::App().unreadBadgeMuted()));
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
	const auto counter = Core::App().unreadBadge();
	const auto muted = Core::App().unreadBadgeMuted();

	if (_iconGraphic->isRefreshNeeded(counter, muted, QIcon::themeName())) {
		_icon->setIcon(_iconGraphic->trayIcon(counter, muted));
	}
}

void Tray::createMenu() {
	if (!_menu) {
		_menu = base::make_unique_q<QMenu>(Parent());
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

} // namespace Platform
