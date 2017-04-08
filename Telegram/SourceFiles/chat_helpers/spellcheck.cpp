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

#include "chat_helpers/spellcheck.h"

namespace {

	ChatHelpers::SpellHelperSet *SingleInstance = nullptr;

} // namespace

namespace ChatHelpers {

SpellHelperSet *SpellHelperSet::InstancePointer() {
	return SingleInstance;
}

SpellHelperSet::SpellHelperSet() {
	t_assert(SingleInstance == nullptr);
	SingleInstance = this;
}

SpellHelperSet::~SpellHelperSet() {
	t_assert(SingleInstance == this);
	_helpers.clear();
}

void SpellHelperSet::addLanguages(const QStringList &languages) {
	for (const auto &lang : languages) {
		if (_helpers.count(lang) == 0) {
			auto helper = std::unique_ptr<HunspellHelper>(std::make_unique<HunspellHelper>(lang.toLatin1()));
			if (helper->isOpen()) {
				_helpers.emplace(lang, std::move(helper));
			}
		}
	}
}

bool SpellHelperSet::isWordCorrect(const QString &word) const {
	if (_helpers.empty())
		return true;

	bool isCorrect = false;
	for (auto &&helper : _helpers) {
		if (helper.second->spell(word)) {
			isCorrect = true;
			break;
		}
	}

	return isCorrect;
}

bool SpellHelperSet::isWordCorrect(const QStringRef &word) const {
	if (_helpers.empty())
		return true;

	bool isCorrect = false;
	for (auto &&helper : _helpers) {
		if (helper.second->spell(word)) {
			isCorrect = true;
			break;
		}
	}

	return isCorrect;
}

QList<QVector<QString>> SpellHelperSet::getSuggestions(const QString &word) const {
	QList<QVector<QString>> result;

	for (auto &&helper : _helpers) {
		auto vec = helper.second->suggest(word);
		if (!vec.empty()) result.append(vec);
	}

	return result;
}

void SpellHighlighter::highlightBlock(const QString &text) {
	// If the theme was changed, color would be changed too 
	_underlineFmt.setUnderlineColor(st::spellUnderline->c);
	// Ownership gets transfered with setCurrentBlockUserData
	auto *codeBlocks = new CodeBlocksData();
	// \b - split the string into an alternating seq of non-word and word tokens
	bool skip = true, code = previousBlockState() != -1;
	int codepos = 0;
	for (auto &ref : text.splitRef(QRegExp("\\b"))) {
		if (!skip) {
			if (!code && !SpellHelperSet::InstancePointer()->isWordCorrect(ref))
				setFormat(ref.position(), ref.size(), _underlineFmt);
		} else {
			if (ref.contains("```")) {
				if (code) {
					CodeblockInfo info{codepos, ref.position() + ref.length() - codepos};
					codeBlocks->codeBlocks.append(info);
					auto fmt = QTextCharFormat();
				} else {
					codepos = ref.position();
				}
				code = !code;
			}
		}
		skip = !skip;
	}
	if (code) {
		setCurrentBlockState(1);
		codeBlocks->codeBlocks.append({codepos, text.length() - codepos});
	}
	setCurrentBlockUserData(codeBlocks);
}

} //namespace ChatHelpers
