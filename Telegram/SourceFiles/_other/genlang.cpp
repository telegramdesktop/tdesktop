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
#include "genlang.h"

#ifdef Q_OS_WIN
#include <QtCore/QtPlugin>
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
#endif

typedef unsigned int uint32;

QString layoutDirection;
typedef QMap<QString, QString> LangKeys;
LangKeys keys;
typedef QVector<QString> KeysOrder;
KeysOrder keysOrder;

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
            if (from < end) ++from;
			return true;
		} else {
			return true;
		}
	}
	return true;
}

bool skipJunk(const char *&from, const char *end) {
	const char *start;
	do {
		start = from;
		if (!skipWhitespaces(from, end)) return false;
		if (!skipComment(from, end)) throw Exception("Unexpected end of comment!");
	} while (start != from);
	return true;
}

void readKeyValue(const char *&from, const char *end) {
	if (!skipJunk(from, end)) return;

	const char *nameStart = from;
	while (from < end && ((*from >= 'a' && *from <= 'z') || (*from >= 'A' && *from <= 'Z') || *from == '_' || (*from >= '0' && *from <= '9'))) {
		++from;
	}

	QString varName = QString::fromUtf8(nameStart, int(from - nameStart));

	if (!skipJunk(from, end)) throw Exception("Unexpected end of file!");
	if (*from != ':') throw Exception(QString("':' expected after '%1'").arg(varName));

	if (!skipJunk(++from, end)) throw Exception("Unexpected end of file!");
	if (*from != '"') throw Exception(QString("Expected string after '%1:'").arg(varName));

	QByteArray varValue;
	const char *start = ++from;
	while (from < end && *from != '"') {
		if (*from == '\\') {
			if (from + 1 >= end) throw Exception("Unexpected end of file!");
			if (*(from + 1) == '"' || *(from + 1) == '\\') {
				if (from > start) varValue.append(start, int(from - start));
				start = ++from;
			}
		}
		++from;
	}
	if (from >= end) throw Exception("Unexpected end of file!");
	if (from > start) varValue.append(start, int(from - start));

	if (!skipJunk(++from, end)) throw Exception("Unexpected end of file!");
	if (*from != ';') throw Exception(QString("';' expected after '%1: \"value\"'").arg(varName));

	skipJunk(++from, end);

	if (varName == "direction") {
		if (varValue == "LTR" || varValue == "RTL") {
			layoutDirection = QString::fromUtf8(varValue);
		} else {
			throw Exception(QString("Unexpected value for 'direction' key: '%1'").arg(QString::fromUtf8(varValue)));
		}
	} else if (varName.midRef(0, 4) != "lng_") {
		throw Exception(QString("Bad key '%1'").arg(varName));
	} else if (keys.constFind(varName) != keys.cend()) {
		throw Exception(QString("Key doubled '%1'").arg(varName));
	} else {
		keys.insert(varName, QString::fromUtf8(varValue));
		keysOrder.push_back(varName);
	}
}

QString escapeCpp(const QString &key, QString value, bool wideChar) {
	if (value.isEmpty()) return "QString()";
	value = value.replace('\\', "\\\\").replace('\n', "\\n").replace('\r', "").replace('"', "\\\"");
	QString res;
	res.reserve(value.size() * 10);
	bool instr = false;
	for (const QChar *ch = value.constData(), *e = value.constData() + value.size(); ch != e; ++ch) {
		if (ch->unicode() < 32) {
			throw Exception(QString("Bad value for key '%1'").arg(key));
		} else if (ch->unicode() > 127) {
			if (instr) {
				res.append('"');
				instr = false;
			}
			res.append(' ');
			if (wideChar) {
				res.append('L').append('"').append('\\').append('x').append(QString("%1").arg(ch->unicode(), 4, 16, QChar('0'))).append('"');
			} else {
				res.append('"');
				QByteArray utf(QString(*ch).toUtf8());
				for (const unsigned char *uch = (const unsigned char *)utf.constData(), *ue = (const unsigned char *)utf.constData() + utf.size(); uch != ue; ++uch) {
					res.append('\\').append('x').append(QString("%1").arg(ushort(*uch), 2, 16, QChar('0')));
				}
				res.append('"');
			}
		} else {
			if (!instr) {
				res.append(' ');
				if (wideChar) res.append('L');
				res.append('"');
				instr = true;
			}
			res.append(*ch);
		}
	}
	if (instr) res.append('"');
	return (wideChar ? "qsl(" : "QString::fromUtf8(") + res.mid(wideChar ? 2 : 1) + ")";
}

