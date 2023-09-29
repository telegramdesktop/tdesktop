/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/linux/file_utilities_linux.h"

#include "platform/linux/linux_xdp_open_with_dialog.h"

namespace Platform {
namespace File {

bool UnsafeShowOpenWith(const QString &filepath) {
	return internal::ShowXDPOpenWithDialog(filepath);
}

} // namespace File
} // namespace Platform
