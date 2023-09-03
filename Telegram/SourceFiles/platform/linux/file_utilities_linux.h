/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "platform/platform_file_utilities.h"

namespace Platform {
namespace File {

inline QString UrlToLocal(const QUrl &url) {
	return ::File::internal::UrlToLocalDefault(url);
}

inline void UnsafeOpenUrl(const QString &url) {
	return ::File::internal::UnsafeOpenUrlDefault(url);
}

inline void UnsafeOpenEmailLink(const QString &email) {
	return ::File::internal::UnsafeOpenEmailLinkDefault(email);
}

inline bool UnsafeShowOpenWithDropdown(const QString &filepath) {
	return false;
}

inline void UnsafeLaunch(const QString &filepath) {
	return ::File::internal::UnsafeLaunchDefault(filepath);
}

inline void PostprocessDownloaded(const QString &filepath) {
}

} // namespace File

namespace FileDialog {

inline void InitLastPath() {
	::FileDialog::internal::InitLastPathDefault();
}

} // namespace FileDialog
} // namespace Platform
