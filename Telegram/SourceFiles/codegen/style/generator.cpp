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
#include "codegen/style/generator.h"

#include <memory>
#include <functional>
#include <QtCore/QDir>
#include <QtCore/QSet>
#include <QtCore/QBuffer>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include "codegen/style/parsed_file.h"

using Module = codegen::style::structure::Module;
using Struct = codegen::style::structure::Struct;
using Variable = codegen::style::structure::Variable;
using Tag = codegen::style::structure::TypeTag;

namespace codegen {
namespace style {
namespace {

constexpr int kErrorBadIconSize   = 861;
constexpr int kErrorBadIconFormat = 862;

char hexChar(uchar ch) {
	if (ch < 10) {
		return '0' + ch;
	} else if (ch < 16) {
		return 'a' + (ch - 10);
	}
	return '0';
}

char hexSecondChar(char ch) {
	return hexChar((*reinterpret_cast<uchar*>(&ch)) & 0x0F);
}

char hexFirstChar(char ch) {
	return hexChar((*reinterpret_cast<uchar*>(&ch)) >> 4);
}

QString stringToEncodedString(const std::string &str) {
	QString result, lineBreak = "\\\n";
	result.reserve(str.size() * 8);
	bool writingHexEscapedCharacters = false, startOnNewLine = false;
	int lastCutSize = 0;
	for (uchar ch : str) {
		if (result.size() - lastCutSize > 80) {
			startOnNewLine = true;
			result.append(lineBreak);
			lastCutSize = result.size();
		}
		if (ch == '\n') {
			writingHexEscapedCharacters = false;
			result.append("\\n");
		} else if (ch == '\t') {
			writingHexEscapedCharacters = false;
			result.append("\\t");
		} else if (ch == '"' || ch == '\\') {
			writingHexEscapedCharacters = false;
			result.append('\\').append(ch);
		} else if (ch < 32 || ch > 127) {
			writingHexEscapedCharacters = true;
			result.append("\\x").append(hexFirstChar(ch)).append(hexSecondChar(ch));
		} else {
			if (writingHexEscapedCharacters) {
				writingHexEscapedCharacters = false;
				result.append("\"\"");
			}
			result.append(ch);
		}
	}
	return '"' + (startOnNewLine ? lineBreak : QString()) + result + '"';
}

QString stringToBinaryArray(const std::string &str) {
	QStringList rows, chars;
	chars.reserve(13);
	rows.reserve(1 + (str.size() / 13));
	for (uchar ch : str) {
		if (chars.size() > 12) {
			rows.push_back(chars.join(", "));
			chars.clear();
		}
		chars.push_back(QString("0x") + hexFirstChar(ch) + hexSecondChar(ch));
	}
	if (!chars.isEmpty()) {
		rows.push_back(chars.join(", "));
	}
	return QString("{") + ((rows.size() > 1) ? '\n' : ' ') + rows.join(",\n") + " }";
}

QString pxValueName(int value) {
	QString result = "px";
	if (value < 0) {
		value = -value;
		result += 'm';
	}
	return result + QString::number(value);
}

} // namespace

Generator::Generator(const structure::Module &module, const QString &destBasePath, const common::ProjectInfo &project)
: module_(module)
, basePath_(destBasePath)
, baseName_(QFileInfo(basePath_).baseName())
, project_(project) {
}

bool Generator::writeHeader() {
	header_ = std::make_unique<common::CppFile>(basePath_ + ".h", project_);

	header_->include("ui/style/style_core.h").newline();

	if (!writeHeaderStyleNamespace()) {
		return false;
	}
	if (!writeRefsDeclarations()) {
		return false;
	}

	return header_->finalize();
}

bool Generator::writeSource() {
	source_ = std::make_unique<common::CppFile>(basePath_ + ".cpp", project_);

	writeIncludesInSource();

	if (module_.hasVariables()) {
		source_->pushNamespace().newline();
		source_->stream() << "\
bool inited = false;\n\
\n\
class Module_" << baseName_ << " : public style::internal::ModuleBase {\n\
public:\n\
	Module_" << baseName_ << "() { style::internal::registerModule(this); }\n\
	~Module_" << baseName_ << "() { style::internal::unregisterModule(this); }\n\
\n\
	void start() override {\n\
		style::internal::init_" << baseName_ << "();\n\
	}\n\
	void stop() override {\n\
	}\n\
};\n\
Module_" << baseName_ << " registrator;\n";
		if (!writeVariableDefinitions()) {
			return false;
		}
		source_->newline().popNamespace();

		source_->newline().pushNamespace("st");
		if (!writeRefsDefinition()) {
			return false;
		}

		source_->popNamespace().newline();
		source_->newline().pushNamespace("style").pushNamespace("internal").newline();
		if (!writeVariableInit()) {
			return false;
		}
	}

	return source_->finalize();
}

// Empty result means an error.
QString Generator::typeToString(structure::Type type) const {
	switch (type.tag) {
	case Tag::Invalid: return QString();
	case Tag::Int: return "int";
	case Tag::Double: return "double";
	case Tag::Pixels: return "int";
	case Tag::String: return "QString";
	case Tag::Color: return "style::color";
	case Tag::Point: return "style::point";
	case Tag::Sprite: return "style::sprite";
	case Tag::Size: return "style::size";
	case Tag::Transition: return "style::transition";
	case Tag::Cursor: return "style::cursor";
	case Tag::Align: return "style::align";
	case Tag::Margins: return "style::margins";
	case Tag::Font: return "style::font";
	case Tag::Icon: return "style::icon";
	case Tag::Struct: return "style::" + type.name.back();
	}
	return QString();
}

// Empty result means an error.
QString Generator::typeToDefaultValue(structure::Type type) const {
	switch (type.tag) {
	case Tag::Invalid: return QString();
	case Tag::Int: return "0";
	case Tag::Double: return "0.";
	case Tag::Pixels: return "0";
	case Tag::String: return "QString()";
	case Tag::Color: return "{ Qt::Uninitialized }";
	case Tag::Point: return "{ 0, 0 }";
	case Tag::Sprite: return "{ 0, 0, 0, 0 }";
	case Tag::Size: return "{ 0, 0 }";
	case Tag::Transition: return "anim::linear";
	case Tag::Cursor: return "style::cur_default";
	case Tag::Align: return "style::al_topleft";
	case Tag::Margins: return "{ 0, 0, 0, 0 }";
	case Tag::Font: return "{ Qt::Uninitialized }";
	case Tag::Icon: return "{ Qt::Uninitialized }";
	case Tag::Struct: {
		if (auto realType = module_.findStruct(type.name)) {
			QStringList fields;
			for (auto field : realType->fields) {
				fields.push_back(typeToDefaultValue(field.type));
			}
			return "{ " + fields.join(", ") + " }";
		}
		return QString();
	} break;
	}
	return QString();
}

// Empty result means an error.
QString Generator::valueAssignmentCode(structure::Value value) const {
	auto copy = value.copyOf();
	if (!copy.isEmpty()) {
		return "st::" + copy.back();
	}

	switch (value.type().tag) {
	case Tag::Invalid: return QString();
	case Tag::Int: return QString("%1").arg(value.Int());
	case Tag::Double: return QString("%1").arg(value.Double());
	case Tag::Pixels: return pxValueName(value.Int());
	case Tag::String: return QString("qsl(%1)").arg(stringToEncodedString(value.String()));
	case Tag::Color: {
		auto v(value.Color());
		return QString("{ %1, %2, %3, %4 }").arg(v.red).arg(v.green).arg(v.blue).arg(v.alpha);
	} break;
	case Tag::Point: {
		auto v(value.Point());
		return QString("{ %1, %2 }").arg(pxValueName(v.x)).arg(pxValueName(v.y));
	} break;
	case Tag::Sprite: {
		auto v(value.Sprite());
		return QString("{ %1, %2, %3, %4 }").arg(pxValueName(v.left)).arg(pxValueName(v.top)).arg(pxValueName(v.width)).arg(pxValueName(v.height));
	} break;
	case Tag::Size: {
		auto v(value.Size());
		return QString("{ %1, %2 }").arg(pxValueName(v.width)).arg(pxValueName(v.height));
	} break;
	case Tag::Transition: return QString("anim::%1").arg(value.String().c_str());
	case Tag::Cursor: return QString("style::cur_%1").arg(value.String().c_str());
	case Tag::Align: return QString("style::al_%1").arg(value.String().c_str());
	case Tag::Margins: {
		auto v(value.Margins());
		return QString("{ %1, %2, %3, %4 }").arg(pxValueName(v.left)).arg(pxValueName(v.top)).arg(pxValueName(v.right)).arg(pxValueName(v.bottom));
	} break;
	case Tag::Font: {
		auto v(value.Font());
		QString family = "0";
		if (!v.family.empty()) {
			auto familyIndex = fontFamilies_.value(v.family, -1);
			if (familyIndex < 0) {
				return QString();
			}
			family = QString("font%1index").arg(familyIndex);
		}
		return QString("{ %1, %2, %3 }").arg(pxValueName(v.size)).arg(v.flags).arg(family);
	} break;
	case Tag::Icon: {
		auto v(value.Icon());
		if (v.parts.empty()) return QString();

		QStringList parts;
		for (const auto &part : v.parts) {
			auto maskIndex = iconMasks_.value(part.filename, -1);
			if (maskIndex < 0) {
				return QString();
			}
			auto color = valueAssignmentCode(part.color);
			auto offset = valueAssignmentCode(part.offset);
			parts.push_back(QString("MonoIcon{ &iconMask%1, %2, %3 }").arg(maskIndex).arg(color).arg(offset));
		}
		return QString("{ %1 }").arg(parts.join(", "));
	} break;
	case Tag::Struct: {
		if (!value.Fields()) return QString();

		QStringList fields;
		for (auto field : *value.Fields()) {
			fields.push_back(valueAssignmentCode(field.variable.value));
		}
		return "{ " + fields.join(", ") + " }";
	} break;
	}
	return QString();
}

bool Generator::writeHeaderStyleNamespace() {
	if (!module_.hasStructs() && !module_.hasVariables()) {
		return true;
	}
	header_->pushNamespace("style");

	if (module_.hasVariables()) {
		header_->pushNamespace("internal").newline();
		header_->stream() << "void init_" << baseName_ << "();\n\n";
		header_->popNamespace();
	}
	if (module_.hasStructs()) {
		header_->newline();
		if (!writeStructsDefinitions()) {
			return false;
		}
	}

	header_->popNamespace().newline();
	return true;
}

bool Generator::writeStructsDefinitions() {
	if (!module_.hasStructs()) {
		return true;
	}

	bool result = module_.enumStructs([this](const Struct &value) -> bool {
		header_->stream() << "struct " << value.name.back() << " {\n";
		for (const auto &field : value.fields) {
			auto type = typeToString(field.type);
			if (type.isEmpty()) {
				return false;
			}
			header_->stream() << "\t" << type << " " << field.name.back() << ";\n";
		}
		header_->stream() << "};\n\n";
		return true;
	});

	return result;
}

bool Generator::writeRefsDeclarations() {
	if (!module_.hasVariables()) {
		return true;
	}

	header_->pushNamespace("st");

	bool result = module_.enumVariables([this](const Variable &value) -> bool {
		auto name = value.name.back();
		auto type = typeToString(value.value.type());
		if (type.isEmpty()) {
			return false;
		}

		header_->stream() << "extern const " << type << " &" << name << ";\n";
		return true;
	});

	header_->popNamespace();

	return result;
}

bool Generator::writeIncludesInSource() {
	if (!module_.hasIncludes()) {
		return true;
	}

	bool result = module_.enumIncludes([this](const Module &module) -> bool {
		source_->include("style_" + QFileInfo(module.filepath()).baseName() + ".h");
		return true;
	});
	source_->newline();
	return result;
}

bool Generator::writeVariableDefinitions() {
	if (!module_.hasVariables()) {
		return true;
	}

	source_->newline();
	bool result = module_.enumVariables([this](const Variable &variable) -> bool {
		auto name = variable.name.back();
		auto type = typeToString(variable.value.type());
		if (type.isEmpty()) {
			return false;
		}
		source_->stream() << type << " _" << name << " = " << typeToDefaultValue(variable.value.type()) << ";\n";
		return true;
	});
	return result;
}

bool Generator::writeRefsDefinition() {
	if (!module_.hasVariables()) {
		return true;
	}

	source_->newline();
	bool result = module_.enumVariables([this](const Variable &variable) -> bool {
		auto name = variable.name.back();
		auto type = typeToString(variable.value.type());
		if (type.isEmpty()) {
			return false;
		}
		source_->stream() << "const " << type << " &" << name << "(_" << name << ");\n";
		return true;
	});
	return result;
}

bool Generator::writeVariableInit() {
	if (!module_.hasVariables()) {
		return true;
	}

	if (!collectUniqueValues()) {
		return false;
	}
	bool hasUniqueValues = (!pxValues_.isEmpty() || !fontFamilies_.isEmpty() || !iconMasks_.isEmpty());
	if (hasUniqueValues) {
		source_->pushNamespace();
		if (!writePxValuesInit()) {
			return false;
		}
		if (!writeFontFamiliesInit()) {
			return false;
		}
		if (!writeIconValues()) {
			return false;
		}
		source_->popNamespace().newline();
	}

	source_->stream() << "\
void init_" << baseName_ << "() {\n\
	if (inited) return;\n\
	inited = true;\n\n";

	if (module_.hasIncludes()) {
		bool writtenAtLeastOne = false;
		bool result = module_.enumIncludes([this,&writtenAtLeastOne](const Module &module) -> bool {
			if (module.hasVariables()) {
				source_->stream() << "\tinit_style_" + QFileInfo(module.filepath()).baseName() + "();\n";
				writtenAtLeastOne = true;
			}
			return true;
		});
		if (!result) {
			return false;
		}
		if (writtenAtLeastOne) {
			source_->newline();
		}
	}

	if (!pxValues_.isEmpty() || !fontFamilies_.isEmpty()) {
		if (!pxValues_.isEmpty()) {
			source_->stream() << "\tinitPxValues();\n";
		}
		if (!fontFamilies_.isEmpty()) {
			source_->stream() << "\tinitFontFamilies();\n";
		}
		source_->newline();
	}

	bool result = module_.enumVariables([this](const Variable &variable) -> bool {
		auto name = variable.name.back();
		auto value = valueAssignmentCode(variable.value);
		if (value.isEmpty()) {
			return false;
		}
		source_->stream() << "\t_" << name << " = " << value << ";\n";
		return true;
	});

	source_->stream() << "\
}\n\n";
	return result;
}

bool Generator::writePxValuesInit() {
	if (pxValues_.isEmpty()) {
		return true;
	}

	for (auto i = pxValues_.cbegin(), e = pxValues_.cend(); i != e; ++i) {
		source_->stream() << "int " << pxValueName(i.key()) << " = " << i.key() << ";\n";
	}
	source_->stream() << "\
void initPxValues() {\n\
	if (cRetina()) return;\n\
\n\
	switch (cScale()) {\n";
	for (int i = 1, scalesCount = scales.size(); i < scalesCount; ++i) {
		source_->stream() << "\tcase " << scaleNames.at(i) << ":\n";
		for (auto it = pxValues_.cbegin(), e = pxValues_.cend(); it != e; ++it) {
			auto value = it.key();
			int adjusted = structure::data::pxAdjust(value, scales.at(i));
			if (adjusted != value) {
				source_->stream() << "\t\t" << pxValueName(value) << " = " << adjusted << ";\n";
			}
		}
		source_->stream() << "\tbreak;\n";
	}
	source_->stream() << "\
	}\n\
}\n\n";
	return true;
}

bool Generator::writeFontFamiliesInit() {
	if (fontFamilies_.isEmpty()) {
		return true;
	}

	for (auto familyIndex : fontFamilies_) {
		source_->stream() << "int font" << familyIndex << "index;\n";
	}
	source_->stream() << "void initFontFamilies() {\n";
	for (auto i = fontFamilies_.cbegin(), e = fontFamilies_.cend(); i != e; ++i) {
		auto family = stringToEncodedString(i.key());
		source_->stream() << "\tfont" << i.value() << "index = style::internal::registerFontFamily(" << family << ");\n";
	}
	source_->stream() << "}\n\n";
	return true;
}

namespace {

QByteArray iconMaskValueSize(int width, int height) {
	QByteArray result;
	QLatin1String generateTag("GENERATE:");
	result.append(generateTag.data(), generateTag.size());
	QLatin1String sizeTag("SIZE:");
	result.append(sizeTag.data(), sizeTag.size());
	{
		QBuffer buffer(&result);
		buffer.open(QIODevice::Append);

		QDataStream stream(&buffer);
		stream.setVersion(QDataStream::Qt_5_1);
		stream << qint32(width) << qint32(height);
	}
	return result;
}

QByteArray iconMaskValuePng(const QString &filepath) {
	QByteArray result;

	QImage png100x(filepath + ".png");
	QImage png200x(filepath + "@2x.png");
	png100x.setDevicePixelRatio(1.);
	png200x.setDevicePixelRatio(1.);
	if (png100x.isNull()) {
		common::logError(common::kErrorFileNotOpened, filepath + ".png") << "could not open icon file";
		return result;
	}
	if (png200x.isNull()) {
		common::logError(common::kErrorFileNotOpened, filepath + "@2x.png") << "could not open icon file";
		return result;
	}
	if (png100x.format() != png200x.format()) {
		common::logError(kErrorBadIconFormat, filepath + ".png") << "1x and 2x icons have different format";
		return result;
	}
	if (png100x.width() * 2 != png200x.width() || png100x.height() * 2 != png200x.height()) {
		common::logError(kErrorBadIconSize, filepath + ".png") << "bad icons size, 1x: " << png100x.width() << "x" << png100x.height() << ", 2x: " << png200x.width() << "x" << png200x.height();
		return result;
	}
	QImage png125x = png200x.scaled(structure::data::pxAdjust(png100x.width(), 5), structure::data::pxAdjust(png100x.height(), 5), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	QImage png150x = png200x.scaled(structure::data::pxAdjust(png100x.width(), 6), structure::data::pxAdjust(png100x.height(), 6), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

	QImage composed(png200x.width() + png100x.width(), png200x.height() + png150x.height(), png100x.format());
	{
		QPainter p(&composed);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.fillRect(0, 0, composed.width(), composed.height(), QColor(0, 0, 0, 255));
		p.drawImage(0, 0, png200x);
		p.drawImage(png200x.width(), 0, png100x);
		p.drawImage(0, png200x.height(), png150x);
		p.drawImage(png150x.width(), png200x.height(), png125x);
	}
	{
		QBuffer buffer(&result);
		composed.save(&buffer, "PNG");
//		composed.save(filePath + "@final.png", "PNG");
	}
	return result;
}

} // namespace

bool Generator::writeIconValues() {
	if (iconMasks_.isEmpty()) {
		return true;
	}

	for (auto i = iconMasks_.cbegin(), e = iconMasks_.cend(); i != e; ++i) {
		QString filePath = i.key();
		QByteArray maskData;
		QImage png100x, png200x;
		if (filePath.startsWith("size://")) {
			QStringList dimensions = filePath.mid(7).split(',');
			if (dimensions.size() < 2 || dimensions.at(0).toInt() <= 0 || dimensions.at(1).toInt() <= 0) {
				common::logError(common::kErrorFileNotOpened, filePath) << "bad dimensions";
				return false;
			}
			maskData = iconMaskValueSize(dimensions.at(0).toInt(), dimensions.at(1).toInt());
		} else {
			maskData = iconMaskValuePng(filePath);
		}
		if (maskData.isEmpty()) {
			return false;
		}
		source_->stream() << "const uchar iconMask" << i.value() << "Data[] = " << stringToBinaryArray(maskData.toStdString()) << ";\n";
		source_->stream() << "IconMask iconMask" << i.value() << "(iconMask" << i.value() << "Data);\n\n";
	}
	return true;
}

bool Generator::collectUniqueValues() {
	int fontFamilyIndex = 0;
	int iconMaskIndex = 0;
	std::function<bool(const Variable&)> collector = [this, &collector, &fontFamilyIndex, &iconMaskIndex](const Variable &variable) {
		auto value = variable.value;
		switch (value.type().tag) {
		case Tag::Invalid:
		case Tag::Int:
		case Tag::Double:
		case Tag::String:
		case Tag::Color:
		case Tag::Transition:
		case Tag::Cursor:
		case Tag::Align: break;
		case Tag::Pixels: pxValues_.insert(value.Int(), true); break;
		case Tag::Point: {
			auto v(value.Point());
			pxValues_.insert(v.x, true);
			pxValues_.insert(v.y, true);
		} break;
		case Tag::Sprite: {
			auto v(value.Sprite());
			pxValues_.insert(v.left, true);
			pxValues_.insert(v.top, true);
			pxValues_.insert(v.width, true);
			pxValues_.insert(v.height, true);
		} break;
		case Tag::Size: {
			auto v(value.Size());
			pxValues_.insert(v.width, true);
			pxValues_.insert(v.height, true);
		} break;
		case Tag::Margins: {
			auto v(value.Margins());
			pxValues_.insert(v.left, true);
			pxValues_.insert(v.top, true);
			pxValues_.insert(v.right, true);
			pxValues_.insert(v.bottom, true);
		} break;
		case Tag::Font: {
			auto v(value.Font());
			pxValues_.insert(v.size, true);
			if (!v.family.empty() && !fontFamilies_.contains(v.family)) {
				fontFamilies_.insert(v.family, ++fontFamilyIndex);
			}
		} break;
		case Tag::Icon: {
			auto v(value.Icon());
			for (const auto &part : v.parts) {
				pxValues_.insert(part.offset.Point().x, true);
				pxValues_.insert(part.offset.Point().y, true);
				if (!iconMasks_.contains(part.filename)) {
					iconMasks_.insert(part.filename, ++iconMaskIndex);
				}
			}
		} break;
		case Tag::Struct: {
			auto fields = variable.value.Fields();
			if (!fields) {
				return false;
			}

			for (auto field : *fields) {
				if (!collector(field.variable)) {
					return false;
				}
			}
		} break;
		}
		return true;
	};
	return module_.enumVariables(collector);
}

} // namespace style
} // namespace codegen
