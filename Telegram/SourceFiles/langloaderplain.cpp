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

	bool readKeyValue(const char *&from, const char *end, QString &name, QString &value) {
		if (!skipJunk(from, end)) return false;

		const char *nameStart = from;
		while (from < end && ((*from >= 'a' && *from <= 'z') || (*from >= 'A' && *from <= 'Z') || *from == '_' || (*from >= '0' && *from <= '9'))) {
			++from;
		}

		QString varName = QString::fromUtf8(nameStart, from - nameStart);

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
					if (from > start) varValue.append(start, from - start);
					start = ++from;
				}
			}
			++from;
		}
        if (from >= end) throw Exception("Unexpected end of file!");
		if (from > start) varValue.append(start, from - start);

        if (!skipJunk(++from, end)) throw Exception("Unexpected end of file!");
        if (*from != ';') throw Exception(QString("';' expected after '%1: \"value\"'").arg(varName));

		skipJunk(++from, end);

		name = varName;
		value = QString::fromUtf8(varValue);
		return true;
	}
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
			QString name, value;
			if (!readKeyValue(from, end, name, value)) {
				break;
			}
			if (!feedKeyValue(name, value)) {
				break;
			}
		}
	} catch (exception &e) {
		error(QString::fromUtf8(e.what()));
		return;
	}
}
