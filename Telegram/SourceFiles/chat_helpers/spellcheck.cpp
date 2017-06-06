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

} //namespace ChatHelpers
