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

#include <memory>
#include <QtCore/QString>
#include <QtCore/QSet>
#include <QtGui/QImage>
#include "codegen/style/structure_types.h"

namespace codegen {
namespace style {
namespace structure {
class Module;
} // namespace structure

class SpriteGenerator {
public:
	SpriteGenerator(const structure::Module &module);
	SpriteGenerator(const SpriteGenerator &other) = delete;
	SpriteGenerator &operator=(const SpriteGenerator &other) = delete;

	bool writeSprites();

private:

	bool collectSprites();
	QImage generateSprite(int scale); // scale = 5 for 125% and 6 for 150%.

	const structure::Module &module_;
	QString basePath_;
	QImage sprite2x_;
	QList<structure::Variable> sprites_;

};

} // namespace style
} // namespace codegen
