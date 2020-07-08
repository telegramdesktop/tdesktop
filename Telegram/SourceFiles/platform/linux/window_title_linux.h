/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_window_title.h"
#include "platform/linux/linux_desktop_environment.h"
#include "base/object_ptr.h"

namespace Window {
namespace Theme {

int DefaultPreviewTitleHeight();
void DefaultPreviewWindowFramePaint(QImage &preview, const style::palette &palette, QRect body, int outerWidth);

} // namespace Theme
} // namespace Window

namespace Platform {

inline bool AllowNativeWindowFrameToggle() {
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0) || defined DESKTOP_APP_QT_PATCHED
	return !DesktopEnvironment::IsUnity();
#else // Qt >= 5.15 || DESKTOP_APP_QT_PATCHED
	return false;
#endif // Qt >= 5.15 || DESKTOP_APP_QT_PATCHED
}

inline object_ptr<Window::TitleWidget> CreateTitleWidget(QWidget *parent) {
	return AllowNativeWindowFrameToggle()
		? object_ptr<Window::TitleWidgetQt>(parent)
		: object_ptr<Window::TitleWidgetQt>{ nullptr };
}

inline bool NativeTitleRequiresShadow() {
	return false;
}

inline int PreviewTitleHeight() {
	return Window::Theme::DefaultPreviewTitleHeight();
}

inline void PreviewWindowFramePaint(QImage &preview, const style::palette &palette, QRect body, int outerWidth) {
	return Window::Theme::DefaultPreviewWindowFramePaint(preview, palette, body, outerWidth);
}

} // namespace Platform
