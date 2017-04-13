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
#include "lang/lang_keys.h"

#include "lang/lang_file_parser.h"

//#define NEW_VER_TAG lt_link
//#define NEW_VER_TAG_VALUE "https://telegram.org/blog/desktop-1-0"

QString langNewVersionText() {
#ifdef NEW_VER_TAG
	return lng_new_version_text(NEW_VER_TAG, QString::fromUtf8(NEW_VER_TAG_VALUE));
#else // NEW_VER_TAG
	return lang(lng_new_version_text);
#endif // NEW_VER_TAG
}

#undef NEW_VER_TAG_VALUE
#undef NEW_VER_TAG

bool langFirstNameGoesSecond() {
	auto fullname = lang(lng_full_name__tagged);
	for (auto begin = fullname.constData(), ch = begin, end = ch + fullname.size(); ch != end;) {
		if (*ch == TextCommand) {
			if (ch + 3 < end && (ch + 1)->unicode() == TextCommandLangTag && *(ch + 3) == TextCommand) {
				if ((ch + 2)->unicode() == 0x0020 + lt_last_name) {
					return true;
				} else if ((ch + 2)->unicode() == 0x0020 + lt_first_name) {
					break;
				}
			}
		}
	}
	return false;
}
