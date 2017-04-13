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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "lang/lang_keys.h"

namespace Lang {

class FileParser {
public:
	using Result = QMap<LangKey, QString>;

	FileParser(const QString &file, const std::set<LangKey> &request);
	FileParser(const QByteArray &content, base::lambda<void(QLatin1String key, const QByteArray &value)> callback);

	static QByteArray ReadFile(const QString &absolutePath, const QString &relativePath);

	const QString &errors() const;
	const QString &warnings() const;

	Result found() const {
		return _result;
	}

private:
	void parse();

	bool error(const QString &text) {
		_errorsList.push_back(text);
		return false;
	}
	void warning(const QString &text) {
		_warningsList.push_back(text);
	}
	bool readKeyValue(const char *&from, const char *end);

	mutable QStringList _errorsList, _warningsList;
	mutable QString _errors, _warnings;

	const QByteArray _content;
	const std::set<LangKey> _request;
	const base::lambda<void(QLatin1String key, const QByteArray &value)> _callback;

	Result _result;

};

} // namespace Lang
