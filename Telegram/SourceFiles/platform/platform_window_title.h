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

object_ptr<Window::TitleWidget> CreateTitleWidget(QWidget *parent);

int PreviewTitleHeight();
void PreviewWindowFramePaint(QImage &preview, const style::palette &palette, QRect body, int outerWidth);

} // namespace Platform

// Platform dependent implementations.

#ifdef Q_OS_MAC
#include "platform/mac/window_title_mac.h"
#elif defined Q_OS_WIN // Q_OS_MAC
#include "platform/win/window_title_win.h"
#else // Q_OS_MAC || Q_OS_WIN

namespace Platform {

inline object_ptr<Window::TitleWidget> CreateTitleWidget(QWidget *parent) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0) || defined DESKTOP_APP_QT_PATCHED
	return object_ptr<Window::TitleWidgetQt>(parent);
#endif // Qt >= 5.15 || DESKTOP_APP_QT_PATCHED

	return { nullptr };
}

inline int PreviewTitleHeight() {
	return Window::Theme::DefaultPreviewTitleHeight();
}

inline void PreviewWindowFramePaint(QImage &preview, const style::palette &palette, QRect body, int outerWidth) {
	return Window::Theme::DefaultPreviewWindowFramePaint(preview, palette, body, outerWidth);
}

} // namespace Platform

#endif // Q_OS_MAC || Q_OS_WIN
