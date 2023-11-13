/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_main_window.h"
#include "base/flags.h"

#include <windows.h>

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Platform {

class MainWindow : public Window::MainWindow {
public:
	explicit MainWindow(not_null<Window::Controller*> controller);

	HWND psHwnd() const;

	void updateWindowIcon() override;
	bool isActiveForTrayMenu() override;

	// Custom shadows.
	void shadowsActivate();
	void shadowsDeactivate();

	[[nodiscard]] bool hasTabletView() const;

	void destroyedFromSystem();

	bool setDwmThumbnail(QSize size);
	bool setDwmPreview(QSize size, int radius);

	~MainWindow();

protected:
	void initHook() override;
	void unreadCounterChangedHook() override;

	void workmodeUpdated(Core::Settings::WorkMode mode) override;

	bool initGeometryFromSystem() override;

private:
	struct Private;

	class BitmapPointer {
	public:
		BitmapPointer(HBITMAP value = nullptr);
		BitmapPointer(BitmapPointer &&other);
		BitmapPointer& operator=(BitmapPointer &&other);
		~BitmapPointer();

		[[nodiscard]] HBITMAP get() const;
		[[nodiscard]] explicit operator bool() const;

		void release();
		void reset(HBITMAP value = nullptr);

	private:
		HBITMAP _value = nullptr;

	};

	void setupNativeWindowFrame();
	void setupPreviewPasscodeLock();
	void updateTaskbarAndIconCounters();
	void validateWindowTheme(bool native, bool night);

	void forceIconRefresh();
	void destroyCachedIcons();
	void validateDwmPreviewColors();

	const std::unique_ptr<Private> _private;
	const std::unique_ptr<QWindow> _taskbarHiderWindow;

	HWND _hWnd = nullptr;
	HICON _iconBig = nullptr;
	HICON _iconSmall = nullptr;
	HICON _iconOverlay = nullptr;

	BitmapPointer _dwmThumbnail;
	BitmapPointer _dwmPreview;
	QSize _dwmThumbnailSize;
	QSize _dwmPreviewSize;
	QColor _dwmBackground;
	int _dwmPreviewRadius = 0;

	// Workarounds for activation from tray icon.
	crl::time _lastDeactivateTime = 0;

	bool _hasActiveFrame = false;

};

[[nodiscard]] int32 ScreenNameChecksum(const QString &name);

} // namespace Platform
