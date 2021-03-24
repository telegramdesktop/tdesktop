/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/window.h"

namespace Webview {
class Window;
} // namespace Webview

namespace Payments::Ui {

using namespace ::Ui;

class PanelDelegate;

class WebviewWindow final {
public:
	WebviewWindow(
		const QString &userDataPath,
		const QString &url,
		not_null<PanelDelegate*> delegate);

	[[nodiscard]] bool shown() const;
	void navigate(const QString &url);

private:
	Ui::Window _window;
	Webview::Window *_webview = nullptr;

};

} // namespace Payments::Ui
