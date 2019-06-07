/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/win/wrapper_windows_h.h"

namespace Platform {

class MainWindow;

class EventFilter : public QAbstractNativeEventFilter {
public:
	bool nativeEventFilter(const QByteArray &eventType, void *message, long *result);
	bool mainWindowEvent(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT *result);

	bool sessionLoggedOff() const {
		return _sessionLoggedOff;
	}
	void setSessionLoggedOff(bool loggedOff) {
		_sessionLoggedOff = loggedOff;
	}

	static EventFilter *CreateInstance(not_null<MainWindow*> window);
	static EventFilter *GetInstance();
	static void Destroy();

private:
	explicit EventFilter(not_null<MainWindow*> window);

	not_null<MainWindow*> _window;
	bool _sessionLoggedOff = false;

};

} // namespace Platform
