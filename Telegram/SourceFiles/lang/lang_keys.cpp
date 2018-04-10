/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "lang/lang_keys.h"

#include "lang/lang_file_parser.h"

bool langFirstNameGoesSecond() {
	auto fullname = lang(lng_full_name__tagged);
	for (auto begin = fullname.constData(), ch = begin, end = ch + fullname.size(); ch != end; ++ch) {
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
