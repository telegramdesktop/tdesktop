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

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "langloaderplain.h"

namespace {

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

	bool skipJunk(const char *&from, const char *end) {
		const char *start;
		do {
			start = from;
			if (!skipWhitespaces(from, end)) return false;
            if (!skipComment(from, end)) throw Exception("Unexpected end of comment!");
		} while (start != from);
		return true;
	}
}

bool LangLoaderPlain::readKeyValue(const char *&from, const char *end) {
	if (!skipJunk(from, end)) return false;

	if (*from != '"') throw Exception(QString("Expected quote after key name!"));
	++from;
	const char *nameStart = from;
	while (from < end && ((*from >= 'a' && *from <= 'z') || (*from >= 'A' && *from <= 'Z') || *from == '_' || (*from >= '0' && *from <= '9'))) {
		++from;
	}

	QByteArray varName = QByteArray(nameStart, from - nameStart);

	if (*from != '"') throw Exception(QString("Expected quote after key name '%1'!").arg(QLatin1String(varName)));
	++from;

	if (!skipJunk(from, end)) throw Exception(QString("Unexpected end of file in key '%1'!").arg(QLatin1String(varName)));
	if (*from != '=') throw Exception(QString("'=' expected in key '%1'!").arg(QLatin1String(varName)));

	if (!skipJunk(++from, end)) throw Exception(QString("Unexpected end of file in key '%1'!").arg(QLatin1String(varName)));
	if (*from != '"') throw Exception(QString("Expected string after '=' in key '%1'!").arg(QLatin1String(varName)));

	LangKey varKey = keyIndex(varName);
	if (varKey == lngkeys_cnt) throw Exception(QString("Unknown key '%1'!").arg(QLatin1String(varName)));

	QByteArray varValue;
	QMap<ushort, bool> tagsUsed;
	const char *start = ++from;
	while (from < end && *from != '"') {
		if (*from == '\n') {
			throw Exception(QString("Unexpected end of string in key '%1'!").arg(QLatin1String(varName)));
		}
		if (*from == '\\') {
			if (from + 1 >= end) throw Exception(QString("Unexpected end of file in key '%1'!").arg(QLatin1String(varName)));
			if (*(from + 1) == '"' || *(from + 1) == '\\' || *(from + 1) == '{') {
				if (from > start) varValue.append(start, from - start);
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

			ushort index = tagIndex(tagName);
			if (index == lngtags_cnt) throw Exception(QString("Tag '%1' not found in key '%2'!").arg(QLatin1String(tagName)).arg(QLatin1String(varName)));

			if (!tagReplaced(varKey, index)) throw Exception(QString("Unexpected tag '%1' in key '%2'!").arg(QLatin1String(tagName)).arg(QLatin1String(varName)));
			if (tagsUsed.contains(index)) throw Exception(QString("Tag '%1' double used in key '%2'!").arg(QLatin1String(tagName)).arg(QLatin1String(varName)));
			tagsUsed.insert(index, true);

			QString tagReplacer(4, TextCommand);
			tagReplacer[1] = TextCommandLangTag;
			tagReplacer[2] = QChar(0x0020 + index);
			varValue.append(tagReplacer.toUtf8());
			
			if (*from == ':') {
				start = ++from;

				QByteArray subvarValue;
				bool foundtag = false;
				int countedIndex = 0;
				while (from < end && *from != '"' && *from != '}') {
					if (*from == '|') {
						if (from > start) subvarValue.append(start, int(from - start));
						if (countedIndex >= lngtags_max_counted_values) throw Exception(QString("Too many values inside counted tag '%1' in '%2' key!").arg(QLatin1String(tagName)).arg(QLatin1String(varName)));
						if (!feedKeyValue(subkeyIndex(varKey, index, countedIndex++), QString::fromUtf8(subvarValue))) throw Exception(QString("Tag '%1' is not counted in key '%2'!").arg(QLatin1String(tagName)).arg(QLatin1String(varName)));
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
				if (from >= end) throw Exception(QString("Unexpected end of file inside counted tag '%1' in '%2' key!").arg(QString::fromUtf8(tagName)).arg(QString::fromUtf8(varName)));
				if (*from == '"') throw Exception(QString("Unexpected end of string inside counted tag '%1' in '%2' key!").arg(QString::fromUtf8(tagName)).arg(QString::fromUtf8(varName)));

				if (from > start) subvarValue.append(start, int(from - start));
				if (countedIndex >= lngtags_max_counted_values) throw Exception(QString("Too many values inside counted tag '%1' in '%2' key!").arg(QLatin1String(tagName)).arg(QLatin1String(varName)));
				if (!feedKeyValue(subkeyIndex(varKey, index, countedIndex++), QString::fromUtf8(subvarValue))) throw Exception(QString("Tag '%1' is not counted in key '%2'!").arg(QLatin1String(tagName)).arg(QLatin1String(varName)));
			}
			start = from + 1;
		}
		++from;
	}
	if (from >= end) throw Exception(QString("Unexpected end of file in key '%1'!").arg(QLatin1String(varName)));
	if (from > start) varValue.append(start, from - start);

	if (!skipJunk(++from, end)) throw Exception(QString("Unexpected end of file in key '%1'!").arg(QLatin1String(varName)));
	if (*from != ';') throw Exception(QString("';' expected after \"value\" in key '%1'!").arg(QLatin1String(varName)));

	skipJunk(++from, end);

	if (!feedKeyValue(varKey, QString::fromUtf8(varValue))) throw Exception(QString("Could not write value in key '%1'!").arg(QLatin1String(varName)));

	return true;
}

LangLoaderPlain::LangLoaderPlain(const QString &file) {
	QFile f(file);
	if (!f.open(QIODevice::ReadOnly)) {
		error(qsl("Could not open input file!"));
		return;
	}
	if (f.size() > 1024 * 1024) {
		error(qsl("Too big file: %1").arg(f.size()));
		return;
	}
	QByteArray data = f.readAll();
	f.close();

	try {
		const char *from = data.constData(), *end = data.constData() + data.size();
		while (true) {
			if (!readKeyValue(from, end)) {
				break;
			}
		}
	} catch (exception &e) {
		error(QString::fromUtf8(e.what()));
		return;
	}
}
