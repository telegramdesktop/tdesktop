/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/main_window_linux.h"

#include "styles/style_window.h"
#include "platform/linux/specific_linux.h"
#include "platform/linux/linux_wayland_integration.h"
#include "history/history.h"
#include "history/history_widget.h"
#include "history/history_inner_widget.h"
#include "main/main_account.h" // Account::sessionChanges.
#include "main/main_session.h"
#include "mainwindow.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/sandbox.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/about_box.h"
#include "lang/lang_keys.h"
#include "storage/localstorage.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "base/platform/base_platform_info.h"
#include "base/event_filter.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/input_fields.h"
#include "ui/ui_utility.h"

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
#include "base/platform/linux/base_linux_glibmm_helper.h"
#include "base/platform/linux/base_linux_dbus_utilities.h"
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
#include "base/platform/linux/base_linux_xcb_utilities.h"
#endif // !DESKTOP_APP_DISABLE_X11_INTEGRATION

#include <QtCore/QSize>
#include <QtCore/QMimeData>
#include <QtGui/QWindow>
#include <QtWidgets/QMenuBar>

#include <private/qguiapplication_p.h>
#include <private/qaction_p.h>

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
#include <glibmm.h>
#include <giomm.h>
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

namespace Platform {
namespace {

using internal::WaylandIntegration;
using WorkMode = Core::Settings::WorkMode;

constexpr auto kPanelTrayIconName = "telegram-panel"_cs;
constexpr auto kMutePanelTrayIconName = "telegram-mute-panel"_cs;
constexpr auto kAttentionPanelTrayIconName = "telegram-attention-panel"_cs;

bool TrayIconMuted = true;
int32 TrayIconCount = 0;
base::flat_map<int, QImage> TrayIconImageBack;
QIcon TrayIcon;
QString TrayIconThemeName, TrayIconName;

#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
void XCBSkipTaskbar(QWindow *window, bool skip) {
	const auto connection = base::Platform::XCB::GetConnectionFromQt();
	if (!connection) {
		return;
	}

	const auto root = base::Platform::XCB::GetRootWindow(connection);
	if (!root.has_value()) {
		return;
	}

	const auto stateAtom = base::Platform::XCB::GetAtom(
		connection,
		"_NET_WM_STATE");

	if (!stateAtom.has_value()) {
		return;
	}

	const auto skipTaskbarAtom = base::Platform::XCB::GetAtom(
		connection,
		"_NET_WM_STATE_SKIP_TASKBAR");

	if (!skipTaskbarAtom.has_value()) {
		return;
	}

	xcb_client_message_event_t xev;
	xev.response_type = XCB_CLIENT_MESSAGE;
	xev.type = *stateAtom;
	xev.sequence = 0;
	xev.window = window->winId();
	xev.format = 32;
	xev.data.data32[0] = skip ? 1 : 0;
	xev.data.data32[1] = *skipTaskbarAtom;
	xev.data.data32[2] = 0;
	xev.data.data32[3] = 0;
	xev.data.data32[4] = 0;

	xcb_send_event(
		connection,
		false,
		*root,
		XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT
			| XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
		reinterpret_cast<const char*>(&xev));
}

void XCBSetDesktopFileName(QWindow *window) {
	const auto connection = base::Platform::XCB::GetConnectionFromQt();
	if (!connection) {
		return;
	}

	const auto desktopFileAtom = base::Platform::XCB::GetAtom(
		connection,
		"_KDE_NET_WM_DESKTOP_FILE");

	const auto utf8Atom = base::Platform::XCB::GetAtom(
		connection,
		"UTF8_STRING");

	if (!desktopFileAtom.has_value() || !utf8Atom.has_value()) {
		return;
	}

	const auto filename = QGuiApplication::desktopFileName()
		.chopped(8)
		.toUtf8();

	xcb_change_property(
		connection,
		XCB_PROP_MODE_REPLACE,
		window->winId(),
		*desktopFileAtom,
		*utf8Atom,
		8,
		filename.size(),
		filename.data());
}
#endif // !DESKTOP_APP_DISABLE_X11_INTEGRATION

void SkipTaskbar(QWindow *window, bool skip) {
	if (const auto integration = WaylandIntegration::Instance()) {
		integration->skipTaskbar(window, skip);
	}

#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
	if (IsX11()) {
		XCBSkipTaskbar(window, skip);
	}
#endif // !DESKTOP_APP_DISABLE_X11_INTEGRATION
}

QString GetPanelIconName(int counter, bool muted) {
	return (counter > 0)
		? (muted
			? kMutePanelTrayIconName.utf16()
			: kAttentionPanelTrayIconName.utf16())
		: kPanelTrayIconName.utf16();
}

QString GetTrayIconName(int counter, bool muted) {
	const auto iconName = GetIconName();
	const auto panelIconName = GetPanelIconName(counter, muted);

	if (QIcon::hasThemeIcon(panelIconName)) {
		return panelIconName;
	} else if (QIcon::hasThemeIcon(iconName)) {
		return iconName;
	}

	return QString();
}

int GetCounterSlice(int counter) {
	return (counter >= 1000)
		? (1000 + (counter % 100))
		: counter;
}

bool IsIconRegenerationNeeded(
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

QIcon TrayIconGen(int counter, bool muted) {
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

void SendKeySequence(
	Qt::Key key,
	Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
	const auto focused = QApplication::focusWidget();
	if (qobject_cast<QLineEdit*>(focused)
		|| qobject_cast<QTextEdit*>(focused)
		|| dynamic_cast<HistoryInner*>(focused)) {
		QApplication::postEvent(
			focused,
			new QKeyEvent(QEvent::KeyPress, key, modifiers));

		QApplication::postEvent(
			focused,
			new QKeyEvent(QEvent::KeyRelease, key, modifiers));
	}
}

void ForceDisabled(QAction *action, bool disabled) {
	if (action->isEnabled()) {
		if (disabled) action->setDisabled(true);
	} else if (!disabled) {
		action->setDisabled(false);

		const auto privateAction = QActionPrivate::get(action);
		privateAction->setShortcutEnabled(
			false,
			QGuiApplicationPrivate::instance()->shortcutMap);
	}
}

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
bool UseUnityCounter() {
	static const auto Result = [&] {
		try {
			const auto connection = Gio::DBus::Connection::get_sync(
				Gio::DBus::BusType::BUS_TYPE_SESSION);

			return base::Platform::DBus::NameHasOwner(
				connection,
				"com.canonical.Unity");
		} catch (...) {
		}

		return false;
	}();

	return Result;
}

uint djbStringHash(const std::string &string) {
	uint hash = 5381;
	for (const auto &curChar : string) {
		hash = (hash << 5) + hash + curChar;
	}
	return hash;
}
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

} // namespace

MainWindow::MainWindow(not_null<Window::Controller*> controller)
: Window::MainWindow(controller) {
}

void MainWindow::initHook() {
	base::install_event_filter(windowHandle(), [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::Expose) {
			auto ee = static_cast<QExposeEvent*>(e.get());
			if (ee->region().isNull()) {
				return base::EventFilterResult::Continue;
			}
			if (!windowHandle()
				|| windowHandle()->parent()
				|| !windowHandle()->isVisible()) {
				return base::EventFilterResult::Continue;
			}
			handleNativeSurfaceChanged(true);
		} else if (e->type() == QEvent::Hide) {
			if (!windowHandle() || windowHandle()->parent()) {
				return base::EventFilterResult::Continue;
			}
			handleNativeSurfaceChanged(false);
		}
		return base::EventFilterResult::Continue;
	});

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	if (UseUnityCounter()) {
		LOG(("Using Unity launcher counter."));
	} else {
		LOG(("Not using Unity launcher counter."));
	}
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

#ifndef DESKTOP_APP_DISABLE_X11_INTEGRATION
	XCBSetDesktopFileName(windowHandle());
#endif // !DESKTOP_APP_DISABLE_X11_INTEGRATION

	LOG(("System tray available: %1").arg(Logs::b(TrayIconSupported())));
}

bool MainWindow::hasTrayIcon() const {
	return trayIcon;
}

bool MainWindow::isActiveForTrayMenu() {
	updateIsActive();
	return Platform::IsWayland() ? isVisible() : isActive();
}

void MainWindow::psShowTrayMenu() {
	_trayIconMenuXEmbed->popup(QCursor::pos());
}

void MainWindow::psTrayMenuUpdated() {
}

void MainWindow::psSetupTrayIcon() {
	if (!trayIcon) {
		trayIcon = new QSystemTrayIcon(this);
		trayIcon->setContextMenu(trayIconMenu);
		trayIcon->setIcon(TrayIconGen(
			Core::App().unreadBadge(),
			Core::App().unreadBadgeMuted()));

		attachToTrayIcon(trayIcon);
	}
	updateIconCounters();

	trayIcon->show();
}

void MainWindow::workmodeUpdated(Core::Settings::WorkMode mode) {
	if (!TrayIconSupported()) {
		return;
	} else if (mode == WorkMode::WindowOnly) {
		if (trayIcon) {
			trayIcon->setContextMenu(0);
			trayIcon->deleteLater();
		}
		trayIcon = nullptr;
	} else {
		psSetupTrayIcon();
	}

	SkipTaskbar(windowHandle(), mode == WorkMode::TrayOnly);
}

void MainWindow::unreadCounterChangedHook() {
	updateIconCounters();
}

void MainWindow::updateIconCounters() {
	const auto counter = Core::App().unreadBadge();
	const auto muted = Core::App().unreadBadgeMuted();

	updateWindowIcon();

#ifndef DESKTOP_APP_DISABLE_DBUS_INTEGRATION
	if (UseUnityCounter()) {
		const auto launcherUrl = Glib::ustring(
			"application://"
				+ QGuiApplication::desktopFileName().toStdString());
		const auto counterSlice = std::min(counter, 9999);
		std::map<Glib::ustring, Glib::VariantBase> dbusUnityProperties;

		if (counterSlice > 0) {
			// According to the spec, it should be of 'x' D-Bus signature,
			// which corresponds to gint64 type with glib
			// https://wiki.ubuntu.com/Unity/LauncherAPI#Low_level_DBus_API:_com.canonical.Unity.LauncherEntry
			dbusUnityProperties["count"] = Glib::Variant<gint64>::create(
				counterSlice);
			dbusUnityProperties["count-visible"] =
				Glib::Variant<bool>::create(true);
		} else {
			dbusUnityProperties["count-visible"] =
				Glib::Variant<bool>::create(false);
		}

		try {
			const auto connection = Gio::DBus::Connection::get_sync(
				Gio::DBus::BusType::BUS_TYPE_SESSION);

			connection->emit_signal(
				"/com/canonical/unity/launcherentry/"
					+ std::to_string(djbStringHash(launcherUrl)),
				"com.canonical.Unity.LauncherEntry",
				"Update",
				{},
				base::Platform::MakeGlibVariant(std::tuple{
					launcherUrl,
					dbusUnityProperties,
				}));
		} catch (...) {
		}
	}
#endif // !DESKTOP_APP_DISABLE_DBUS_INTEGRATION

	if (trayIcon && IsIconRegenerationNeeded(counter, muted)) {
		trayIcon->setIcon(TrayIconGen(counter, muted));
	}
}

void MainWindow::initTrayMenuHook() {
	_trayIconMenuXEmbed.emplace(nullptr, trayIconMenu);
	_trayIconMenuXEmbed->deleteOnHide(false);
}

void MainWindow::createGlobalMenu() {
	const auto ensureWindowShown = [=] {
		if (isHidden()) {
			showFromTray();
		}
	};

	psMainMenu = new QMenuBar(this);
	psMainMenu->hide();

	auto file = psMainMenu->addMenu(tr::lng_mac_menu_file(tr::now));

	psLogout = file->addAction(
		tr::lng_mac_menu_logout(tr::now),
		this,
		[=] {
			ensureWindowShown();
			controller().showLogoutConfirmation();
		});

	auto quit = file->addAction(
		tr::lng_mac_menu_quit_telegram(tr::now, lt_telegram, qsl("Telegram")),
		this,
		[=] { quitFromTray(); },
		QKeySequence::Quit);

	quit->setMenuRole(QAction::QuitRole);

	auto edit = psMainMenu->addMenu(tr::lng_mac_menu_edit(tr::now));

	psUndo = edit->addAction(
		tr::lng_linux_menu_undo(tr::now),
		[] { SendKeySequence(Qt::Key_Z, Qt::ControlModifier); },
		QKeySequence::Undo);

	psRedo = edit->addAction(
		tr::lng_linux_menu_redo(tr::now),
		[] {
			SendKeySequence(
				Qt::Key_Z,
				Qt::ControlModifier | Qt::ShiftModifier);
		},
		QKeySequence::Redo);

	edit->addSeparator();

	psCut = edit->addAction(
		tr::lng_mac_menu_cut(tr::now),
		[] { SendKeySequence(Qt::Key_X, Qt::ControlModifier); },
		QKeySequence::Cut);

	psCopy = edit->addAction(
		tr::lng_mac_menu_copy(tr::now),
		[] { SendKeySequence(Qt::Key_C, Qt::ControlModifier); },
		QKeySequence::Copy);

	psPaste = edit->addAction(
		tr::lng_mac_menu_paste(tr::now),
		[] { SendKeySequence(Qt::Key_V, Qt::ControlModifier); },
		QKeySequence::Paste);

	psDelete = edit->addAction(
		tr::lng_mac_menu_delete(tr::now),
		[] { SendKeySequence(Qt::Key_Delete); },
		QKeySequence(Qt::ControlModifier | Qt::Key_Backspace));

	edit->addSeparator();

	psBold = edit->addAction(
		tr::lng_menu_formatting_bold(tr::now),
		[] { SendKeySequence(Qt::Key_B, Qt::ControlModifier); },
		QKeySequence::Bold);

	psItalic = edit->addAction(
		tr::lng_menu_formatting_italic(tr::now),
		[] { SendKeySequence(Qt::Key_I, Qt::ControlModifier); },
		QKeySequence::Italic);

	psUnderline = edit->addAction(
		tr::lng_menu_formatting_underline(tr::now),
		[] { SendKeySequence(Qt::Key_U, Qt::ControlModifier); },
		QKeySequence::Underline);

	psStrikeOut = edit->addAction(
		tr::lng_menu_formatting_strike_out(tr::now),
		[] {
			SendKeySequence(
				Qt::Key_X,
				Qt::ControlModifier | Qt::ShiftModifier);
		},
		Ui::kStrikeOutSequence);

	psMonospace = edit->addAction(
		tr::lng_menu_formatting_monospace(tr::now),
		[] {
			SendKeySequence(
				Qt::Key_M,
				Qt::ControlModifier | Qt::ShiftModifier);
		},
		Ui::kMonospaceSequence);

	psClearFormat = edit->addAction(
		tr::lng_menu_formatting_clear(tr::now),
		[] {
			SendKeySequence(
				Qt::Key_N,
				Qt::ControlModifier | Qt::ShiftModifier);
		},
		Ui::kClearFormatSequence);

	edit->addSeparator();

	psSelectAll = edit->addAction(
		tr::lng_mac_menu_select_all(tr::now),
		[] { SendKeySequence(Qt::Key_A, Qt::ControlModifier); },
		QKeySequence::SelectAll);

	edit->addSeparator();

	auto prefs = edit->addAction(
		tr::lng_mac_menu_preferences(tr::now),
		this,
		[=] {
			ensureWindowShown();
			controller().showSettings();
		},
		QKeySequence(Qt::ControlModifier | Qt::Key_Comma));

	prefs->setMenuRole(QAction::PreferencesRole);

	auto tools = psMainMenu->addMenu(tr::lng_linux_menu_tools(tr::now));

	psContacts = tools->addAction(
		tr::lng_mac_menu_contacts(tr::now),
		crl::guard(this, [=] {
			if (isHidden()) {
				showFromTray();
			}

			if (!sessionController()) {
				return;
			}

			sessionController()->show(
				PrepareContactsBox(sessionController()));
		}));

	psAddContact = tools->addAction(
		tr::lng_mac_menu_add_contact(tr::now),
		this,
		[=] {
			Expects(sessionController() != nullptr);
			ensureWindowShown();
			sessionController()->showAddContact();
		});

	tools->addSeparator();

	psNewGroup = tools->addAction(
		tr::lng_mac_menu_new_group(tr::now),
		this,
		[=] {
			Expects(sessionController() != nullptr);
			ensureWindowShown();
			sessionController()->showNewGroup();
		});

	psNewChannel = tools->addAction(
		tr::lng_mac_menu_new_channel(tr::now),
		this,
		[=] {
			Expects(sessionController() != nullptr);
			ensureWindowShown();
			sessionController()->showNewChannel();
		});

	auto help = psMainMenu->addMenu(tr::lng_linux_menu_help(tr::now));

	auto about = help->addAction(
		tr::lng_mac_menu_about_telegram(
			tr::now,
			lt_telegram,
			qsl("Telegram")),
		[=] {
			ensureWindowShown();
			controller().show(Box<AboutBox>());
		});

	about->setMenuRole(QAction::AboutQtRole);

	// avoid shadowing actual shortcuts by the menubar
	for (const auto &child : psMainMenu->children()) {
		const auto action = qobject_cast<QAction*>(child);
		if (action) {
			const auto privateAction = QActionPrivate::get(action);
			privateAction->setShortcutEnabled(
				false,
				QGuiApplicationPrivate::instance()->shortcutMap);
		}
	}

	updateGlobalMenu();
}

void MainWindow::updateGlobalMenuHook() {
	if (!positionInited()) {
		return;
	}

	const auto focused = QApplication::focusWidget();
	auto canUndo = false;
	auto canRedo = false;
	auto canCut = false;
	auto canCopy = false;
	auto canPaste = false;
	auto canDelete = false;
	auto canSelectAll = false;
	const auto mimeData = QGuiApplication::clipboard()->mimeData();
	const auto clipboardHasText = mimeData ? mimeData->hasText() : false;
	auto markdownEnabled = false;
	if (const auto edit = qobject_cast<QLineEdit*>(focused)) {
		canCut = canCopy = canDelete = edit->hasSelectedText();
		canSelectAll = !edit->text().isEmpty();
		canUndo = edit->isUndoAvailable();
		canRedo = edit->isRedoAvailable();
		canPaste = clipboardHasText;
	} else if (const auto edit = qobject_cast<QTextEdit*>(focused)) {
		canCut = canCopy = canDelete = edit->textCursor().hasSelection();
		canSelectAll = !edit->document()->isEmpty();
		canUndo = edit->document()->isUndoAvailable();
		canRedo = edit->document()->isRedoAvailable();
		canPaste = clipboardHasText;
		if (canCopy) {
			if (const auto inputField = dynamic_cast<Ui::InputField*>(
				focused->parentWidget())) {
				markdownEnabled = inputField->isMarkdownEnabled();
			}
		}
	} else if (const auto list = dynamic_cast<HistoryInner*>(focused)) {
		canCopy = list->canCopySelected();
		canDelete = list->canDeleteSelected();
	}
	updateIsActive();
	const auto logged = (sessionController() != nullptr);
	const auto inactive = !logged || controller().locked();
	const auto support = logged && account().session().supportMode();
	ForceDisabled(psLogout, !logged && !Core::App().passcodeLocked());
	ForceDisabled(psUndo, !canUndo);
	ForceDisabled(psRedo, !canRedo);
	ForceDisabled(psCut, !canCut);
	ForceDisabled(psCopy, !canCopy);
	ForceDisabled(psPaste, !canPaste);
	ForceDisabled(psDelete, !canDelete);
	ForceDisabled(psSelectAll, !canSelectAll);
	ForceDisabled(psContacts, inactive || support);
	ForceDisabled(psAddContact, inactive);
	ForceDisabled(psNewGroup, inactive || support);
	ForceDisabled(psNewChannel, inactive || support);

	ForceDisabled(psBold, !markdownEnabled);
	ForceDisabled(psItalic, !markdownEnabled);
	ForceDisabled(psUnderline, !markdownEnabled);
	ForceDisabled(psStrikeOut, !markdownEnabled);
	ForceDisabled(psMonospace, !markdownEnabled);
	ForceDisabled(psClearFormat, !markdownEnabled);
}

bool MainWindow::eventFilter(QObject *obj, QEvent *evt) {
	QEvent::Type t = evt->type();
	if (t == QEvent::MouseButtonPress
		&& obj->objectName() == qstr("QSystemTrayIconSys")) {
		const auto ee = static_cast<QMouseEvent*>(evt);
		if (ee->button() == Qt::RightButton) {
			Core::Sandbox::Instance().customEnterFromEventLoop([&] {
				handleTrayIconActication(QSystemTrayIcon::Context);
			});
			return true;
		}
	} else if (t == QEvent::FocusIn || t == QEvent::FocusOut) {
		if (qobject_cast<QLineEdit*>(obj)
			|| qobject_cast<QTextEdit*>(obj)
			|| dynamic_cast<HistoryInner*>(obj)) {
			updateGlobalMenu();
		}
	}
	return Window::MainWindow::eventFilter(obj, evt);
}

void MainWindow::handleNativeSurfaceChanged(bool exist) {
	if (exist) {
		SkipTaskbar(
			windowHandle(),
			(Core::App().settings().workMode() == WorkMode::TrayOnly)
				&& TrayIconSupported());
	}
}

MainWindow::~MainWindow() {
}

} // namespace Platform
