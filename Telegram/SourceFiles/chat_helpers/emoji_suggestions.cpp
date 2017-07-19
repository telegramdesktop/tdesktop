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
#include "chat_helpers/emoji_suggestions.h"

namespace Ui {
namespace Emoji {
namespace {

class Completer {
public:
	Completer(const QString &query);

	QVector<Suggestion> resolve();

private:
	struct Result {
		gsl::not_null<const Replacement*> replacement;
		int wordsUsed;
	};

	static QString NormalizeQuery(const QString &query);
	void addResult(gsl::not_null<const Replacement*> replacement);
	bool isDuplicateOfLastResult(gsl::not_null<const Replacement*> replacement) const;
	bool isBetterThanLastResult(gsl::not_null<const Replacement*> replacement) const;
	void processInitialList();
	void filterInitialList();
	void initWordsTracking();
	bool matchQueryForCurrentItem();
	bool matchQueryTailStartingFrom(int position);
	gsl::span<const QString> findWordsStartingWith(QChar ch);
	int findEqualCharsCount(int position, const QString *word);
	QVector<Suggestion> prepareResult();

	std::vector<Result> _result;

	const QString _query;
	const QChar *_queryBegin = nullptr;
	int _querySize = 0;

	const std::vector<gsl::not_null<const Replacement*>> *_initialList = nullptr;

	gsl::span<const QString> _currentItemWords;
	int _currentItemWordsUsedCount = 0;

	class UsedWordGuard {
	public:
		UsedWordGuard(QVector<bool> &map, int index);
		UsedWordGuard(const UsedWordGuard &other) = delete;
		UsedWordGuard(UsedWordGuard &&other);
		UsedWordGuard &operator=(const UsedWordGuard &other) = delete;
		UsedWordGuard &operator=(UsedWordGuard &&other) = delete;
		explicit operator bool() const;
		~UsedWordGuard();

