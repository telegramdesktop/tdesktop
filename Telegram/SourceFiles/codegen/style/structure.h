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
#include <QtCore/QList>
#include <QtCore/QStringList>

namespace codegen {
namespace style {
namespace structure {

// List of names, like overview.document.bg
using FullName = QStringList;

enum class TypeTag {
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
};

struct Variable;
struct Value {
	QString data; // for plain types
	QList<Variable> fields; // for struct types
};

struct Variable {
	FullName name;
	Type type;
	Value value;
};

struct StructField {
	FullName name;
	Type type;
};

struct Struct {
	FullName name;
	QList<StructField> fields;
};

struct Module {
	QString fullpath;
	QList<Module> includes;
	QList<Struct> structs;
	QList<Variable> variables;
};

} // namespace structure
} // namespace style
} // namespace codegen
