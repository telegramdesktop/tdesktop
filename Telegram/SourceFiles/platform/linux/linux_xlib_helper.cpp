/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/linux_xlib_helper.h"

#ifndef TDESKTOP_DISABLE_GTK_INTEGRATION
extern "C" {
#include <X11/Xlib.h>
}

namespace Platform {
namespace internal {

class XErrorHandlerRestorer::Private {
public:
	Private()
	: _oldErrorHandler(XSetErrorHandler(nullptr)) {
	}

	~Private() {
		XSetErrorHandler(_oldErrorHandler);
	}

private:
	int (*_oldErrorHandler)(Display *, XErrorEvent *);
};

XErrorHandlerRestorer::XErrorHandlerRestorer()
: _private(std::make_unique<Private>()) {
}

XErrorHandlerRestorer::~XErrorHandlerRestorer() = default;

} // namespace internal
} // namespace Platform
#endif // !TDESKTOP_DISABLE_GTK_INTEGRATION
