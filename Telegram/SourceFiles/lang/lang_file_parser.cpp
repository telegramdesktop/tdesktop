/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "lang/lang_file_parser.h"

#include "base/parse_helper.h"
#include "ui/integration.h"

#include <QtCore/QTextStream>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>

namespace Lang {
namespace {

constexpr auto kLangFileLimit = 1024 * 1024;

} // namespace

FileParser::FileParser(const QString &file, const std::set<ushort> &request)
: _content(base::parse::stripComments(ReadFile(file, file)))
, _request(request) {
	parse();
}

FileParser::FileParser(const QByteArray &content, Fn<void(QLatin1String key, const QByteArray &value)> callback)
: _content(base::parse::stripComments(content))
, _callback(std::move(callback)) {
	parse();
}

void FileParser::parse() {
	if (_content.isEmpty()) {
		error(u"Got empty lang file content"_q);
		return;
	}

	auto text = _content.constData(), end = text + _content.size();
	while (text != end) {
		if (!readKeyValue(text, end)) {
			break;
		}
	}
}

const QString &FileParser::errors() const {
	if (_errors.isEmpty() && !_errorsList.isEmpty()) {
		_errors = _errorsList.join('\n');
	}
	return _errors;
}

const QString &FileParser::warnings() const {
	if (_warnings.isEmpty() && !_warningsList.isEmpty()) {
		_warnings = _warningsList.join('\n');
	}
	return _warnings;
}

bool FileParser::readKeyValue(const char *&from, const char *end) {
	using base::parse::skipWhitespaces;
	if (!skipWhitespaces(from, end)) return false;

	if (*from != '"') {
		return error("Expected quote before key name!");
	}
	++from;
	const char *nameStart = from;
	while (from < end && ((*from >= 'a' && *from <= 'z') || (*from >= 'A' && *from <= 'Z') || *from == '_' || (*from >= '0' && *from <= '9') || *from == '#')) {
		++from;
	}

	auto key = QLatin1String(nameStart, from - nameStart);

	if (from == end || *from != '"') {
		return error(u"Expected quote after key name '%1'!"_q.arg(key));
	}
	++from;

	if (!skipWhitespaces(from, end)) {
		return error(u"Unexpected end of file in key '%1'!"_q.arg(key));
	}
	if (*from != '=') {
		return error(u"'=' expected in key '%1'!"_q.arg(key));
	}
	if (!skipWhitespaces(++from, end)) {
		return error(u"Unexpected end of file in key '%1'!"_q.arg(key));
	}
	if (*from != '"') {
		return error(u"Expected string after '=' in key '%1'!"_q.arg(key));
	}

	auto skipping = false;
	auto keyIndex = kKeysCount;
	if (!_callback) {
		keyIndex = GetKeyIndex(key);
		skipping = (_request.find(keyIndex) == _request.end());
	}

	auto value = QByteArray();
	auto appendValue = [&value, skipping](auto&&... args) {
		if (!skipping) {
			value.append(std::forward<decltype(args)>(args)...);
		}
	};
	const char *start = ++from;
	while (from < end && *from != '"') {
		if (*from == '\n') {
			return error(u"Unexpected end of string in key '%1'!"_q.arg(key));
		}
		if (*from == '\\') {
			if (from + 1 >= end) {
				return error(u"Unexpected end of file in key '%1'!"_q.arg(key));
			}
			if (*(from + 1) == '"' || *(from + 1) == '\\') {
				if (from > start) appendValue(start, from - start);
				start = ++from;
			} else if (*(from + 1) == 'n') {
				if (from > start) appendValue(start, from - start);
				appendValue('\n');
				start = (++from) + 1;
			}
		}
		++from;
	}
	if (from >= end) {
		return error(u"Unexpected end of file in key '%1'!"_q.arg(key));
	}
	if (from > start) {
		appendValue(start, from - start);
	}

	if (!skipWhitespaces(++from, end)) {
		return error(u"Unexpected end of file in key '%1'!"_q.arg(key));
	}
	if (*from != ';') {
		return error(u"';' expected after \"value\" in key '%1'!"_q.arg(key));
	}

	skipWhitespaces(++from, end);

	if (_callback) {
		_callback(key, value);
	} else if (!skipping) {
		_result.insert(keyIndex, QString::fromUtf8(value));
	}

	return true;
}

QByteArray FileParser::ReadFile(const QString &absolutePath, const QString &relativePath) {
	QFile file(QFileInfo::exists(relativePath) ? relativePath : absolutePath);
	if (!file.open(QIODevice::ReadOnly)) {
		Ui::Integration::Instance().writeLogEntry(u"Lang Error: Could not open file at '%1' ('%2')"_q.arg(relativePath, absolutePath));
		return QByteArray();
	}
	if (file.size() > kLangFileLimit) {
		Ui::Integration::Instance().writeLogEntry(u"Lang Error: File is too big: %1"_q.arg(file.size()));
		return QByteArray();
	}

	constexpr auto kCodecMagicSize = 3;
	auto codecMagic = file.read(kCodecMagicSize);
	if (codecMagic.size() < kCodecMagicSize) {
		Ui::Integration::Instance().writeLogEntry(u"Lang Error: Found bad file at '%1' ('%2')"_q.arg(relativePath, absolutePath));
		return QByteArray();
	}
	file.seek(0);

	QByteArray data;
	int skip = 0;
	auto readUtf16Stream = [relativePath, absolutePath](auto &&stream) {
		stream.setCodec("UTF-16");
		auto string = stream.readAll();
		if (stream.status() != QTextStream::Ok) {
			Ui::Integration::Instance().writeLogEntry(u"Lang Error: Could not read UTF-16 data from '%1' ('%2')"_q.arg(relativePath, absolutePath));
			return QByteArray();
		}
		if (string.isEmpty()) {
			Ui::Integration::Instance().writeLogEntry(u"Lang Error: Empty UTF-16 content in '%1' ('%2')"_q.arg(relativePath, absolutePath));
			return QByteArray();
		}
		return string.toUtf8();
	};
	if ((codecMagic.at(0) == '\xFF' && codecMagic.at(1) == '\xFE') || (codecMagic.at(0) == '\xFE' && codecMagic.at(1) == '\xFF') || (codecMagic.at(1) == 0)) {
		return readUtf16Stream(QTextStream(&file));
	} else if (codecMagic.at(0) == 0) {
		auto utf16WithBOM = "\xFE\xFF" + file.readAll();
		return readUtf16Stream(QTextStream(utf16WithBOM));
	}
	data = file.readAll();
	if (codecMagic.at(0) == '\xEF' && codecMagic.at(1) == '\xBB' && codecMagic.at(2) == '\xBF') {
		data = data.mid(3); // skip UTF-8 BOM
	}
	if (data.isEmpty()) {
		Ui::Integration::Instance().writeLogEntry(u"Lang Error: Empty UTF-8 content in '%1' ('%2')"_q.arg(relativePath, absolutePath));
		return QByteArray();
	}
	return data;
}

} // namespace Lang
