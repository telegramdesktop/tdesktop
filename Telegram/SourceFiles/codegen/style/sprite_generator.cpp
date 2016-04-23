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
#include "codegen/style/sprite_generator.h"

#include <QtCore/QFileInfo>
#include <QtCore/QDir>
#include <QtCore/QBuffer>
#include <QtGui/QPainter>
#include <QtGui/QColor>
#include <functional>
#include "codegen/style/parsed_file.h"

using Module = codegen::style::structure::Module;
using Struct = codegen::style::structure::Struct;
using Variable = codegen::style::structure::Variable;
using Tag = codegen::style::structure::TypeTag;

namespace codegen {
namespace style {

using structure::logFullName;

namespace {

constexpr int kErrorSpritesIntersect  = 841;
constexpr int kErrorCouldNotGenerate  = 842;
constexpr int kErrorCouldNotSerialize = 843;
constexpr int kErrorCouldNotOpen      = 844;
constexpr int kErrorCouldNotWrite     = 845;

} // namespace

SpriteGenerator::SpriteGenerator(const structure::Module &module)
: module_(module)
, basePath_(QFileInfo(module.filepath()).dir().absolutePath()) {
}

bool SpriteGenerator::writeSprites() {
	if (!collectSprites()) {
		return false;
	}
	if (sprites_.isEmpty()) {
		return true;
	}

	sprite2x_ = QImage(basePath_ + "/art/sprite_200x.png");
	if (sprite2x_.isNull()) {
		common::logError(common::kErrorFileNotFound, "/art/sprite_200x.png") << "sprite file was not found";
		return false;
	}
	std::vector<int> sizes = { 5, 6 };
	std::vector<const char *> postfixes = { "125", "150" };
	for (int i = 0, l = sizes.size(); i < l; ++i) {
		auto sprite = generateSprite(sizes[i]);
		QString filepath = basePath_ + "/art/sprite_" + postfixes[i] + "x.png";
		if (sprite.isNull()) {
			common::logError(kErrorCouldNotGenerate, filepath) << "could not generate sprite file";
			return false;
		}
		QByteArray spriteData;
		{
			QBuffer spriteBuffer(&spriteData);
			if (!sprite.save(&spriteBuffer, "PNG")) {
				common::logError(kErrorCouldNotSerialize, filepath) << "could not serialize sprite file";
				return false;
			}
		}
		QFile file(filepath);
		if (file.open(QIODevice::ReadOnly)) {
			if (file.readAll() == spriteData) {
				continue;
			}
			file.close();
		}
		if (!file.open(QIODevice::WriteOnly)) {
			common::logError(kErrorCouldNotOpen, filepath) << "could not open sprite file for write";
			return false;
		}
		if (file.write(spriteData) != spriteData.size()) {
			common::logError(kErrorCouldNotWrite, filepath) << "could not write sprite file";
			return false;
		}

		// Touch resource file.
		filepath = basePath_ + "/telegram.qrc";
		QFile qrc(filepath);
		if (qrc.open(QIODevice::ReadOnly)) {
			auto qrcContent = qrc.readAll();
			qrc.close();
			if (!qrc.open(QIODevice::WriteOnly)) {
				common::logError(kErrorCouldNotOpen, filepath) << "could not open .qrc file for write";
				return false;
			}
			if (qrc.write(qrcContent) != qrcContent.size()) {
				common::logError(kErrorCouldNotWrite, filepath) << "could not write .qrc file";
				return false;
			}
		}
	}

	return true;
}

bool SpriteGenerator::collectSprites() {
	std::function<bool(const Variable&)> collector = [this, &collector](const Variable &variable) {
		auto value = variable.value;
		if (value.type().tag == Tag::Sprite) {
			auto v(value.Sprite());
			if (!v.width || !v.height) return true;

			QRect vRect(v.left, v.top, v.width, v.height);
			bool found = false;
			for (auto var : sprites_) {
				auto sprite = var.value.Sprite();
				QRect spriteRect(sprite.left, sprite.top, sprite.width, sprite.height);
				if (spriteRect == vRect) {
					found = true;
				} else if (spriteRect.intersects(vRect)) {
					common::logError(kErrorSpritesIntersect, module_.filepath()) << "sprite '" << logFullName(variable.name) << "' intersects with '" << logFullName(var.name) << "'";
					return false;
				}
			}
			if (!found) {
				sprites_.push_back(variable);
			}
		} else if (value.type().tag == Tag::Struct) {
			auto fields = variable.value.Fields();
			if (!fields) {
				return false;
			}

			for (auto field : *fields) {
				if (!collector(field.variable)) {
					return false;
				}
			}
		}
		return true;
	};
	return module_.enumVariables(collector);
}

QImage SpriteGenerator::generateSprite(int scale) {
	auto convert = [scale](int value) -> int { return structure::data::pxAdjust(value, scale); };
	QImage result(convert(sprite2x_.width() / 2), convert(sprite2x_.height() / 2), sprite2x_.format());
	{
		QPainter p(&result);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.fillRect(0, 0, result.width(), result.height(), QColor(0, 0, 0, 0));
		for (auto variable : sprites_) {
			auto sprite = variable.value.Sprite();
			auto copy = sprite2x_.copy(sprite.left * 2, sprite.top * 2, sprite.width * 2, sprite.height * 2);
			copy = copy.scaled(convert(sprite.width), convert(sprite.height), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
			p.drawImage(convert(sprite.left), convert(sprite.top), copy);
		}
	}
	return result;
}

} // namespace style
} // namespace codegen
