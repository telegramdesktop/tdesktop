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
#include "lang/lang_file_parser.h"

#include "base/parse_helper.h"

namespace Lang {

FileParser::FileParser(const QString &file, const std::set<LangKey> &request)
: _filePath(file)
, _request(request)
, _readingAll(request.find(kLangKeysCount) != request.end()) {
	QFile f(_filePath);
	if (!f.open(QIODevice::ReadOnly)) {
		error(qsl("Could not open input file!"));
		return;
	}
	if (f.size() > 1024 * 1024) {
		error(qsl("Too big file: %1").arg(f.size()));
		return;
	}
	QByteArray checkCodec = f.read(3);
	if (checkCodec.size() < 3) {
		error(qsl("Bad lang input file: %1").arg(file));
		return;
	}
	f.seek(0);

	QByteArray data;
	int skip = 0;
	if ((checkCodec.at(0) == '\xFF' && checkCodec.at(1) == '\xFE') || (checkCodec.at(0) == '\xFE' && checkCodec.at(1) == '\xFF') || (checkCodec.at(1) == 0)) {
		QTextStream stream(&f);
		stream.setCodec("UTF-16");

		QString string = stream.readAll();
		if (stream.status() != QTextStream::Ok) {
			error(qsl("Could not read valid UTF-16 file: % 1").arg(file));
			return;
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
			error(qsl("Could not read valid UTF-16 file: % 1").arg(file));
			return;
		}

		data = string.toUtf8();
	} else {
		data = f.readAll();
		if (checkCodec.at(0) == '\xEF' && checkCodec.at(1) == '\xBB' && checkCodec.at(2) == '\xBF') {
			skip = 3; // skip UTF-8 BOM
		}
	}

	data = base::parse::stripComments(data);

	auto text = data.constData() + skip, end = text + data.size() - skip;
	try {
		while (text != end) {
			if (!readKeyValue(text, end)) {
				break;
			}
		}
	} catch (std::exception &e) {
		error(QString::fromUtf8(e.what()));
		return;
	}
}

const QString &FileParser::errors() const {
	if (_errors.isEmpty() && !_errorsList.isEmpty()) {
		_errors = _errorsList.join('\n');
	}
	return _errors;
}

const QString &FileParser::warnings() const {
	if (!_checked) {
		for (auto i = 0; i < kLangKeysCount; ++i) {
			if (!_found[i]) {
				_warningsList.push_back(qsl("No value found for key '%1'").arg(GetKeyName(LangKey(i))));
			}
		}
		_checked = true;
	}
	if (_warnings.isEmpty() && !_warningsList.isEmpty()) {
		_warnings = _warningsList.join('\n');
	}
	return _warnings;
}

void FileParser::foundKeyValue(LangKey key) {
	if (key < kLangKeysCount) {
		_found[key] = true;
	}
}

bool FileParser::readKeyValue(const char *&from, const char *end) {
	using base::parse::skipWhitespaces;
	if (!skipWhitespaces(from, end)) return false;

	if (*from != '"') throw Exception(QString("Expected quote before key name!"));
	++from;
	const char *nameStart = from;
	while (from < end && ((*from >= 'a' && *from <= 'z') || (*from >= 'A' && *from <= 'Z') || *from == '_' || (*from >= '0' && *from <= '9'))) {
		++from;
	}

	auto varName = QLatin1String(nameStart, from - nameStart);

	if (from == end || *from != '"') throw Exception(QString("Expected quote after key name '%1'!").arg(varName));
	++from;

	if (!skipWhitespaces(from, end)) throw Exception(QString("Unexpected end of file in key '%1'!").arg(varName));
	if (*from != '=') throw Exception(QString("'=' expected in key '%1'!").arg(varName));

	if (!skipWhitespaces(++from, end)) throw Exception(QString("Unexpected end of file in key '%1'!").arg(varName));
	if (*from != '"') throw Exception(QString("Expected string after '=' in key '%1'!").arg(varName));

	auto varKey = GetKeyIndex(varName);
	bool feedingValue = _request.empty();
	if (feedingValue) {
		if (varKey == kLangKeysCount) {
			warning(QString("Unknown key '%1'!").arg(varName));
		}
	} else if (!_readingAll && _request.find(varKey) == _request.end()) {
		varKey = kLangKeysCount;
	}
	bool readingValue = (varKey != kLangKeysCount);

	QByteArray varValue;
	QMap<ushort, bool> tagsUsed;
	const char *start = ++from;
	while (from < end && *from != '"') {
		if (*from == '\n') {
			throw Exception(QString("Unexpected end of string in key '%1'!").arg(varName));
		}
		if (*from == '\\') {
			if (from + 1 >= end) throw Exception(QString("Unexpected end of file in key '%1'!").arg(varName));
			if (*(from + 1) == '"' || *(from + 1) == '\\' || *(from + 1) == '{') {
				if (readingValue && from > start) varValue.append(start, from - start);
				start = ++from;
			} else if (*(from + 1) == 'n') {
				if (readingValue) {
					if (from > start) varValue.append(start, int(from - start));
					varValue.append('\n');
				}
				start = (++from) + 1;
			}
		} else if (readingValue && *from == '{') {
			if (from > start) varValue.append(start, int(from - start));

			const char *tagStart = ++from;
			while (from < end && ((*from >= 'a' && *from <= 'z') || (*from >= 'A' && *from <= 'Z') || *from == '_' || (*from >= '0' && *from <= '9'))) {
				++from;
			}
			if (from == tagStart) {
				readingValue = false;
				warning(QString("Expected tag name in key '%1'!").arg(varName));
				continue;
			}
			auto tagName = QLatin1String(tagStart, int(from - tagStart));

			if (from == end || (*from != '}' && *from != ':')) throw Exception(QString("Expected '}' or ':' after tag name in key '%1'!").arg(varName));

			auto index = GetTagIndex(tagName);
			if (index == kTagsCount) {
				readingValue = false;
				warning(QString("Tag '%1' not found in key '%2', not using value.").arg(tagName).arg(varName));
				continue;
			}

			if (!IsTagReplaced(varKey, index)) {
				readingValue = false;
				warning(QString("Unexpected tag '%1' in key '%2', not using value.").arg(tagName).arg(varName));
				continue;
			}
			if (tagsUsed.contains(index)) throw Exception(QString("Tag '%1' double used in key '%2'!").arg(tagName).arg(varName));
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
						if (countedIndex >= kTagsPluralVariants) throw Exception(QString("Too many values inside counted tag '%1' in '%2' key!").arg(tagName).arg(varName));
						auto subkey = GetSubkeyIndex(varKey, index, countedIndex++);
						if (subkey == kLangKeysCount) {
							readingValue = false;
							warning(QString("Unexpected counted tag '%1' in key '%2', not using value.").arg(tagName).arg(varName));
							break;
						} else {
							if (feedingValue) {
								if (!feedKeyValue(subkey, QString::fromUtf8(subvarValue))) throw Exception(QString("Tag '%1' is not counted in key '%2'!").arg(tagName).arg(varName));
							} else {
								foundKeyValue(subkey);
							}
						}
						subvarValue = QByteArray();
						foundtag = false;
						start = from + 1;
					}
					if (*from == '\n') {
						throw Exception(QString("Unexpected end of string inside counted tag '%1' in '%2' key!").arg(tagName).arg(varName));
					}
					if (*from == '\\') {
						if (from + 1 >= end) throw Exception(QString("Unexpected end of file inside counted tag '%1' in '%2' key!").arg(tagName).arg(varName));
						if (*(from + 1) == '"' || *(from + 1) == '\\' || *(from + 1) == '{' || *(from + 1) == '#') {
							if (from > start) subvarValue.append(start, int(from - start));
							start = ++from;
						} else if (*(from + 1) == 'n') {
							if (from > start) subvarValue.append(start, int(from - start));

							subvarValue.append('\n');

							start = (++from) + 1;
						}
					} else if (*from == '{') {
						throw Exception(QString("Unexpected tag inside counted tag '%1' in '%2' key!").arg(tagName).arg(varName));
					} else if (*from == '#') {
						if (foundtag) throw Exception(QString("Replacement '#' double used inside counted tag '%1' in '%2' key!").arg(tagName).arg(varName));
						foundtag = true;
						if (from > start) subvarValue.append(start, int(from - start));
						subvarValue.append(tagReplacer.toUtf8());
						start = from + 1;
					}
					++from;
				}
				if (!readingValue) continue;
				if (from >= end) throw Exception(QString("Unexpected end of file inside counted tag '%1' in '%2' key!").arg(tagName).arg(varName));
				if (*from == '"') throw Exception(QString("Unexpected end of string inside counted tag '%1' in '%2' key!").arg(tagName).arg(varName));

				if (from > start) subvarValue.append(start, int(from - start));
				if (countedIndex >= kTagsPluralVariants) throw Exception(QString("Too many values inside counted tag '%1' in '%2' key!").arg(tagName).arg(varName));

				auto subkey = GetSubkeyIndex(varKey, index, countedIndex++);
				if (subkey == kLangKeysCount) {
					readingValue = false;
					warning(QString("Unexpected counted tag '%1' in key '%2', not using value.").arg(tagName).arg(varName));
					break;
				} else {
					if (feedingValue) {
						if (!feedKeyValue(subkey, QString::fromUtf8(subvarValue))) throw Exception(QString("Tag '%1' is not counted in key '%2'!").arg(tagName).arg(varName));
					} else {
						foundKeyValue(subkey);
					}
				}
			}
			start = from + 1;
		}
		++from;
	}
	if (from >= end) throw Exception(QString("Unexpected end of file in key '%1'!").arg(varName));
	if (readingValue && from > start) varValue.append(start, from - start);

	if (!skipWhitespaces(++from, end)) throw Exception(QString("Unexpected end of file in key '%1'!").arg(varName));
	if (*from != ';') throw Exception(QString("';' expected after \"value\" in key '%1'!").arg(varName));

	skipWhitespaces(++from, end);

	if (readingValue) {
		if (feedingValue) {
			if (!feedKeyValue(varKey, QString::fromUtf8(varValue))) throw Exception(QString("Could not write value in key '%1'!").arg(varName));
		} else {
			foundKeyValue(varKey);
			_result.insert(varKey, QString::fromUtf8(varValue));
		}
	}

	return true;
}

bool FileParser::feedKeyValue(LangKey key, const QString &value) {
	if (key < kLangKeysCount) {
		_found[key] = 1;
		FeedKeyValue(key, value);
		return true;
	}
	return false;
}

} // namespace Lang