	private:
		QVector<bool> &_map;
		int _index = 0;
		bool _guarded = false;

	};
	QVector<bool> _currentItemWordsUsedMap;

};

Completer::UsedWordGuard::UsedWordGuard(QVector<bool> &map, int index) : _map(map), _index(index) {
	Expects(_map.size() > _index);
	if (!_map[_index]) {
		_guarded = _map[_index] = true;
	}
}

Completer::UsedWordGuard::UsedWordGuard(UsedWordGuard &&other) : _map(other._map), _index(other._index), _guarded(base::take(other._guarded)) {
}

Completer::UsedWordGuard::operator bool() const {
	return _guarded;
}

Completer::UsedWordGuard::~UsedWordGuard() {
	if (_guarded) {
		_map[_index] = false;
	}
}

Completer::Completer(const QString &query) : _query(NormalizeQuery(query)) {
}

// Remove all non-letters-or-numbers.
// Leave '-' and '+' only if they're followed by a number or
// at the end of the query (so it is possibly followed by a number).
QString Completer::NormalizeQuery(const QString &query) {
	auto result = query;
	auto copyFrom = query.constData();
	auto e = copyFrom + query.size();
	auto copyTo = (QChar*)nullptr;
	for (auto i = query.constData(); i != e; ++i) {
		if (i->isLetterOrNumber()) {
			continue;
		} else if (*i == '-' || *i == '+') {
			if (i + 1 == e || (i + 1)->isNumber()) {
				continue;
			}
		}
		if (i > copyFrom) {
			if (!copyTo) copyTo = result.data();
			memcpy(copyTo, copyFrom, (i - copyFrom) * sizeof(QChar));
			copyTo += (i - copyFrom);
		}
		copyFrom = i + 1;
	}
	if (copyFrom == query.constData()) {
		return query;
	} else if (e > copyFrom) {
		if (!copyTo) copyTo = result.data();
		memcpy(copyTo, copyFrom, (e - copyFrom) * sizeof(QChar));
		copyTo += (e - copyFrom);
	}
	result.chop(result.constData() + result.size() - copyTo);
	return result;
}

QVector<Suggestion> Completer::resolve() {
	_queryBegin = _query.constData();
	_querySize = _query.size();
	if (!_querySize) {
		return QVector<Suggestion>();
	}
	_initialList = Ui::Emoji::GetReplacements(*_queryBegin);
	if (!_initialList) {
		return QVector<Suggestion>();
	}
	_result.reserve(_initialList->size());
	processInitialList();
	return prepareResult();
}

bool Completer::isDuplicateOfLastResult(gsl::not_null<const Replacement*> item) const {
	if (_result.empty()) {
		return false;
	}
	return (_result.back().replacement->id == item->id);
}

bool Completer::isBetterThanLastResult(gsl::not_null<const Replacement*> item) const {
	Expects(!_result.empty());
	auto &last = _result.back();
	if (_currentItemWordsUsedCount < last.wordsUsed) {
		return true;
	}

	auto firstCharOfQuery = _query[0];
	auto firstCharAfterColonLast = last.replacement->replacement[1];
	auto firstCharAfterColonCurrent = item->replacement[1];
	auto goodLast = (firstCharAfterColonLast == firstCharOfQuery);
	auto goodCurrent = (firstCharAfterColonCurrent == firstCharOfQuery);
	return !goodLast && goodCurrent;
}

void Completer::addResult(gsl::not_null<const Replacement*> item) {
	if (!isDuplicateOfLastResult(item)) {
		_result.push_back({ item, _currentItemWordsUsedCount });
	} else if (isBetterThanLastResult(item)) {
		_result.back() = { item, _currentItemWordsUsedCount };
	}
}

void Completer::processInitialList() {
	if (_querySize > 1) {
		filterInitialList();
	} else {
		_currentItemWordsUsedCount = 1;
		for (auto item : *_initialList) {
			addResult(item);
		}
	}
}

void Completer::initWordsTracking() {
	auto maxWordsCount = 0;
	for (auto item : *_initialList) {
		accumulate_max(maxWordsCount, item->words.size());
	}
	_currentItemWordsUsedMap = QVector<bool>(maxWordsCount, false);
}

void Completer::filterInitialList() {
	initWordsTracking();
	for (auto item : *_initialList) {
		_currentItemWords = gsl::make_span(item->words);
		_currentItemWordsUsedCount = 1;
		if (matchQueryForCurrentItem()) {
			addResult(item);
		}
		_currentItemWordsUsedCount = 0;
	}
}

bool Completer::matchQueryForCurrentItem() {
	Expects(!_currentItemWords.empty());
	if (_currentItemWords.size() < 2) {
		return _currentItemWords.data()->startsWith(_query);
	}
	return matchQueryTailStartingFrom(0);
}

bool Completer::matchQueryTailStartingFrom(int position) {
	auto charsLeftToMatch = (_querySize - position);
	if (!charsLeftToMatch) {
		return true;
	}

	auto firstCharToMatch = *(_queryBegin + position);
	auto foundWords = findWordsStartingWith(firstCharToMatch);

	for (auto word = foundWords.data(), foundWordsEnd = word + foundWords.size(); word != foundWordsEnd; ++word) {
		auto wordIndex = word - _currentItemWords.data();
		if (auto guard = UsedWordGuard(_currentItemWordsUsedMap, wordIndex)) {
			++_currentItemWordsUsedCount;
			auto equalCharsCount = findEqualCharsCount(position, word);
			for (auto check = equalCharsCount; check != 0; --check) {
				if (matchQueryTailStartingFrom(position + check)) {
					return true;
				}
			}
			--_currentItemWordsUsedCount;
		}
	}
	return false;
}

int Completer::findEqualCharsCount(int position, const QString *word) {
	auto charsLeft = (_querySize - position);
	auto wordBegin = word->constData();
	auto wordSize = word->size();
	auto possibleEqualCharsCount = qMin(charsLeft, wordSize);
	for (auto equalTill = 1; equalTill != possibleEqualCharsCount; ++equalTill) {
		auto wordCh = *(wordBegin + equalTill);
		auto queryCh = *(_queryBegin + position + equalTill);
		if (wordCh != queryCh) {
			return equalTill;
		}
	}
	return possibleEqualCharsCount;
}

QVector<Suggestion> Completer::prepareResult() {
	auto firstCharOfQuery = _query[0];
	std::stable_partition(_result.begin(), _result.end(), [firstCharOfQuery](Result &result) {
		auto firstCharAfterColon = result.replacement->replacement[1];
		return (firstCharAfterColon == firstCharOfQuery);
	});
	std::stable_partition(_result.begin(), _result.end(), [](Result &result) {
		return (result.wordsUsed < 2);
	});
	std::stable_partition(_result.begin(), _result.end(), [](Result &result) {
		return (result.wordsUsed < 3);
	});

	auto result = QVector<Suggestion>();
	result.reserve(_result.size());
	for (auto &item : _result) {
		result.push_back({ item.replacement->id, item.replacement->label, item.replacement->replacement });
	}
	return result;
}

gsl::span<const QString> Completer::findWordsStartingWith(QChar ch) {
	auto begin = std::lower_bound(_currentItemWords.cbegin(), _currentItemWords.cend(), ch, [](const QString &word, QChar ch) {
		return word[0] < ch;
	});
	auto end = std::upper_bound(_currentItemWords.cbegin(), _currentItemWords.cend(), ch, [](QChar ch, const QString &word) {
		return ch < word[0];
	});
	return _currentItemWords.subspan(begin - _currentItemWords.cbegin(), end - begin);
}

} // namespace

QVector<Suggestion> GetSuggestions(const QString &query) {
	return Completer(query).resolve();
}

} // namespace Emoji
} // namespace Ui
