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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

namespace Dialogs {

class Row;
class FakeRow;

namespace Layout {

class RowPainter {
public:
	static void paint(Painter &p, const Row *row, int w, bool active, bool selected, bool onlyBackground);
	static void paint(Painter &p, const FakeRow *row, int w, bool active, bool selected, bool onlyBackground);
};

void paintImportantSwitch(Painter &p, Mode current, int w, bool selected, bool onlyBackground);

// This will be moved somewhere outside as soon as anyone starts using that.
class StyleSheet {
public:
	virtual ~StyleSheet() = 0;
};
inline StyleSheet::~StyleSheet() = default;

namespace internal {

void registerStyleSheet(StyleSheet **p);

} // namespace

// Must be created in global scope!
template <typename T>
class StyleSheetPointer {
public:
	StyleSheetPointer() = default;
	StyleSheetPointer(const StyleSheetPointer<T> &other) = delete;
	StyleSheetPointer &operator=(const StyleSheetPointer<T> &other) = delete;

	void createIfNull() {
		if (!_p) {
			_p = new T();
			internal::registerStyleSheet(&_p);
		}
	}
	T *operator->() {
		t_assert(_p != nullptr);
		return static_cast<T*>(_p);
	}

private:
	StyleSheet *_p;

};

void clearStyleSheets();

} // namespace Layout
} // namespace Dialogs
