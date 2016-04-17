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

#include <QtCore/QString>
#include <QtCore/QStringList>

namespace codegen {
namespace style {
namespace structure {

// List of names, like overview.document.bg
using FullName = QStringList;

enum class TypeTag {
	Invalid,
	Int,
	Double,
	Pixels,
	String,
	Color,
	Point,
	Sprite,
	Size,
	Transition,
	Cursor,
	Align,
	Margins,
	Font,
	Struct,
};

struct Type {
	TypeTag tag;
	FullName name; // only for type == ClassType::Struct

	explicit operator bool() const {
		return (tag != TypeTag::Invalid);
	}
};
inline bool operator==(const Type &a, const Type &b) {
	return (a.tag == b.tag) && (a.name == b.name);
}
inline bool operator!=(const Type &a, const Type &b) {
	return !(a == b);
}

struct Variable;
struct Value {
	Type type;
	QString data; // for plain types
	QList<Variable> fields; // for struct types
	FullName copy; // for copies of existing named values

	explicit operator bool() const {
		return !data.isEmpty() || !fields.isEmpty() || !copy.isEmpty();
	}
};

struct Variable {
	FullName name;
	Value value;

	explicit operator bool() const {
		return !name.isEmpty();
	}
};

struct StructField {
	FullName name;
	Type type;

	explicit operator bool() const {
		return !name.isEmpty();
	}
};

struct Struct {
	FullName name;
	QList<StructField> fields;

	explicit operator bool() const {
		return !name.isEmpty();
	}
};

struct Module {
	QString fullpath;
	QList<Module> includes;
	QList<Struct> structs;
	QList<Variable> variables;

	explicit operator bool() const {
		return !fullpath.isEmpty();
	}
};

} // namespace structure
} // namespace style
} // namespace codegen
