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
#include "genlang.h"

#include <QtCore/QtPlugin>

#ifdef Q_OS_WIN
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
#endif

#ifdef Q_OS_MAC
Q_IMPORT_PLUGIN(QCocoaIntegrationPlugin)
Q_IMPORT_PLUGIN(QDDSPlugin)
Q_IMPORT_PLUGIN(QICNSPlugin)
Q_IMPORT_PLUGIN(QICOPlugin)
Q_IMPORT_PLUGIN(QJp2Plugin)
Q_IMPORT_PLUGIN(QMngPlugin)
Q_IMPORT_PLUGIN(QTgaPlugin)
Q_IMPORT_PLUGIN(QTiffPlugin)
Q_IMPORT_PLUGIN(QWbmpPlugin)
Q_IMPORT_PLUGIN(QWebpPlugin)
#endif

typedef unsigned int uint32;

QString layoutDirection;
typedef QMap<QByteArray, QString> LangKeys;
LangKeys keys;
typedef QMap<QByteArray, ushort> LangTags;
LangTags tags;
typedef QMap<QByteArray, QVector<QByteArray> > LangKeysTags;
LangKeysTags keysTags;
typedef QVector<QByteArray> KeysOrder;
KeysOrder keysOrder;
KeysOrder tagsOrder;
typedef QMap<QByteArray, QMap<QByteArray, QVector<QString> > > LangKeysCounted;
LangKeysCounted keysCounted;

static const QChar TextCommand(0x0010);
static const QChar TextCommandLangTag(0x0020);

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

inline bool _lngEquals(const QByteArray &key, int from, int len, const char *value, int size) {
	if (size != len || from + len > key.size()) return false;
	for (const char *v = key.constData() + from, *e = v + len; v != e; ++v, ++value) {
		if (*v != *value) return false;
	}
	return true;
}

#define LNG_EQUALS_PART(key, from, len, value) _lngEquals(key, from, len, value, sizeof(value) - 1)
#define LNG_EQUALS_TAIL(key, from, value) _lngEquals(key, from, key.size() - from, value, sizeof(value) - 1)
#define LNG_EQUALS(key, value) _lngEquals(key, 0, key.size(), value, sizeof(value) - 1)

static const int MaxCountedValues = 6;

