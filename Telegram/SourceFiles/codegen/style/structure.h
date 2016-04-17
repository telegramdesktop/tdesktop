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
#include <QtCore/QMap>
#include "codegen/style/structure_types.h"

namespace codegen {
namespace style {
namespace structure {

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
	QMap<QString, int> structsByName;
	QMap<QString, int> variablesByName;

	explicit operator bool() const {
		return !fullpath.isEmpty();
	}
};

} // namespace structure
} // namespace style
} // namespace codegen
