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
#include "emoji_suggestions.h"

#include <algorithm>
#include "emoji_suggestions_data.h"

#ifndef Expects
#include <cassert>
#define Expects(condition) assert(condition)
#endif // Expects

namespace Ui {
namespace Emoji {
namespace internal {
namespace {

checksum Crc32Table[256];
class Crc32Initializer {
public:
	Crc32Initializer() {
		checksum poly = 0x04C11DB7U;
		for (auto i = 0; i != 256; ++i) {
			Crc32Table[i] = reflect(i, 8) << 24;
			for (auto j = 0; j != 8; ++j) {
				Crc32Table[i] = (Crc32Table[i] << 1) ^ (Crc32Table[i] & (1 << 31) ? poly : 0);
			}
			Crc32Table[i] = reflect(Crc32Table[i], 32);
		}
	}

private:
	checksum reflect(checksum val, char ch) {
		checksum result = 0;
		for (int i = 1; i < (ch + 1); ++i) {
			if (val & 1) {
				result |= 1 << (ch - i);
			}
			val >>= 1;
		}
		return result;
	}

};

} // namespace

checksum countChecksum(const void *data, std::size_t size) {
	static Crc32Initializer InitTable;

	auto buffer = static_cast<const unsigned char*>(data);
	auto result = checksum(0xFFFFFFFFU);
	for (auto i = std::size_t(0); i != size; ++i) {
		result = (result >> 8) ^ Crc32Table[(result & 0xFFU) ^ buffer[i]];
	}
	return (result ^ 0xFFFFFFFFU);
}

} // namespace internal

namespace {

class string_span {
public:
	string_span() = default;
	string_span(const utf16string *data, std::size_t size) : begin_(data), size_(size) {
	}
	string_span(const std::vector<utf16string> &data) : begin_(data.data()), size_(data.size()) {
	}
	string_span(const string_span &other) = default;
	string_span &operator=(const string_span &other) = default;

	const utf16string *begin() const {
		return begin_;
	}
	const utf16string *end() const {
		return begin_ + size_;
	}
	std::size_t size() const {
		return size_;
	}

	string_span subspan(std::size_t offset, std::size_t size) {
		return string_span(begin_ + offset, size);
	}

private:
	const utf16string *begin_ = nullptr;
	std::size_t size_ = 0;

};

bool IsNumber(utf16char ch) {
	return (ch >= '0' && ch <= '9');
}

bool IsLetterOrNumber(utf16char ch) {
	return (ch >= 'a' && ch <= 'z') || IsNumber(ch);
}

using Replacement = internal::Replacement;

class Completer {
public:
	Completer(utf16string query);

	std::vector<Suggestion> resolve();

private:
	struct Result {
		const Replacement *replacement;
		int wordsUsed;
	};

	static std::vector<utf16char> NormalizeQuery(utf16string query);
	void addResult(const Replacement *replacement);
	bool isDuplicateOfLastResult(const Replacement *replacement) const;
	bool isBetterThanLastResult(const Replacement *replacement) const;
	void processInitialList();
	void filterInitialList();
	void initWordsTracking();
	bool matchQueryForCurrentItem();
	bool matchQueryTailStartingFrom(int position);
	string_span findWordsStartingWith(utf16char ch);
	int findEqualCharsCount(int position, const utf16string *word);
	std::vector<Suggestion> prepareResult();
	bool startsWithQuery(utf16string word);
	bool isExactMatch(utf16string replacement);

	std::vector<Result> _result;

	utf16string _initialQuery;
	const std::vector<utf16char> _query;
	const utf16char *_queryBegin = nullptr;
	int _querySize = 0;

	const std::vector<const Replacement*> *_initialList = nullptr;

	string_span _currentItemWords;
	int _currentItemWordsUsedCount = 0;

	class UsedWordGuard {
	public:
		UsedWordGuard(std::vector<small> &map, int index);
		UsedWordGuard(const UsedWordGuard &other) = delete;
		UsedWordGuard(UsedWordGuard &&other);
		UsedWordGuard &operator=(const UsedWordGuard &other) = delete;
		UsedWordGuard &operator=(UsedWordGuard &&other) = delete;
		explicit operator bool() const;
		~UsedWordGuard();