void readKeyValue(const char *&from, const char *end) {
	if (!skipJunk(from, end)) return;

	if (*from != '"') throw Exception(QString("Expected quote before key name!"));
	const char *nameStart = ++from;
	while (from < end && ((*from >= 'a' && *from <= 'z') || (*from >= 'A' && *from <= 'Z') || *from == '_' || (*from >= '0' && *from <= '9'))) {
		++from;
	}

	if (from == nameStart) throw Exception(QString("Expected key name!"));
	QByteArray varName = QByteArray(nameStart, int(from - nameStart));
	for (const char *t = nameStart; t + 1 < from; ++t) {
		if (*t == '_') {
			if (*(t + 1) == '_') throw Exception(QString("Bad key name: %1").arg(QLatin1String(varName)));
			++t;
		}
	}

	if (from == end || *from != '"') throw Exception(QString("Expected quote after key name in key '%1'!").arg(QLatin1String(varName)));
	++from;

	if (!skipJunk(from, end)) throw Exception(QString("Unexpected end of file in key '%1'!").arg(QLatin1String(varName)));
	if (*from != '=') throw Exception(QString("'=' expected in key '%1'!").arg(QLatin1String(varName)));

	if (!skipJunk(++from, end)) throw Exception(QString("Unexpected end of file in key '%1'!").arg(QLatin1String(varName)));
	if (*from != '"') throw Exception(QString("Expected string after '=' in key '%1'!").arg(QLatin1String(varName)));

	QByteArray varValue;
	const char *start = ++from;
	QVector<QByteArray> tagsList;
	while (from < end && *from != '"') {
		if (*from == '\n') {
			throw Exception(QString("Unexpected end of string in key '%1'!").arg(QLatin1String(varName)));
		}
		if (*from == '\\') {
			if (from + 1 >= end) throw Exception(QString("Unexpected end of file in key '%1'!").arg(QLatin1String(varName)));
			if (*(from + 1) == '"' || *(from + 1) == '\\' || *(from + 1) == '{') {
				if (from > start) varValue.append(start, int(from - start));
				start = ++from;
			} else if (*(from + 1) == 'n') {
				if (from > start) varValue.append(start, int(from - start));

				varValue.append('\n');

				start = (++from) + 1;
			}
		} else if (*from == '{') {
			if (from > start) varValue.append(start, int(from - start));

			const char *tagStart = ++from;
			while (from < end && ((*from >= 'a' && *from <= 'z') || (*from >= 'A' && *from <= 'Z') || *from == '_' || (*from >= '0' && *from <= '9'))) {
				++from;
			}
			if (from == tagStart) throw Exception(QString("Expected tag name in key '%1'!").arg(QLatin1String(varName)));
			QByteArray tagName = QByteArray(tagStart, int(from - tagStart));

			if (from == end || (*from != '}' && *from != ':')) throw Exception(QString("Expected '}' or ':' after tag name in key '%1'!").arg(QLatin1String(varName)));

			LangTags::const_iterator i = tags.constFind(tagName);
			if (i == tags.cend()) {
				i = tags.insert(tagName, tagsOrder.size());
				tagsOrder.push_back(tagName);
			}
			if (0x0020 + *i > 0x007F) throw Exception(QString("Too many different tags in key '%1'").arg(QLatin1String(varName)));

			QString tagReplacer(4, TextCommand);
			tagReplacer[1] = TextCommandLangTag;
			tagReplacer[2] = QChar(0x0020 + *i);
			varValue.append(tagReplacer.toUtf8());
			for (int j = 0, s = tagsList.size(); j < s; ++j) {
				if (tagsList.at(j) == tagName) throw Exception(QString("Tag '%1' double used in key '%2'!").arg(QLatin1String(tagName)).arg(QLatin1String(varName)));
			}
			tagsList.push_back(tagName);

			if (*from == ':') {
				start = ++from;
				
				QVector<QString> &counted(keysCounted[varName][tagName]);
				QByteArray subvarValue;
				bool foundtag = false;
				while (from < end && *from != '"' && *from != '}') {
					if (*from == '|') {
						if (from > start) subvarValue.append(start, int(from - start));
						counted.push_back(QString::fromUtf8(subvarValue));
						subvarValue = QByteArray();
						foundtag = false;
						start = from + 1;
					}
					if (*from == '\n') {
						throw Exception(QString("Unexpected end of string inside counted tag '%1' in '%2' key!").arg(QLatin1String(tagName)).arg(QLatin1String(varName)));
					}
					if (*from == '\\') {
						if (from + 1 >= end) throw Exception(QString("Unexpected end of file inside counted tag '%1' in '%2' key!").arg(QLatin1String(tagName)).arg(QLatin1String(varName)));
						if (*(from + 1) == '"' || *(from + 1) == '\\' || *(from + 1) == '{' || *(from + 1) == '#') {
							if (from > start) subvarValue.append(start, int(from - start));
							start = ++from;
						} else if (*(from + 1) == 'n') {
							if (from > start) subvarValue.append(start, int(from - start));

							subvarValue.append('\n');

							start = (++from) + 1;
						}
					} else if (*from == '{') {
						throw Exception(QString("Unexpected tag inside counted tag '%1' in '%2' key!").arg(QLatin1String(tagName)).arg(QLatin1String(varName)));
					} else if (*from == '#') {
						if (foundtag) throw Exception(QString("Replacement '#' double used inside counted tag '%1' in '%2' key!").arg(QLatin1String(tagName)).arg(QLatin1String(varName)));
						foundtag = true;
						if (from > start) subvarValue.append(start, int(from - start));
						subvarValue.append(tagReplacer.toUtf8());
						start = from + 1;
					}
					++from;
				}
				if (from >= end) throw Exception(QString("Unexpected end of file inside counted tag '%1' in '%2' key!").arg(QLatin1String(tagName)).arg(QLatin1String(varName)));
				if (*from == '"') throw Exception(QString("Unexpected end of string inside counted tag '%1' in '%2' key!").arg(QLatin1String(tagName)).arg(QLatin1String(varName)));

				if (from > start) subvarValue.append(start, int(from - start));
				counted.push_back(QString::fromUtf8(subvarValue));

				if (counted.size() > MaxCountedValues) {
					throw Exception(QString("Too many values inside counted tag '%1' in '%2' key!").arg(QLatin1String(tagName)).arg(QLatin1String(varName)));
				}
			}
			start = from + 1;
		}
		++from;
	}
	if (from >= end) throw Exception(QString("Unexpected end of file in key '%1'!").arg(QLatin1String(varName)));
	if (from > start) varValue.append(start, int(from - start));

	if (!skipJunk(++from, end)) throw Exception(QString("Unexpected end of file in key '%1'!").arg(QLatin1String(varName)));
	if (*from != ';') throw Exception(QString("';' expected after \"value\" in key '%1'!").arg(QLatin1String(varName)));

	skipJunk(++from, end);

	if (varName == "direction") {
		throw Exception(QString("Unexpected value for 'direction' in key '%1'!").arg(QLatin1String(varName)));
	} else if (!LNG_EQUALS_PART(varName, 0, 4, "lng_")) {
		throw Exception(QString("Bad key '%1'!").arg(QLatin1String(varName)));
	} else if (keys.constFind(varName) != keys.cend()) {
		throw Exception(QString("Key '%1' doubled!").arg(QLatin1String(varName)));
	} else {
		keys.insert(varName, QString::fromUtf8(varValue));
		keysTags.insert(varName, tagsList);
		keysOrder.push_back(varName);
	}
}

