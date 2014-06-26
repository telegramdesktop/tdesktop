/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#include "genstyles.h"

#ifdef Q_OS_WIN
#include <QtCore/QtPlugin>
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
#endif

enum ScalarType {
	scNumber,
	scString,
	scColor,
	scPoint,
	scRect,
	scSprite,
	scSize,
	scTransition,
	scCursor,
	scAlign,
	scMargins,
	scFont,

	scTypesCount,
};

string scalarTypeNames[] = {
	"number",
	"string",
	"color",
	"point",
	"rect",
	"sprite",
	"size",
	"transition",
	"cursor",
	"align",
	"margins",
	"font",
};

string outputTypeNames[] = {
	"number",
	"string",
	"color",
	"point",
	"rect",
	"sprite",
	"size",
	"transition",
	"cursor",
	"align",
	"margins",
	"font",
};

enum ClassGenTokenType {
	csName, // [a-zA-Z_][a-zA-Z0-9_]*
	csDelimeter, // ':'
	csFieldFinish, // ';'
	csClassStart, // '{'
	csClassFinish, // '}'
};

bool skipWhitespaces(const char *&from, const char *end) {
	while (from < end && (*from == ' ' || *from == '\n' || *from == '\t' || *from == '\r')) {
		++from;
	}
	return (from < end);
}

bool skipComment(const char *&from, const char *end) {
	if (from >= end) return false;
	if (*from == '/') {
		if (from + 1 >= end) return true;
		if (*(from + 1) == '*') {
			from += 2;
			while (from + 1 < end && (*from != '*' || *(from + 1) != '/')) {
				++from;
			}
			from += 2;
			return (from < end);
		} else if (*(from + 1) == '/') {
			from += 2;
			while (from < end && *from != '\n' && *from != '\r') {
				++from;
			}
			++from;
			return (from < end);
		} else {
			return true;
		}
	}
	return true;
}

void readName(const char *&from, const char *end, string &token) {
	if (from >= end) throw Exception("Unexpected end of file!");

	const char *start = from;
	char ch = *from;
	if (!((ch >= 'a' && ch <= 'z') || ((ch >= 'A') && (ch <= 'Z')))) {
		throw Exception("Unknown error :(");
	}
	while (++from < end) {
		ch = *from;
		if (!((ch >= 'a' && ch <= 'z') || ((ch >= 'A') && (ch <= 'Z')) || ((ch >= '0') && (ch <= '9')) || ch == '_')) break;
	}
	token = string(start, from);
}

void readString(const char *&from, const char *end, string &token) {
	if (from + 1 >= end) throw Exception("Unexpected end of file!");

	token = "";
	char border = *from;
	if (border != '"' && border != '\'') {
		throw Exception("Unknown error :(");
	}

	bool spec = false;
	for (++from; spec || (*from != border); ) {
		if (*from == '\\') {
			spec = true;
		} else if (spec) {
			if (*from == 'n') {
				token += '\n';
			} else if (*from == 't') {
				token += '\t';
			} else if (*from == '\\' || *from == '"' || *from == '\'') {
				token += *from;
			} else {
				throw Exception(QString("Unexpected escaped character in string: %1").arg(*from));
			}
			spec = false;
		} else {
			token += *from;
		}
		if (++from >= end) throw Exception("Unexpected end of file!");
	}
	++from;
}

char hexChar(char ch) {
	if (ch >= 'a' && ch <= 'f') {
		return ch + ('A' - 'a');
	}
	return ch;
}

void readColor(const char *&from, const char *end, string &token) {
	if (from + 3 >= end) throw Exception("Unexpected end of file!");

	token.resize(8);

	int len = 0;
	for (const char *ch = from + 1; ch < end; ++ch) {
		if ((*ch >= '0' && *ch <= '9') || (*ch >= 'A' && *ch <= 'F') || (*ch >= 'a' && *ch <= 'f')) {
			++len;
		} else {
			break;
		}
	}
	if (len != 3 && len != 4 && len != 6 && len != 8) {
		throw Exception("Bad color token");
	}
	if (len == 3 || len == 4) {
		token[0] = token[1] = hexChar(*(++from));
		token[2] = token[3] = hexChar(*(++from));
		token[4] = token[5] = hexChar(*(++from));
		if (len == 3) {
			token[6] = token[7] = 'F';
		} else {
			token[6] = token[7] = hexChar(*(++from));
		}
	} else {
		token[0] = hexChar(*(++from));
		token[1] = hexChar(*(++from));
		token[2] = hexChar(*(++from));
		token[3] = hexChar(*(++from));
		token[4] = hexChar(*(++from));
		token[5] = hexChar(*(++from));
		if (len == 6) {
			token[6] = token[7] = 'F';
		} else {
			token[6] = hexChar(*(++from));
			token[7] = hexChar(*(++from));
		}
	}
	++from;
}

void readNumber(const char *&from, const char *end, string &token) {
	if (from >= end) throw Exception("Unexpected end of file!");

	bool neg = false;
	if (*from == '-') {
		neg = true;
		if (++from >= end) throw Exception("Unexpected end of file!");
	}

	if (*from == '0' && from < end && *(from + 1) >= '0' && *(from + 1) <= '9') throw Exception("Bad number token!");

	token = neg ? "-" : "";
	for (bool wasDot = false; from < end; ++from) {
		if (*from == '.') {
			if (wasDot) throw Exception("Unexpected dot in number!");
			wasDot = true;
		} else if (*from < '0' || *from > '9') {
			break;
		}
		token += *from;
	}
}

void readClassGenToken(const char *&from, const char *end, ClassGenTokenType &tokenType, string &token) {
	const char *start;
	do {
		start = from;
		if (!skipWhitespaces(from, end)) throw Exception("Unexpected end of file!");
		if (!skipComment(from, end)) throw Exception("Unexpected end of comment!");
	} while (start != from);

	if ((*from >= 'a' && *from <= 'z') || ((*from >= 'A') && (*from <= 'Z'))) {
		tokenType = csName;
		return readName(from, end, token);
	} else if (*from == ':') {
		tokenType = csDelimeter;
	} else if (*from == ';') {
		tokenType = csFieldFinish;
	} else if (*from == '{') {
		tokenType = csClassStart;
	} else if (*from == '}') {
		tokenType = csClassFinish;
	} else {
		throw Exception("Could not parse token!");
	}
	++from;
	return;
}

typedef QMap<string, ScalarType> FieldTypesMap;
struct ClassData {
	string name;
	FieldTypesMap fields;
};

typedef QMap<string, ClassData> Classes;
Classes classes;

typedef QMap<string, int> ByName;

