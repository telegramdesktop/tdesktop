/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_window_title.h"
#include "platform/linux/linux_wayland_integration.h"
#include "base/object_ptr.h"

namespace Window {
namespace Theme {

int DefaultPreviewTitleHeight();
void DefaultPreviewWindowFramePaint(QImage &preview, const style::palette &palette, QRect body, int outerWidth);

} // namespace Theme
} // namespace Window

namespace Platform {

inline bool AllowNativeWindowFrameToggle() {
	const auto waylandIntegration = internal::WaylandIntegration::Instance();
	return !waylandIntegration
			|| waylandIntegration->supportsXdgDecoration();
}

inline object_ptr<Window::TitleWidget> CreateTitleWidget(QWidget *parent) {
	return object_ptr<Window::TitleWidgetQt>(parent);
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
