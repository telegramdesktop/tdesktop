/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace base {
namespace parse {

// Strip all C-style comments.
QByteArray stripComments(const QByteArray &content);

inline bool skipWhitespaces(const char *&from, const char *end) {
	Assert(from <= end);
	while (from != end && (
		(*from == ' ') ||
		(*from == '\n') ||
		(*from == '\t') ||
		(*from == '\r'))) {
		++from;
	}
	return (from != end);
}

inline QLatin1String readName(const char *&from, const char *end) {
	Assert(from <= end);
	auto start = from;
	while (from != end && (
		(*from >= 'a' && *from <= 'z') ||
		(*from >= 'A' && *from <= 'Z') ||
		(*from >= '0' && *from <= '9') ||
		(*from == '_'))) {
		++from;
	}
	return QLatin1String(start, from - start);
}

} // namespace parse
} // namespace base