bool genClasses(const QString &classes_in, const QString &classes_out) {
	QFile f(classes_in);
	if (!f.open(QIODevice::ReadOnly)) {
		cout << "Could not open style classes input file '" << classes_in.toUtf8().constData() << "'!\n";
		QCoreApplication::exit(1);
		return false;
	}

	QByteArray blob = f.readAll();
	const char *text = blob.constData(), *end = blob.constData() + blob.size();
	ByName byName;
	QVector<ClassData> byIndex;
	string token;
	ClassGenTokenType type;
	try {
		while (true) {
			try {
				readClassGenToken(text, end, type, token);
			} catch (exception &e) {
				if (e.what() != string("Unexpected end of file!")) {
					throw;
				}
				break;
			}
			if (type != csName) {
				throw Exception(QString("Unexpected token, type %1: %2").arg(type).arg(token.c_str()));
			}

			byIndex.push_back(ClassData());
			ClassData &cls(byIndex.back());
			cls.name = token;
			readClassGenToken(text, end, type, token);
			if (type == csDelimeter) {
				readClassGenToken(text, end, type, token);
				if (type != csName) throw Exception(QString("Unexpected token after '%1:', type %2").arg(cls.name.c_str()).arg(type));

				QMap<string, int>::const_iterator i = byName.constFind(token);
				if (i == byName.cend()) throw Exception(QString("Parent class '%1' not found for class '%2'").arg(token.c_str()).arg(cls.name.c_str()));
				cls.fields = byIndex[i.value()].fields;
				readClassGenToken(text, end, type, token);
			}
			if (type != csClassStart) throw Exception(QString("Unexpected token after '%1:%2', type %3").arg(cls.name.c_str()).arg(token.c_str()).arg(type));

			do {
				string fname, ftype;
				readClassGenToken(text, end, type, fname);
				if (type == csClassFinish) {
					byName.insert(cls.name, byIndex.size() - 1);
					break;
				}
				if (type != csName) throw Exception(QString("Unexpected token %1 while reading class '%2'").arg(type).arg(cls.name.c_str()));
				readClassGenToken(text, end, type, token);
				if (type != csDelimeter) throw Exception(QString("Unexpected token %1 while reading field '%2' in class '%3'").arg(type).arg(fname.c_str()).arg(cls.name.c_str()));
				readClassGenToken(text, end, type, ftype);
				if (type != csName) throw Exception(QString("Unexpected token %1 while reading field '%2' in class '%3'").arg(type).arg(fname.c_str()).arg(cls.name.c_str()));
				readClassGenToken(text, end, type, token);
				if (type != csFieldFinish) throw Exception(QString("Unexpected token %1 while reading field '%2:%3' in class '%4'").arg(type).arg(fname.c_str()).arg(ftype.c_str()).arg(cls.name.c_str()));

				ScalarType typeIndex = scTypesCount;
				for (int t = 0; t < scTypesCount; ++t) {
					if (ftype == scalarTypeNames[t]) {
						typeIndex = ScalarType(t);
						break;
					}
				}
				if (typeIndex == scTypesCount) throw Exception(QString("Unknown field type %1 while reading field '%2' in class '%3'").arg(ftype.c_str()).arg(fname.c_str()).arg(cls.name.c_str()));
				FieldTypesMap::const_iterator alr = cls.fields.find(fname);
				if (alr != cls.fields.cend()) throw Exception(QString("Redeclaration of field '%1' in class '%2'").arg(fname.c_str()).arg(cls.name.c_str()));
				cls.fields.insert(fname, typeIndex);
			} while(true);
		}

		QByteArray outText;
		{
			QTextStream tout(&outText);
			tout << "\
/*\n\
Created from \'/Resources/style_classes.txt\' by \'/MetaStyle\' project\n\
\n\
WARNING! All changes made in this file will be lost!\n\
\n\
This file is part of Telegram Desktop,\n\
an unofficial desktop messaging app, see https://telegram.org\n\
\n\
Telegram Desktop is free software: you can redistribute it and/or modify\n\
it under the terms of the GNU General Public License as published by\n\
the Free Software Foundation, either version 3 of the License, or\n\
(at your option) any later version.\n\
\n\
It is distributed in the hope that it will be useful,\n\
but WITHOUT ANY WARRANTY; without even the implied warranty of\n\
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the\n\
GNU General Public License for more details.\n\
\n\
Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE\n\
Copyright (c) 2014 John Preston, https://tdesktop.com\n\
*/\n";
			tout << "#pragma once\n\n#include \"style.h\"\n\nnamespace style {\n";
			for (int i = 0, l = byIndex.size(); i < l; ++i) {
				ClassData &cls(byIndex[i]);
				classes.insert(cls.name, cls);
				tout << "\n\tclass " << cls.name.c_str() << " {\n\tpublic:\n\t\t" << cls.name.c_str() << "(";
				for (FieldTypesMap::const_iterator j = cls.fields.cbegin(), e = cls.fields.cend(); j != e;) {
					tout << "const style::" << outputTypeNames[j.value()].c_str() << " &_" << j.key().c_str();
					if (++j != e) {
						tout << ", ";
					}
				}
				tout << ", Qt::Initialization = Qt::Uninitialized)";
				if (!cls.fields.isEmpty()) {
					tout << " : ";
				}
				for (FieldTypesMap::const_iterator j = cls.fields.cbegin(), e = cls.fields.cend(); j != e;) {
					tout << j.key().c_str() << "(_" << j.key().c_str() << ")";
					if (++j != e) {
						tout << ", ";
					}
				}
				tout << " {\n\t\t}\n\n";
				for (FieldTypesMap::const_iterator j = cls.fields.cbegin(), e = cls.fields.cend(); j != e;) {
					tout << "\t\tstyle::" << outputTypeNames[j.value()].c_str() << " " << j.key().c_str() << ";\n";
					++j;
				}
				tout << "\t};\n";
			}
			tout << "\n};\n";
		}
		QFile out(classes_out);
		if (out.open(QIODevice::ReadOnly)) {
			QByteArray wasOut = out.readAll();
			if (wasOut.size() == outText.size()) {
				if (!memcmp(wasOut.constData(), outText.constData(), outText.size())) {
					return true;
				}
			}
			out.close();
		}
		cout << "Style classes compiled, writing " << byIndex.size() << " classes.\n";
		if (!out.open(QIODevice::WriteOnly)) throw Exception("Could not open style_classes.h for writing!");
		if (out.write(outText) != outText.size()) throw Exception("Could not open style_classes.h for writing!");
	} catch (exception &e) {
		cout << e.what() << "\n";
		QCoreApplication::exit(1);
		return false;
	}
	return true;
}

enum StyleGenTokenType {
	stName, // [a-zA-Z_][a-zA-Z0-9_]*
	stDelimeter, // ':'
	stFieldFinish, // ';'
	stObjectStart, // '{'
	stObjectFinish, // '}'
	stConsStart, // '('
	stConsFinish, // ')'
	stComma, // ','
	stVariant, // '/'
	stString, // "text" or 'text'
	stColor, // #rgb or #rrggbb
	stNumber, // -?([1-9][0-9]+(\.[0-9]+)?|\.[0-9]+)
};

static const int variants[] = { 0, 2, 3, 4 }, variantsCount = sizeof(variants) / sizeof(variants[0]);
static const char *variantNames[] = { "dbisOne", "dbisOneAndQuarter", "dbisOneAndHalf", "dbisTwo" };

static const char *variantPostfixes[] = { "", "_125x", "_150x", "_200x" };
QPixmap *spriteMax = 0;
QImage *variantSprites = 0;
QImage *variantGrids = 0;

void readStyleGenToken(const char *&from, const char *end, StyleGenTokenType &tokenType, string &token) {
	const char *start;
	do {
		start = from;
		if (!skipWhitespaces(from, end)) throw Exception("Unexpected end of file!");
		if (!skipComment(from, end)) throw Exception("Unexpected end of comment!");
	} while (start != from);

	if ((*from >= 'a' && *from <= 'z') || ((*from >= 'A') && (*from <= 'Z'))) {
		tokenType = stName;
		return readName(from, end, token);
	} else if (*from == '"' || *from == '\'') {
		tokenType = stString;
		return readString(from, end, token);
	} else if (*from == '#') {
		tokenType = stColor;
		return readColor(from, end, token);
	} else if (*from == '.' || (*from >= '0' && *from <= '9') || *from == '-') {
		tokenType = stNumber;
		return readNumber(from, end, token);
	} else if (*from == ':') {
		tokenType = stDelimeter;
	} else if (*from == ';') {
		tokenType = stFieldFinish;
	} else if (*from == '{') {
		tokenType = stObjectStart;
	} else if (*from == '}') {
		tokenType = stObjectFinish;
	} else if (*from == '(') {
		tokenType = stConsStart;
	} else if (*from == ')') {
		tokenType = stConsFinish;
	} else if (*from == ',') {
		tokenType = stComma;
	} else if (*from == '/') {
		tokenType = stVariant;
	} else {
		throw Exception("Could not parse token!");
	}
	++from;
	return;
}

bool readPxAfterNumber(const char *&from, const char *end) {
	if (from + 2 <= end && *from == 'p' && *(from + 1) == 'x') {
		from += 2;
		return true;
	}
	return false;
}

typedef QMap<int, string> ScalarValue;
typedef QPair<ScalarType, ScalarValue> ScalarData;
typedef QPair<string, ScalarData> Scalar;
typedef QMap<string, ScalarData> Fields;
typedef QPair<string, Fields> ObjectData;
typedef QPair<string, ObjectData> Object; 
typedef QVector<Object> Objects;
typedef QVector<Scalar> Scalars;

string findScalarVariant(const ScalarValue &value, int variant) {
	ScalarValue::const_iterator i = value.constFind(variant);
	if (i != value.cend()) return i.value();

	return value[0];
	//string result;
	//for (ScalarValue::const_iterator i = value.cbegin(), e = value.cend(); i != e; ++i) {
	//	if (i.key() > variant) {
	//		break;
	//	}
	//	result = i.value();
	//}
	//return result;
}

Objects objects;
Scalars scalars;

ByName objectsMap;
ByName scalarsMap;

ScalarValue fillPrepareResult(int variant, const string &result) {
	ScalarValue r;
	r[variant] = result;
	if (!variant) {
		for (int i = 1; i < variantsCount; ++i) {
			r[variants[i]] = result;
		}
	}
	return r;
}

int adjustPx(int variant, int number, bool ispx) {
	if (!ispx || !variant) return number;

	switch (variant) {
	case 2: return qRound(number * 1.25 + (number > 0 ? -0.01 : 0.01));
	case 3: return qRound(number * 1.5 + (number > 0 ? -0.01 : 0.01));
	case 4: return number * 2;
	}
	return number;
}

string adjustPx(int variant, const string &number, bool ispx) {
	if (!variant || !ispx) return number;
	return QString::number(adjustPx(variant, QString(number.c_str()).toInt(), ispx)).toUtf8().constData();
}

ScalarValue prepareString(int variant, const string &token) {
	string result;
	result.reserve(token.length() * 2);
	result += "(qsl(\"";
	for (quint64 i = 0, l = token.length(); i < l; ++i) {
		if (token[i] == '\n') {
			result += "\\n";
		} else if (token[i] == '\r') {
			result += "\\r";
		} else if (token[i] == '\t') {
			result += "\\t";
		} else {
			if (token[i] == '\\' || token[i] == '"') {
				result += '\\';
			}
			result += token[i];
		}
	}
	result += "\"))";
	return fillPrepareResult(variant, result);

}

int hexDec(char a, char b) {
	int da = (a >= '0' && a <= '9') ? (a - '0') : (10 + a - 'A');
	int db = (b >= '0' && b <= '9') ? (b - '0') : (10 + b - 'A');
	return da * 16 + db;
}

