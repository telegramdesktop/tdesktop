/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/platform/win/base_windows_shlobj_h.h"
#include "base/platform/win/base_windows_winrt.h"
#include "platform/platform_integration.h"

#include <QAbstractNativeEventFilter>

namespace Platform {

class WindowsIntegration final
	: public Integration
	, public QAbstractNativeEventFilter {
public:
	void init() override;

	[[nodiscard]] ITaskbarList3 *taskbarList() const;

	[[nodiscard]] static WindowsIntegration &Instance();

private:
	bool nativeEventFilter(
		const QByteArray &eventType,
		void *message,
		long *result) override;
	bool processEvent(
		HWND hWnd,
		UINT msg,
		WPARAM wParam,
		LPARAM lParam,
		LRESULT *result);

	void createCustomJumpList();
	void refreshCustomJumpList();

	uint32 _taskbarCreatedMsgId = 0;
	winrt::com_ptr<ITaskbarList3> _taskbarList;
	winrt::com_ptr<ICustomDestinationList> _jumpList;

};

[[nodiscard]] std::unique_ptr<Integration> CreateIntegration();

} // namespace Platform
