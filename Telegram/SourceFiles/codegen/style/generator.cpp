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

#include <QtCore/QDir>
#include <QtCore/QSet>
#include <functional>
#include "codegen/style/parsed_file.h"

using Module = codegen::style::structure::Module;
using Struct = codegen::style::structure::Struct;
using Variable = codegen::style::structure::Variable;
using Tag = codegen::style::structure::TypeTag;

namespace codegen {
namespace style {
namespace {

char hexChar(uchar ch) {
	if (ch < 10) {
		return '0' + ch;
	} else if (ch < 16) {
		return 'a' + (ch - 10);
	}
	return '0';
}

char hexSecondChar(char ch) {
	return hexChar((*reinterpret_cast<uchar*>(&ch)) << 4);
}

char hexFirstChar(char ch) {
	return hexChar((*reinterpret_cast<uchar*>(&ch)) & 0x0F);
}

QString stringToEncodedString(const std::string &str) {
	QString result;
	result.reserve(str.size() * 4);
	for (auto ch : str) {
		if (ch == '\n') {
			result.append("\\n");
		} else if (ch == '\t') {
			result.append("\\t");
		} else if (ch == '"' || ch == '\\') {
			result.append('\\').append(ch);
		} else if (ch < 32 || ch > 127) {
			result.append("\\x").append(hexFirstChar(ch)).append(hexSecondChar(ch));
		} else {
			result.append(ch);
		}
	}
	return '"' + result + '"';
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

	header_->include("ui/style_core.h").newline();

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
	case Tag::Color: return QString("{ %1, %2, %3, %4 }").arg(value.Color().red).arg(value.Color().green).arg(value.Color().blue).arg(value.Color().alpha);
	case Tag::Point: {
		auto v(value.Point());
		return QString("{ %1, %2 }").arg(pxValueName(v.x)).arg(pxValueName(v.y));
	}
	case Tag::Sprite: {
		auto v(value.Sprite());
		return QString("{ %1, %2, %3, %4 }").arg(pxValueName(v.left)).arg(pxValueName(v.top)).arg(pxValueName(v.width)).arg(pxValueName(v.height));
	}
	case Tag::Size: {
		auto v(value.Size());
		return QString("{ %1, %2 }").arg(pxValueName(v.width)).arg(pxValueName(v.height));
	}
	case Tag::Transition: return QString("anim::%1").arg(value.String().c_str());
	case Tag::Cursor: return QString("style::cur_%1").arg(value.String().c_str());
	case Tag::Align: return QString("style::al_%1").arg(value.String().c_str());
	case Tag::Margins: {
		auto v(value.Margins());
		return QString("{ %1, %2, %3, %4 }").arg(pxValueName(v.left)).arg(pxValueName(v.top)).arg(pxValueName(v.right)).arg(pxValueName(v.bottom));
	}
	case Tag::Font: {
		auto v(value.Font());
		QString family = "0";
		if (!v.family.empty()) {
			auto familyIndex = fontFamilyValues_.value(v.family, -1);
			if (familyIndex < 0) {
				return QString();
			} else {
				family = QString("font%1index").arg(familyIndex);
			}
		}
		return QString("{ %1, %2, %3 }").arg(pxValueName(v.size)).arg(v.flags).arg(family);
	}
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
	bool hasUniqueValues = (!pxValues_.isEmpty() || !fontFamilyValues_.isEmpty());
	if (hasUniqueValues) {
		source_->pushNamespace();
		if (!writePxValues()) {
			return false;
		}
		if (!writeFontFamilyValues()) {
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

	if (hasUniqueValues) {
		if (!pxValues_.isEmpty()) {
			source_->stream() << "\tinitPxValues();\n";
		}
		if (!fontFamilyValues_.isEmpty()) {
			source_->stream() << "\tinitFontFamilyValues();\n";
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

bool Generator::writePxValues() {
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

bool Generator::writeFontFamilyValues() {
	if (fontFamilyValues_.isEmpty()) {
		return true;
	}

	for (auto i = fontFamilyValues_.cbegin(), e = fontFamilyValues_.cend(); i != e; ++i) {
		source_->stream() << "int font" << i.value() << "index;\n";
	}
	source_->stream() << "void initFontFamilyValues() {\n";
	for (auto i = fontFamilyValues_.cbegin(), e = fontFamilyValues_.cend(); i != e; ++i) {
		auto family = stringToEncodedString(i.key());
		source_->stream() << "\tfont" << i.value() << "index = style::internal::registerFontFamily(" << family << ");\n";
	}
	source_->stream() << "}\n\n";
	return true;
}

bool Generator::collectUniqueValues() {
	int fontFamilyIndex = 0;
	std::function<bool(const Variable&)> collector = [this, &collector, &fontFamilyIndex](const Variable &variable) {
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
			if (!v.family.empty() && !fontFamilyValues_.contains(v.family)) {
				fontFamilyValues_.insert(v.family, ++fontFamilyIndex);
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