typedef QMap<string, QPair<ScalarType, string> > Named;
QMap<int, Named> named;

struct Color {
	string color;
};

typedef QMap<string, Color> Colors;
QMap<int, Colors> colors;

ScalarValue prepareColor(int variant, const string &name, const string &token) {
	QString result;
	result.reserve(20);

	int r = hexDec(token[0], token[1]), g = hexDec(token[2], token[3]), b = hexDec(token[4], token[5]), a = hexDec(token[6], token[7]);
	if (a == 255) {
		Color c;
		c.color = QString("%1, %2, %3, 255").arg(r).arg(g).arg(b).toUtf8().constData();
		colors[variant][name] = c;
		if (!variant) {
			for (int i = 1; i < variantsCount; ++i) {
				colors[variants[i]][name] = c;
			}
		}
		return fillPrepareResult(variant, "(Qt::Uninitialized)");
	}
	Color c;
	c.color = QString("%1, %2, %3, %4").arg(r).arg(g).arg(b).arg(a).toUtf8().constData();
	colors[variant][name] = c;
	if (!variant) {
		for (int i = 1; i < variantsCount; ++i) {
			colors[variants[i]][name] = c;
		}
	}
	return fillPrepareResult(variant, "(Qt::Uninitialized)");
}

ScalarValue prepareNumber(int variant, const string &token, const char *&text, const char *end) {
	bool ispx = readPxAfterNumber(text, end);
	ScalarValue r;
	r[variant] = '(' + token + ')';
	if (!variant) {
		for (int i = 1; i < variantsCount; ++i) {
			r[variants[i]] = '(' + adjustPx(variants[i], token, ispx) + ')';
		}
	}
	return r;
}

ScalarValue prepareColorRGB(int variant, const string &name, const char *&text, const char *end) {
	StyleGenTokenType type;
	string token;
	
	readStyleGenToken(text, end, type, token);
	if (type != stConsStart) throw Exception(QString("Unexpected token %1 while reading rgb() cons!").arg(type));

	readStyleGenToken(text, end, type, token);
	if (type != stNumber) throw Exception(QString("Unexpected token %1 while reading rgb() cons!").arg(type));
	string r = token;

	readStyleGenToken(text, end, type, token);
	if (type != stComma) throw Exception(QString("Unexpected token %1 while reading rgb() cons!").arg(type));

	readStyleGenToken(text, end, type, token);
	if (type != stNumber) throw Exception(QString("Unexpected token %1 while reading rgb() cons!").arg(type));
	string g = token;

	readStyleGenToken(text, end, type, token);
	if (type != stComma) throw Exception(QString("Unexpected token %1 while reading rgb() cons!").arg(type));

	readStyleGenToken(text, end, type, token);
	if (type != stNumber) throw Exception(QString("Unexpected token %1 while reading rgb() cons!").arg(type));
	string b = token;

	readStyleGenToken(text, end, type, token);
	if (type != stConsFinish) throw Exception(QString("Unexpected token %1 while reading rgb() cons!").arg(type));

	Color c;
	c.color = QString("%1, %2, %3, 255").arg(r.c_str()).arg(g.c_str()).arg(b.c_str()).toUtf8().constData();
	colors[variant][name] = c;
	if (!variant) {
		for (int i = 1; i < variantsCount; ++i) {
			colors[variants[i]][name] = c;
		}
	}
	return fillPrepareResult(variant, "(Qt::Uninitialized)");
}

ScalarValue prepareColorRGBA(int variant, const string &name, const char *&text, const char *end) {
	StyleGenTokenType type;
	string token;
	
	readStyleGenToken(text, end, type, token);
	if (type != stConsStart) throw Exception(QString("Unexpected token %1 while reading rgba() cons!").arg(type));

	readStyleGenToken(text, end, type, token);
	if (type != stNumber) throw Exception(QString("Unexpected token %1 while reading rgba() cons!").arg(type));
	string r = token;

	readStyleGenToken(text, end, type, token);
	if (type != stComma) throw Exception(QString("Unexpected token %1 while reading rgba() cons!").arg(type));

	readStyleGenToken(text, end, type, token);
	if (type != stNumber) throw Exception(QString("Unexpected token %1 while reading rgba() cons!").arg(type));
	string g = token;

	readStyleGenToken(text, end, type, token);
	if (type != stComma) throw Exception(QString("Unexpected token %1 while reading rgba() cons!").arg(type));

	readStyleGenToken(text, end, type, token);
	if (type != stNumber) throw Exception(QString("Unexpected token %1 while reading rgba() cons!").arg(type));
	string b = token;

	readStyleGenToken(text, end, type, token);
	if (type != stComma) throw Exception(QString("Unexpected token %1 while reading rgba() cons!").arg(type));

	readStyleGenToken(text, end, type, token);
	if (type != stNumber) throw Exception(QString("Unexpected token %1 while reading rgba() cons!").arg(type));
	string a = token;

	readStyleGenToken(text, end, type, token);
	if (type != stConsFinish) throw Exception(QString("Unexpected token %1 while reading rgba() cons!").arg(type));

	Color c;
	c.color = QString("%1, %2, %3, %4").arg(r.c_str()).arg(g.c_str()).arg(b.c_str()).arg(a.c_str()).toUtf8().constData();
	colors[variant][name] = c;
	if (!variant) {
		for (int i = 1; i < variantsCount; ++i) {
			colors[variants[i]][name] = c;
		}
	}
	return fillPrepareResult(variant, "(Qt::Uninitialized)");
}

ScalarValue prepareRect(int variant, const char *&text, const char *end) {
	StyleGenTokenType type;
	string token;
	
	readStyleGenToken(text, end, type, token);
	if (type != stConsStart) throw Exception(QString("Unexpected token %1 while reading rect() cons!").arg(type));

	readStyleGenToken(text, end, type, token);
	if (type != stNumber) throw Exception(QString("Unexpected token %1 while reading rect() cons!").arg(type));
	string x = token;
	bool xpx = readPxAfterNumber(text, end);

	readStyleGenToken(text, end, type, token);
	if (type != stComma) throw Exception(QString("Unexpected token %1 while reading rect() cons!").arg(type));

	readStyleGenToken(text, end, type, token);
	if (type != stNumber) throw Exception(QString("Unexpected token %1 while reading rect() cons!").arg(type));
	string y = token;
	bool ypx = readPxAfterNumber(text, end);

	readStyleGenToken(text, end, type, token);
	if (type != stComma) throw Exception(QString("Unexpected token %1 while reading rect() cons!").arg(type));

	readStyleGenToken(text, end, type, token);
	if (type != stNumber) throw Exception(QString("Unexpected token %1 while reading rect() cons!").arg(type));
	string w = token;
	bool wpx = readPxAfterNumber(text, end);

	readStyleGenToken(text, end, type, token);
	if (type != stComma) throw Exception(QString("Unexpected token %1 while reading rect() cons!").arg(type));

	readStyleGenToken(text, end, type, token);
	if (type != stNumber) throw Exception(QString("Unexpected token %1 while reading rect() cons!").arg(type));
	string h = token;
	bool hpx = readPxAfterNumber(text, end);

	readStyleGenToken(text, end, type, token);
	if (type != stConsFinish) throw Exception(QString("Unexpected token %1 while reading rect() cons!").arg(type));

	ScalarValue r;
	r[variant] = QString("(%1, %2, %3, %4)").arg(x.c_str()).arg(y.c_str()).arg(w.c_str()).arg(h.c_str()).toUtf8().constData();
	if (!variant) {
		for (int i = 1; i < variantsCount; ++i) {
			r[variants[i]] = QString("(%1, %2, %3, %4)").arg(adjustPx(variants[i], x, xpx).c_str()).arg(adjustPx(variants[i], y, ypx).c_str()).arg(adjustPx(variants[i], w, wpx).c_str()).arg(adjustPx(variants[i], h, hpx).c_str()).toUtf8().constData();
		}
	}
	return r;
}

typedef QVector<QPair<QRect, QString> > SpriteRects;
SpriteRects sprites;

