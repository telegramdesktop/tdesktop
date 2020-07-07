/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "window/window_title.h"
#include "window/window_title_qt.h"
#include "window/themes/window_theme_preview.h"
#include "base/object_ptr.h"

namespace Platform {

bool AllowNativeWindowFrameToggle();
object_ptr<Window::TitleWidget> CreateTitleWidget(QWidget *parent);
bool NativeTitleRequiresShadow();

int PreviewTitleHeight();
void PreviewWindowFramePaint(QImage &preview, const style::palette &palette, QRect body, int outerWidth);

} // namespace Platform

// Platform dependent implementations.

#ifdef Q_OS_MAC
#include "platform/mac/window_title_mac.h"
#elif defined Q_OS_WIN // Q_OS_MAC
#include "platform/win/window_title_win.h"
#elif defined Q_OS_UNIX // Q_OS_MAC || Q_OS_WIN
#include "platform/linux/window_title_linux.h"
#else // Q_OS_MAC || Q_OS_WIN || Q_OS_UNIX

namespace Platform {

inline bool AllowNativeWindowFrameToggle() {
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0) || defined DESKTOP_APP_QT_PATCHED
	return true;
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

#endif // Q_OS_MAC || Q_OS_WIN || Q_OS_UNIX
