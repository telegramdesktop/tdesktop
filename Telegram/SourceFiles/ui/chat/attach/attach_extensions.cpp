/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/attach/attach_extensions.h"

namespace Ui {

const QStringList &ImageExtensions() {
	static const auto result = QStringList{
		u".bmp"_q,
		u".jpg"_q,
		u".jpeg"_q,
		u".png"_q,
		u".gif"_q,
	};
	return result;
}

const QStringList &ExtensionsForCompression() {
	static const auto result = QStringList{
		u".bmp"_q,
		u".jpg"_q,
		u".jpeg"_q,
		u".png"_q,
	};
	return result;
}

} // namespace Ui