ScalarValue prepareSprite(int variant, const char *&text, const char *end) {
	StyleGenTokenType type;
	string token;

	if (variant) throw Exception(QString("Unexpected variant in sprite rectangle!"));

	readStyleGenToken(text, end, type, token);
	if (type != stConsStart) throw Exception(QString("Unexpected token %1 while reading sprite() cons!").arg(type));

	readStyleGenToken(text, end, type, token);
	if (type != stNumber) throw Exception(QString("Unexpected token %1 while reading sprite() cons!").arg(type));
	string x = token;
	if (!readPxAfterNumber(text, end)) throw Exception(QString("All number in sprite() cons must be in px!"));

	readStyleGenToken(text, end, type, token);
	if (type != stComma) throw Exception(QString("Unexpected token %1 while reading sprite() cons!").arg(type));

	readStyleGenToken(text, end, type, token);
	if (type != stNumber) throw Exception(QString("Unexpected token %1 while reading sprite() cons!").arg(type));
	string y = token;
	if (!readPxAfterNumber(text, end)) throw Exception(QString("All number in sprite() cons must be in px!"));

	readStyleGenToken(text, end, type, token);
	if (type != stComma) throw Exception(QString("Unexpected token %1 while reading sprite() cons!").arg(type));

	readStyleGenToken(text, end, type, token);
	if (type != stNumber) throw Exception(QString("Unexpected token %1 while reading sprite() cons!").arg(type));
	string w = token;
	if (!readPxAfterNumber(text, end)) throw Exception(QString("All number in sprite() cons must be in px!"));

	readStyleGenToken(text, end, type, token);
	if (type != stComma) throw Exception(QString("Unexpected token %1 while reading sprite() cons!").arg(type));

	readStyleGenToken(text, end, type, token);
	if (type != stNumber) throw Exception(QString("Unexpected token %1 while reading sprite() cons!").arg(type));
	string h = token;
	if (!readPxAfterNumber(text, end)) throw Exception(QString("All number in sprite() cons must be in px!"));

	readStyleGenToken(text, end, type, token);
	if (type != stConsFinish) throw Exception(QString("Unexpected token %1 while reading sprite() cons!").arg(type));

	ScalarValue r;
	r[variant] = QString("(%1, %2, %3, %4)").arg(x.c_str()).arg(y.c_str()).arg(w.c_str()).arg(h.c_str()).toUtf8().constData();
	if (!variant) {
		for (int i = 1; i < variantsCount; ++i) {
			r[variants[i]] = QString("(%1, %2, %3, %4)").arg(adjustPx(variants[i], x, true).c_str()).arg(adjustPx(variants[i], y, true).c_str()).arg(adjustPx(variants[i], w, true).c_str()).arg(adjustPx(variants[i], h, true).c_str()).toUtf8().constData();
		}
	}

	bool found = false;
	QRect sprite(QString(x.c_str()).toInt(), QString(y.c_str()).toInt(), QString(w.c_str()).toInt(), QString(h.c_str()).toInt());
	for (SpriteRects::const_iterator i = sprites.cbegin(), e = sprites.cend(); i != e; ++i) {
		if (i->first == sprite) {
			found = true;
			break;
		}
		if (i->first.intersects(sprite)) {
			cout << QString("Sprites intersection, %1 intersects with %2").arg(i->second).arg(r[variant].c_str()).toUtf8().constData() << "\n";
//			throw Exception(QString("Sprites intersection, %1 intersects with %2").arg(i->second).arg(r[variant].c_str()));
		}
	}
	if (!found) {
		sprites.push_back(QPair<QRect, QString>(sprite, QString(r[variant].c_str())));

		if (sprite.x() < 0 || sprite.y() < 0 || sprite.x() + sprite.width() > variantSprites[0].width() || sprite.y() + sprite.height() > variantSprites[0].height()) {
			throw Exception(QString("Bad sprite size %1").arg(r[variant].c_str()));
		}

		int varLast = variants[variantsCount - 1];
		QImage lastCopy = variantSprites[variantsCount - 1].copy(adjustPx(varLast, sprite.x(), true), adjustPx(varLast, sprite.y(), true), adjustPx(varLast, sprite.width(), true), adjustPx(varLast, sprite.height(), true));
		for (int i = 1; i < variantsCount - 1; ++i) {
			QPainter p(&variantSprites[i]);
			QPixmap copy = QPixmap::fromImage(lastCopy.scaled(adjustPx(variants[i], sprite.width(), true), adjustPx(variants[i], sprite.height(), true), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
			p.drawPixmap(QPoint(adjustPx(variants[i], sprite.x(), true), adjustPx(variants[i], sprite.y(), true)), copy);
		}

		for (int i = 0; i < variantsCount; ++i) {
			QPainter p(&variantGrids[i]);
			p.setBrush(Qt::NoBrush);
			p.setPen(QColor(0, 255, 255));
			p.drawRect(QRect(adjustPx(variants[i], sprite.x(), true), adjustPx(variants[i], sprite.y(), true), adjustPx(variants[i], sprite.width(), true) - 1, adjustPx(variants[i], sprite.height(), true) - 1));
		}
	}

	return r;
}

ScalarValue preparePoint(int variant, const char *&text, const char *end) {
	StyleGenTokenType type;
	string token;
	
	readStyleGenToken(text, end, type, token);
	if (type != stConsStart) throw Exception(QString("Unexpected token %1 while reading point() cons!").arg(type));

	readStyleGenToken(text, end, type, token);
	if (type != stNumber) throw Exception(QString("Unexpected token %1 while reading point() cons!").arg(type));
	string x = token;
	bool xpx = readPxAfterNumber(text, end);

	readStyleGenToken(text, end, type, token);
	if (type != stComma) throw Exception(QString("Unexpected token %1 while reading point() cons!").arg(type));

	readStyleGenToken(text, end, type, token);
	if (type != stNumber) throw Exception(QString("Unexpected token %1 while reading point() cons!").arg(type));
	string y = token;
	bool ypx = readPxAfterNumber(text, end);

	readStyleGenToken(text, end, type, token);
	if (type != stConsFinish) throw Exception(QString("Unexpected token %1 while reading point() cons!").arg(type));

	ScalarValue r;
	r[variant] = QString("(%1, %2)").arg(x.c_str()).arg(y.c_str()).toUtf8().constData();
	if (!variant) {
		for (int i = 1; i < variantsCount; ++i) {
			r[variants[i]] = QString("(%1, %2)").arg(adjustPx(variants[i], x, xpx).c_str()).arg(adjustPx(variants[i], y, ypx).c_str()).toUtf8().constData();
		}
	}
	return r;
}

ScalarValue prepareSize(int variant, const char *&text, const char *end) {
	StyleGenTokenType type;
	string token;
	
	readStyleGenToken(text, end, type, token);
	if (type != stConsStart) throw Exception(QString("Unexpected token %1 while reading size() cons!").arg(type));

	readStyleGenToken(text, end, type, token);
	if (type != stNumber) throw Exception(QString("Unexpected token %1 while reading size() cons!").arg(type));
	string x = token;
	bool xpx = readPxAfterNumber(text, end);

	readStyleGenToken(text, end, type, token);
	if (type != stComma) throw Exception(QString("Unexpected token %1 while reading size() cons!").arg(type));

	readStyleGenToken(text, end, type, token);
	if (type != stNumber) throw Exception(QString("Unexpected token %1 while reading size() cons!").arg(type));
	string y = token;
	bool ypx = readPxAfterNumber(text, end);

	readStyleGenToken(text, end, type, token);
	if (type != stConsFinish) throw Exception(QString("Unexpected token %1 while reading size() cons!").arg(type));

	ScalarValue r;
	r[variant] = QString("(%1, %2)").arg(x.c_str()).arg(y.c_str()).toUtf8().constData();
	if (!variant) {
		for (int i = 1; i < variantsCount; ++i) {
			r[variants[i]] = QString("(%1, %2)").arg(adjustPx(variants[i], x, xpx).c_str()).arg(adjustPx(variants[i], y, ypx).c_str()).toUtf8().constData();
		}
	}
	return r;
}

ScalarValue prepareTransition(int variant, const char *&text, const char *end) {
	StyleGenTokenType type;
	string token;
	
	readStyleGenToken(text, end, type, token);
	if (type != stConsStart) throw Exception(QString("Unexpected token %1 while reading transition() cons!").arg(type));

	readStyleGenToken(text, end, type, token);
	if (type != stName) throw Exception(QString("Unexpected token %1 while reading transition() cons!").arg(type));
	string func = token;

	readStyleGenToken(text, end, type, token);
	if (type != stConsFinish) throw Exception(QString("Unexpected token %1 while reading transition() cons!").arg(type));

	return fillPrepareResult(variant, QString("(anim::%1)").arg(func.c_str()).toUtf8().constData());
}

ScalarValue prepareCursor(int variant, const char *&text, const char *end) {
	StyleGenTokenType type;
	string token;
	
	readStyleGenToken(text, end, type, token);
	if (type != stConsStart) throw Exception(QString("Unexpected token %1 while reading cursor() cons!").arg(type));

	readStyleGenToken(text, end, type, token);
	if (type != stName) throw Exception(QString("Unexpected token %1 while reading cursor() cons!").arg(type));
	string func = token;

	readStyleGenToken(text, end, type, token);
	if (type != stConsFinish) throw Exception(QString("Unexpected token %1 while reading cursor() cons!").arg(type));

	return fillPrepareResult(variant, QString("(style::cur_%1)").arg(func.c_str()).toUtf8().constData());
}

ScalarValue prepareAlign(int variant, const char *&text, const char *end) {
	StyleGenTokenType type;
	string token;
	
	readStyleGenToken(text, end, type, token);
	if (type != stConsStart) throw Exception(QString("Unexpected token %1 while reading align() cons!").arg(type));

	readStyleGenToken(text, end, type, token);
	if (type != stName) throw Exception(QString("Unexpected token %1 while reading align() cons!").arg(type));
	string func = token;

	readStyleGenToken(text, end, type, token);
	if (type != stConsFinish) throw Exception(QString("Unexpected token %1 while reading align() cons!").arg(type));

	return fillPrepareResult(variant, QString("(style::al_%1)").arg(func.c_str()).toUtf8().constData());
}

ScalarValue prepareMargins(int variant, const char *&text, const char *end) {
	StyleGenTokenType type;
	string token;
	
	readStyleGenToken(text, end, type, token);
	if (type != stConsStart) throw Exception(QString("Unexpected token %1 while reading margins() cons!").arg(type));

	readStyleGenToken(text, end, type, token);
	if (type != stNumber) throw Exception(QString("Unexpected token %1 while reading margins() cons!").arg(type));
	string x = token;
	bool xpx = readPxAfterNumber(text, end);

	readStyleGenToken(text, end, type, token);
	if (type != stComma) throw Exception(QString("Unexpected token %1 while reading margins() cons!").arg(type));

	readStyleGenToken(text, end, type, token);
	if (type != stNumber) throw Exception(QString("Unexpected token %1 while reading margins() cons!").arg(type));
	string y = token;
	bool ypx = readPxAfterNumber(text, end);

	readStyleGenToken(text, end, type, token);
	if (type != stComma) throw Exception(QString("Unexpected token %1 while reading margins() cons!").arg(type));

	readStyleGenToken(text, end, type, token);
	if (type != stNumber) throw Exception(QString("Unexpected token %1 while reading margins() cons!").arg(type));
	string w = token;
	bool wpx = readPxAfterNumber(text, end);

	readStyleGenToken(text, end, type, token);
	if (type != stComma) throw Exception(QString("Unexpected token %1 while reading margins() cons!").arg(type));

	readStyleGenToken(text, end, type, token);
	if (type != stNumber) throw Exception(QString("Unexpected token %1 while reading margins() cons!").arg(type));
	string h = token;
	bool hpx = readPxAfterNumber(text, end);

	readStyleGenToken(text, end, type, token);
	if (type != stConsFinish) throw Exception(QString("Unexpected token %1 while reading margins() cons!").arg(type));

	ScalarValue r;
	r[variant] = QString("(%1, %2, %3, %4)").arg(x.c_str()).arg(y.c_str()).arg(w.c_str()).arg(h.c_str()).toUtf8().constData();
	if (!variant) {
		for (int i = 1; i < variantsCount; ++i) {
			r[variants[i]] = QString("(%1, %2, %3, %4)").arg(adjustPx(variants[i], x, xpx).c_str()).arg(adjustPx(variants[i], y, ypx).c_str()).arg(adjustPx(variants[i], w, wpx).c_str()).arg(adjustPx(variants[i], h, hpx).c_str()).toUtf8().constData();
		}
	}
	return r;
}

enum FontFlagBits {
	FontBoldBit,
	FontItalicBit,
	FontUnderlineBit,

	FontFlagsBits
};

enum FontFlags {
	FontBold = (1 << FontBoldBit),
	FontItalic = (1 << FontItalicBit),
	FontUnderline = (1 << FontUnderlineBit),

	FontDifferentFlags = (1 << FontFlagsBits)
};

struct Font {
	string family, size;
	int flags;
};

typedef QMap<string, Font> Fonts;
QMap<int, Fonts> fonts;

ScalarValue prepareFont(int variant, const string &name, const char *&text, const char *end) {
	StyleGenTokenType type;
	string token;
	
	ScalarValue sizeScalar, familyScalar;

	string size, family;
	int flags = 0;
	bool sizepx;

	readStyleGenToken(text, end, type, token);
	if (type != stConsStart) throw Exception(QString("Unexpected token %1 (%2) while reading font() cons!").arg(type).arg(token.c_str()));

	do {
		readStyleGenToken(text, end, type, token);
		if (type == stNumber) {
			if (size.empty() && sizeScalar.isEmpty()) {
				size = token;
				sizepx = readPxAfterNumber(text, end);
			} else {
				throw Exception(QString("Unexpected second number %1 while reading font() cons!").arg(token.c_str()));
			}
		} else if (type == stName) {
			int bit = 0;
			if (token == "bold") {
				bit = FontBold;
			} else if (token == "italic") {
				bit = FontItalic;
			} else if (token == "underline") {
				bit = FontUnderline;
			} else {
				ByName::const_iterator j = scalarsMap.constFind(token);
				if (j != scalarsMap.cend()) {
					if (scalars[j.value()].second.first == scNumber) {
						if (size.empty() && sizeScalar.isEmpty()) {
							sizeScalar = scalars[j.value()].second.second;
//							size = findScalarVariant(scalars[j.value()].second.second, variant);
						} else {
							throw Exception(QString("Unexpected second number %1 while reading font() cons!").arg(token.c_str()));
						}
					} else if (scalars[j.value()].second.first == scString) {
						if (scalars[j.value()].second.second.empty()) {
							throw Exception(QString("Unexpected empty string %1 while reading font() cons!").arg(token.c_str()));
						} else if (!family.empty() || !familyScalar.empty()) {
							throw Exception(QString("Unexpected second string %1 while reading font() cons!").arg(token.c_str()));
						}
						familyScalar = scalars[j.value()].second.second;
//						family = findScalarVariant(scalars[j.value()].second.second, variant);
					} else {
						throw Exception(QString("Unexpected name token %1 type %2 while reading font() cons!").arg(token.c_str()).arg(scalars[j.value()].second.first));
					}
				} else {
					throw Exception(QString("Unexpected name token %1 while reading font() cons!").arg(token.c_str()));
				}
			}
			if (flags & bit) {
				throw Exception(QString("Unexpected second time token %1 while reading font() cons!").arg(token.c_str()));
			}
			flags |= bit;
		} else if (type == stString) {
			if (token.empty()) {
				throw Exception(QString("Unexpected empty string while reading font() cons!"));
			} else if (!family.empty() || !familyScalar.empty()) {
				throw Exception(QString("Unexpected second string %1 while reading font() cons!").arg(token.c_str()));
			}
			family = token;
		} else if (type == stConsFinish) {
			break;
		} else {
			throw Exception(QString("Unexpected token %1 while reading font() cons!").arg(type));
		}
	} while (true);

	if (family.empty() && familyScalar.isEmpty()) {
		ByName::const_iterator j = scalarsMap.constFind("defaultFontFamily");
		if (j != scalarsMap.cend()) {
			if (scalars[j.value()].second.first == scString) {
				if (scalars[j.value()].second.second.empty()) {
					throw Exception(QString("Unexpected empty string %1 while reading font() cons!").arg(token.c_str()));
				} else if (!family.empty() || !familyScalar.isEmpty()) {
					throw Exception(QString("Unexpected second string %1 while reading font() cons!").arg(token.c_str()));
				}
//				family = findScalarVariant(scalars[j.value()].second.second, variant);
				familyScalar = scalars[j.value()].second.second;
			} else {
				throw Exception(QString("Font family not found while reading font() cons!"));
			}
		} else {
			throw Exception(QString("Font family not found while reading font() cons!"));
		}
	}
	if (size.empty() && sizeScalar.isEmpty()) throw Exception(QString("Font size not found while reading font() cons!"));

	Font font;
	font.family = familyScalar.empty() ? family : findScalarVariant(familyScalar, variant);
	font.size = sizeScalar.empty() ? size : findScalarVariant(sizeScalar, variant);
	font.flags = flags;
	fonts[variant][name] = font;
	if (!variant) {
		for (int i = 1; i < variantsCount; ++i) {
			Font varFont = font;
			if (!familyScalar.empty()) varFont.family = findScalarVariant(familyScalar, variants[i]);
			varFont.size = sizeScalar.empty() ? adjustPx(variants[i], size, sizepx) : findScalarVariant(sizeScalar, variants[i]);
			fonts[variants[i]][name] = varFont;
		}
	}

	return fillPrepareResult(variant, "(Qt::Uninitialized)");
}

ScalarData readScalarElement(string name, const char *&text, const char *end, string objName, const Fields *objFields, int variant) {
	string fullName = objFields ? (objName + '.' + name) : name;
	ScalarData result;
	StyleGenTokenType type;
	string token;
	readStyleGenToken(text, end, type, token);
	if (type == stString) {
		result.first = scString;
		result.second = prepareString(variant, token);
	} else if (type == stNumber) {
		result.first = scNumber;
		result.second = prepareNumber(variant, token, text, end);
	} else if (type == stColor) {
		result.first = scColor;
		result.second = prepareColor(variant, fullName, token);
	} else if (type == stName) {
		if (token == "rgb") {
			result.first = scColor;
			result.second = prepareColorRGB(variant, fullName, text, end);
		} else if (token == "rgba") {
			result.first = scColor;
			result.second = prepareColorRGBA(variant, fullName, text, end);
		} else if (token == "rect") {
			result.first = scRect;
			result.second = prepareRect(variant, text, end);
		} else if (token == "sprite") {
			result.first = scSprite;
			result.second = prepareSprite(variant, text, end);
		} else if (token == "point") {
			result.first = scPoint;
			result.second = preparePoint(variant, text, end);
		} else if (token == "size") {
			result.first = scSize;
			result.second = preparePoint(variant, text, end);
		} else if (token == "transition") {
			result.first = scTransition;
			result.second = prepareTransition(variant, text, end);
		} else if (token == "cursor") {
			result.first = scCursor;
			result.second = prepareCursor(variant, text, end);
		} else if (token == "align") {
			result.first = scAlign;
			result.second = prepareAlign(variant, text, end);
		} else if (token == "margins") {
			result.first = scMargins;
			result.second = prepareMargins(variant, text, end);
		} else if (token == "font") {
			result.first = scFont;
			result.second = prepareFont(variant, fullName, text, end);
		} else {
			bool found = false;
			if (objFields) {
				//Fields::const_iterator j = objFields->constFind(token);
				//if (j != objFields->cend()) {
				//	found = true;
				//	result.second = j.value();
				//}
			}
			if (!found) {
				ByName::const_iterator j = scalarsMap.constFind(token);
				if (j != scalarsMap.cend()) {
					found = true;
					result.first = scalars[j.value()].second.first;
					result.second = scalars[j.value()].second.second;
					if (result.first == scFont) {
						named[variant][fullName] = QPair<ScalarType, string>(result.first, token);
						if (!variant) {
							for (int i = 1; i < variantsCount; ++i) {
								named[variants[i]][fullName] = QPair<ScalarType, string>(result.first, token);
							}
						}
					} else if (result.first == scColor) {
						named[variant][fullName] = QPair<ScalarType, string>(result.first, token);
						if (!variant) {
							for (int i = 1; i < variantsCount; ++i) {
								named[variants[i]][fullName] = QPair<ScalarType, string>(result.first, token);
							}
						}
					}
				}
			}
			if (!found) {
				result.first = scTypesCount;
				result.second = fillPrepareResult(variant, token);
			}
		}
	} else {
		throw Exception(QString("Unexpected token after '%1:', type %2").arg(name.c_str()).arg(type));
	}
	return result;
}


Scalar readScalarData(StyleGenTokenType &type, string &token, const char *&text, const char *end, string objName = string(), const Fields *objFields = 0) {
	if (type != stName) {
		throw Exception(QString("Unexpected token, type %1: %2").arg(type).arg(token.c_str()));
	}

	string name = token;
	if (!objFields) {
		ByName::const_iterator i = objectsMap.constFind(name);
		if (i != objectsMap.cend()) throw Exception(QString("Redefinition of style object %1").arg(name.c_str()));

		ByName::const_iterator j = scalarsMap.constFind(name);
		if (j != scalarsMap.cend()) throw Exception(QString("Redefinition of style scalar %1").arg(name.c_str()));
	}

	readStyleGenToken(text, end, type, token);
	if (type != stDelimeter) {
		throw Exception(QString("Unexpected token, type %1: %2").arg(type).arg(token.c_str()));
	}

	string fullName = objFields ? (objName + '.' + name) : name;
	Scalar result;
	result.first = name;
	result.second = readScalarElement(name, text, end, objName, objFields, 0);

	readStyleGenToken(text, end, type, token);
	while (type == stVariant) {
		readStyleGenToken(text, end, type, token);
		if (type != stNumber) {
			throw Exception(QString("Unexpected token '%1' reading variants of '%2' scalar").arg(token.c_str()).arg(name.c_str()));
		}
		int variant = QString(token.c_str()).toInt();
		if (variant != 2 && variant != 3 && variant != 4) {
			throw Exception(QString("Unexpected variant index '%1' in '%2' scalar").arg(token.c_str()).arg(name.c_str()));
		}
		readStyleGenToken(text, end, type, token);
		if (type != stDelimeter) {
			throw Exception(QString("Unexpected token '%1' reading variants of '%2' scalar, expected delimeter").arg(token.c_str()).arg(name.c_str()));
		}
		ScalarData el = readScalarElement(name, text, end, objName, objFields, variant);
		if (el.first != result.second.first) {
			throw Exception(QString("Type changed in variant for '%1'").arg(name.c_str()));
		}
		result.second.second.insert(variant, el.second[variant]);

		readStyleGenToken(text, end, type, token);
	}
	return result;
}

string prepareObject(const string &cls, Fields fields, const string &obj, int variant) {
	string result = "(";
	Classes::const_iterator i = classes.constFind(cls);
	if (i == classes.cend()) throw Exception("Unknown error :(");

	for (FieldTypesMap::const_iterator j = i.value().fields.cbegin(), e = i.value().fields.cend(); j != e;) {
		result += "style::" + outputTypeNames[j.value()];

		Fields::iterator f = fields.find(j.key());
		if (f == fields.end()) {
			result += "()";
		} else if (f.value().first != j.value()) {
			throw Exception(QString("Bad type of field %1 while parsing %2").arg(j.key().c_str()).arg(obj.c_str()));
		} else {
            if (variant == -1) { // retina
                result += findScalarVariant(f.value().second, (j.value() == scSprite) ? 4 : 0);
            } else {
                result += findScalarVariant(f.value().second, variant);
            }
		}
		fields.erase(f);
		if (++j != e) {
			result += ", ";
		}
	}

	if (fields.size()) {
		throw Exception(QString("Unknown fields found in %1, for example %2").arg(obj.c_str()).arg(fields.begin().key().c_str()));
	}

	return result + ", Qt::Uninitialized)";
}

bool genStyles(const QString &classes_in, const QString &classes_out, const QString &styles_in, const QString &styles_out, const QString &path_to_sprites) {
	if (!genClasses(classes_in, classes_out)) return false;

	QString styles_cpp = QString(styles_out).replace(".h", ".cpp");
	if (styles_cpp == styles_out) {
		cout << "Bad output file name '" << styles_out.toUtf8().constData() << "'!\n";
		QCoreApplication::exit(1);
		return false;
	}

	QFile f(styles_in);
	if (!f.open(QIODevice::ReadOnly)) {
		cout << "Could not open styles input file '" << styles_in.toUtf8().constData() << "'!\n";
		QCoreApplication::exit(1);
		return false;
	}

	QImage sprites[variantsCount];
	variantSprites = sprites;

	QString sprite0(path_to_sprites + "sprite" + QString(variantPostfixes[0]) + ".png"), spriteLast(path_to_sprites + "sprite" + QString(variantPostfixes[variantsCount - 1]) + ".png");
	variantSprites[0] = QImage(sprite0);
	for (int i = 1; i < variantsCount - 1; ++i) {
		variantSprites[i] = QImage(adjustPx(variants[i], variantSprites[0].width(), true), adjustPx(variants[i], variantSprites[0].height(), true), QImage::Format_ARGB32_Premultiplied);
		QPainter p(&variantSprites[i]);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.fillRect(0, 0, variantSprites[i].width(), variantSprites[i].height(), Qt::transparent);
	}
	variantSprites[variantsCount - 1] = QImage(spriteLast);

	QPixmap spriteMaxPix = QPixmap::fromImage(variantSprites[variantsCount - 1]);
	spriteMax = &spriteMaxPix;

	if (!variantSprites[0].width() || !variantSprites[0].height()) {
		cout << "Could not open input sprite file '" << sprite0.toUtf8().constData() << "'!\n";
		QCoreApplication::exit(1);
		return false;
	}
	if (!variantSprites[variantsCount - 1].width() || !variantSprites[variantsCount - 1].height()) {
		cout << "Could not open input sprite file '" << spriteLast.toUtf8().constData() << "'!\n";
		QCoreApplication::exit(1);
		return false;
	}
	if (adjustPx(variants[variantsCount - 1], variantSprites[0].width(), true) != variantSprites[variantsCount - 1].width()) {
		cout << "Bad sprite file width '" << spriteLast.toUtf8().constData() << "'!\n";
		QCoreApplication::exit(1);
		return false;
	}
	if (adjustPx(variants[variantsCount - 1], variantSprites[0].height(), true) != variantSprites[variantsCount - 1].height()) {
		cout << "Bad sprite file height '" << spriteLast.toUtf8().constData() << "'!\n";
		QCoreApplication::exit(1);
		return false;
	}

	QImage grids[variantsCount];
	variantGrids = grids;
	for (int i = 0; i < variantsCount; ++i) {
		variantGrids[i] = QImage(variantSprites[i].width(), variantSprites[i].height(), QImage::Format_ARGB32_Premultiplied);
		QPainter p(&variantGrids[i]);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.fillRect(0, 0, variantSprites[i].width(), variantSprites[i].height(), Qt::transparent);
	}

	QByteArray blob = f.readAll();
	const char *text = blob.constData(), *end = blob.constData() + blob.size();
	QMap<string, int> byName;
	QVector<ClassData> byIndex;
	string token;
	StyleGenTokenType type;
	try {
		while (true) {
			try {
				readStyleGenToken(text, end, type, token);
			} catch (exception &e) {
				if (e.what() != string("Unexpected end of file!")) {
					throw;
				}
				break;
			}
			string name = token;
			Scalar scalar = readScalarData(type, token, text, end);
			if (scalar.second.first != scTypesCount) {
				scalarsMap.insert(scalar.first, scalars.size());
				scalars.push_back(scalar);
				if (type != stFieldFinish) throw Exception(QString("Unexpected token after scalar %1, type %2").arg(name.c_str()).arg(type));
				continue;
			}

			string objType = scalar.second.second[0];

			Object obj;
			obj.first = name;
			obj.second.first = objType;

			Classes::const_iterator c = classes.constFind(objType);
			if (c == classes.cend()) throw Exception(QString("Unknown type %1 used for object %2").arg(objType.c_str()).arg(name.c_str()));
			if (type == stConsStart) {
				do {
					readStyleGenToken(text, end, type, token);
					string parent = token;
					if (type != stName) throw Exception(QString("Unexpected token %1 while parsing object %2").arg(type).arg(name.c_str()));

					ByName::const_iterator p = objectsMap.constFind(parent);
					if (p == objectsMap.cend()) throw Exception(QString("Parent object %1 not found, while parsing object %2").arg(parent.c_str()).arg(name.c_str()));

					const ObjectData &alr(objects[p.value()].second);
					for (Fields::const_iterator f = alr.second.cbegin(), e = alr.second.cend(); f != e; ++f) {
//						Fields::const_iterator a = obj.second.second.constFind(f.key());
//						if (a == obj.second.second.cend()) {
							obj.second.second.insert(f.key(), f.value());
							if (f.value().first == scFont) {
								for (int v = 0; v < variantsCount; ++v) {
									named[variants[v]][name + '.' + f.key()] = QPair<ScalarType, string>(f.value().first, parent + '.' + f.key());
								}
							} else if (f.value().first == scColor) {
								for (int v = 0; v < variantsCount; ++v) {
									named[variants[v]][name + '.' + f.key()] = QPair<ScalarType, string>(f.value().first, parent + '.' + f.key());
								}
							}
//						}
					}

					readStyleGenToken(text, end, type, token);
					if (type == stConsFinish) break;
					if (type != stComma) throw Exception(QString("Unexpected token %1, expected , or ) while parsing object %2").arg(type).arg(name.c_str()));
				} while (true);
				readStyleGenToken(text, end, type, token);
			}
			if (type != stObjectStart) throw Exception(QString("Unexpected token %1, expected { while parsing object %2").arg(type).arg(name.c_str()));

			while (true) {
				readStyleGenToken(text, end, type, token);
				if (type == stObjectFinish) {
					objectsMap.insert(name, objects.size());
					objects.push_back(obj);
					break;
				}

				for (int v = 0; v < variantsCount; ++v) {
					named[variants[v]].remove(name + '.' + token);
				}

				Scalar scalar = readScalarData(type, token, text, end, name, &obj.second.second);
				if (scalar.second.first == scTypesCount) throw Exception(QString("Unexpected type name %1 while parsing object %2").arg(scalar.second.second[0].c_str()).arg(name.c_str()));

				obj.second.second.insert(scalar.first, scalar.second);

				if (type != stFieldFinish) throw Exception(QString("Unexpected token after scalar %1 in object %2, type %3").arg(scalar.first.c_str()).arg(name.c_str()).arg(type));
			}
		}

		QByteArray outText, cppText;
		{
			int variant = 0;

			QTextStream tout(&outText), tcpp(&cppText);
			tout << "\
/*\n\
Created from \'/Resources/style.txt\' by \'/MetaStyle\' project\n\
\n\
WARNING! All changes made in this file will be lost!\n\
\n\
This file is part of Telegram Desktop,\n\
an unofficial desktop messaging app, see https://telegram.org\n\
\n\
Telegram Desktop is free software: you can redistribute it and/or modify\n\
it under the terms of the GNU General Public License as published by\n\
the Free Software Foundation, either version 3 of the License, or\n\
(at your option) any later version.\n\
\n\
It is distributed in the hope that it will be useful,\n\
but WITHOUT ANY WARRANTY; without even the implied warranty of\n\
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the\n\
GNU General Public License for more details.\n\
\n\
Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE\n\
Copyright (c) 2014 John Preston, https://tdesktop.com\n\
*/\n";
			tout << "#pragma once\n\n#include \"style.h\"\n\nnamespace st {\n";
			tcpp << "\
/*\n\
Created from \'/Resources/style.txt\' by \'/MetaStyle\' project\n\
\n\
WARNING! All changes made in this file will be lost!\n\
\n\
This file is part of Telegram Desktop,\n\
an unofficial desktop messaging app, see https://telegram.org\n\
\n\
Telegram Desktop is free software: you can redistribute it and/or modify\n\
it under the terms of the GNU General Public License as published by\n\
the Free Software Foundation, either version 3 of the License, or\n\
(at your option) any later version.\n\
\n\
It is distributed in the hope that it will be useful,\n\
but WITHOUT ANY WARRANTY; without even the implied warranty of\n\
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the\n\
GNU General Public License for more details.\n\
\n\
Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE\n\
Copyright (c) 2014 John Preston, https://tdesktop.com\n\
*/\n";
			tcpp << "#include \"stdafx.h\"\n#include \"style_auto.h\"\n\nnamespace {\n";
			for (int i = 0, l = scalars.size(); i < l; ++i) {
				Scalar &sc(scalars[i]);
				tout << "\textern const style::" << outputTypeNames[sc.second.first].c_str() << " &" << sc.first.c_str() << ";\n";
				tcpp << "\tstyle::" << outputTypeNames[sc.second.first].c_str() << " _" << sc.first.c_str() << findScalarVariant(sc.second.second, variant).c_str() << ";\n";
			}
			tout << "\n";
			tcpp << "\n";
			for (int i = 0, l = objects.size(); i < l; ++i) {
				Object &obj(objects[i]);
				tout << "\textern const style::" << obj.second.first.c_str() << " &" << obj.first.c_str() << ";\n";
				tcpp << "\tstyle::" << obj.second.first.c_str() << " _" << obj.first.c_str() << prepareObject(obj.second.first, obj.second.second, obj.first, variant).c_str() << ";\n";
			}
			tout << "};\n";
            tcpp << "};\n";

			tcpp << "\nnamespace st {\n";
			for (int i = 0, l = scalars.size(); i < l; ++i) {
				Scalar &sc(scalars[i]);
				tcpp << "\tconst style::" << outputTypeNames[sc.second.first].c_str() << " &" << sc.first.c_str() << "(_" << sc.first.c_str() << ");\n";
			}
			tcpp << "\n";
			for (int i = 0, l = objects.size(); i < l; ++i) {
				Object &obj(objects[i]);
				tcpp << "\tconst style::" << obj.second.first.c_str() << " &" << obj.first.c_str() << "(_" << obj.first.c_str() << ");\n";
			}
			tcpp << "};\n";

			tcpp << "\nnamespace style {\n\n";
			tcpp << "\tFontFamilies _fontFamilies;\n";
			tcpp << "\tFontDatas _fontsMap;\n";
			tcpp << "\tColorDatas _colorsMap;\n\n";
			tcpp << "\tvoid startManager() {\n";
            
            tcpp << "\n\t\tif (cRetina()) {\n";
            tcpp << "\t\t\tcSetRealScale(dbisOne);\n\n";
            for (int i = 0, l = scalars.size(); i < l; ++i) {
                Scalar &sc(scalars[i]);
                if (sc.second.first == scSprite || sc.first == "spriteFile" || sc.first == "emojisFile" || sc.first == "emojiImgSize") {
                    string v = findScalarVariant(sc.second.second, 4);
                    if (v != findScalarVariant(sc.second.second, 0)) {
                        tcpp << "\t\t\t_" << sc.first.c_str() << " = style::" << outputTypeNames[sc.second.first].c_str() << v.c_str() << ";\n";
                    }
                }
            }
            for (int i = 0, l = objects.size(); i < l; ++i) {
                Object &obj(objects[i]);
                string v = prepareObject(obj.second.first, obj.second.second, obj.first, -1); // retina
                if (v != prepareObject(obj.second.first, obj.second.second, obj.first, 0)) {
                    tcpp << "\t\t\t_" << obj.first.c_str() << " = style::" << obj.second.first.c_str() << v.c_str() << ";\n";
                }
            }
            tcpp << "\t\t} else switch (cScale()) {\n\n";
			for (int i = 1; i < variantsCount; ++i) {
				variant = variants[i];
				const char *varName = variantNames[i];

				tcpp << "\t\tcase " << varName << ":\n";

				typedef QMap<string, int> FontFamilies;
				FontFamilies fontFamilies;

				for (int i = 0, l = scalars.size(); i < l; ++i) {
					Scalar &sc(scalars[i]);
					string v = findScalarVariant(sc.second.second, variant);
					if (v != findScalarVariant(sc.second.second, 0)) {
						tcpp << "\t\t\t_" << sc.first.c_str() << " = style::" << outputTypeNames[sc.second.first].c_str() << v.c_str() << ";\n";
					}
				}

				for (int i = 0, l = objects.size(); i < l; ++i) {
					Object &obj(objects[i]);
					string v = prepareObject(obj.second.first, obj.second.second, obj.first, variant);
					if (v != prepareObject(obj.second.first, obj.second.second, obj.first, 0)) {
						tcpp << "\t\t\t_" << obj.first.c_str() << " = style::" << obj.second.first.c_str() << v.c_str() << ";\n";
					}
				}
				tcpp << "\t\tbreak;\n\n";
			}
			tcpp << "\t\t}\n\n";

			Colors &clrs(colors[variant]);
			for (Colors::const_iterator i = clrs.cbegin(), e = clrs.cend(); i != e; ++i) {
				bool differ = false;
				for (int j = 1; j < variantsCount; ++j) {
					const Colors &otherClrs(colors[variants[j]]);
					Colors::const_iterator k = otherClrs.constFind(i.key());
					if (k == otherClrs.cend() || k.value().color != i.value().color) {
						differ = true;
						break;
					}
				}
				if (!differ) {
					tcpp << "\t\t_" << i.key().c_str() << ".init(" << i.value().color.c_str() << ");\n";
				}
			}

			for (int i = 0; i < variantsCount; ++i) {
				variant = variants[i];
				Named &nmd(named[variant]);
				while (true) {
					bool found = false;
					for (Named::iterator i = nmd.begin(), e = nmd.end(); i != e; ++i) {
						if (i.key() == i.value().second) {
							throw Exception(QString("Object '%1' is equal to itself!").arg(i.key().c_str()));
						}
						Named::const_iterator j = nmd.constFind(i.value().second);
						if (j != nmd.cend()) {
							*i = *j;
							found = true;
						}
					}
					if (!found) break;
				}
			}

			tcpp << "\n\t\tswitch (cScale()) {\n\n";
			for (int i = 0; i < variantsCount; ++i) {
				variant = variants[i];
				const char *varName = variantNames[i];

				tcpp << "\t\tcase " << varName << ":\n";

				typedef QMap<string, int> FontFamilies;
				FontFamilies fontFamilies;
				int familyIndex = 0;
				ByName::const_iterator j = scalarsMap.constFind("defaultFontFamily");
				if (j != scalarsMap.cend()) {
					if (scalars[j.value()].second.first == scString) {
						if (scalars[j.value()].second.second.empty()) {
							throw Exception(QString("Unexpected empty string in defaultFontFamily!").arg(token.c_str()));
						}
						string v = findScalarVariant(scalars[j.value()].second.second, variant);
						tcpp << "\t\t\t_fontFamilies.push_back" << v.c_str() << ";\n";
						fontFamilies.insert(v, familyIndex++);
					} else {
						throw Exception(QString("defaultFontFamily has bad type!"));
					}
				} else {
					throw Exception(QString("defaultFontFamily not found!"));
				}

				Fonts &fnts(fonts[variant]);
				for (Fonts::const_iterator i = fnts.cbegin(), e = fnts.cend(); i != e; ++i) {
					FontFamilies::const_iterator j = fontFamilies.constFind(i.value().family);
					if (j == fontFamilies.cend()) {
						tcpp << "\n\t\t\t_fontFamilies.push_back" << i.value().family.c_str() << ";\n";
						j = fontFamilies.insert(i.value().family, familyIndex++);
					}
					tcpp << "\t\t\t_" << i.key().c_str() << ".init(" << i.value().size.c_str() << ", " << i.value().flags << ", " << j.value() << ", 0);\n";
				}

				Colors &clrs(colors[variant]);
				if (!clrs.empty()) tcpp << "\n";
				for (Colors::const_iterator i = clrs.cbegin(), e = clrs.cend(); i != e; ++i) {
					bool differ = false;
					for (int j = 0; j < variantsCount; ++j) {
						if (variant == variants[j]) continue;

						const Colors &otherClrs(colors[variants[j]]);
						Colors::const_iterator k = otherClrs.constFind(i.key());
						if (k == otherClrs.cend() || k.value().color != i.value().color) {
							differ = true;
							break;
						}
					}
					if (differ) {
						tcpp << "\t\t\t_" << i.key().c_str() << ".init(" << i.value().color.c_str() << ");\n";
					}
				}

				Named &nmd(named[variant]);
				for (Named::const_iterator i = nmd.cbegin(), e = nmd.cend(); i != e; ++i) {
					bool differ = false;
					for (int j = 0; j < variantsCount; ++j) {
						if (variant == variants[j]) continue;

						const Named &otherNmd(named[variants[j]]);
						Named::const_iterator k = otherNmd.constFind(i.key());
						if (k == otherNmd.cend() || k.value().second != i.value().second) {
							differ = true;
							break;
						}
					}
					if (differ) {
						tcpp << "\t\t\t_" << i.key().c_str() << " = _" << i.value().second.c_str() << ";\n";
					}
				}
				tcpp << "\t\tbreak;\n\n";
			}
			tcpp << "\t\t}\n\n";

			variant = 0;
			Named &nmd(named[variant]);
			for (Named::const_iterator i = nmd.cbegin(), e = nmd.cend(); i != e; ++i) {
				bool differ = false;
				for (int j = 1; j < variantsCount; ++j) {
					const Named &otherNmd(named[variants[j]]);
					Named::const_iterator k = otherNmd.constFind(i.key());
					if (k == otherNmd.cend() || k.value().second != i.value().second) {
						differ = true;
						break;
					}
				}
				if (!differ) {
					tcpp << "\t\t_" << i.key().c_str() << " = _" << i.value().second.c_str() << ";\n";
				}
			}

			tcpp << "\t}\n";
			tcpp << "\n};\n";
		}

		for (int i = 1; i < variantsCount - 1; ++i) {
			QString spritei(path_to_sprites + "sprite" + QString(variantPostfixes[i]) + ".png"), spriteLast(path_to_sprites + "sprite" + QString(variantPostfixes[i]) + ".png");
			QByteArray sprite;
			{
				QBuffer sbuf(&sprite);
				if (!variantSprites[i].save(&sbuf, "PNG")) {
					throw Exception(("Could not write intermediate sprite '" + spritei + "'!"));
				}
			}
			bool needResave = !QFileInfo(spritei).exists();
			if (!needResave) {
				QFile sf(spritei);
				if (!sf.open(QIODevice::ReadOnly)) {
					needResave = true;
				} else {
					QByteArray already(sf.readAll());
					if (already.size() != sprite.size() || memcmp(already.constData(), sprite.constData(), already.size())) {
						needResave = true;
					}
				}
			}
			if (needResave) {
				QFile sf(spritei);
				if (!sf.open(QIODevice::WriteOnly)) {
					throw Exception(("Could not write intermediate sprite '" + spritei + "'!"));
				} else {
					if (sf.write(sprite) != sprite.size()) {
						throw Exception(("Could not write intermediate sprite '" + spritei + "'!"));
					}
				}
			}
		}
		for (int i = 0; i < variantsCount; ++i) {
			QString spritei(path_to_sprites + "grid" + QString(variantPostfixes[i]) + ".png"), spriteLast(path_to_sprites + "sprite" + QString(variantPostfixes[i]) + ".png");
			QByteArray grid;
			{
				QBuffer gbuf(&grid);
				if (!variantGrids[i].save(&gbuf, "PNG")) {
					throw Exception(("Could not write intermediate grid '" + spritei + "'!"));
				}
			}
			bool needResave = !QFileInfo(spritei).exists();
			if (!needResave) {
				QFile gf(spritei);
				if (!gf.open(QIODevice::ReadOnly)) {
					needResave = true;
				} else {
					QByteArray already(gf.readAll());
					if (already.size() != grid.size() || memcmp(already.constData(), grid.constData(), already.size())) {
						needResave = true;
					}
				}
			}
			if (needResave) {
				QFile gf(spritei);
				if (!gf.open(QIODevice::WriteOnly)) {
					throw Exception(("Could not write intermediate grid '" + spritei + "'!"));
				} else {
					if (gf.write(grid) != grid.size()) {
						throw Exception(("Could not write intermediate grid '" + spritei + "'!"));
					}
				}
			}
		}

		QFile out(styles_out), cpp(styles_cpp);
		bool write_out = true;
		if (out.open(QIODevice::ReadOnly)) {
			QByteArray wasOut = out.readAll();
			if (wasOut.size() == outText.size()) {
				if (!memcmp(wasOut.constData(), outText.constData(), outText.size())) {
					write_out = false;
				}
			}
			out.close();
		}
		if (write_out) {
			cout << "Style compiled, writing " << scalars.size() << " scalars and " << objects.size() << " objects.\n";
			if (!out.open(QIODevice::WriteOnly)) throw Exception("Could not open style_auto.h for writing!");
			if (out.write(outText) != outText.size()) throw Exception("Could not open style_auto.h for writing!");
		}
		bool write_cpp = true;
		if (cpp.open(QIODevice::ReadOnly)) {
			QByteArray wasCpp = cpp.readAll();
			if (wasCpp.size() == cppText.size()) {
				if (!memcmp(wasCpp.constData(), cppText.constData(), cppText.size())) {
					write_cpp = false;
				}
			}
			cpp.close();
		}
		if (write_cpp) {
			if (!write_out) cout << "Style updated, writing " << scalars.size() << " scalars and " << objects.size() << " objects.\n";
			if (!cpp.open(QIODevice::WriteOnly)) throw Exception("Could not open style_auto.cpp for writing!");
			if (cpp.write(cppText) != cppText.size()) throw Exception("Could not open style_auto.cpp for writing!");
		}
	} catch (exception &e) {
		cout << e.what() << "\n";
		QCoreApplication::exit(1);
		return false;
	}
	return true;
}
