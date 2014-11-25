/*
This file is part of Telegram Desktop,
an official desktop messaging app, see https://telegram.org

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
#include "supporttl.h"

namespace {
	typedef QMap<QString, QString> SupportTemplates;
	SupportTemplates _supportTemplates;

	void saveTemplate(QStringList &keys, QString &value) {
		if (!keys.isEmpty() && !value.isEmpty()) {
			if (value.at(value.size() - 1) == '\n') {
				value = value.mid(0, value.size() - 1);
			}
			for (QStringList::const_iterator i = keys.cbegin(), e = keys.cend(); i != e; ++i) {
				_supportTemplates[textSearchKey(*i)] = value;
			}
		}
		value = QString();
	}
}

void readSupportTemplates() {
	QStringList files(cWorkingDir() + qsl("support_tl.txt"));
	QDir supp(cWorkingDir() + qsl("tsupport"));
	if (supp.exists()) {
		QStringList all = supp.entryList(QDir::Files);
		for (QStringList::const_iterator i = all.cbegin(), e = all.cend(); i != e; ++i) {
			if (i->startsWith(qsl("tl_"))) {
				files.push_back(cWorkingDir() + qsl("tsupport/") + *i);
			}
		}
	}

	typedef QList<QByteArray> TemplatesLines;
	enum ReadingState {
		ReadingNone = 0,
		ReadingKeys = 1,
		ReadingValue = 2,
		ReadingMoreValue = 3,
	};

	for (QStringList::const_iterator i = files.cbegin(), e = files.cend(); i != e; ++i) {
		QFile f(*i);
		if (!f.open(QIODevice::ReadOnly)) continue;

		TemplatesLines lines = f.readAll().split('\n');

		f.close();

		ReadingState state = ReadingNone;
		QStringList keys;
		QString value;
		for (TemplatesLines::const_iterator i = lines.cbegin(), e = lines.cend(); i != e; ++i) {
			QString line = QString::fromUtf8(*i).trimmed();
			QRegularExpressionMatch m = QRegularExpression(qsl("^\\{([A-Z_]+)\\}$")).match(line);
			if (m.hasMatch()) {
				saveTemplate(keys, value);

				QString token = m.captured(1);
				if (token == qsl("KEYS")) {
					keys.clear();
					state = ReadingKeys;
				} else if (token == qsl("VALUE")) {
					state = ReadingValue;
				} else {
					keys.clear();
					state = ReadingNone;
				}
				continue;
			}

			switch (state) {
			case ReadingKeys:
				if (!line.isEmpty()) {
					keys.push_back(line);
				}
				break;

			case ReadingMoreValue:
				value += '\n';
			case ReadingValue:
				value += line;
				state = ReadingMoreValue;
				break;
			}
		}
		saveTemplate(keys, value);
	}
}

const QString &supportTemplate(const QString &key) {
	SupportTemplates::const_iterator i = _supportTemplates.constFind(textSearchKey(key));
	if (i != _supportTemplates.cend()) {
		return *i;
	}

	static const QString _tmp;
	return _tmp;
}
