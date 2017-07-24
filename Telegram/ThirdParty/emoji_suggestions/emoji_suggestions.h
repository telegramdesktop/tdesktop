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

#include <vector>

namespace Ui {
namespace Emoji {

using small = unsigned char;
using medium = unsigned short;
using utf16char = unsigned short;

static_assert(sizeof(utf16char) == 2, "Bad UTF-16 character size.");

class utf16string {
public:
	utf16string() = default;
	utf16string(const utf16char *data, std::size_t size) : data_(data), size_(size) {
	}
	utf16string(const utf16string &other) = default;
	utf16string &operator=(const utf16string &other) = default;

	const utf16char *data() const {
		return data_;
	}
	std::size_t size() const {
		return size_;
	}

	utf16char operator[](int index) const {
		return data_[index];
	}

private:
	const utf16char *data_ = nullptr;
	std::size_t size_ = 0;

};

inline bool operator==(utf16string a, utf16string b) {
	return (a.size() == b.size()) && (!a.size() || !memcmp(a.data(), b.data(), a.size() * sizeof(utf16char)));
}

namespace internal {

using checksum = unsigned int;
checksum countChecksum(const void *data, std::size_t size);

utf16string GetReplacementEmoji(utf16string replacement);

} // namespace internal

class Suggestion {
public:
	Suggestion() = default;
	Suggestion(utf16string emoji, utf16string label, utf16string replacement) : emoji_(emoji), label_(label), replacement_(replacement) {
	}
	Suggestion(const Suggestion &other) = default;
	Suggestion &operator=(const Suggestion &other) = default;

	utf16string emoji() const {
		return emoji_;
	}
	utf16string label() const {
		return label_;
	}
	utf16string replacement() const {
		return replacement_;
	}

private:
	utf16string emoji_;
	utf16string label_;
	utf16string replacement_;

};

std::vector<Suggestion> GetSuggestions(utf16string query);

inline utf16string GetSuggestionEmoji(utf16string replacement) {
	return internal::GetReplacementEmoji(replacement);
}

int GetSuggestionMaxLength();

} // namespace Emoji
} // namespace Ui
