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

	void psRefreshTaskbarIcon();

	virtual QImage iconWithCounter(int size, int count, style::color bg, style::color fg, bool smallIcon) = 0;

	[[nodiscard]] static uint32 TaskbarCreatedMsgId();
	static void TaskbarCreated();

	// Custom shadows.
	void shadowsActivate();
	void shadowsDeactivate();

	[[nodiscard]] bool hasTabletView() const;

	void psShowTrayMenu();

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
	virtual void placeSmallCounter(QImage &img, int size, int count, style::color bg, const QPoint &shift, style::color color) = 0;

	void showTrayTooltip() override;

	void workmodeUpdated(Core::Settings::WorkMode mode) override;

	bool initGeometryFromSystem() override;

	QRect computeDesktopRect() const override;

private:
	struct Private;

	void setupNativeWindowFrame();
	void updateIconCounters();
	void validateWindowTheme(bool native, bool night);
	void psDestroyIcons();

	const std::unique_ptr<Private> _private;

	bool _hasActiveFrame = false;

	// Workarounds for activation from tray icon.
	crl::time _lastDeactivateTime = 0;
	rpl::lifetime _showFromTrayLifetime;

	HWND ps_hWnd = nullptr;
	HWND ps_tbHider_hWnd = nullptr;
	HICON ps_iconBig = nullptr;
	HICON ps_iconSmall = nullptr;
	HICON ps_iconOverlay = nullptr;

};

} // namespace Platform