QString escapeCpp(const QByteArray &key, QString value) {
	if (value.isEmpty()) return "QString()";

	QString res;
	res.reserve(value.size() * 10);
	bool instr = false;
	for (const QChar *ch = value.constData(), *e = value.constData() + value.size(); ch != e; ++ch) {
		if (ch->unicode() > 0x007F) {
			if (instr) {
				res.append('"');
				instr = false;
			}
			res.append(' ').append('u').append('"').append('\\').append('x').append(QString("%1").arg(ch->unicode(), 4, 16, QChar('0'))).append('"');
		} else {
			if (ch->unicode() == '\\' || ch->unicode() == '\n' || ch->unicode() == '\r' || ch->unicode() == '"') {
				if (!instr) {
					res.append(' ').append('u').append('"');
					instr = true;
				}
				res.append('\\');
				if (ch->unicode() == '\\' || ch->unicode() == '"') {
					res.append(*ch);
				} else if (ch->unicode() == '\n') {
					res.append('n');
				} else if (ch->unicode() == '\r') {
					res.append('r');
				}
			} else if (ch->unicode() < 0x0020) {
				if (*ch == TextCommand) {
					if (ch + 3 >= e || (ch + 1)->unicode() != TextCommandLangTag || (ch + 2)->unicode() > 0x007F || (ch + 2)->unicode() < 0x0020 || *(ch + 3) != TextCommand) {
						throw Exception(QString("Bad value for key '%1'").arg(QLatin1String(key)));
					} else {
						if (instr) {
							res.append('"');
							instr = false;
						}
						res.append(' ').append('u').append('"');
						res.append('\\').append('x').append(QString("%1").arg(ch->unicode(), 2, 16, QChar('0')));
						res.append('\\').append('x').append(QString("%1").arg((ch + 1)->unicode(), 2, 16, QChar('0')));
						res.append('\\').append('x').append(QString("%1").arg((ch + 2)->unicode(), 2, 16, QChar('0')));
						res.append('\\').append('x').append(QString("%1").arg((ch + 3)->unicode(), 2, 16, QChar('0')));
						res.append('"');
						ch += 3;
					}
				} else {
					throw Exception(QString("Bad value for key '%1'").arg(QLatin1String(key)));
				}
			} else {
				if (!instr) {
					res.append(' ').append('u').append('"');
					instr = true;
				}
				res.append(*ch);
			}
		}
	}
	if (instr) res.append('"');
	return "qsl(" + res.mid(1) + ")";
}

