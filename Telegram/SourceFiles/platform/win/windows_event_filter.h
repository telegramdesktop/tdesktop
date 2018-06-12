/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <windows.h>

namespace Platform {

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

	static EventFilter *createInstance();
	static EventFilter *getInstance();
	static void destroy();

private:
	EventFilter() {
	}

	bool _sessionLoggedOff = false;

};

} // namespace Platform
