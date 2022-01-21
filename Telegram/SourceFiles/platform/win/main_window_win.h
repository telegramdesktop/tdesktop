/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_main_window.h"
#include "base/platform/win/base_windows_h.h"
#include "base/flags.h"

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Platform {

class MainWindow : public Window::MainWindow {
public:
	explicit MainWindow(not_null<Window::Controller*> controller);

	void showFromTrayMenu() override;

	HWND psHwnd() const;

	void updateWindowIcon() override;
	bool isActiveForTrayMenu() override;

	// Custom shadows.
	void shadowsActivate();
	void shadowsDeactivate();

	[[nodiscard]] bool hasTabletView() const;

	void psShowTrayMenu();

	void destroyedFromSystem();

	~MainWindow();

protected:
	void initHook() override;
	int32 screenNameChecksum(const QString &name) const override;
	void unreadCounterChangedHook() override;

	bool hasTrayIcon() const override {
		return trayIcon;
	}

	QSystemTrayIcon *trayIcon = nullptr;
	Ui::PopupMenu *trayIconMenu = nullptr;

	void psTrayMenuUpdated();
	void psSetupTrayIcon();

	void showTrayTooltip() override;

	void workmodeUpdated(Core::Settings::WorkMode mode) override;

	bool initGeometryFromSystem() override;

	QRect computeDesktopRect() const override;

private:
	struct Private;

	void setupNativeWindowFrame();
	void updateIconCounters();
	void validateWindowTheme(bool native, bool night);

	void forceIconRefresh();
	void destroyCachedIcons();

	const std::unique_ptr<Private> _private;
	const std::unique_ptr<QWindow> _taskbarHiderWindow;

	HWND _hWnd = nullptr;
	HICON _iconBig = nullptr;
	HICON _iconSmall = nullptr;
	HICON _iconOverlay = nullptr;

	// Workarounds for activation from tray icon.
	crl::time _lastDeactivateTime = 0;
	rpl::lifetime _showFromTrayLifetime;

	bool _hasActiveFrame = false;

};

} // namespace Platform
