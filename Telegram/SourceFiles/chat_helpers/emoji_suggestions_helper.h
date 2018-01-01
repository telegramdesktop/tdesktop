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
#pragma once

#include "emoji_suggestions.h"
#include "emoji_suggestions_data.h"

namespace Ui {
namespace Emoji {

inline utf16string QStringToUTF16(const QString &string) {
	return utf16string(reinterpret_cast<const utf16char*>(string.constData()), string.size());
}

inline QString QStringFromUTF16(utf16string string) {
	return QString::fromRawData(reinterpret_cast<const QChar*>(string.data()), string.size());
}

constexpr auto kSuggestionMaxLength = internal::kReplacementMaxLength;

} // namespace Emoji
} // namespace Ui
