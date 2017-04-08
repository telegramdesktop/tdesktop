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

#include <hunspell/hunspell.hxx>

namespace ChatHelpers {

class HunspellHelper {
public:
	HunspellHelper(const QByteArray &lang)
	: _hunspell(nullptr)
	, _codec(nullptr)
	{
		const auto basePath = (cWorkingDir() + qsl("tdata/spell/") + lang).toUtf8();
		const auto affPath = basePath + ".aff";
		const auto dicPath = basePath + ".dic";
		if (QFileInfo(affPath).isFile() && QFileInfo(dicPath).isFile()) {
			_hunspell = std::make_unique<Hunspell>(affPath, dicPath);
			_codec = QTextCodec::codecForName(_hunspell->get_dic_encoding());
			if (!_codec) {
				_hunspell.reset();
			}
		}
	}

	inline bool isOpen() const {
		return (bool)_hunspell;
	}

	inline bool spell(const QString &word) {
		return _hunspell->spell(_codec->fromUnicode(word).toStdString());
	}
	inline bool spell(const QStringRef &word) {
		return _hunspell->spell(_codec->fromUnicode(word.data(), word.length()).toStdString());
	}

	QVector<QString> suggest(const QString &word) {
		auto suggestions = _hunspell->suggest(_codec->fromUnicode(word).toStdString());

		QVector<QString> result;
		result.reserve(suggestions.size());

		for (auto &suggestion : suggestions) {
			result.append(_codec->toUnicode(suggestion.data(), suggestion.length()));
		}

		return result;
	}

private:
	std::unique_ptr<Hunspell> _hunspell;
	QTextCodec *_codec;

};

class SpellHelperSet final {
public:
	SpellHelperSet();

	SpellHelperSet(const SpellHelperSet &other) = delete;
	SpellHelperSet &operator=(const SpellHelperSet &other) = delete;

	~SpellHelperSet();

	static SpellHelperSet *InstancePointer();
	static SpellHelperSet &Instance() {
		auto result = InstancePointer();
		t_assert(result != nullptr);
		return *result;
	}

	void addLanguages(const QStringList &languages);
	bool isWordCorrect(const QString &word) const;
	bool isWordCorrect(const QStringRef &word) const;
	QList<QVector<QString>> getSuggestions(const QString &word) const;

private:
	std::map<QString, std::unique_ptr<HunspellHelper>> _helpers;

};

class SpellHighlighter : public QSyntaxHighlighter {
public:
	SpellHighlighter(QTextEdit *textEdit)
	: QSyntaxHighlighter(textEdit->document()) {
		//_underlineFmt.setFontUnderline(true);
		_underlineFmt.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
	}

private:
	void highlightBlock(const QString &text) final;
	
	QTextCharFormat _underlineFmt;

};

struct CodeblockInfo {
	int pos;
	int len;
};

struct CodeBlocksData : public QTextBlockUserData {
	QList<CodeblockInfo> codeBlocks;
};

} // namespace ChatHelpers
