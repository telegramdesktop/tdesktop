/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "codegen/style/parsed_file.h"

#include <iostream>
#include <QtCore/QMap>
#include <QtCore/QDir>
#include <QtCore/QRegularExpression>
#include "codegen/common/basic_tokenized_file.h"
#include "codegen/common/logging.h"

using BasicToken = codegen::common::BasicTokenizedFile::Token;
using BasicType = BasicToken::Type;

namespace codegen {
namespace style {

using structure::logFullName;

namespace {

constexpr int kErrorInIncluded         = 801;
constexpr int kErrorTypeMismatch       = 802;
constexpr int kErrorUnknownField       = 803;
constexpr int kErrorIdentifierNotFound = 804;
constexpr int kErrorAlreadyDefined     = 805;
constexpr int kErrorBadString          = 806;
constexpr int kErrorIconDuplicate      = 807;
constexpr int kErrorBadIconModifier    = 808;
constexpr int kErrorCyclicDependency   = 809;

QString findInputFile(const Options &options) {
	for (const auto &dir : options.includePaths) {
		QString tryPath = QDir(dir).absolutePath() + '/' + options.inputPath;
		if (QFileInfo(tryPath).exists()) {
			return tryPath;
		}
	}
	return options.inputPath;
}

QString tokenValue(const BasicToken &token) {
	if (token.type == BasicType::String) {
		return token.value;
	}
	return token.original.toStringUnchecked();
}

bool isValidColor(const QString &str) {
	auto len = str.size();
	if (len != 6 && len != 8) {
		return false;
	}

	for (auto ch : str) {
		auto code = ch.unicode();
		if ((code < '0' || code > '9') && (code < 'a' || code > 'f')) {
			return false;
		}
	}
	return true;
}

uchar toGray(uchar r, uchar g, uchar b) {
	return qMax(qMin(int(0.21 * r + 0.72 * g + 0.07 * b), 255), 0);
}

uchar readHexUchar(QChar ch) {
	auto code = ch.unicode();
	return (code >= '0' && code <= '9') ? ((code - '0') & 0xFF) : ((code + 10 - 'a') & 0xFF);
}

uchar readHexUchar(QChar char1, QChar char2) {
	return ((readHexUchar(char1) & 0x0F) << 4) | (readHexUchar(char2) & 0x0F);
}

structure::data::color convertWebColor(const QString &str, const QString &fallback = QString()) {
	uchar r = 0, g = 0, b = 0, a = 255;
	if (isValidColor(str)) {
		r = readHexUchar(str.at(0), str.at(1));
		g = readHexUchar(str.at(2), str.at(3));
		b = readHexUchar(str.at(4), str.at(5));
		if (str.size() == 8) {
			a = readHexUchar(str.at(6), str.at(7));
		}
	}
	return { r, g, b, a, fallback };
}

structure::data::color convertIntColor(int r, int g, int b, int a) {
	return { uchar(r & 0xFF), uchar(g & 0xFF), uchar(b & 0xFF), uchar(a & 0xFF) };
}

std::string logType(const structure::Type &type) {
	if (type.tag == structure::TypeTag::Struct) {
		return "struct " + logFullName(type.name);
	}
	static auto builtInTypes = new QMap<structure::TypeTag, std::string> {
		{ structure::TypeTag::Int       , "int" },
		{ structure::TypeTag::Double    , "double" },
		{ structure::TypeTag::Pixels    , "pixels" },
		{ structure::TypeTag::String    , "string" },
		{ structure::TypeTag::Color     , "color" },
		{ structure::TypeTag::Point     , "point" },
		{ structure::TypeTag::Size      , "size" },
		{ structure::TypeTag::Align     , "align" },
		{ structure::TypeTag::Margins   , "margins" },
		{ structure::TypeTag::Font      , "font" },
	};
	return builtInTypes->value(type.tag, "invalid");
}

bool validateAnsiString(const QString &value) {
	for (auto ch : value) {
		if (ch.unicode() > 127) {
			return false;
		}
	}
	return true;
}

bool validateAlignString(const QString &value) {
	return QRegularExpression("^[a-z_]+$").match(value).hasMatch();
}

} // namespace

Modifier GetModifier(const QString &name) {
	static QMap<QString, Modifier> modifiers;
	if (modifiers.empty()) {
		modifiers.insert("invert", [](QImage &png100x, QImage &png200x) {
			png100x.invertPixels();
			png200x.invertPixels();
		});
		modifiers.insert("flip_horizontal", [](QImage &png100x, QImage &png200x) {
			png100x = png100x.mirrored(true, false);
			png200x = png200x.mirrored(true, false);
		});
		modifiers.insert("flip_vertical", [](QImage &png100x, QImage &png200x) {
			png100x = png100x.mirrored(false, true);
			png200x = png200x.mirrored(false, true);
		});
	}
	return modifiers.value(name);
}

ParsedFile::ParsedFile(
	const Options &options,
	std::vector<QString> includeStack)
: filePath_(findInputFile(options))
, file_(filePath_)
, options_(options)
, includeStack_(includeStack) {
}

bool ParsedFile::read() {
	if (std::find(begin(includeStack_), end(includeStack_), filePath_)
		!= end(includeStack_)) {
		logError(kErrorCyclicDependency) << "include cycle detected.";
		return false;
	} else if (!file_.read()) {
		return false;
	}

	auto absolutePath = QFileInfo(filePath_).absoluteFilePath();
	module_ = std::make_unique<structure::Module>(absolutePath);
	do {
		if (auto startToken = file_.getToken(BasicType::Name)) {
			if (tokenValue(startToken) == "using") {
				if (auto includedResult = readIncluded()) {
					module_->addIncluded(std::move(includedResult));
					continue;
				}
			} else if (auto braceOpen = file_.getToken(BasicType::LeftBrace)) {
				if (auto structResult = readStruct(tokenValue(startToken))) {
					if (module_->addStruct(structResult)) {
						continue;
					}
					logError(kErrorAlreadyDefined) << "struct '" << logFullName(structResult.name) << "' already defined";
					break;
				}
			} else if (auto colonToken = file_.getToken(BasicType::Colon)) {
				if (auto variableResult = readVariable(tokenValue(startToken))) {
					if (module_->addVariable(variableResult)) {
						continue;
					}
					logError(kErrorAlreadyDefined) << "variable '" << logFullName(variableResult.name) << "' already defined";
					break;
				}
			}
		}
		if (file_.atEnd()) {
			break;
		}
		logErrorUnexpectedToken() << "using keyword, or struct definition, or variable definition";
	} while (!failed());

	if (failed()) {
		module_ = nullptr;
	}
	return !failed();
}

common::LogStream ParsedFile::logErrorTypeMismatch() {
	return logError(kErrorTypeMismatch) << "type mismatch: ";
}

ParsedFile::ModulePtr ParsedFile::readIncluded() {
	if (auto usingFile = assertNextToken(BasicType::String)) {
		if (assertNextToken(BasicType::Semicolon)) {
			auto includeStack = includeStack_;
			includeStack.push_back(filePath_);
			ParsedFile included(
				includedOptions(tokenValue(usingFile)),
				includeStack);
			if (included.read()) {
				return included.getResult();
			} else {
				logError(kErrorInIncluded) << "error while parsing '" << tokenValue(usingFile).toStdString() << "'";
			}
		}
	}
	return nullptr;
}

structure::Struct ParsedFile::readStruct(const QString &name) {
	if (options_.isPalette) {
		logErrorUnexpectedToken() << "unique color variable for the palette";
		return {};
	}

	structure::Struct result = { composeFullName(name) };
	do {
		if (auto fieldName = file_.getToken(BasicType::Name)) {
			if (auto field = readStructField(tokenValue(fieldName))) {
				result.fields.push_back(field);
			}
		} else if (assertNextToken(BasicType::RightBrace)) {
			if (result.fields.isEmpty()) {
				logErrorUnexpectedToken() << "at least one field in struct";
			}
			break;
		}
	} while (!failed());
	return result;
}

structure::Variable ParsedFile::readVariable(const QString &name) {
	structure::Variable result = { composeFullName(name) };
	if (auto value = readValue()) {
		result.value = value;
		if (options_.isPalette && value.type().tag != structure::TypeTag::Color) {
			logErrorUnexpectedToken() << "unique color variable for the palette";
			return {};
		}
		if (value.type().tag != structure::TypeTag::Struct || !value.copyOf().empty()) {
			assertNextToken(BasicType::Semicolon);
			result.description = file_.getCurrentLineComment();
		}
	}
	return result;
}

structure::StructField ParsedFile::readStructField(const QString &name) {
	structure::StructField result = { composeFullName(name) };
	if (auto colonToken = assertNextToken(BasicType::Colon)) {
		if (auto type = readType()) {
			result.type = type;
			assertNextToken(BasicType::Semicolon);
		}
	}
	return result;
}

structure::Type ParsedFile::readType() {
	structure::Type result;
	if (auto nameToken = assertNextToken(BasicType::Name)) {
		auto name = tokenValue(nameToken);
		if (auto builtInType = typeNames_.value(name.toStdString())) {
			result = builtInType;
		} else {
			auto fullName = composeFullName(name);
			if (module_->findStruct(fullName)) {
				result.tag = structure::TypeTag::Struct;
				result.name = fullName;
			} else {
				logError(kErrorIdentifierNotFound) << "type name '" << logFullName(fullName) << "' not found";
			}
		}
	}
	return result;
}

structure::Value ParsedFile::readValue() {
	if (auto colorValue = readColorValue()) {
		return colorValue;
	} else if (auto pointValue = readPointValue()) {
		return pointValue;
	} else if (auto sizeValue = readSizeValue()) {
		return sizeValue;
	} else if (auto alignValue = readAlignValue()) {
		return alignValue;
	} else if (auto marginsValue = readMarginsValue()) {
		return marginsValue;
	} else if (auto fontValue = readFontValue()) {
		return fontValue;
	} else if (auto iconValue = readIconValue()) {
		return iconValue;
	} else if (auto numericValue = readNumericValue()) {
		return numericValue;
	} else if (auto stringValue = readStringValue()) {
		return stringValue;
	} else if (auto structValue = readStructValue()) {
		return structValue;
	} else if (auto copyValue = readCopyValue()) {
		return copyValue;
	} else {
		logErrorUnexpectedToken() << "variable value";
	}
	return {};
}

structure::Value ParsedFile::readStructValue() {
	if (auto structName = file_.getToken(BasicType::Name)) {
		if (auto result = defaultConstructedStruct(composeFullName(tokenValue(structName)))) {
			if (file_.getToken(BasicType::LeftParenthesis)) {
				if (!readStructParents(result)) {
					return {};
				}
			}
			if (assertNextToken(BasicType::LeftBrace)) {
				readStructValueInner(result);
			}
			return result;
		}
		file_.putBack();
	}
	return {};
}

structure::Value ParsedFile::defaultConstructedStruct(const structure::FullName &structName) {
	if (auto pattern = module_->findStruct(structName)) {
		QList<structure::data::field> fields;
		fields.reserve(pattern->fields.size());
		for (const auto &fieldType : pattern->fields) {
			fields.push_back({
				{ // variable
					fieldType.name,
					{ fieldType.type, Qt::Uninitialized }, // value
				},
				structure::data::field::Status::Uninitialized, // status
			});
		}
		return { structName, fields };
	}
	return {};
}

void ParsedFile::applyStructParent(structure::Value &result, const structure::FullName &parentName) {
	bool fromTheSameModule = false;
	if (auto parent = module_->findVariable(parentName, &fromTheSameModule)) {
		if (parent->value.type() != result.type()) {
			logErrorTypeMismatch() << "parent '" << logFullName(parentName) << "' has type '" << logType(parent->value.type()) << "' while child value has type " << logType(result.type());
			return;
		}

		const auto *srcFields(parent->value.Fields());
		auto *dstFields(result.Fields());
		if (!srcFields || !dstFields) {
			logAssert(false) << "struct data check failed";
			return;
		}

		logAssert(srcFields->size() == dstFields->size()) << "struct size check failed";
		for (int i = 0, s = srcFields->size(); i != s; ++i) {
			const auto &srcField(srcFields->at(i));
			auto &dstField((*dstFields)[i]);
			using Status = structure::data::field::Status;
			if (srcField.status == Status::Explicit ||
				dstField.status == Status::Uninitialized) {
				const auto &srcValue(srcField.variable.value);
				auto &dstValue(dstField.variable.value);
				logAssert(srcValue.type() == dstValue.type()) << "struct field type check failed";

				// Optimization: don't let the style files to contain unnamed inherited
				// icons from the other (included) style files, because they will
				// duplicate the binary data across different style c++ source files.
				//
				// Example:
				// a.style has "A: Struct { icon: icon { ..file.. } };" and
				// b.style has "B: Struct(A) { .. };" with non-overriden icon field.
				// Then both style_a.cpp and style_b.cpp will contain binary data of "file".
				if (!fromTheSameModule
					&& srcValue.type().tag == structure::TypeTag::Icon
					&& !srcValue.Icon().parts.empty()
					&& srcValue.copyOf().isEmpty()) {
					logError(kErrorIconDuplicate) << "an unnamed icon field '" << logFullName(srcField.variable.name) << "' is inherited from parent '" << logFullName(parentName) << "'";
					return;
				}
				dstValue = srcValue;
				dstField.status = Status::Implicit;
			}
		}
	} else {
		logError(kErrorIdentifierNotFound) << "parent '" << logFullName(parentName) << "' not found";
	}
}

bool ParsedFile::readStructValueInner(structure::Value &result) {
	do {
		if (auto fieldName = file_.getToken(BasicType::Name)) {
			if (!assertNextToken(BasicType::Colon)) {
				return false;
			}

			if (auto field = readVariable(tokenValue(fieldName))) {
				if (!assignStructField(result, field)) {
					return false;
				}
			}
		} else if (assertNextToken(BasicType::RightBrace)) {
			return true;
		}
	} while (!failed());
	return false;
}

bool ParsedFile::assignStructField(structure::Value &result, const structure::Variable &field) {
	auto *fields = result.Fields();
	if (!fields) {
		logAssert(false) << "struct data check failed";
		return false;
	}
	for (auto &already : *fields) {
		if (already.variable.name == field.name) {
			if (already.variable.value.type() == field.value.type()) {
				already.variable.value = field.value;
				already.status = structure::data::field::Status::Explicit;
				return true;
			} else {
				logErrorTypeMismatch() << "field '" << logFullName(already.variable.name) << "' has type '" << logType(already.variable.value.type()) << "' while value has type '" << logType(field.value.type()) << "'";
				return false;
			}
		}
	}
	logError(kErrorUnknownField) << "field '" << logFullName(field.name) << "' was not found in struct of type '" << logType(result.type()) << "'";
	return false;
}

bool ParsedFile::readStructParents(structure::Value &result) {
	do {
		if (auto parentName = assertNextToken(BasicType::Name)) {
			applyStructParent(result, composeFullName(tokenValue(parentName)));
			if (file_.getToken(BasicType::RightParenthesis)) {
				return true;
			} else {
				assertNextToken(BasicType::Comma);
			}
		} else {
			logErrorUnexpectedToken() << "struct variable parent";
		}
	} while (!failed());
	return false;
}

structure::Value ParsedFile::readPositiveValue() {
	auto numericToken = file_.getAnyToken();
	if (numericToken.type == BasicType::Int) {
		return { structure::TypeTag::Int, tokenValue(numericToken).toInt() };
	} else if (numericToken.type == BasicType::Double) {
		return { structure::TypeTag::Double, tokenValue(numericToken).toDouble() };
	} else if (numericToken.type == BasicType::Name) {
		auto value = tokenValue(numericToken);
		auto match = QRegularExpression("^\\d+px$").match(value);
		if (match.hasMatch()) {
			return { structure::TypeTag::Pixels, value.mid(0, value.size() - 2).toInt() };
		}
	}
	file_.putBack();
	return {};
}

structure::Value ParsedFile::readNumericValue() {
	if (auto value = readPositiveValue()) {
		return value;
	} else if (auto minusToken = file_.getToken(BasicType::Minus)) {
		if (auto positiveValue = readNumericValue()) {
			return { positiveValue.type().tag, -positiveValue.Int() };
		}
		logErrorUnexpectedToken() << "numeric value";
	}
	return {};
}

structure::Value ParsedFile::readStringValue() {
	if (auto stringToken = file_.getToken(BasicType::String)) {
		auto value = tokenValue(stringToken);
		if (validateAnsiString(value)) {
			return { structure::TypeTag::String, stringToken.value.toStdString() };
		}
		logError(kErrorBadString) << "unicode symbols are not supported";
	}
	return {};
}

structure::Value ParsedFile::readColorValue() {
	if (auto numberSign = file_.getToken(BasicType::Number)) {
		if (options_.isPalette) {
			auto color = file_.getAnyToken();
			if (color.type == BasicType::Int || color.type == BasicType::Name) {
				auto chars = tokenValue(color).toLower();
				if (isValidColor(chars)) {
					if (auto fallbackSeparator = file_.getToken(BasicType::Or)) {
						if (options_.isPalette) {
							if (auto fallbackName = file_.getToken(BasicType::Name)) {
								structure::FullName name = { tokenValue(fallbackName) };
								if (auto variable = module_->findVariableInModule(name, *module_)) {
									return { convertWebColor(chars, tokenValue(fallbackName)) };
								} else {
									logError(kErrorIdentifierNotFound) << "fallback color name";
								}
							} else {
								logErrorUnexpectedToken() << "fallback color name";
							}
						} else {
							logErrorUnexpectedToken() << "';', color fallbacks are only allowed in palette module";
						}
					} else {
						return { convertWebColor(chars) };
					}
				}
			} else {
				logErrorUnexpectedToken() << "color value in #ccc, #ccca, #cccccc or #ccccccaa format";
			}
		} else {
			logErrorUnexpectedToken() << "color value alias, unique color values are only allowed in palette module";
		}
	} else if (auto transparentName = file_.getToken(BasicType::Name)) {
		if (tokenValue(transparentName) == "transparent") {
			return { structure::data::color { 255, 255, 255, 0 } };
		}
		file_.putBack();
	}

	return {};
}

structure::Value ParsedFile::readPointValue() {
	if (auto font = file_.getToken(BasicType::Name)) {
		if (tokenValue(font) == "point") {
			assertNextToken(BasicType::LeftParenthesis);

			auto x = readNumericOrNumericCopyValue(); assertNextToken(BasicType::Comma);
			auto y = readNumericOrNumericCopyValue();
			if (x.type().tag != structure::TypeTag::Pixels ||
				y.type().tag != structure::TypeTag::Pixels) {
				logErrorTypeMismatch() << "expected two px values for the point";
			}

			assertNextToken(BasicType::RightParenthesis);

			return { structure::data::point { x.Int(), y.Int() } };
		}
		file_.putBack();
	}
	return {};
}

structure::Value ParsedFile::readSizeValue() {
	if (auto font = file_.getToken(BasicType::Name)) {
		if (tokenValue(font) == "size") {
			assertNextToken(BasicType::LeftParenthesis);

			auto w = readNumericOrNumericCopyValue(); assertNextToken(BasicType::Comma);
			auto h = readNumericOrNumericCopyValue();
			if (w.type().tag != structure::TypeTag::Pixels ||
				h.type().tag != structure::TypeTag::Pixels) {
				logErrorTypeMismatch() << "expected two px values for the size";
			}

			assertNextToken(BasicType::RightParenthesis);

			return { structure::data::size { w.Int(), h.Int() } };
		}
		file_.putBack();
	}
	return {};
}

structure::Value ParsedFile::readAlignValue() {
	if (auto font = file_.getToken(BasicType::Name)) {
		if (tokenValue(font) == "align") {
			assertNextToken(BasicType::LeftParenthesis);

			auto align = tokenValue(assertNextToken(BasicType::Name));

			assertNextToken(BasicType::RightParenthesis);

			if (validateAlignString(align)) {
				return { structure::TypeTag::Align, align.toStdString() };
			} else {
				logError(kErrorBadString) << "bad align string";
			}
		}
		file_.putBack();
	}
	return {};
}

structure::Value ParsedFile::readMarginsValue() {
	if (auto font = file_.getToken(BasicType::Name)) {
		if (tokenValue(font) == "margins") {
			assertNextToken(BasicType::LeftParenthesis);

			auto l = readNumericOrNumericCopyValue(); assertNextToken(BasicType::Comma);
			auto t = readNumericOrNumericCopyValue(); assertNextToken(BasicType::Comma);
			auto r = readNumericOrNumericCopyValue(); assertNextToken(BasicType::Comma);
			auto b = readNumericOrNumericCopyValue();
			if (l.type().tag != structure::TypeTag::Pixels ||
				t.type().tag != structure::TypeTag::Pixels ||
				r.type().tag != structure::TypeTag::Pixels ||
				b.type().tag != structure::TypeTag::Pixels) {
				logErrorTypeMismatch() << "expected four px values for the margins";
			}

			assertNextToken(BasicType::RightParenthesis);

			return { structure::data::margins { l.Int(), t.Int(), r.Int(), b.Int() } };
		}
		file_.putBack();
	}
	return {};
}

structure::Value ParsedFile::readFontValue() {
	if (auto font = file_.getToken(BasicType::Name)) {
		if (tokenValue(font) == "font") {
			assertNextToken(BasicType::LeftParenthesis);

			int flags = 0;
			structure::Value family, size;
			do {
				if (auto formatToken = file_.getToken(BasicType::Name)) {
					if (tokenValue(formatToken) == "bold") {
						flags |= structure::data::font::Bold;
					} else if (tokenValue(formatToken) == "italic") {
						flags |= structure::data::font::Italic;
					} else if (tokenValue(formatToken) == "underline") {
						flags |= structure::data::font::Underline;
					} else {
						file_.putBack();
					}
				}
				if (auto familyValue = readStringOrStringCopyValue()) {
					family = familyValue;
				} else if (auto sizeValue = readNumericOrNumericCopyValue()) {
					size = sizeValue;
				} else if (file_.getToken(BasicType::RightParenthesis)) {
					break;
				} else {
					logErrorUnexpectedToken() << "font family, font size or ')'";
				}
			} while (!failed());

			if (size.type().tag != structure::TypeTag::Pixels) {
				logErrorTypeMismatch() << "px value for the font size expected";
			}
			return { structure::data::font { family.String(), size.Int(), flags } };
		}
		file_.putBack();
	}
	return {};
}

structure::Value ParsedFile::readIconValue() {
	if (auto font = file_.getToken(BasicType::Name)) {
		if (tokenValue(font) == "icon") {
			std::vector<structure::data::monoicon> parts;
			if (file_.getToken(BasicType::LeftBrace)) { // complex icon
				do {
					if (file_.getToken(BasicType::RightBrace)) {
						break;
					} else if (file_.getToken(BasicType::LeftBrace)) {
						if (auto part = readMonoIconFields()) {
							assertNextToken(BasicType::RightBrace);
							parts.push_back(part);
							file_.getToken(BasicType::Comma);
							continue;
						}
						return {};
					} else {
						logErrorUnexpectedToken() << "icon part or '}'";
						return {};
					}
				} while (true);

			} else if (file_.getToken(BasicType::LeftParenthesis)) { // short icon
				if (auto theOnlyPart = readMonoIconFields()) {
					assertNextToken(BasicType::RightParenthesis);
					parts.push_back(theOnlyPart);
				}
			}

			return { structure::data::icon { parts } };
		}
		file_.putBack();
	}
	return {};
}

structure::Value ParsedFile::readCopyValue() {
	if (auto copyName = file_.getToken(BasicType::Name)) {
		structure::FullName name = { tokenValue(copyName) };
		if (auto variable = module_->findVariable(name)) {
			return variable->value.makeCopy(variable->name);
		}
		file_.putBack();
	}
	return {};
}

structure::Value ParsedFile::readNumericOrNumericCopyValue() {
	if (auto result = readNumericValue()) {
		return result;
	} else if (auto copy = readCopyValue()) {
		auto type = copy.type().tag;
		if (type == structure::TypeTag::Int
			|| type == structure::TypeTag::Double
			|| type == structure::TypeTag::Pixels) {
			return copy;
		} else {
			file_.putBack();
		}
	}
	return {};
}

structure::Value ParsedFile::readStringOrStringCopyValue() {
	if (auto result = readStringValue()) {
		return result;
	} else if (auto copy = readCopyValue()) {
		auto type = copy.type().tag;
		if (type == structure::TypeTag::String) {
			return copy;
		} else {
			file_.putBack();
		}
	}
	return {};
}

structure::data::monoicon ParsedFile::readMonoIconFields() {
	structure::data::monoicon result;
	result.filename = readMonoIconFilename();
	if (!result.filename.isEmpty() && file_.getToken(BasicType::Comma)) {
		if (auto color = readValue()) {
			if (color.type().tag == structure::TypeTag::Color) {
				result.color = color;
				if (file_.getToken(BasicType::Comma)) {
					if (auto offset = readValue()) {
						if (offset.type().tag == structure::TypeTag::Point) {
							result.offset = offset;
						} else {
							logErrorUnexpectedToken() << "icon offset";
						}
					} else {
						logErrorUnexpectedToken() << "icon offset";
					}
				} else {
					result.offset = { structure::data::point { 0, 0 } };
				}
			} else {
				logErrorUnexpectedToken() << "icon color";
			}
		} else {
			logErrorUnexpectedToken() << "icon color";
		}
	}
	return result;
}

QString ParsedFile::readMonoIconFilename() {
	if (auto filename = readValue()) {
		if (filename.type().tag == structure::TypeTag::String) {
			auto fullpath = QString::fromStdString(filename.String());
			auto pathAndModifiers = fullpath.split('-');
			auto filepath = pathAndModifiers[0];
			auto modifiers = pathAndModifiers.mid(1);
			for (auto modifierName : modifiers) {
				if (!GetModifier(modifierName)) {
					logError(kErrorBadIconModifier) << "unknown modifier: " << modifierName.toStdString();
					return QString();
				}
			}
			for (auto &path : options_.includePaths) {
				QFileInfo fileinfo(path + '/' + filepath + ".png");
				if (fileinfo.exists()) {
					return path + '/' + fullpath;
				}
			}
			for (auto &path : options_.includePaths) {
				QFileInfo fileinfo(path + "/icons/" + filepath + ".png");
				if (fileinfo.exists()) {
					return path + "/icons/" + fullpath;
				}
			}
			logError(common::kErrorFileNotFound) << "could not open icon file '" << filename.String() << "'";
		} else if (filename.type().tag == structure::TypeTag::Size) {
			return QString("size://%1,%2").arg(filename.Size().width).arg(filename.Size().height);
		}
	}
	logErrorUnexpectedToken() << "icon filename or rect size";
	return QString();
}

BasicToken ParsedFile::assertNextToken(BasicToken::Type type) {
	auto result = file_.getToken(type);
	if (!result) {
		logErrorUnexpectedToken() << type;
	}
	return result;
}

Options ParsedFile::includedOptions(const QString &filepath) {
	auto result = options_;
	result.inputPath = filepath;
	result.includePaths[0] = QFileInfo(filePath_).dir().absolutePath();
	result.isPalette = (QFileInfo(filepath).suffix() == "palette");
	return result;
}

// Compose context-dependent full name.
structure::FullName ParsedFile::composeFullName(const QString &name) {
	return { name };
}

} // namespace style
} // namespace codegen
