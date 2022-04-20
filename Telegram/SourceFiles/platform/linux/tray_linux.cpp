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
#include "platform/linux/specific_linux.h"
#include "ui/ui_utility.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_controller.h"
#include "styles/style_window.h"

#include <QtCore/QCoreApplication>
#include <QtWidgets/QMenu>
#include <QtWidgets/QSystemTrayIcon>

namespace Platform {

namespace {

constexpr auto kPanelTrayIconName = "telegram-panel"_cs;
constexpr auto kMutePanelTrayIconName = "telegram-mute-panel"_cs;
constexpr auto kAttentionPanelTrayIconName = "telegram-attention-panel"_cs;

bool TrayIconMuted = true;
int32 TrayIconCount = 0;
base::flat_map<int, QImage> TrayIconImageBack;
QIcon TrayIcon;
QString TrayIconThemeName, TrayIconName;

[[nodiscard]] QString GetPanelIconName(int counter, bool muted) {
	return (counter > 0)
		? (muted
			? kMutePanelTrayIconName.utf16()
			: kAttentionPanelTrayIconName.utf16())
		: kPanelTrayIconName.utf16();
}

[[nodiscard]] QString GetTrayIconName(int counter, bool muted) {
	const auto iconName = GetIconName();
	const auto panelIconName = GetPanelIconName(counter, muted);

	if (QIcon::hasThemeIcon(panelIconName)) {
		return panelIconName;
	} else if (QIcon::hasThemeIcon(iconName)) {
		return iconName;
	}

	return QString();
}


[[nodiscard]] int GetCounterSlice(int counter) {
	return (counter >= 1000)
		? (1000 + (counter % 100))
		: counter;
}

[[nodiscard]] bool IsIconRegenerationNeeded(
		int counter,
		bool muted,
		const QString &iconThemeName = QIcon::themeName()) {
	const auto iconName = GetTrayIconName(counter, muted);
	const auto counterSlice = GetCounterSlice(counter);

	return TrayIcon.isNull()
		|| iconThemeName != TrayIconThemeName
		|| iconName != TrayIconName
		|| muted != TrayIconMuted
		|| counterSlice != TrayIconCount;
}

void UpdateIconRegenerationNeeded(
		const QIcon &icon,
		int counter,
		bool muted,
		const QString &iconThemeName) {
	const auto iconName = GetTrayIconName(counter, muted);
	const auto counterSlice = GetCounterSlice(counter);

	TrayIcon = icon;
	TrayIconMuted = muted;
	TrayIconCount = counterSlice;
	TrayIconThemeName = iconThemeName;
	TrayIconName = iconName;
}

[[nodiscard]] QIcon TrayIconGen(int counter, bool muted) {
	const auto iconThemeName = QIcon::themeName();

	if (!IsIconRegenerationNeeded(counter, muted, iconThemeName)) {
		return TrayIcon;
	}

	const auto iconName = GetTrayIconName(counter, muted);
	const auto panelIconName = GetPanelIconName(counter, muted);

	if (iconName == panelIconName) {
		const auto result = QIcon::fromTheme(iconName);
		UpdateIconRegenerationNeeded(result, counter, muted, iconThemeName);
		return result;
	}

	QIcon result;
	QIcon systemIcon;

	static const auto iconSizes = {
		16,
		22,
		24,
		32,
		48,
	};

	static const auto dprSize = [](const QImage &image) {
		return image.size() / image.devicePixelRatio();
	};

	for (const auto iconSize : iconSizes) {
		auto &currentImageBack = TrayIconImageBack[iconSize];
		const auto desiredSize = QSize(iconSize, iconSize);

		if (currentImageBack.isNull()
			|| iconThemeName != TrayIconThemeName
			|| iconName != TrayIconName) {
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

	UpdateIconRegenerationNeeded(result, counter, muted, iconThemeName);

	return result;
}

[[nodiscard]] QWidget *Parent() {
	Expects(Core::App().primaryWindow() != nullptr);
	return Core::App().primaryWindow()->widget();
}

} // namespace

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
}

void Tray::createIcon() {
	if (!_icon) {
		const auto showXEmbed = [=] {
			_aboutToShowRequests.fire({});
			InvokeQueued(_menuXEmbed.get(), [=] {
				_menuXEmbed->popup(QCursor::pos());
			});
		};

		_icon = base::make_unique_q<QSystemTrayIcon>(Parent());
		_icon->setIcon(TrayIconGen(
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
	if (!_icon) {
		return;
	}
	const auto counter = Core::App().unreadBadge();
	const auto muted = Core::App().unreadBadgeMuted();

	if (IsIconRegenerationNeeded(counter, muted, QIcon::themeName())) {
		_icon->setIcon(TrayIconGen(counter, muted));
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

rpl::lifetime &Tray::lifetime() {
	return _lifetime;
}

} // namespace Platform