	private:
		std::vector<small> &_map;
		int _index = 0;
		small _guarded = 0;

	};
	std::vector<small> _currentItemWordsUsedMap;

};

Completer::UsedWordGuard::UsedWordGuard(std::vector<small> &map, int index) : _map(map), _index(index) {
	Expects(_map.size() > _index);
	if (!_map[_index]) {
		_guarded = _map[_index] = 1;
	}
}

Completer::UsedWordGuard::UsedWordGuard(UsedWordGuard &&other) : _map(other._map), _index(other._index), _guarded(other._guarded) {
	other._guarded = 0;
}

Completer::UsedWordGuard::operator bool() const {
	return _guarded;
}

Completer::UsedWordGuard::~UsedWordGuard() {
	if (_guarded) {
		_map[_index] = 0;
	}
}

Completer::Completer(utf16string query) : _initialQuery(query), _query(NormalizeQuery(query)) {
}

// Remove all non-letters-or-numbers.
// Leave '-' and '+' only if they're followed by a number or
// at the end of the query (so it is possibly followed by a number).
std::vector<utf16char> Completer::NormalizeQuery(utf16string query) {
	auto result = std::vector<utf16char>();
	result.reserve(query.size());
	auto copyFrom = query.data();
	auto e = copyFrom + query.size();
	auto copyTo = result.data();
	for (auto i = query.data(); i != e; ++i) {
		if (IsLetterOrNumber(*i)) {
			continue;
		} else if (*i == '-' || *i == '+') {
			if (i + 1 == e || IsNumber(*(i + 1))) {
				continue;
			}
		}
		if (i > copyFrom) {
			result.resize(result.size() + (i - copyFrom));
			memcpy(copyTo, copyFrom, (i - copyFrom) * sizeof(utf16char));
			copyTo += (i - copyFrom);
		}
		copyFrom = i + 1;
	}
	if (e > copyFrom) {
		result.resize(result.size() + (e - copyFrom));
		memcpy(copyTo, copyFrom, (e - copyFrom) * sizeof(utf16char));
		copyTo += (e - copyFrom);
	}
	return result;
}

std::vector<Suggestion> Completer::resolve() {
	_queryBegin = _query.data();
	_querySize = _query.size();
	if (!_querySize) {
		return std::vector<Suggestion>();
	}
	_initialList = Ui::Emoji::internal::GetReplacements(*_queryBegin);
	if (!_initialList) {
		return std::vector<Suggestion>();
	}
	_result.reserve(_initialList->size());
	processInitialList();
	return prepareResult();
}

bool Completer::isDuplicateOfLastResult(const Replacement *item) const {
	if (_result.empty()) {
		return false;
	}
	return (_result.back().replacement->emoji == item->emoji);
}

bool Completer::isBetterThanLastResult(const Replacement *item) const {
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

void Completer::addResult(const Replacement *item) {
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
		auto wordsCount = item->words.size();
		if (maxWordsCount < wordsCount) {
			maxWordsCount = wordsCount;
		}
	}
	_currentItemWordsUsedMap = std::vector<small>(maxWordsCount, 0);
}

void Completer::filterInitialList() {
	initWordsTracking();
	for (auto item : *_initialList) {
		_currentItemWords = string_span(item->words);
		_currentItemWordsUsedCount = 1;
		if (matchQueryForCurrentItem()) {
			addResult(item);
		}
		_currentItemWordsUsedCount = 0;
	}
}

bool Completer::matchQueryForCurrentItem() {
	Expects(_currentItemWords.size() != 0);
	if (_currentItemWords.size() < 2) {
		return startsWithQuery(*_currentItemWords.begin());
	}
	return matchQueryTailStartingFrom(0);
}

bool Completer::startsWithQuery(utf16string word) {
	if (word.size() < _query.size()) {
		return false;
	}
	for (auto i = std::size_t(0), size = _query.size(); i != size; ++i) {
		if (word[i] != _query[i]) {
			return false;
		}
	}
	return true;
}

bool Completer::isExactMatch(utf16string replacement) {
	if (replacement.size() != _initialQuery.size() + 1) {
		return false;
	}
	for (auto i = std::size_t(0), size = _initialQuery.size(); i != size; ++i) {
		if (replacement[i] != _initialQuery[i]) {
			return false;
		}
	}
	return true;
}

bool Completer::matchQueryTailStartingFrom(int position) {
	auto charsLeftToMatch = (_querySize - position);
	if (!charsLeftToMatch) {
		return true;
	}

	auto firstCharToMatch = *(_queryBegin + position);
	auto foundWords = findWordsStartingWith(firstCharToMatch);

	for (auto word = foundWords.begin(), foundWordsEnd = word + foundWords.size(); word != foundWordsEnd; ++word) {
		auto wordIndex = word - _currentItemWords.begin();
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

int Completer::findEqualCharsCount(int position, const utf16string *word) {
	auto charsLeft = (_querySize - position);
	auto wordBegin = word->data();
	auto wordSize = word->size();
	auto possibleEqualCharsCount = (charsLeft > wordSize ? wordSize : charsLeft);
	for (auto equalTill = 1; equalTill != possibleEqualCharsCount; ++equalTill) {
		auto wordCh = *(wordBegin + equalTill);
		auto queryCh = *(_queryBegin + position + equalTill);
		if (wordCh != queryCh) {
			return equalTill;
		}
	}
	return possibleEqualCharsCount;
}

std::vector<Suggestion> Completer::prepareResult() {
	auto firstCharOfQuery = _query[0];
	auto reorder = [&](auto &&predicate) {
		std::stable_partition(
			std::begin(_result),
			std::end(_result),
			std::forward<decltype(predicate)>(predicate));
	};
	reorder([firstCharOfQuery](Result &result) {
		auto firstCharAfterColon = result.replacement->replacement[1];
		return (firstCharAfterColon == firstCharOfQuery);
	});
	reorder([](Result &result) {
		return (result.wordsUsed < 2);
	});
	reorder([](Result &result) {
		return (result.wordsUsed < 3);
	});
	reorder([&](Result &result) {
		return isExactMatch(result.replacement->replacement);
	});

	auto result = std::vector<Suggestion>();
	result.reserve(_result.size());
	for (auto &item : _result) {
		result.emplace_back(
			item.replacement->emoji,
			item.replacement->replacement,
			item.replacement->replacement);
	}
	return result;
}

string_span Completer::findWordsStartingWith(utf16char ch) {
	auto begin = std::lower_bound(
		std::begin(_currentItemWords),
		std::end(_currentItemWords),
		ch,
		[](utf16string word, utf16char ch) { return word[0] < ch; });
	auto end = std::upper_bound(
		std::begin(_currentItemWords),
		std::end(_currentItemWords),
		ch,
		[](utf16char ch, utf16string word) { return ch < word[0]; });
	return _currentItemWords.subspan(
		begin - _currentItemWords.begin(),
		end - begin);
}

} // namespace

std::vector<Suggestion> GetSuggestions(utf16string query) {
	return Completer(query).resolve();
}

int GetSuggestionMaxLength() {
	return internal::kReplacementMaxLength;
}

} // namespace Emoji
} // namespace Ui
