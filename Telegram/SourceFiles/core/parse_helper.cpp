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
#include "core/parse_helper.h"

namespace base {
namespace parse {

// inspired by https://github.com/sindresorhus/strip-json-comments
QByteArray stripComments(const QByteArray &content) {
	enum class InsideComment {
		None,
		SingleLine,
		MultiLine,
	};
	auto insideComment = InsideComment::None;
	auto insideString = false;

	QByteArray result;
	auto begin = content.cbegin(), end = content.cend(), offset = begin;
	auto feedContent = [&result, &offset, end](const char *ch) {
		if (ch > offset) {
			if (result.isEmpty()) result.reserve(end - offset - 2);
			result.append(offset, ch - offset);
			offset = ch;
		}
	};
	auto feedComment = [&result, &offset, end](const char *ch) {
		if (ch > offset) {
			if (result.isEmpty()) result.reserve(end - offset - 2);
			result.append(' ');
			offset = ch;
		}
	};
	for (auto ch = offset; ch != end;) {
		auto currentChar = *ch;
		auto nextChar = (ch + 1 == end) ? 0 : *(ch + 1);

		if (insideComment == InsideComment::None && currentChar == '"') {
			auto escaped = ((ch > begin) && *(ch - 1) == '\\') && ((ch - 1 < begin) || *(ch - 2) != '\\');
			if (!escaped) {
				insideString = !insideString;
			}
		}
		if (insideString) {
			++ch;
			continue;
		}

		if (insideComment == InsideComment::None && currentChar == '/' && nextChar == '/') {
			feedContent(ch);
			insideComment = InsideComment::SingleLine;
			ch += 2;
		} else if (insideComment == InsideComment::SingleLine && currentChar == '\r' && nextChar == '\n') {
			feedComment(ch);
			ch += 2;
			insideComment = InsideComment::None;
		} else if (insideComment == InsideComment::SingleLine && currentChar == '\n') {
			feedComment(ch);
			++ch;
			insideComment = InsideComment::None;
		} else if (insideComment == InsideComment::None && currentChar == '/' && nextChar == '*') {
			feedContent(ch);
			ch += 2;
			insideComment = InsideComment::MultiLine;
		} else if (insideComment == InsideComment::MultiLine && currentChar == '*' && nextChar == '/') {
			ch += 2;
			feedComment(ch);
			insideComment = InsideComment::None;
		} else if (insideComment == InsideComment::MultiLine && currentChar == '\r' && nextChar == '\n') {
			feedComment(ch);
			ch += 2;
			feedContent(ch);
		} else if (insideComment == InsideComment::MultiLine && currentChar == '\n') {
			feedComment(ch);
			++ch;
			feedContent(ch);
		} else {
			++ch;
		}
	}

	if (insideComment == InsideComment::MultiLine) {
		// unexpected end of content
	}
	if (insideComment == InsideComment::None && end > offset) {
		if (result.isEmpty()) {
			return content;
		} else {
			result.append(offset, end - offset);
		}
	}
	return result;
}

} // namespace parse
} // namespace base
