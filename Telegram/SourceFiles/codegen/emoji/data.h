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

#include "codegen/common/logging.h"
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <QtCore/QString>
#include <QtCore/QSet>
#include <QtCore/QMap>

namespace codegen {
namespace emoji {

using Id = QString;
struct Emoji {
	Id id;
	bool postfixed = false;
	bool variated = false;
	bool colored = false;
};

struct Data {
	std::vector<Emoji> list;
	std::map<Id, int, std::greater<Id>> map;
	std::vector<std::vector<int>> categories;
	std::map<QString, int, std::greater<QString>> replaces;
};
Data PrepareData();

constexpr auto kPostfix = 0xFE0FU;

common::LogStream logDataError();

} // namespace emoji
} // namespace codegen