void writeCppKey(QTextStream &tcpp, const QByteArray &key, const QString &val) {
	tcpp << "\t\t\tset(" << key << ", " << escapeCpp(key, val) << ");\n";
}

bool genLang(const QString &lang_in, const QString &lang_out) {
	QString lang_cpp = lang_out + ".cpp", lang_h = lang_out + ".h";
	QFile f(lang_in);
	if (!f.open(QIODevice::ReadOnly)) {
		cout << "Could not open lang input file '" << lang_in.toUtf8().constData() << "'!\n";
		QCoreApplication::exit(1);
		return false;
	}
	QByteArray checkCodec = f.read(3);
	if (checkCodec.size() < 3) {
		cout << "Bad lang input file '" << lang_in.toUtf8().constData() << "'!\n";
		QCoreApplication::exit(1);
		return false;
	}
	f.seek(0);

	QByteArray data;
	int skip = 0;
	if ((checkCodec.at(0) == '\xFF' && checkCodec.at(1) == '\xFE') || (checkCodec.at(0) == '\xFE' && checkCodec.at(1) == '\xFF') || (checkCodec.at(1) == 0)) {
		QTextStream stream(&f);
		stream.setCodec("UTF-16");

		QString string = stream.readAll();
		if (stream.status() != QTextStream::Ok) {
			cout << "Could not read valid UTF-16 file '" << lang_in.toUtf8().constData() << "'!\n";
			QCoreApplication::exit(1);
			return false;
		}
		f.close();

		data = string.toUtf8();
	} else if (checkCodec.at(0) == 0) {
		QByteArray tmp = "\xFE\xFF" + f.readAll(); // add fake UTF-16 BOM
		f.close();

		QTextStream stream(&tmp);
		stream.setCodec("UTF-16");
		QString string = stream.readAll();
		if (stream.status() != QTextStream::Ok) {
			cout << "Could not read valid UTF-16 file '" << lang_in.toUtf8().constData() << "'!\n";
			QCoreApplication::exit(1);
			return false;
		}

		data = string.toUtf8();
	} else {
		data = f.readAll();
		if (checkCodec.at(0) == '\xEF' && checkCodec.at(1) == '\xBB' && checkCodec.at(2) == '\xBF') {
			skip = 3; // skip UTF-8 BOM
		}
	}

	const char *text = data.constData() + skip, *end = text + data.size() - skip;
	try {
		while (text < end) {
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
the official desktop version of Telegram messaging app, see https://telegram.org\n\
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
In addition, as a special exception, the copyright holders give permission\n\
to link the code of portions of this program with the OpenSSL library.\n\
\n\
Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE\n\
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org\n\
*/\n";
			th << "#pragma once\n\n";

			for (int i = 0, l = tagsOrder.size(); i < l; ++i) {
				th << "enum lngtag_" << tagsOrder[i] << " { lt_" << tagsOrder[i] << " = " << i << " };\n";
			}
			th << "static const ushort lngtags_cnt = " << tagsOrder.size() << ";\n";
			th << "static const ushort lngtags_max_counted_values = " << MaxCountedValues << ";\n";
			th << "\n";

			th << "enum LangKey {\n";
			for (int i = 0, l = keysOrder.size(); i < l; ++i) {
				if (keysTags[keysOrder[i]].isEmpty()) {
					th << "\t" << keysOrder[i] << (i ? "" : " = 0") << ",\n";
				} else {
					th << "\t" << keysOrder[i] << "__tagged" << (i ? "" : " = 0") << ",\n";
					QMap<QByteArray, QVector<QString> > &countedTags(keysCounted[keysOrder[i]]);
					if (!countedTags.isEmpty()) {
						for (QMap<QByteArray, QVector<QString> >::const_iterator j = countedTags.cbegin(), e = countedTags.cend(); j != e; ++j) {
							const QVector<QString> &counted(*j);
							for (int k = 0, s = counted.size(); k < s; ++k) {
								th << "\t" << keysOrder[i] << "__" + j.key() + QString::number(k).toUtf8() << ",\n";
							}
						}
					}
				}
			}
			th << "\n\tlngkeys_cnt\n";
			th << "};\n\n";

			th << "LangString lang(LangKey key);\n\n";
			th << "LangString langOriginal(LangKey key);\n\n";

			for (int i = 0, l = keysOrder.size(); i < l; ++i) {
				QVector<QByteArray> &tagsList(keysTags[keysOrder[i]]);
				if (tagsList.isEmpty()) continue;

				QMap<QByteArray, QVector<QString> > &countedTags(keysCounted[keysOrder[i]]);
				th << "inline LangString " << keysOrder[i] << "(";
				for (int j = 0, s = tagsList.size(); j < s; ++j) {
					if (countedTags[tagsList[j]].isEmpty()) {
						th << "lngtag_" << tagsList[j] << ", const QString &" << tagsList[j] << "__val";
					} else {
						th << "lngtag_" << tagsList[j] << ", float64 " << tagsList[j] << "__val";
					}
					if (j + 1 < s) th << ", ";
				}
				th << ") {\n";
				th << "\treturn lang(" << keysOrder[i] << "__tagged)";
				for (int j = 0, s = tagsList.size(); j < s; ++j) {
					if (countedTags[tagsList[j]].isEmpty()) {
						th << ".tag(lt_" << tagsList[j] << ", " << tagsList[j] << "__val)";
					} else {
						th << ".tag(lt_" << tagsList[j] << ", langCounted(" << keysOrder[i] << "__" << tagsList[j] << "0, lt_" << tagsList[j] << ", " << tagsList[j] << "__val))";
					}
				}
				th << ";\n";
				th << "}\n";
			}

			tcpp << "\
/*\n\
Created from \'/Resources/lang.txt\' by \'/MetaLang\' project\n\
\n\
WARNING! All changes made in this file will be lost!\n\
\n\
This file is part of Telegram Desktop,\n\
the official desktop version of Telegram messaging app, see https://telegram.org\n\
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
In addition, as a special exception, the copyright holders give permission\n\
to link the code of portions of this program with the OpenSSL library.\n\
\n\
Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE\n\
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org\n\
*/\n";
			tcpp << "#include \"stdafx.h\"\n#include \"lang.h\"\n\n";
			tcpp << "namespace {\n";

			tcpp << "\tconst char *_langKeyNames[lngkeys_cnt] = {\n";
			for (int i = 0, l = keysOrder.size(); i < l; ++i) {
				if (keysTags[keysOrder[i]].isEmpty()) {
					tcpp << "\t\t\"" << keysOrder[i] << "\",\n";
				} else {
					tcpp << "\t\t\"" << keysOrder[i] << "__tagged\",\n";
					QMap<QByteArray, QVector<QString> > &countedTags(keysCounted[keysOrder[i]]);
					if (!countedTags.isEmpty()) {
						for (QMap<QByteArray, QVector<QString> >::const_iterator j = countedTags.cbegin(), e = countedTags.cend(); j != e; ++j) {
							const QVector<QString> &counted(*j);
							for (int k = 0, s = counted.size(); k < s; ++k) {
								tcpp << "\t\t\"" << keysOrder[i] << "__" + j.key() + QString::number(k).toUtf8() << "\",\n";
							}
						}
					}
				}
			}
			tcpp << "\t};\n\n";

			tcpp << "\tLangString _langValues[lngkeys_cnt], _langValuesOriginal[lngkeys_cnt];\n\n";
			tcpp << "\tvoid set(LangKey key, const QString &val) {\n";
			tcpp << "\t\t_langValues[key] = val;\n";
			tcpp << "\t}\n\n";

			tcpp << "\tclass LangInit {\n";
			tcpp << "\tpublic:\n";
			tcpp << "\t\tLangInit() {\n";
			for (int i = 0, l = keysOrder.size(); i < l; ++i) {
				writeCppKey(tcpp, keysOrder[i] + (keysTags[keysOrder[i]].isEmpty() ? "" : "__tagged"), keys[keysOrder[i]]);

				QMap<QByteArray, QVector<QString> > &countedTags(keysCounted[keysOrder[i]]);
				if (!countedTags.isEmpty()) {
					for (QMap<QByteArray, QVector<QString> >::const_iterator j = countedTags.cbegin(), e = countedTags.cend(); j != e; ++j) {
						const QVector<QString> &counted(*j);
						for (int k = 0, s = counted.size(); k < s; ++k) {
							writeCppKey(tcpp, keysOrder[i] + "__" + j.key() + QString::number(k).toUtf8(), counted[k]);
						}
					}
				}
			}
			tcpp << "\t\t}\n";
			tcpp << "\t};\n\n";

			tcpp << "\tLangInit _langInit;\n\n";

			tcpp << "\tinline bool _lngEquals(const QByteArray &key, int from, int len, const char *value, int size) {\n";
			tcpp << "\t\tif (size != len || from + len > key.size()) return false;\n";
			tcpp << "\t\tfor (const char *v = key.constData() + from, *e = v + len; v != e; ++v, ++value) {\n";
			tcpp << "\t\t\tif (*v != *value) return false;\n";
			tcpp << "\t\t}\n";
			tcpp << "\t\treturn true; \n";
			tcpp << "\t}\n";

			tcpp << "}\n\n";

			tcpp << "#define LNG_EQUALS_PART(key, from, len, value) _lngEquals(key, from, len, value, sizeof(value) - 1)\n";
			tcpp << "#define LNG_EQUALS_TAIL(key, from, value) _lngEquals(key, from, key.size() - from, value, sizeof(value) - 1)\n";
			tcpp << "#define LNG_EQUALS(key, value) _lngEquals(key, 0, key.size(), value, sizeof(value) - 1)\n\n";

			tcpp << "LangString lang(LangKey key) {\n";
			tcpp << "\treturn (key < 0 || key > lngkeys_cnt) ? QString() : _langValues[key];\n";
			tcpp << "}\n\n";

			tcpp << "LangString langOriginal(LangKey key) {\n";
			tcpp << "\treturn (key < 0 || key > lngkeys_cnt || _langValuesOriginal[key] == qsl(\"{}\")) ? QString() : (_langValuesOriginal[key].isEmpty() ? _langValues[key] : _langValuesOriginal[key]);\n";
			tcpp << "}\n\n";

			tcpp << "const char *langKeyName(LangKey key) {\n";
			tcpp << "\treturn (key < 0 || key > lngkeys_cnt) ? \"\" : _langKeyNames[key];\n";
			tcpp << "}\n\n";

			tcpp << "ushort LangLoader::tagIndex(const QByteArray &tag) const {\n";
			tcpp << "\tif (tag.isEmpty()) return lngtags_cnt;\n\n";
			if (!tags.isEmpty()) {
				QString tab("\t");
				tcpp << "\tconst char *ch = tag.constData(), *e = tag.constData() + tag.size();\n";
				QByteArray current;
				int depth = current.size();
				tcpp << "\tswitch (*ch) {\n";
				for (LangTags::const_iterator i = tags.cbegin(), j = i + 1, e = tags.cend(); i != e; ++i) {
					QByteArray tag = i.key();
					while (depth > 0 && tag.mid(0, depth) != current) {
						tcpp << tab.repeated(depth + 1) << "}\n";
						current.chop(1);
						--depth;
						tcpp << tab.repeated(depth + 1) << "break;\n";
					}
					do {
						if (tag == current) break;

						char ich = i.key().at(current.size());
						tcpp << tab.repeated(current.size() + 1) << "case '" << ich << "':\n";
						if (j == e || ich != ((j.key().size() > depth) ? j.key().at(depth) : 0)) {
							if (tag == current + ich) {
								tcpp << tab.repeated(depth + 1) << "\tif (ch + " << (depth + 1) << " == e) return lt_" << tag << ";\n";
							} else {
								tcpp << tab.repeated(depth + 1) << "\tif (LNG_EQUALS_TAIL(tag, " << (depth + 1) << ", \"" << i.key().mid(depth + 1) << "\")) return lt_" << tag << ";\n";
							}
							tcpp << tab.repeated(depth + 1) << "break;\n";
							break;
						}

						++depth;
						current += ich;

						if (tag == current) {
							tcpp << tab.repeated(depth + 1) << "if (ch + " << depth << " == e) {\n";
							tcpp << tab.repeated(depth + 1) << "\treturn lt_" << tag << ";\n";
							tcpp << tab.repeated(depth + 1) << "}\n";
						}

						tcpp << tab.repeated(depth + 1) << "if (ch + " << depth << " < e) switch (*(ch + " << depth << ")) {\n";
					} while (true);
					++j;
				}
				while (QByteArray() != current) {
					tcpp << tab.repeated(depth + 1) << "}\n";
					current.chop(1);
					--depth;
					tcpp << tab.repeated(depth + 1) << "break;\n";
				}
				tcpp << "\t}\n\n";
			}
			tcpp << "\treturn lngtags_cnt;\n";
			tcpp << "}\n\n";

			tcpp << "LangKey LangLoader::keyIndex(const QByteArray &key) const {\n";
			tcpp << "\tif (key.size() < 5 || !LNG_EQUALS_PART(key, 0, 4, \"lng_\")) return lngkeys_cnt;\n\n";
			if (!keys.isEmpty()) {
				QString tab("\t");
				tcpp << "\tconst char *ch = key.constData(), *e = key.constData() + key.size();\n";
				QByteArray current("lng_");
				int depth = current.size();
				tcpp << "\tswitch (*(ch + " << depth << ")) {\n";
				for (LangKeys::const_iterator i = keys.cbegin(), j = i + 1, e = keys.cend(); i != e; ++i) {
					QByteArray key = i.key();
					while (key.mid(0, depth) != current) {
						tcpp << tab.repeated(depth - 3) << "}\n";
						current.chop(1);
						--depth;
						tcpp << tab.repeated(depth - 3) << "break;\n";
					}
					do {
						if (key == current) break;
							
						char ich = i.key().at(current.size());
						tcpp << tab.repeated(current.size() - 3) << "case '" << ich << "':\n";
						if (j == e || ich != ((j.key().size() > depth) ? j.key().at(depth) : 0)) {
							if (key == current + ich) {
								tcpp << tab.repeated(depth - 3) << "\tif (ch + " << (depth + 1) << " == e) return " << key << (keysTags[key].isEmpty() ? "" : "__tagged") << ";\n";
							} else {
								tcpp << tab.repeated(depth - 3) << "\tif (LNG_EQUALS_TAIL(key, " << (depth + 1) << ", \"" << i.key().mid(depth + 1) << "\")) return " << key << (keysTags[key].isEmpty() ? "" : "__tagged") << ";\n";
							}
							tcpp << tab.repeated(depth - 3) << "break;\n";
							break;
						}

						++depth;
						current += ich;

						if (key == current) {
							tcpp << tab.repeated(depth - 3) << "if (ch + " << depth << " == e) {\n";
							tcpp << tab.repeated(depth - 3) << "\treturn " << key << (keysTags[key].isEmpty() ? "" : "__tagged") << ";\n";
							tcpp << tab.repeated(depth - 3) << "}\n";
						}

						tcpp << tab.repeated(depth - 3) << "if (ch + " << depth << " < e) switch (*(ch + " << depth << ")) {\n";
					} while (true);
					++j;
				}
				while (QByteArray("lng_") != current) {
					tcpp << tab.repeated(depth - 3) << "}\n";
					current.chop(1);
					--depth;
					tcpp << tab.repeated(depth - 3) << "break;\n";
				}
				tcpp << "\t}\n\n";
			}
			tcpp << "\treturn lngkeys_cnt;\n";
			tcpp << "}\n\n";

			tcpp << "bool LangLoader::tagReplaced(LangKey key, ushort tag) const {\n";
			if (!tags.isEmpty()) {
				tcpp << "\tswitch (key) {\n";
				for (int i = 0, l = keysOrder.size(); i < l; ++i) {
					QVector<QByteArray> &tagsList(keysTags[keysOrder[i]]);
					if (tagsList.isEmpty()) continue;

					tcpp << "\tcase " << keysOrder[i] << "__tagged: {\n";
					tcpp << "\t\tswitch (tag) {\n";
					for (int j = 0, s = tagsList.size(); j < s; ++j) {
						tcpp << "\t\tcase lt_" << tagsList[j] << ":\n";
					}
					tcpp << "\t\t\treturn true;\n";
					tcpp << "\t\t}\n";
					tcpp << "\t} break;\n";
				}
				tcpp << "\t}\n\n";
			}
			tcpp << "\treturn false;";
			tcpp << "}\n\n";

			tcpp << "LangKey LangLoader::subkeyIndex(LangKey key, ushort tag, ushort index) const {\n";
			tcpp << "\tif (index >= lngtags_max_counted_values) return lngkeys_cnt;\n\n";
			if (!tags.isEmpty()) {
				tcpp << "\tswitch (key) {\n";
				for (int i = 0, l = keysOrder.size(); i < l; ++i) {
					QVector<QByteArray> &tagsList(keysTags[keysOrder[i]]);
					if (tagsList.isEmpty()) continue;

					QMap<QByteArray, QVector<QString> > &countedTags(keysCounted[keysOrder[i]]);
					tcpp << "\tcase " << keysOrder[i] << "__tagged: {\n";
					tcpp << "\t\tswitch (tag) {\n";
					for (int j = 0, s = tagsList.size(); j < s; ++j) {
						if (!countedTags[tagsList[j]].isEmpty()) {
							tcpp << "\t\tcase lt_" << tagsList[j] << ": return LangKey(" << keysOrder[i] << "__" << tagsList[j] << "0 + index);\n";
						}
					}
					tcpp << "\t\t}\n";
					tcpp << "\t} break;\n";
				}
				tcpp << "\t}\n\n";
			}
			tcpp << "\treturn lngkeys_cnt;";
			tcpp << "}\n\n";

			tcpp << "bool LangLoader::feedKeyValue(LangKey key, const QString &value) {\n";
			tcpp << "\tif (key < lngkeys_cnt) {\n";
			tcpp << "\t\t_found[key] = 1;\n";
			tcpp << "\t\tif (_langValuesOriginal[key].isEmpty()) {\n";
			tcpp << "\t\t\t_langValuesOriginal[key] = _langValues[key].isEmpty() ? qsl(\"{}\") : _langValues[key];\n";
			tcpp << "\t\t}\n";
			tcpp << "\t\t_langValues[key] = value;\n";
			tcpp << "\t\treturn true;\n";
			tcpp << "\t}\n";
			tcpp << "\treturn false;\n";
			tcpp << "}\n\n";
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
