/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/platform/win/base_windows_h.h"

#include <QtCore/QAbstractNativeEventFilter>

namespace Platform {

class MainWindow;

class EventFilter : public QAbstractNativeEventFilter {
public:
	bool nativeEventFilter(const QByteArray &eventType, void *message, long *result);
	bool mainWindowEvent(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT *result);

	static EventFilter *CreateInstance(not_null<MainWindow*> window);
	static void Destroy();

private:
	explicit EventFilter(not_null<MainWindow*> window);

	not_null<MainWindow*> _window;

};

} // namespace Platform
