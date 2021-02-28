/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class QLibrary;

extern "C" {
#include <gtk/gtk.h>
#include <gdk/gdk.h>
} // extern "C"

namespace Platform {
namespace internal {

void GdkHelperLoad(QLibrary &lib);
bool GdkHelperLoaded();
void XSetTransientForHint(GdkWindow *window, quintptr winId);

} // namespace internal
} // namespace Platform
