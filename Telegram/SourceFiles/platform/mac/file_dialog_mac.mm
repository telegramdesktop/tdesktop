/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "platform/mac/file_dialog_mac.h"

#include "platform/mac/mac_utilities.h"

#include <Cocoa/Cocoa.h>
#include <CoreFoundation/CFURL.h>

namespace Platform {
namespace FileDialog {

QString UrlToLocal(const QUrl &url) {
	auto result = url.toLocalFile();
	if (result.startsWith(qsl("/.file/id="))) {
		NSString *nsurl = [[[NSURL URLWithString: [NSString stringWithUTF8String: (qsl("file://") + result).toUtf8().constData()]] filePathURL] path];
		if (!nsurl) return QString();

		return NS2QString(nsurl);
	}
	return result;
}

} // namespace FileDialog
} // namespace Platform
