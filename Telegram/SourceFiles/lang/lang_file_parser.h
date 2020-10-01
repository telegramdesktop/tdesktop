/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "lang/lang_keys.h"

#include <set>
#include <QtCore/QMap>

namespace Lang {

class FileParser {
public:
	using Result = QMap<ushort, QString>;

	FileParser(const QString &file, const std::set<ushort> &request);
	FileParser(const QByteArray &content, Fn<void(QLatin1String key, const QByteArray &value)> callback);

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
	const std::set<ushort> _request;
	const Fn<void(QLatin1String key, const QByteArray &value)> _callback;

	Result _result;

};

} // namespace Lang