void writeCppKey(QTextStream &tcpp, const QString &key, const QString &val) {
	QString wide = escapeCpp(key, val, true), utf = escapeCpp(key, val, false);
	if (wide.indexOf(" L\"") < 0) {
		tcpp << "\t\t\tset(" << key << ", " << wide << ");\n";
	} else {
		tcpp << "#ifdef Q_OS_WIN\n";
		tcpp << "\t\t\tset(" << key << ", " << wide << ");\n";
		tcpp << "#else\n";
		tcpp << "\t\t\tset(" << key << ", " << utf << ");\n";
		tcpp << "#endif\n";
	}
}

bool genLang(const QString &lang_in, const QString &lang_out) {
	QString lang_cpp = lang_out + ".cpp", lang_h = lang_out + ".h";
	QFile f(lang_in);
	if (!f.open(QIODevice::ReadOnly)) {
		cout << "Could not open styles input file '" << lang_in.toUtf8().constData() << "'!\n";
		QCoreApplication::exit(1);
		return false;
	}

	QByteArray blob = f.readAll();
	const char *text = blob.constData(), *end = blob.constData() + blob.size();
	f.close();

	try {
		while (text != end) {
			readKeyValue(text, end);
		}

		QByteArray cppText, hText;
		{
			QTextStream tcpp(&cppText), th(&hText);
			tcpp.setCodec("ISO 8859-1");
			th.setCodec("ISO 8859-1");
			th << "\
/*\n\
Created from \'/Resources/lang.txt\' by \'/MetaLang\' project\n\
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
			th << "#pragma once\n\n";
			th << "enum LangKey {\n";
			for (int i = 0, l = keysOrder.size(); i < l; ++i) {
				th << "\t" << keysOrder[i] << (i ? "" : " = 0") << ",\n";
			}
			th << "\n\tlng_keys_cnt\n";
			th << "};\n\n";
			th << "QString lang(LangKey key);\n";
			th << "inline QString langDayOfMonth(const QDate &date) {\n";
			th << "\tint32 month = date.month(), day = date.day();\n";
			th << "\treturn (month > 0 && month <= 12) ? lang(lng_month_day).replace(qsl(\"{month}\"), lang(LangKey(lng_month1 + month - 1))).replace(qsl(\"{day}\"), QString::number(day)) : qsl(\"{err}\");\n";
			th << "}\n\n";
			th << "inline QString langDayOfWeek(const QDate &date) {\n";
			th << "\tint32 day = date.dayOfWeek();\n";
			th << "\treturn (day > 0 && day <= 7) ? lang(LangKey(lng_weekday1 + day - 1)) : qsl(\"{err}\");\n";
			th << "}\n\n";
			th << "Qt::LayoutDirection langDir();\n\n";
			th << "class LangLoader {\n";
			th << "public:\n";
			th << "\tconst QString &errors() const;\n";
			th << "\tconst QString &warnings() const;\n\n";
			th << "protected:\n";
			th << "\tLangLoader() : _checked(false) {\n";
			th << "\t\tmemset(_found, 0, sizeof(_found));\n";
			th << "\t}\n\n";
			th << "\tbool feedKeyValue(const QString &key, const QString &value);\n\n";
			th << "\tvoid error(const QString &text) {\n";
			th << "\t\t_err.push_back(text);\n";
			th << "\t}\n";
			th << "\tvoid warning(const QString &text) {\n";
			th << "\t\t_warn.push_back(text);\n";
			th << "\t}\n\n";
			th << "private:\n";
			th << "\tmutable QStringList _err, _warn;\n";
			th << "\tmutable QString _errors, _warnings;\n";
			th << "\tmutable bool _checked;\n";
			th << "\tbool _found[lng_keys_cnt];\n\n";
			th << "\tLangLoader(const LangLoader &);\n";
			th << "\tLangLoader &operator=(const LangLoader &);\n";
			th << "};\n";

			tcpp << "\
/*\n\
Created from \'/Resources/lang.txt\' by \'/MetaLang\' project\n\
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
			tcpp << "#include \"stdafx.h\"\n#include \"lang.h\"\n\n";
			tcpp << "namespace {\n";
			tcpp << "\tQt::LayoutDirection _langDir = Qt::" << (layoutDirection == "LTR" ? "LeftToRight" : "RightToLeft") << ";\n";
			tcpp << "\tconst char *_langKeyNames[lng_keys_cnt + 1] = {\n";
			for (int i = 0, l = keysOrder.size(); i < l; ++i) {
				tcpp << "\t\t\"" << keysOrder[i] << "\",\n";
			}
			tcpp << "\t\t\"lng_keys_cnt\"\n";
			tcpp << "\t};\n\n";
			tcpp << "\tQString _langValues[lng_keys_cnt + 1];\n\n";
			tcpp << "\tvoid set(LangKey key, const QString &val) {\n";
			tcpp << "\t\t_langValues[key] = val;\n";
			tcpp << "\t}\n\n";
			tcpp << "\tclass LangInit {\n";
			tcpp << "\tpublic:\n";
			tcpp << "\t\tLangInit() {\n";
			for (int i = 0, l = keysOrder.size(); i < l; ++i) {
				writeCppKey(tcpp, keysOrder[i], keys[keysOrder[i]]);
			}
			tcpp << "\t\t\tset(lng_keys_cnt, QString());\n";
			tcpp << "\t\t}\n";
			tcpp << "\t};\n\n";
			tcpp << "\tLangInit _langInit;\n";
			tcpp << "}\n\n";

			tcpp << "QString lang(LangKey key) {\n";
			tcpp << "\treturn _langValues[(key < 0 || key > lng_keys_cnt) ? lng_keys_cnt : key];\n";
			tcpp << "}\n\n";

			tcpp << "Qt::LayoutDirection langDir() {\n";
			tcpp << "\treturn _langDir;\n";
			tcpp << "}\n\n";

			tcpp << "bool LangLoader::feedKeyValue(const QString &key, const QString &value) {\n";
			tcpp << "\tif (key == qsl(\"direction\")) {\n";
			tcpp << "\t\tif (value == qsl(\"LTR\")) {\n";
			tcpp << "\t\t\t_langDir = Qt::LeftToRight;\n";
			tcpp << "\t\t\treturn true;\n";
			tcpp << "\t\t} else if (value == qsl(\"RTL\")) {\n";
			tcpp << "\t\t\t_langDir = Qt::RightToLeft;\n";
			tcpp << "\t\t\treturn true;\n";
			tcpp << "\t\t} else {\n";
			tcpp << "\t\t\t_err.push_back(qsl(\"Bad value for 'direction' key.\"));\n";
			tcpp << "\t\t\treturn false;\n";
			tcpp << "\t\t}\n";
			tcpp << "\t}\n";
			tcpp << "\tif (key.size() < 5 || key.midRef(0, 4) != qsl(\"lng_\")) {\n";
			tcpp << "\t\t_err.push_back(qsl(\"Bad key name '%1'\").arg(key));\n";
			tcpp << "\t\treturn false;\n";
			tcpp << "\t}\n\n";
			if (!keys.isEmpty()) {
				QString tab("\t");
				tcpp << "\tLangKey keyIndex = lng_keys_cnt;\n";
				tcpp << "\tconst QChar *ch = key.constData(), *e = key.constData() + key.size();\n";
				QString current("lng_");
				int depth = current.size();
				tcpp << "\tswitch ((ch + " << depth << ")->unicode()) {\n";
				for (LangKeys::const_iterator i = keys.cbegin(), j = i + 1, e = keys.cend(); i != e; ++i) {
					QString key = i.key();
					while (key.midRef(0, depth) != current) {
						tcpp << tab.repeated(depth - 3) << "}\n";
						current.chop(1);
						--depth;
						tcpp << tab.repeated(depth - 3) << "break;\n";
					}
					do {
						if (key == current) break;
							
						QChar ich = i.key().at(current.size());
						tcpp << tab.repeated(current.size() - 3) << "case '" << ich << "':\n";
						if (j == e || ich != ((j.key().size() > depth) ? j.key().at(depth) : 0)) {
							if (key == current + ich) {
								tcpp << tab.repeated(depth - 3) << "\tif (ch + " << (depth + 1) << " == e) keyIndex = " << key << ";\n";
							} else {
								tcpp << tab.repeated(depth - 3) << "\tif (key.midRef(" << (depth + 1) << ") == qsl(\"" << i.key().mid(depth + 1) << "\")) keyIndex = " << key << ";\n";
							}
							tcpp << tab.repeated(depth - 3) << "break;\n";
							break;
						}

						++depth;
						current += ich;

						if (key == current) {
							tcpp << tab.repeated(depth - 3) << "if (ch + " << depth << " == e) {\n";
							tcpp << tab.repeated(depth - 3) << "\tkeyIndex = " << key << ";\n";
							tcpp << tab.repeated(depth - 3) << "}\n";
						}

						tcpp << tab.repeated(depth - 3) << "if (ch + " << depth << " < e) switch ((ch + " << depth << ")->unicode()) {\n";
					} while (true);
					++j;
				}
				while (QString("lng_") != current) {
					tcpp << tab.repeated(depth - 3) << "}\n";
					current.chop(1);
					--depth;
					tcpp << tab.repeated(depth - 3) << "break;\n";
				}
				tcpp << "\t}\n\n";
				tcpp << "\tif (keyIndex < lng_keys_cnt) {\n";
				tcpp << "\t\t_found[keyIndex] = 1;\n";
				tcpp << "\t\t_langValues[keyIndex] = value;\n";
				tcpp << "\t\treturn true;\n";
				tcpp << "\t}\n\n";
			}
			tcpp << "\t_err.push_back(qsl(\"Unknown key name '%1'\").arg(key));\n";
			tcpp << "\treturn false;\n";
			tcpp << "}\n\n";

			tcpp << "const QString &LangLoader::errors() const {\n";
			tcpp << "\tif (_errors.isEmpty() && !_err.isEmpty()) {\n";
			tcpp << "\t\t_errors = _err.join('\\n');\n";
			tcpp << "\t}\n";
			tcpp << "\treturn _errors;\n";
			tcpp << "}\n\n";

			tcpp << "const QString &LangLoader::warnings() const {\n";
			tcpp << "\tif (!_checked) {\n";
			tcpp << "\t\tfor (int32 i = 0; i < lng_keys_cnt; ++i) {\n";
			tcpp << "\t\t\tif (!_found[i]) {\n";
			tcpp << "\t\t\t\t_warn.push_back(qsl(\"No value found for key '%1'\").arg(_langKeyNames[i]));\n";
			tcpp << "\t\t\t}\n";
			tcpp << "\t\t}\n";
			tcpp << "\t\t_checked = true;\n";
			tcpp << "\t}\n";
			tcpp << "\tif (_warnings.isEmpty() && !_warn.isEmpty()) {\n";
			tcpp << "\t\t_warnings = _warn.join('\\n');\n";
			tcpp << "\t}\n";
			tcpp << "\treturn _warnings;\n";
			tcpp << "}\n";
		}

		QFile cpp(lang_cpp), h(lang_h);
		bool write_cpp = true, write_h = true;
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
			cout << "lang.cpp updated, writing " << keysOrder.size() << " rows.\n";
			if (!cpp.open(QIODevice::WriteOnly)) throw Exception("Could not open lang.cpp for writing!");
			if (cpp.write(cppText) != cppText.size()) throw Exception("Could not open lang.cpp for writing!");
		}
		if (h.open(QIODevice::ReadOnly)) {
			QByteArray wasH = h.readAll();
			if (wasH.size() == hText.size()) {
				if (!memcmp(wasH.constData(), hText.constData(), hText.size())) {
					write_h = false;
				}
			}
			h.close();
		}
		if (write_h) {
			cout << "lang.h updated, writing " << keysOrder.size() << " rows.\n";
			if (!h.open(QIODevice::WriteOnly)) throw Exception("Could not open lang.h for writing!");
			if (h.write(hText) != hText.size()) throw Exception("Could not open lang.h for writing!");
		}
	} catch (exception &e) {
		cout << e.what() << "\n";
		QCoreApplication::exit(1);
		return false;
	}
	return true;
}
