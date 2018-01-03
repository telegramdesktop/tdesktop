/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
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
